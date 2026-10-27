/* PocketQI (github.com/stephaneweg/PocketQI, commit 3eda2e5), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
//
// admin_tls.cpp -- Pocket alpha admin_web : the HTTPS server side (mbedTLS 3.6.3 of Onyx)
//
// Certificate: ECDSA P-256, self-signed, made on the robot the first time (hardware RNG of the
// BCM2837 through kapi_random), kept in SD:/etc/https.key and SD:/etc/https.crt. Subject
// "pocketqi.local", alternative names pocketqi.local and 192.168.4.1, serverAuth: what browsers
// expect of a server certificate, even one accepted by hand. About 800 days (Apple refuses
// longer ones), renewed at boot 30 days before the end; a damaged pair is made again.
//
// Each connection: TLS 1.2 or 1.3, one HTTP request, the response, close_notify. No session
// cache or tickets on this side: a full handshake each time (about 0.1 s on the Pi).
//
#include "admin_core.h"
#include "kapi_circle.h"		// kapi_random, kapi_get_datetime (Circle or PC)
#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/ecp.h>
#include <mbedtls/oid.h>
#include <mbedtls/sha256.h>
#include <mbedtls/net_sockets.h>	// MBEDTLS_ERR_NET_* (codes only)
#include <psa/crypto.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define PEM_MAX			4096
#define CERT_DAYS		800
#define CERT_RENEW_DAYS		30
#define HANDSHAKE_TIMEOUT_MS	10000
#define CONNECTION_TIMEOUT_MS	20000
#define CERT_SUBJECT		"CN=pocketqi.local,O=Pocket robot"

static bool s_bReady = false;
static mbedtls_entropy_context s_Entropy;
static mbedtls_ctr_drbg_context s_DRBG;
static mbedtls_ssl_config s_Conf;
static mbedtls_x509_crt s_Cert;
static mbedtls_pk_context s_Key;
static char s_Fingerprint[32 * 3 + 1];
static char s_Validity[48] = "-";
static TAdminTlsStats s_Stats;

static const char *s_ALPN[] = { "http/1.1", 0 };

// ---- dates ----------------------------------------------------------------------------------

static long DaysFromCivil (int y, int m, int d)		// H. Hinnant
{
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned) (y - era * 400);
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long) doe - 719468;
}

static void CivilFromDays (long z, int *pY, int *pM, int *pD)
{
	z += 719468;
	long era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned) (z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp = (5 * doy + 2) / 153;
	*pD = (int) (doy - (153 * mp + 2) / 5 + 1);
	*pM = (int) (mp < 10 ? mp + 3 : mp - 9);
	*pY = (int) (yoe + era * 400 + (*pM <= 2));
}

// today, in days since 1970; the clock may not be set (no NTP in access point mode): the
// kernel then has the last known time (SD:/etc/clock) or its build date
static long Today (bool *pKnown)
{
	int y, mo, d, h, mi, s;
	*pKnown = kapi_get_datetime (&y, &mo, &d, &h, &mi, &s) != 0;
	if (!*pKnown)
	{
		return DaysFromCivil (2026, 10, 1);
	}
	return DaysFromCivil (y, mo, d);
}

// ---- RNG --------------------------------------------------------------------------------------

// PSA crypto (TLS 1.3) is built with MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG: its random bytes are
// kapi_random's. Weak, as in onyx_tls.hpp (the https / llm commands have the same one).
#if defined(MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG)
extern "C" __attribute__ ((weak)) psa_status_t mbedtls_psa_external_get_random (
	mbedtls_psa_external_random_context_t *pContext, uint8_t *pOut, size_t nSize, size_t *pLength)
{
	(void) pContext;
	size_t nGot = 0;
	while (nGot < nSize)
	{
		unsigned nWant = nSize - nGot > 4096 ? 4096u : (unsigned) (nSize - nGot);
		int n = kapi_random (pOut + nGot, nWant);
		if (n <= 0)
		{
			*pLength = nGot;
			return PSA_ERROR_INSUFFICIENT_ENTROPY;
		}
		nGot += (size_t) n;
	}
	*pLength = nGot;
	return PSA_SUCCESS;
}
#endif

static int HardwareEntropy (void *pData, unsigned char *pOut, size_t nLength, size_t *pResult)
{
	(void) pData;
	int n = kapi_random (pOut, (unsigned) nLength);
	if (n <= 0)
	{
		*pResult = 0;
		return -1;
	}
	*pResult = (size_t) n;
	return 0;
}

// ---- certificate --------------------------------------------------------------------------

static bool LoadPair (bool *pRenew)
{
	*pRenew = false;
	char *pPEM = (char *) malloc (PEM_MAX);
	if (pPEM == 0)
	{
		return false;
	}

	bool bOK = false;
	int n = AdminFileRead (ADMIN_HTTPS_KEY, pPEM, PEM_MAX);
	if (   n > 0
	    && mbedtls_pk_parse_key (&s_Key, (const unsigned char *) pPEM, (size_t) n + 1, 0, 0,
				     mbedtls_ctr_drbg_random, &s_DRBG) == 0)
	{
		AdminWipe (pPEM, PEM_MAX);
		n = AdminFileRead (ADMIN_HTTPS_CRT, pPEM, PEM_MAX);
		bOK =    n > 0
		      && mbedtls_x509_crt_parse (&s_Cert, (const unsigned char *) pPEM, (size_t) n + 1) == 0
		      && mbedtls_pk_check_pair (&s_Cert.pk, &s_Key, mbedtls_ctr_drbg_random, &s_DRBG) == 0;
	}
	AdminWipe (pPEM, PEM_MAX);
	free (pPEM);

	if (bOK)
	{
		bool bKnown;
		long nToday = Today (&bKnown);
		long nEnd = DaysFromCivil (s_Cert.valid_to.year, s_Cert.valid_to.mon, s_Cert.valid_to.day);
		*pRenew = bKnown && nEnd - nToday < CERT_RENEW_DAYS;
	}
	return bOK;
}

static bool MakePair (char *pError, unsigned nErrorSize)
{
	mbedtls_pk_free (&s_Key);
	mbedtls_pk_init (&s_Key);
	mbedtls_x509_crt_free (&s_Cert);
	mbedtls_x509_crt_init (&s_Cert);

	int ret = mbedtls_pk_setup (&s_Key, mbedtls_pk_info_from_type (MBEDTLS_PK_ECKEY));
	if (ret == 0)
	{
		ret = mbedtls_ecp_gen_key (MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec (s_Key),
					   mbedtls_ctr_drbg_random, &s_DRBG);
	}
	if (ret != 0)
	{
		snprintf (pError, nErrorSize, "ECDSA key generation failed (-0x%04X)", (unsigned) -ret);
		return false;
	}

	bool bKnown;
	long nFrom = Today (&bKnown) - 1;		// (a day of margin for the browser's clock)
	long nTo = nFrom + CERT_DAYS;
	int y1, m1, d1, y2, m2, d2;
	CivilFromDays (nFrom, &y1, &m1, &d1);
	CivilFromDays (nTo, &y2, &m2, &d2);
	char NotBefore[16], NotAfter[16];
	snprintf (NotBefore, sizeof NotBefore, "%04d%02d%02d000000", y1, m1, d1);
	snprintf (NotAfter, sizeof NotAfter, "%04d%02d%02d235959", y2, m2, d2);

	unsigned char Serial[16];
	kapi_random (Serial, sizeof Serial);
	Serial[0] = (unsigned char) ((Serial[0] & 0x7F) | 0x40);	// positive, 16 bytes

	static const unsigned char APAddress[4] = { ADMIN_AP_IP0, ADMIN_AP_IP1, ADMIN_AP_IP2, ADMIN_AP_IP3 };
	static const char DNSName[] = "pocketqi.local";
	mbedtls_x509_san_list SANIP, SANDNS;
	memset (&SANIP, 0, sizeof SANIP);
	memset (&SANDNS, 0, sizeof SANDNS);
	SANDNS.node.type = MBEDTLS_X509_SAN_DNS_NAME;
	SANDNS.node.san.unstructured_name.p = (unsigned char *) DNSName;
	SANDNS.node.san.unstructured_name.len = sizeof DNSName - 1;
	SANDNS.next = &SANIP;
	SANIP.node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
	SANIP.node.san.unstructured_name.p = (unsigned char *) APAddress;
	SANIP.node.san.unstructured_name.len = sizeof APAddress;
	SANIP.next = 0;

	mbedtls_asn1_sequence EKU;
	memset (&EKU, 0, sizeof EKU);
	EKU.buf.tag = MBEDTLS_ASN1_OID;
	EKU.buf.p = (unsigned char *) MBEDTLS_OID_SERVER_AUTH;
	EKU.buf.len = MBEDTLS_OID_SIZE (MBEDTLS_OID_SERVER_AUTH);

	mbedtls_x509write_cert Writer;
	mbedtls_x509write_crt_init (&Writer);
	mbedtls_x509write_crt_set_version (&Writer, MBEDTLS_X509_CRT_VERSION_3);
	mbedtls_x509write_crt_set_md_alg (&Writer, MBEDTLS_MD_SHA256);
	mbedtls_x509write_crt_set_subject_key (&Writer, &s_Key);
	mbedtls_x509write_crt_set_issuer_key (&Writer, &s_Key);

	char *pPEM = (char *) malloc (PEM_MAX);
	if (pPEM == 0)
	{
		ret = MBEDTLS_ERR_X509_ALLOC_FAILED;
	}
	if (ret == 0) ret = mbedtls_x509write_crt_set_subject_name (&Writer, CERT_SUBJECT);
	if (ret == 0) ret = mbedtls_x509write_crt_set_issuer_name (&Writer, CERT_SUBJECT);
	if (ret == 0) ret = mbedtls_x509write_crt_set_serial_raw (&Writer, Serial, sizeof Serial);
	if (ret == 0) ret = mbedtls_x509write_crt_set_validity (&Writer, NotBefore, NotAfter);
	if (ret == 0) ret = mbedtls_x509write_crt_set_basic_constraints (&Writer, 0, -1);
	if (ret == 0) ret = mbedtls_x509write_crt_set_key_usage (&Writer, MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
	if (ret == 0) ret = mbedtls_x509write_crt_set_ext_key_usage (&Writer, &EKU);
	if (ret == 0) ret = mbedtls_x509write_crt_set_subject_alternative_name (&Writer, &SANDNS);
	if (ret == 0) ret = mbedtls_x509write_crt_set_subject_key_identifier (&Writer);
	if (ret == 0) ret = mbedtls_x509write_crt_set_authority_key_identifier (&Writer);
	if (ret == 0) ret = mbedtls_x509write_crt_pem (&Writer, (unsigned char *) pPEM, PEM_MAX,
						       mbedtls_ctr_drbg_random, &s_DRBG);
	if (ret == 0)
	{
		ret = mbedtls_x509_crt_parse (&s_Cert, (const unsigned char *) pPEM, strlen (pPEM) + 1);
	}
	if (ret == 0 && !AdminFileWrite (ADMIN_HTTPS_CRT, pPEM, strlen (pPEM)))
	{
		ret = -1;
	}
	if (ret == 0)
	{
		ret = mbedtls_pk_write_key_pem (&s_Key, (unsigned char *) pPEM, PEM_MAX);
		if (ret == 0 && !AdminFileWrite (ADMIN_HTTPS_KEY, pPEM, strlen (pPEM)))
		{
			ret = -1;
		}
	}
	mbedtls_x509write_crt_free (&Writer);
	if (pPEM != 0)
	{
		AdminWipe (pPEM, PEM_MAX);
		free (pPEM);
	}

	if (ret == -1)
	{
		snprintf (pError, nErrorSize, "cannot write " ADMIN_HTTPS_CRT " / .key");
		return false;
	}
	if (ret != 0)
	{
		snprintf (pError, nErrorSize, "certificate creation failed (-0x%04X)", (unsigned) -ret);
		return false;
	}
	AdminLog ("New HTTPS certificate (ECDSA P-256, %s .. %s, clock %s)", NotBefore, NotAfter,
		  bKnown ? "set" : "not set");
	return true;
}

bool AdminTlsInit (char *pError, unsigned nErrorSize)
{
	if (s_bReady)
	{
		return true;
	}
	if (psa_crypto_init () != PSA_SUCCESS)
	{
		snprintf (pError, nErrorSize, "PSA crypto init failed");
		return false;
	}

	mbedtls_entropy_init (&s_Entropy);
	mbedtls_ctr_drbg_init (&s_DRBG);
	mbedtls_ssl_config_init (&s_Conf);
	mbedtls_x509_crt_init (&s_Cert);
	mbedtls_pk_init (&s_Key);

	int ret = mbedtls_entropy_add_source (&s_Entropy, HardwareEntropy, 0, 32,
					      MBEDTLS_ENTROPY_SOURCE_STRONG);
	if (ret == 0)
	{
		ret = mbedtls_ctr_drbg_seed (&s_DRBG, mbedtls_entropy_func, &s_Entropy,
					     (const unsigned char *) "galo-admin", 10);
	}
	if (ret != 0)
	{
		snprintf (pError, nErrorSize, "RNG seed failed (-0x%04X)", (unsigned) -ret);
		return false;
	}

	bool bRenew;
	if (!LoadPair (&bRenew) || bRenew)
	{
		AdminLog (bRenew ? "HTTPS certificate expires soon: making a new one"
				 : "No usable HTTPS certificate in " ADMIN_ETC ": making one");
		if (!MakePair (pError, nErrorSize))
		{
			return false;
		}
	}

	unsigned char Hash[32];
	mbedtls_sha256 (s_Cert.raw.p, s_Cert.raw.len, Hash, 0);
	for (unsigned i = 0; i < sizeof Hash; i++)
	{
		snprintf (s_Fingerprint + 3 * i, 4, i + 1 < sizeof Hash ? "%02X:" : "%02X", Hash[i]);
	}
	snprintf (s_Validity, sizeof s_Validity, "%04d-%02d-%02d .. %04d-%02d-%02d",
		  s_Cert.valid_from.year, s_Cert.valid_from.mon, s_Cert.valid_from.day,
		  s_Cert.valid_to.year, s_Cert.valid_to.mon, s_Cert.valid_to.day);

	ret = mbedtls_ssl_config_defaults (&s_Conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
					   MBEDTLS_SSL_PRESET_DEFAULT);
	if (ret == 0)
	{
		mbedtls_ssl_conf_rng (&s_Conf, mbedtls_ctr_drbg_random, &s_DRBG);
		mbedtls_ssl_conf_min_tls_version (&s_Conf, MBEDTLS_SSL_VERSION_TLS1_2);
		mbedtls_ssl_conf_max_tls_version (&s_Conf, MBEDTLS_SSL_VERSION_TLS1_3);
		mbedtls_ssl_conf_authmode (&s_Conf, MBEDTLS_SSL_VERIFY_NONE);
		ret = mbedtls_ssl_conf_alpn_protocols (&s_Conf, s_ALPN);
	}
	if (ret == 0)
	{
		ret = mbedtls_ssl_conf_own_cert (&s_Conf, &s_Cert, &s_Key);
	}
	if (ret != 0)
	{
		snprintf (pError, nErrorSize, "TLS server configuration failed (-0x%04X)", (unsigned) -ret);
		return false;
	}

	s_bReady = true;
	AdminLog ("HTTPS certificate SHA-256 %s", s_Fingerprint);
	return true;
}

const char *AdminTlsFingerprint (void)
{
	return s_bReady ? s_Fingerprint : "-";
}

const char *AdminTlsValidity (void)
{
	return s_Validity;
}

const TAdminTlsStats *AdminTlsStats (void)
{
	return &s_Stats;
}

// ---- one connection -----------------------------------------------------------------------

static int BioSend (void *pContext, const unsigned char *pBuffer, size_t nLength)
{
	const TAdminConnIO *pIO = (const TAdminConnIO *) pContext;
	int n = pIO->Send (pIO->pContext, pBuffer, (unsigned) nLength);
	return n >= 0 ? n : MBEDTLS_ERR_NET_SEND_FAILED;
}

static int BioRecv (void *pContext, unsigned char *pBuffer, size_t nLength)
{
	const TAdminConnIO *pIO = (const TAdminConnIO *) pContext;
	int n = pIO->Recv (pIO->pContext, pBuffer, (unsigned) nLength);
	if (n > 0)
	{
		return n;
	}
	return n == 0 ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_CONN_RESET;
}

static bool Again (int ret)
{
	return ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE;
}

void AdminServeTls (CAdminHost *pHost, const TAdminConnIO *pIO)
{
	if (!s_bReady)
	{
		return;
	}
	s_Stats.nConnections++;

	mbedtls_ssl_context SSL;
	mbedtls_ssl_init (&SSL);
	char *pRequest = 0;
	unsigned nStart = pIO->Millis ();
	int ret = mbedtls_ssl_setup (&SSL, &s_Conf);
	if (ret == 0)
	{
		mbedtls_ssl_set_bio (&SSL, (void *) pIO, BioSend, BioRecv, 0);
		while (Again (ret = mbedtls_ssl_handshake (&SSL)))
		{
			if (pIO->Millis () - nStart > HANDSHAKE_TIMEOUT_MS)
			{
				ret = MBEDTLS_ERR_SSL_TIMEOUT;
				break;
			}
			pIO->Sleep (2);
		}
	}
	if (ret != 0)
	{
		// (often harmless: a browser closing its connection after the certificate warning)
		s_Stats.nHandshakeFailures++;
		s_Stats.nLastError = ret;
		mbedtls_ssl_free (&SSL);
		return;
	}
	s_Stats.nLastHandshakeMs = pIO->Millis () - nStart;
	strncpy (s_Stats.LastVersion, mbedtls_ssl_get_version (&SSL), sizeof s_Stats.LastVersion - 1);
	strncpy (s_Stats.LastSuite, mbedtls_ssl_get_ciphersuite (&SSL), sizeof s_Stats.LastSuite - 1);

	// the request (headers + body: up to an app for POST /api/apps/<id>)
	pRequest = (char *) malloc (HTTP_MAX_UPLOAD + 1);
	unsigned nLength = 0;
	THttpRequest Request;
	int nParsed = 0;
	while (pRequest != 0 && nParsed == 0)
	{
		ret = mbedtls_ssl_read (&SSL, (unsigned char *) pRequest + nLength, HTTP_MAX_UPLOAD - nLength);
		if (ret > 0)
		{
			nLength += (unsigned) ret;
			pRequest[nLength] = '\0';
			nParsed = HttpParseRequest (pRequest, nLength, &Request);
		}
		else if (Again (ret))
		{
			if (pIO->Millis () - nStart > CONNECTION_TIMEOUT_MS)
			{
				break;
			}
			pIO->Sleep (2);
		}
		else
		{
			break;				// closed (a preconnection never used) or error
		}
	}

	CAdminText Response;
	if (nParsed > 0)
	{
		s_Stats.nRequests++;
		AdminLog ("https %s %s", Request.Method, Request.Path);
		AdminHandleRequest (pHost, Request, &Response);
	}
	else if (nParsed < 0)
	{
		static const char Text[] = "Pocket : requete invalide\n";
		HttpResponse (&Response, 400, "text/plain; charset=utf-8", Text, sizeof Text - 1);
	}

	const unsigned char *p = (const unsigned char *) Response.Get ();
	unsigned nLeft = Response.GetLength ();
	while (nLeft > 0)
	{
		ret = mbedtls_ssl_write (&SSL, p, nLeft);
		if (ret > 0)
		{
			p += ret;
			nLeft -= (unsigned) ret;
		}
		else if (Again (ret) && pIO->Millis () - nStart <= CONNECTION_TIMEOUT_MS)
		{
			pIO->Sleep (2);
		}
		else
		{
			break;
		}
	}
	for (unsigned i = 0; i < 50 && Again (mbedtls_ssl_close_notify (&SSL)); i++)
	{
		pIO->Sleep (2);
	}

	Response.Clear (true);
	if (pRequest != 0)
	{
		AdminWipe (pRequest, HTTP_MAX_UPLOAD + 1);	// (a key or a passphrase was in it)
		free (pRequest);
	}
	mbedtls_ssl_free (&SSL);
}
