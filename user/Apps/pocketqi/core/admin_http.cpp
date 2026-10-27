/* PocketQI (github.com/stephaneweg/PocketQI, commit 3eda2e5), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
//
// admin_http.cpp -- Pocket alpha admin_web : text buffer, HTTP request parsing, forms, responses
//
// Only what the Circle kernel provides is used: memcpy / strlen / strncasecmp..., malloc,
// vsnprintf (pocket_libc.c). No ctype (its tables are not in the kernel).
//
#include "admin_core.h"
#include <string.h>
#include <strings.h>		// strcasecmp, strncasecmp
#include <stdlib.h>
#include <stdio.h>

// ---- CAdminText -------------------------------------------------------------------------

CAdminText::CAdminText (void)
:	m_pData (0), m_nLength (0), m_nCapacity (0), m_bFailed (false)
{
}

CAdminText::~CAdminText (void)
{
	Clear (true);
}

void CAdminText::Clear (bool bWipe)
{
	if (m_pData != 0)
	{
		if (bWipe)
		{
			AdminWipe (m_pData, m_nCapacity);
		}
		free (m_pData);
	}
	m_pData = 0;
	m_nLength = m_nCapacity = 0;
	m_bFailed = false;
}

bool CAdminText::Reserve (unsigned nMore)
{
	if (m_bFailed)
	{
		return false;
	}
	if (m_nLength + nMore + 1 <= m_nCapacity)
	{
		return true;
	}

	unsigned nCapacity = m_nCapacity < 1024 ? 1024 : m_nCapacity;
	while (nCapacity < m_nLength + nMore + 1)
	{
		nCapacity *= 2;
	}
	char *pData = (char *) malloc (nCapacity);
	if (pData == 0)
	{
		m_bFailed = true;
		return false;
	}
	if (m_pData != 0)
	{
		memcpy (pData, m_pData, m_nLength);
		AdminWipe (m_pData, m_nCapacity);	// (a page may hold a model name, never a key)
		free (m_pData);
	}
	m_pData = pData;
	m_nCapacity = nCapacity;
	m_pData[m_nLength] = '\0';
	return true;
}

void CAdminText::Append (const char *pText, unsigned nLength)
{
	if (nLength == 0 || !Reserve (nLength))
	{
		return;
	}
	memcpy (m_pData + m_nLength, pText, nLength);
	m_nLength += nLength;
	m_pData[m_nLength] = '\0';
}

void CAdminText::Append (const char *pText)
{
	Append (pText, strlen (pText));
}

void CAdminText::AppendHtml (const char *pText)
{
	for (const char *p = pText; *p != '\0'; p++)
	{
		switch (*p)
		{
		case '&':	Append ("&amp;");	break;
		case '<':	Append ("&lt;");	break;
		case '>':	Append ("&gt;");	break;
		case '"':	Append ("&quot;");	break;
		case '\'':	Append ("&#39;");	break;
		default:
			if ((unsigned char) *p < ' ')
			{
				Append ("?", 1);	// (control characters of an SSID)
			}
			else
			{
				Append (p, 1);
			}
			break;
		}
	}
}

void CAdminText::Printf (const char *pFormat, ...)
{
	char Buffer[512];
	va_list Args;
	va_start (Args, pFormat);
	int n = vsnprintf (Buffer, sizeof Buffer, pFormat, Args);
	va_end (Args);
	if (n < 0)
	{
		return;
	}
	if ((unsigned) n >= sizeof Buffer)
	{
		n = sizeof Buffer - 1;			// (short lines only : truncated)
	}
	Append (Buffer, (unsigned) n);
}

void AdminWipe (void *pBuffer, unsigned nLength)
{
	volatile unsigned char *p = (volatile unsigned char *) pBuffer;
	while (nLength-- > 0)
	{
		*p++ = 0;
	}
}

// ---- requests ---------------------------------------------------------------------------

static const char *FindHeaderEnd (const char *pData, unsigned nLength)
{
	for (unsigned i = 0; i + 3 < nLength; i++)
	{
		if (   pData[i] == '\r' && pData[i + 1] == '\n'
		    && pData[i + 2] == '\r' && pData[i + 3] == '\n')
		{
			return pData + i;
		}
	}
	return 0;
}

// copies [pFrom, pTo) into pOut (NUL-terminated); false if it does not fit
static bool CopyField (const char *pFrom, const char *pTo, char *pOut, unsigned nSize)
{
	unsigned n = (unsigned) (pTo - pFrom);
	if (n >= nSize)
	{
		return false;
	}
	memcpy (pOut, pFrom, n);
	pOut[n] = '\0';
	return true;
}

// the value of header pName in [pHeaders, pEnd) (pName ends with ':'), spaces trimmed
static bool GetHeader (const char *pHeaders, const char *pEnd, const char *pName, char *pOut,
		       unsigned nSize)
{
	unsigned nName = strlen (pName);
	const char *p = pHeaders;
	while (p < pEnd)
	{
		const char *pLineEnd = p;
		while (pLineEnd < pEnd && *pLineEnd != '\r')
		{
			pLineEnd++;
		}
		if (   (unsigned) (pLineEnd - p) > nName
		    && strncasecmp (p, pName, nName) == 0)
		{
			const char *v = p + nName;
			while (v < pLineEnd && (*v == ' ' || *v == '\t'))
			{
				v++;
			}
			const char *e = pLineEnd;
			while (e > v && (e[-1] == ' ' || e[-1] == '\t'))
			{
				e--;
			}
			return CopyField (v, e, pOut, nSize);
		}
		p = pLineEnd + 2;			// (\r\n)
	}
	pOut[0] = '\0';
	return false;
}

int HttpParseRequest (char *pData, unsigned nLength, THttpRequest *pRequest)
{
	const char *pHeaderEnd = FindHeaderEnd (pData, nLength);
	if (pHeaderEnd == 0)
	{
		return nLength >= HTTP_MAX_REQUEST ? -1 : 0;
	}

	memset (pRequest, 0, sizeof *pRequest);

	// request line : METHOD SP target SP HTTP/1.x
	const char *p = pData;
	const char *pLineEnd = p;
	while (pLineEnd < pHeaderEnd && *pLineEnd != '\r')
	{
		pLineEnd++;
	}
	const char *pSpace = p;
	while (pSpace < pLineEnd && *pSpace != ' ')
	{
		pSpace++;
	}
	if (pSpace >= pLineEnd || !CopyField (p, pSpace, pRequest->Method, sizeof pRequest->Method))
	{
		return -1;
	}
	const char *pTarget = pSpace + 1;
	const char *pTargetEnd = pTarget;
	while (pTargetEnd < pLineEnd && *pTargetEnd != ' ')
	{
		pTargetEnd++;
	}
	if (pTargetEnd == pTarget || *pTarget != '/')
	{
		return -1;
	}
	const char *pQuery = pTarget;
	while (pQuery < pTargetEnd && *pQuery != '?')
	{
		pQuery++;
	}
	if (!CopyField (pTarget, pQuery, pRequest->Path, sizeof pRequest->Path))
	{
		return -1;
	}
	if (   pQuery < pTargetEnd
	    && !CopyField (pQuery + 1, pTargetEnd, pRequest->Query, sizeof pRequest->Query))
	{
		return -1;
	}

	const char *pHeaders = pLineEnd + 2;
	GetHeader (pHeaders, pHeaderEnd, "Host:", pRequest->Host, sizeof pRequest->Host);
	GetHeader (pHeaders, pHeaderEnd, "Origin:", pRequest->Origin, sizeof pRequest->Origin);
	GetHeader (pHeaders, pHeaderEnd, "Authorization:", pRequest->Authorization, sizeof pRequest->Authorization);
	// (POST /api/apps/<id> : une app entière, admin_apps.cpp)
	bool bUpload = strcmp (pRequest->Method, "POST") == 0 && AdminIsAppsPath (pRequest->Path);

	unsigned nBody = 0;
	char Number[16];
	if (GetHeader (pHeaders, pHeaderEnd, "Content-Length:", Number, sizeof Number))
	{
		char *pNumberEnd;
		unsigned long n = strtoul (Number, &pNumberEnd, 10);
		if (*pNumberEnd != '\0' || n > (bUpload ? ADMIN_APP_MAX_BODY : HTTP_MAX_BODY))
		{
			return -1;
		}
		nBody = (unsigned) n;
	}
	char Encoding[32];
	if (GetHeader (pHeaders, pHeaderEnd, "Transfer-Encoding:", Encoding, sizeof Encoding))
	{
		return -1;				// (chunked bodies: browsers never send them for forms)
	}

	unsigned nHeaderLength = (unsigned) (pHeaderEnd - pData) + 4;
	if (nHeaderLength + nBody > nLength)
	{
		return nHeaderLength + nBody > (bUpload ? HTTP_MAX_UPLOAD : HTTP_MAX_REQUEST) ? -1 : 0;
	}
	pRequest->pBody = pData + nHeaderLength;
	pRequest->nBodyLength = nBody;
	return 1;
}

static int HexValue (char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool FormGet (const char *pForm, unsigned nLength, const char *pName, char *pValue, unsigned nSize)
{
	unsigned nName = strlen (pName);
	const char *p = pForm;
	const char *pEnd = pForm + nLength;
	while (p < pEnd)
	{
		const char *pFieldEnd = p;
		while (pFieldEnd < pEnd && *pFieldEnd != '&')
		{
			pFieldEnd++;
		}
		if (   (unsigned) (pFieldEnd - p) >= nName + 1
		    && memcmp (p, pName, nName) == 0 && p[nName] == '=')
		{
			unsigned n = 0;
			for (const char *v = p + nName + 1; v < pFieldEnd; v++)
			{
				char c = *v;
				if (c == '+')
				{
					c = ' ';
				}
				else if (c == '%')
				{
					int h = v + 2 < pFieldEnd ? HexValue (v[1]) : -1;
					int l = v + 2 < pFieldEnd ? HexValue (v[2]) : -1;
					if (h < 0 || l < 0)
					{
						AdminWipe (pValue, n);
						return false;
					}
					c = (char) (h * 16 + l);
					v += 2;
				}
				if (n + 1 >= nSize)
				{
					AdminWipe (pValue, n);
					return false;
				}
				pValue[n++] = c;
			}
			pValue[n] = '\0';
			return true;
		}
		p = pFieldEnd + 1;
	}
	if (nSize > 0)
	{
		pValue[0] = '\0';
	}
	return false;
}

// ---- responses --------------------------------------------------------------------------

static const char *StatusText (unsigned nStatus)
{
	switch (nStatus)
	{
	case 200:	return "OK";
	case 302:	return "Found";
	case 303:	return "See Other";
	case 400:	return "Bad Request";
	case 403:	return "Forbidden";
	case 401:	return "Unauthorized";
	case 404:	return "Not Found";
	case 405:	return "Method Not Allowed";
	case 409:	return "Conflict";
	case 413:	return "Payload Too Large";
	case 422:	return "Unprocessable Content";
	case 500:	return "Internal Server Error";
	case 503:	return "Service Unavailable";
	default:	return "Status";
	}
}

void HttpResponse (CAdminText *pOut, unsigned nStatus, const char *pContentType, const char *pBody,
		   unsigned nBodyLength, const char *pExtraHeaders)
{
	pOut->Printf ("HTTP/1.1 %u %s\r\n", nStatus, StatusText (nStatus));
	if (pContentType != 0)
	{
		pOut->Printf ("Content-Type: %s\r\n", pContentType);
	}
	pOut->Printf ("Content-Length: %u\r\n", nBodyLength);
	pOut->Append ("Cache-Control: no-store\r\n"
		      "X-Content-Type-Options: nosniff\r\n"
		      "X-Frame-Options: DENY\r\n"
		      // (same-origin, pas no-referrer : avec no-referrer, les navigateurs envoient « Origin: null »
		      // sur les formulaires et le contrôle d'origine des POST les refusait tous ; rien ne part ailleurs)
		      "Referrer-Policy: same-origin\r\n"
		      "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline';"
		      " form-action 'self'; frame-ancestors 'none'\r\n"
		      "Connection: close\r\n");
	if (pExtraHeaders != 0)
	{
		pOut->Append (pExtraHeaders);
	}
	pOut->Append ("\r\n");
	if (pBody != 0)
	{
		pOut->Append (pBody, nBodyLength);
	}
}

// ---- GET /api/face ------------------------------------------------------------------------

static TAdminFaceSource *s_pFaceSource = 0;

void AdminSetFaceSource (TAdminFaceSource *pSource)
{
	s_pFaceSource = pSource;
}

bool AdminIsFacePath (const char *pPath)
{
	return strcmp (pPath, "/api/face") == 0;
}

void AdminFaceRequest (const THttpRequest &rRequest, CAdminText *pResponse)
{
	static const char Type[] = "application/json; charset=utf-8";
	if (strcmp (rRequest.Method, "GET") != 0 && strcmp (rRequest.Method, "HEAD") != 0)
	{
		static const char t[] = "{\"error\": \"GET seulement\"}";
		HttpResponse (pResponse, 405, Type, t, sizeof t - 1);
		return;
	}
	char Json[256];
	unsigned n = s_pFaceSource != 0 ? (*s_pFaceSource) (Json, sizeof Json) : 0;
	if (n == 0 || n >= sizeof Json)
	{
		static const char t[] = "{\"error\": \"pas de visage ici\"}";
		HttpResponse (pResponse, 404, Type, t, sizeof t - 1);
		return;
	}
	HttpResponse (pResponse, 200, Type, Json, n);
}

// ---- GET /api/screen, POST /api/touch (Pocket dessine) ------------------------------------

static TAdminScreenSource *s_pScreenSource = 0;
static TAdminTouch *s_pTouch = 0;

void AdminSetScreen (TAdminScreenSource *pSource, TAdminTouch *pTouch)
{
	s_pScreenSource = pSource;
	s_pTouch = pTouch;
}

bool AdminIsScreenPath (const char *pPath)
{
	return strcmp (pPath, "/api/screen") == 0 || strcmp (pPath, "/api/touch") == 0;
}

// "name": <integer> in a small JSON body
static bool BodyInt (const char *pBody, unsigned nLength, const char *pName, int *pValue)
{
	char Key[16] = "\"";
	strcat (Key, pName);
	strcat (Key, "\"");
	unsigned nKey = strlen (Key);
	for (unsigned i = 0; i + nKey <= nLength; i++)
	{
		if (memcmp (pBody + i, Key, nKey) != 0)
		{
			continue;
		}
		unsigned j = i + nKey;
		while (j < nLength && (pBody[j] == ' ' || pBody[j] == ':'))
		{
			j++;
		}
		bool bMinus = j < nLength && pBody[j] == '-';
		if (bMinus)
		{
			j++;
		}
		if (j >= nLength || pBody[j] < '0' || pBody[j] > '9')
		{
			return false;
		}
		int n = 0;
		while (j < nLength && pBody[j] >= '0' && pBody[j] <= '9' && n < 100000)
		{
			n = n * 10 + (pBody[j++] - '0');
		}
		*pValue = bMinus ? -n : n;
		return true;
	}
	return false;
}

void AdminScreenRequest (const THttpRequest &rRequest, CAdminText *pResponse)
{
	static const char Type[] = "application/json; charset=utf-8";
	if (s_pScreenSource == 0)
	{
		static const char t[] = "{\"error\": \"pas d'ecran ici\"}";
		HttpResponse (pResponse, 404, Type, t, sizeof t - 1);
		return;
	}
	if (strcmp (rRequest.Path, "/api/screen") == 0)
	{
		const unsigned nSize = 160 * 1024;		// (a scene of an app: up to 64 KB, escaped)
		char *pJson = new char[nSize];
		unsigned n = pJson != 0 ? (*s_pScreenSource) (pJson, nSize) : 0;
		if (n == 0)
		{
			static const char t[] = "{\"error\": \"scene trop grande\"}";
			HttpResponse (pResponse, 500, Type, t, sizeof t - 1);
		}
		else
		{
			HttpResponse (pResponse, 200, Type, pJson, n);
		}
		delete [] pJson;
		return;
	}
	int x, y;
	if (   strcmp (rRequest.Method, "POST") != 0 || s_pTouch == 0
	    || !BodyInt (rRequest.pBody, rRequest.nBodyLength, "x", &x)
	    || !BodyInt (rRequest.pBody, rRequest.nBodyLength, "y", &y))
	{
		static const char t[] = "{\"error\": \"POST /api/touch {\\\"x\\\": 10, \\\"y\\\": 20}\"}";
		HttpResponse (pResponse, 400, Type, t, sizeof t - 1);
		return;
	}
	(*s_pTouch) (x, y);
	static const char t[] = "{\"ok\": true}";
	HttpResponse (pResponse, 200, Type, t, sizeof t - 1);
}

// a host name or address we can put in a Location header as it is
static bool SafeHost (const char *pHost)
{
	if (*pHost == '\0')
	{
		return false;
	}
	for (const char *p = pHost; *p != '\0'; p++)
	{
		char c = *p;
		if (!(   (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
		      || c == '.' || c == '-' || c == ':' || c == '[' || c == ']'))
		{
			return false;
		}
	}
	return true;
}

void HttpRedirectToHttps (CAdminText *pOut, const char *pHost, const char *pOwnIP)
{
	// the host without its port (HTTPS: the default 443)
	char Host[96];
	unsigned n = 0;
	while (pHost[n] != '\0' && pHost[n] != ':' && n < sizeof Host - 1)
	{
		Host[n] = pHost[n];
		n++;
	}
	Host[n] = '\0';

	// only our own names: any other one (a captive portal probe, which our DNS sent here)
	// would not match the certificate, and HSTS would forbid accepting it
	bool bOwn =    SafeHost (Host)
		    && (   strcmp (Host, pOwnIP) == 0
			|| (n > 6 && strcasecmp (Host + n - 6, ".local") == 0));

	CAdminText Headers;
	Headers.Printf ("Location: https://%s/\r\n", bOwn ? Host : pOwnIP);
	static const char Body[] = "Pocket : https\n";
	HttpResponse (pOut, 302, "text/plain; charset=utf-8", Body, sizeof Body - 1, Headers.Get ());
}
