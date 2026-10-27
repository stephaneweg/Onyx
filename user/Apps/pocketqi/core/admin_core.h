/* PocketQI (github.com/stephaneweg/PocketQI, commit 3eda2e5), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
//
// admin_core.h -- Pocket alpha admin_web : the portable core of the administration server
//
// Everything here builds both in the Circle kernel (src/) and on the PC (hosttest/): HTTP
// parsing, forms, pages, configuration files, TLS server (mbedTLS), DHCP and DNS packets.
// The platform provides the few functions declared under "platform" below (files, log), and a
// CAdminHost for what only the kernel knows (status, Wi-Fi scan, reboot).
//
// Secrets (API keys, Wi-Fi passphrase) are written to the SD card and never displayed, logged
// or sent back: the pages only say whether a key is present.
//
#ifndef _admin_core_h
#define _admin_core_h

#include <stddef.h>
#include <stdarg.h>

#define ADMIN_ETC		"SD:/etc"
#define ADMIN_WIFI_CONF		ADMIN_ETC "/wpa_supplicant.conf"
#define ADMIN_WIFI_CONF_ROOT	"SD:/wpa_supplicant.conf"	// (first location, still read by the kernel)
#define ADMIN_LLM_PROVIDER	ADMIN_ETC "/llm.provider"
#define ADMIN_LLM_CARACTERE	ADMIN_ETC "/llm.caractere"
#define ADMIN_GROQ_KEY_OLD	"SD:/groq.key"			// (first location of the Groq key)
#define ADMIN_HTTPS_KEY		ADMIN_ETC "/https.key"		// server private key (PEM, made on the Pi)
#define ADMIN_HTTPS_CRT		ADMIN_ETC "/https.crt"		// self-signed certificate (PEM)

#define ADMIN_AP_IP0	192				// access point mode: 192.168.4.1/24
#define ADMIN_AP_IP1	168
#define ADMIN_AP_IP2	4
#define ADMIN_AP_IP3	1

// ---- platform ---------------------------------------------------------------------------
// paths are "SD:/..." (the PC maps them to a folder)

// the whole file into pBuffer, NUL-terminated (at most nSize - 1 bytes); bytes read, -1 if absent
int AdminFileRead (const char *pPath, char *pBuffer, unsigned nSize);
// creates or replaces the file (ADMIN_ETC is created if needed)
bool AdminFileWrite (const char *pPath, const void *pData, unsigned nLength);
// true if the file no longer exists afterwards
bool AdminFileDelete (const char *pPath);
bool AdminFileExists (const char *pPath);
// the kernel log (ASCII); NEVER pass a key or a passphrase
void AdminLog (const char *pFormat, ...) __attribute__ ((format (printf, 1, 2)));
// (pour admin_apps.cpp) true if the directory exists afterwards
bool AdminMakeDir (const char *pPath);
// a file or a directory, on the same card; false if pTo exists or on error
bool AdminRename (const char *pFrom, const char *pTo);
// an empty directory
bool AdminRemoveDir (const char *pPath);
// the entries of a directory (not . and ..): number of entries, -1 if it is not a directory
typedef void TAdminDirEntry (const char *pName, bool bDir, void *pParam);
int AdminListDir (const char *pPath, TAdminDirEntry *pEntry, void *pParam);
// an app was installed, replaced or deleted: read the manifests again (like "skills reload")
void AdminAppsChanged (const char *pId);

// ---- growing text buffer ----------------------------------------------------------------

class CAdminText
{
public:
	CAdminText (void);
	~CAdminText (void);

	void Append (const char *pText);
	void Append (const char *pText, unsigned nLength);
	void AppendHtml (const char *pText);		// &<>"' escaped
	void Printf (const char *pFormat, ...) __attribute__ ((format (printf, 2, 3)));
	void Clear (bool bWipe = false);		// bWipe: zeroes the contents first

	const char *Get (void) const	{ return m_pData != 0 ? m_pData : ""; }
	unsigned GetLength (void) const	{ return m_nLength; }
	bool Failed (void) const	{ return m_bFailed; }	// out of memory somewhere

private:
	bool Reserve (unsigned nMore);

private:
	char *m_pData;
	unsigned m_nLength;
	unsigned m_nCapacity;
	bool m_bFailed;
};

// zeroes a buffer that held a secret (not optimized away)
void AdminWipe (void *pBuffer, unsigned nLength);

// ---- Wi-Fi --------------------------------------------------------------------------------

enum TWifiSecurity
{
	WifiSecOpen,
	WifiSecWEP,
	WifiSecWPA,
	WifiSecWPA2		// RSN (WPA2 or WPA3)
};

struct TWifiAP
{
	char SSID[33];
	unsigned char BSSID[6];
	int nLevel;			// dBm
	unsigned nChannel;
	unsigned char uSecurity;	// TWifiSecurity
	bool bConnected;
};

#define ADMIN_MAX_APS	32

// ---- what the kernel provides to the pages ------------------------------------------------

struct TAdminStatus
{
	bool bAPMode;			// access point (configuration) or Wi-Fi client
	char IP[16];
	char APName[33];		// Pocket-xxxx (access point mode)
	char Hostname[40];		// pocketqi
	char Version[48];
	const char *pReason;		// why the access point is open (0 in client mode)
	unsigned nUptime;		// seconds
	unsigned nDHCPLeases;		// access point mode: addresses handed out
};

class CAdminHost
{
public:
	virtual ~CAdminHost (void) {}

	virtual void GetStatus (TAdminStatus *pStatus) = 0;

	// the networks seen (strongest first); bRefresh: scan again first (a few seconds)
	// *pAge: seconds since that scan
	virtual unsigned GetScan (TWifiAP *pList, unsigned nMax, unsigned *pAge, bool bRefresh) = 0;

	// reboot in nDelayMs (the response leaves first)
	virtual void RequestReboot (unsigned nDelayMs) = 0;
};

// ---- configuration files (admin_config.cpp) ------------------------------------------------

// writes ADMIN_WIFI_CONF: the SSID in hex, the passphrase as its WPA PSK (PBKDF2, never in
// clear); pPassphrase "" = open network. false + pError (French, for the page)
bool WifiWriteConfig (const char *pSSID, const char *pPassphrase, const char *pCountry,
		      bool bHidden, char *pError, unsigned nErrorSize);
// the SSID of the saved configuration ("" if none); true if a usable network block exists
bool WifiReadConfig (char *pSSID, unsigned nSize, char *pCountry, unsigned nCountrySize);
// a configuration with at least one network (ssid=) in /etc or at the root
bool WifiConfigUsable (void);

// LLM providers: those of groq_proto.c (groq, mistral)
enum TKeyState { KeyAbsent, KeyPresent, KeyPresentOld };	// Old: SD:/groq.key
TKeyState LlmKeyState (const char *pProvider);
bool LlmWriteKey (const char *pProvider, const char *pKey, char *pError, unsigned nErrorSize);
bool LlmDeleteKey (const char *pProvider);
bool LlmReadModel (const char *pProvider, char *pModel, unsigned nSize);	// "" = default
bool LlmWriteModel (const char *pProvider, const char *pModel, char *pError, unsigned nErrorSize);
void LlmReadProvider (char *pProvider, unsigned nSize);				// "" = automatic
bool LlmWriteProvider (const char *pProvider, char *pError, unsigned nErrorSize);
int LlmReadCaractere (void);							// 0 (sérieuse) .. GROQ_CARACTERE_MAX
bool LlmWriteCaractere (int nCaractere, char *pError, unsigned nErrorSize);

// ---- HTTP (admin_http.cpp) ---------------------------------------------------------------

#define HTTP_MAX_REQUEST	8192	// headers + body
#define HTTP_MAX_BODY		4096
// POST /api/apps/<id> (admin_apps.cpp): a bigger body, only there
#define ADMIN_APP_MAX_BODY	(64 * 1024)
#define HTTP_MAX_UPLOAD		(ADMIN_APP_MAX_BODY + 4096)	// the request buffer of a connection

struct THttpRequest
{
	char Method[8];
	char Path[96];
	char Query[160];
	char Host[96];
	char Origin[128];		// "" if absent
	char Authorization[160];	// "" if absent (the API's optional password: wiped with the request)
	const char *pBody;		// inside the request buffer
	unsigned nBodyLength;
};

// 1 complete (pRequest filled), 0 incomplete (read more), -1 malformed or too big
int HttpParseRequest (char *pData, unsigned nLength, THttpRequest *pRequest);

// the value of a form field (application/x-www-form-urlencoded, or a query string), decoded;
// false if absent or longer than nSize - 1
bool FormGet (const char *pForm, unsigned nLength, const char *pName, char *pValue, unsigned nSize);

// a complete HTTP response (Connection: close)
void HttpResponse (CAdminText *pOut, unsigned nStatus, const char *pContentType, const char *pBody,
		   unsigned nBodyLength, const char *pExtraHeaders = 0);
// 302 to https://<host>/ (the plain HTTP port, captive portal probes): the Host header if it
// is our address or a .local name, else pOwnIP
void HttpRedirectToHttps (CAdminText *pOut, const char *pHost, const char *pOwnIP);

// ---- pages (admin_pages.cpp) --------------------------------------------------------------

// the response to one request
void AdminHandleRequest (CAdminHost *pHost, const THttpRequest &rRequest, CAdminText *pResponse);

// the pages' frame, for admin_mail.cpp (pHead: extra <head> markup, 0 = none)
void AdminPageBegin (CAdminText *pOut, const char *pTitle, const char *pActive, const char *pHead = 0);
void AdminPageEnd (CAdminText *pOut);
void AdminSendPage (CAdminText *pResponse, CAdminText *pBody);
void AdminSendRedirect (CAdminText *pResponse, const char *pLocation);

// ---- the kernel's mail account (admin_mail.cpp, ../../mail/src/pocket_mail.cpp) --------------

// GET / POST /mail: the address, the provider recognised and its servers, Gmail's app password
// or Outlook's device code (shown, the page refreshing itself until the token comes), the test
void AdminMailRequest (const THttpRequest &rRequest, CAdminText *pResponse);

// ---- the voice apps, for PocketQI Studio (admin_apps.cpp) --------------------------------------

#define ADMIN_APP_MAX_FILES	16
#define ADMIN_APP_MAX_FILE	(48 * 1024)
#define ADMIN_APPS_MAX		16		// POCKETAPPS_MAX (pocketapps.h): the kernel sees no more

// GET /api/apps, POST and DELETE /api/apps/<id> (JSON)
bool AdminIsAppsPath (const char *pPath);
void AdminAppsRequest (const THttpRequest &rRequest, CAdminText *pResponse);
bool AdminAppIdValid (const char *pId);		// a-z 0-9 _, 1 to 23 characters
// main.bas + SD:/lib/galo.bi compiled by Onyx BASIC; false + the file ("main.bas" / "galo.bi"), line and message
bool AdminAppCompile (const char *pMain, unsigned nMain, char *pFile, unsigned nFileSize, unsigned *pLine,
		      char *pError, unsigned nErrorSize);

// POST /api/apps/<id>/run (PocketQI Studio, F5 on a robot): the body is the action, then one argument
// per line (text/plain, UTF-8). The app is checked (installed, action in its manifest), then handed
// to the shell's runner, which launches it without the LLM, as "/run <id> <action> args" typed on
// its console, and returns at once (true), or refuses (false + pError). No runner set: 501.
typedef bool TAdminAppRunner (const char *pId, const char *pAction, const char *const *ppArgs, unsigned nArgs,
			      char *pError, unsigned nErrorSize);
void AdminSetAppRunner (TAdminAppRunner *pRunner);

// ---- the face, for PocketQI Studio's "Visage" window (admin_http.cpp) ---------------------------

// GET /api/face: {"humeur": "joyeux", "intensite": 2, "etat": "parle", "seq": 17} (JSON). Only the
// shell knows its mood and state: it installs a source that writes this JSON (bytes written, 0 =
// none). Without a source: 404. No password: the mood is not a secret, and nothing changes.
typedef unsigned TAdminFaceSource (char *pJson, unsigned nSize);
void AdminSetFaceSource (TAdminFaceSource *pSource);
bool AdminIsFacePath (const char *pPath);
void AdminFaceRequest (const THttpRequest &rRequest, CAdminText *pResponse);

// Pocket dessine (system/modules/dessin): GET /api/screen, the scene shown ({"seq", "largeur", "hauteur", "tactile", "proprio",
// "enfonce", "scene"}: bytes written, 0 = none); POST /api/touch {"x": 10, "y": 20}, a touch in device pixels (a click
// in PocketQI Studio's "Visage" window). Without a source: 404. No password, like /api/face.
typedef unsigned TAdminScreenSource (char *pJson, unsigned nSize);
typedef void TAdminTouch (int x, int y);
void AdminSetScreen (TAdminScreenSource *pSource, TAdminTouch *pTouch);
bool AdminIsScreenPath (const char *pPath);
void AdminScreenRequest (const THttpRequest &rRequest, CAdminText *pResponse);

// ---- TLS server (admin_tls.cpp) -----------------------------------------------------------

// loads ADMIN_HTTPS_KEY / ADMIN_HTTPS_CRT, or makes them (ECDSA P-256, self-signed, about
// 800 days, renewed 30 days before the end); false + pError if TLS cannot run
bool AdminTlsInit (char *pError, unsigned nErrorSize);
// SHA-256 of the certificate, "AB:CD:..." (to compare with the browser's)
const char *AdminTlsFingerprint (void);
const char *AdminTlsValidity (void);		// "2026-10-22 .. 2028-12-31"

struct TAdminConnIO
{
	void *pContext;
	int (*Send) (void *pContext, const void *pBuffer, unsigned nLength);	// bytes sent, < 0 error
	int (*Recv) (void *pContext, void *pBuffer, unsigned nLength);	// > 0, 0 nothing yet, < 0 closed
	void (*Sleep) (unsigned nMillis);
	unsigned (*Millis) (void);
};

struct TAdminTlsStats
{
	unsigned nConnections;
	unsigned nHandshakeFailures;
	unsigned nRequests;
	unsigned nLastHandshakeMs;
	int nLastError;			// mbedTLS error of the last failed connection
	char LastVersion[16];		// TLSv1.3
	char LastSuite[48];
};
const TAdminTlsStats *AdminTlsStats (void);

// one HTTPS connection, from the handshake to close_notify (timeout: 20 s in all)
void AdminServeTls (CAdminHost *pHost, const TAdminConnIO *pIO);

// ---- DHCP server (admin_dhcpd.cpp) ---------------------------------------------------------

#define DHCPD_FIRST_HOST	10	// 192.168.4.10 ...
#define DHCPD_MAX_LEASES	20	// ... 192.168.4.29
#define DHCPD_LEASE_SECS	3600

struct TDhcpLease
{
	unsigned char MAC[6];
	unsigned nExpires;		// seconds (the caller's clock); 0 = free
	bool bOffered;			// offered, not yet acknowledged
};

class CDhcpServerCore
{
public:
	CDhcpServerCore (const unsigned char ServerIP[4]);

	// one request (UDP 67) -> reply to broadcast on port 68; reply length, 0 = no reply
	unsigned Handle (const unsigned char *pRequest, unsigned nLength, unsigned char *pReply,
			 unsigned nReplySize, unsigned nNow);

	unsigned GetLeaseCount (unsigned nNow) const;	// acknowledged and not expired

private:
	int FindLease (const unsigned char *pMAC, unsigned nNow, bool bAllocate);

private:
	unsigned char m_ServerIP[4];
	TDhcpLease m_Leases[DHCPD_MAX_LEASES];
};

// ---- DNS: every name -> our address (admin_dnsd.cpp), access point mode only -------------

// reply length, 0 = ignore
unsigned DnsCatchAll (const unsigned char *pQuery, unsigned nLength, unsigned char *pReply,
		      unsigned nReplySize, const unsigned char IP[4]);

#endif
