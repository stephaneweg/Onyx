/*
 * Copyright 2020 Vincent Sanders <vince@netsurf-browser.org>
 *
 * This file is part of NetSurf.
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * content generator for the about scheme certificate page
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "utils/errors.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "netsurf/inttypes.h"
#include "netsurf/ssl_certs.h"

#include "private.h"
#include "certificate.h"

/**
 * certificate name parameters
 */
struct ns_cert_name {
	char *common_name;
	char *organisation;
	char *organisation_unit;
	char *locality;
	char *province;
	char *country;
};

/**
 * Certificate public key parameters
 */
struct ns_cert_pkey {
	char *algor;
	int size;
	char *modulus;
	char *exponent;
	char *curve;
	char *public;
};

/**
 * Certificate subject alternative name
 */
struct ns_cert_san {
	struct ns_cert_san *next;
	char *name;
};

/**
 * certificate information for certificate chain
 */
struct ns_cert_info {
	struct ns_cert_name subject_name; /**< Subject details */
	struct ns_cert_name issuer_name; /**< Issuer details */
	struct ns_cert_pkey public_key; /**< public key details */
	long version;		/**< Certificate version */
	char *not_before;	/**< Valid from date */
	char *not_after;	/**< Valid to date */
	int sig_type;		/**< Signature type */
	char *sig_algor;        /**< Signature Algorithm */
	char *serialnum;	/**< Serial number */
	char *sha1fingerprint; /**< fingerprint shar1 encoded */
	char *sha256fingerprint; /**< fingerprint shar256 encoded */
	struct ns_cert_san *san; /**< subject alternative names */
	ssl_cert_err err;       /**< Whatever is wrong with this certificate */
};

/**
 * free all resources associated with a certificate information structure
 */
static nserror free_ns_cert_info(struct ns_cert_info *cinfo)
{
	struct ns_cert_san *san;

	free(cinfo->subject_name.common_name);
	free(cinfo->subject_name.organisation);
	free(cinfo->subject_name.organisation_unit);
	free(cinfo->subject_name.locality);
	free(cinfo->subject_name.province);
	free(cinfo->subject_name.country);
	free(cinfo->issuer_name.common_name);
	free(cinfo->issuer_name.organisation);
	free(cinfo->issuer_name.organisation_unit);
	free(cinfo->issuer_name.locality);
	free(cinfo->issuer_name.province);
	free(cinfo->issuer_name.country);
	free(cinfo->public_key.algor);
	free(cinfo->public_key.modulus);
	free(cinfo->public_key.exponent);
	free(cinfo->public_key.curve);
	free(cinfo->public_key.public);
	free(cinfo->not_before);
	free(cinfo->not_after);
	free(cinfo->sig_algor);
	free(cinfo->serialnum);

	/* free san list avoiding use after free */
	san = cinfo->san;
	while (san != NULL) {
		struct ns_cert_san *next;
		next = san->next;
		free(san);
		san = next;
	}

	free(cinfo);

	return NSERROR_OK;
}

#ifdef WITH_OPENSSL

#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#if (OPENSSL_VERSION_NUMBER < 0x30000000L)
/* OpenSSL 1.1.1 or LibreSSL */

# if defined(LIBRESSL_VERSION_NUMBER)
  /* LibreSSL */
#  if (LIBRESSL_VERSION_NUMBER < 0x3050000fL)
   /* LibreSSL <3.5.0 */

#   if (LIBRESSL_VERSION_NUMBER < 0x2070000fL)
    /* LibreSSL <2.7.0 */
static int ns_X509_get_signature_nid(X509 *cert)
{
	return OBJ_obj2nid(cert->cert_info->key->algor->algorithm);
}

static const unsigned char *ns_ASN1_STRING_get0_data(ASN1_STRING *asn1str)
{
	return (const unsigned char *)ASN1_STRING_data(asn1str);
}
#   else
#    define ns_X509_get_signature_nid X509_get_signature_nid
#    define ns_ASN1_STRING_get0_data ASN1_STRING_get0_data
#   endif

static const BIGNUM *ns_RSA_get0_n(const RSA *d)
{
	return d->n;
}

static const BIGNUM *ns_RSA_get0_e(const RSA *d)
{
	return d->e;
}
#  else
   /* LibreSSL >= 3.5.0 */
#   define ns_X509_get_signature_nid X509_get_signature_nid
#   define ns_ASN1_STRING_get0_data ASN1_STRING_get0_data
#   define ns_RSA_get0_n RSA_get0_n
#   define ns_RSA_get0_e RSA_get0_e
#  endif
# else
  /* OpenSSL 1.1.1 */
#  define ns_X509_get_signature_nid X509_get_signature_nid
#  define ns_ASN1_STRING_get0_data ASN1_STRING_get0_data
#  define ns_RSA_get0_n RSA_get0_n
#  define ns_RSA_get0_e RSA_get0_e
# endif

static int ns_EVP_PKEY_get_bn_param(const EVP_PKEY *pkey,
		const char *key_name, BIGNUM **bn) {
	RSA *rsa;
	BIGNUM *result = NULL;

	/* Check parameters: only support allocation-form *bn */
	if (pkey == NULL || key_name == NULL || bn == NULL || *bn != NULL)
		return 0;

	/* Only support RSA keys */
	if (EVP_PKEY_base_id(pkey) != EVP_PKEY_RSA)
		return 0;

	rsa = EVP_PKEY_get1_RSA((EVP_PKEY *) pkey);
	if (rsa == NULL)
		return 0;

	if (strcmp(key_name, "n") == 0) {
		const BIGNUM *n = ns_RSA_get0_n(rsa);
		if (n != NULL)
			result = BN_dup(n);
	} else if (strcmp(key_name, "e") == 0) {
		const BIGNUM *e = ns_RSA_get0_e(rsa);
		if (e != NULL)
			result = BN_dup(e);
	}

	RSA_free(rsa);

	*bn = result;

	return (result != NULL) ? 1 : 0;
}

static int ns_EVP_PKEY_get_utf8_string_param(const EVP_PKEY *pkey,
		const char *key_name, char *str, size_t max_len,
		size_t *out_len)
{
	const EC_GROUP *ecgroup;
	const char *group = "";
	size_t group_len = 0;
	EC_KEY *ec;
	int ret = 0;

	if (pkey == NULL || key_name == NULL)
		return 0;

	/* Only support EC keys */
	if (EVP_PKEY_base_id(pkey) != EVP_PKEY_EC)
		return 0;

	/* Only support fetching the group */
	if (strcmp(key_name, "group") != 0)
		return 0;

	ec = EVP_PKEY_get1_EC_KEY((EVP_PKEY *) pkey);

	ecgroup = EC_KEY_get0_group(ec);
	if (ecgroup != NULL) {
		group = OBJ_nid2ln(EC_GROUP_get_curve_name(ecgroup));
		group_len = strlen(group);
	}

	if (str != NULL && max_len > group_len) {
		memcpy(str, group, group_len);
		str[group_len] = '\0';
		ret = 1;
	}

	if (out_len != NULL) {
		*out_len = group_len;
	}

	EC_KEY_free(ec);

	return ret;
}

static int ns_EVP_PKEY_get_octet_string_param(const EVP_PKEY *pkey,
		const char *key_name, unsigned char *buf, size_t max_len,
		size_t *out_len)
{
	const EC_GROUP *ecgroup;
	const EC_POINT *ecpoint;
	size_t len;
	BN_CTX *bnctx;
	EC_KEY *ec;
	int ret = 0;

	if (pkey == NULL || key_name == NULL)
		return 0;

	/* Only support EC keys */
	if (EVP_PKEY_base_id(pkey) != EVP_PKEY_EC)
		return 0;

	if (strcmp(key_name, "encoded-pub-key") != 0)
		return 0;

	ec = EVP_PKEY_get1_EC_KEY((EVP_PKEY *) pkey);
	if (ec == NULL)
		return 0;

	ecgroup = EC_KEY_get0_group(ec);
	if (ecgroup != NULL) {
		ecpoint = EC_KEY_get0_public_key(ec);
		if (ecpoint != NULL) {
			bnctx = BN_CTX_new();
			len = EC_POINT_point2oct(ecgroup,
						 ecpoint,
						 POINT_CONVERSION_UNCOMPRESSED,
						 NULL,
						 0,
						 bnctx);
			if (len != 0 && len <= max_len) {
				if (EC_POINT_point2oct(ecgroup,
						       ecpoint,
						       POINT_CONVERSION_UNCOMPRESSED,
						       buf,
						       len,
						       bnctx) == len)
					ret = 1;
			}
			if (out_len != NULL)
				*out_len = len;
			BN_CTX_free(bnctx);
		}
	}

	EC_KEY_free(ec);

	return ret;
}
#else
/* OpenSSL 3.x and later */
#define ns_X509_get_signature_nid X509_get_signature_nid
#define ns_ASN1_STRING_get0_data ASN1_STRING_get0_data
#define ns_RSA_get0_n RSA_get0_n
#define ns_RSA_get0_e RSA_get0_e
#define ns_EVP_PKEY_get_bn_param EVP_PKEY_get_bn_param
#define ns_EVP_PKEY_get_octet_string_param EVP_PKEY_get_octet_string_param
#define ns_EVP_PKEY_get_utf8_string_param EVP_PKEY_get_utf8_string_param
#endif

/**
 * extract certificate name information
 *
 * \param xname The X509 name to convert. The reference is borrowed so is not freeed
 * \param iname The info structure to recive the extracted parameters.
 * \return NSERROR_OK on success else error code
 */
static nserror
xname_to_info(X509_NAME *xname, struct ns_cert_name *iname)
{
	int entryidx;
	int entrycnt;
	X509_NAME_ENTRY *entry; /* current name entry */
	ASN1_STRING *value;
	const unsigned char *value_str;
	ASN1_OBJECT *name;
	int name_nid;
	char **field;

	entrycnt = X509_NAME_entry_count(xname);

	for (entryidx = 0; entryidx < entrycnt; entryidx++) {
		entry = X509_NAME_get_entry(xname, entryidx);
		name = X509_NAME_ENTRY_get_object(entry);
		name_nid = OBJ_obj2nid(name);
		value = X509_NAME_ENTRY_get_data(entry);
		value_str = ns_ASN1_STRING_get0_data(value);
		switch (name_nid) {
		case NID_commonName:
			field = &iname->common_name;
			break;
		case NID_countryName:
			field = &iname->country;
			break;
		case NID_localityName:
			field = &iname->locality;
			break;
		case NID_stateOrProvinceName:
			field = &iname->province;
			break;
		case NID_organizationName:
			field = &iname->organisation;
			break;
		case NID_organizationalUnitName:
			field = &iname->organisation_unit;
			break;
		default :
			field = NULL;
			break;
		}
		if (field != NULL) {
			*field = strdup((const char *)value_str);
			NSLOG(netsurf, DEEPDEBUG,
			      "NID:%d value: %s", name_nid, *field);
		} else {
			NSLOG(netsurf, DEEPDEBUG, "NID:%d", name_nid);
		}
	}

	/*
	 * ensure the common name is set to something, this being
	 *  missing means the certificate is broken but this should be
	 *  robust in the face of bad data
	 */
	if (iname->common_name == NULL) {
		iname->common_name = strdup("Unknown");
	}

	return NSERROR_OK;
}


/**
 * duplicate a hex formatted string inserting the colons
 *
 * \todo only uses html entity as separator because netsurfs line breaking
 *       fails otherwise.
 */
static char *hexdup(const char *hex)
{
	int hexlen;
	char *dst;
	char *out;
	int cn = 0;

	hexlen = strlen(hex);
	/* allow space fox XXYY to XX&#58;YY&#58; */
	dst = malloc(((hexlen * 7) + 6) / 2);

	if (dst != NULL) {
		for (out = dst; *hex != 0; hex++) {
			if (cn == 2) {
				cn = 0;
				*out++ = '&';
				*out++ = '#';
				*out++ = '5';
				*out++ = '8';
				*out++ = ';';
			}
			*out++ = *hex;
			cn++;
		}
		*out = 0;
	}
	return dst;
}


/**
 * create a hex formatted string inserting the colons from binary data
 *
 * \todo only uses html entity as separator because netsurfs line breaking
 *       fails otherwise.
 */
static char *bindup(unsigned char *bin, unsigned int binlen)
{
	char *dst;
	char *out;
	unsigned int idx;
	const char hex[] = { '0', '1', '2', '3', '4', '5', '6', '7',
			     '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

	/* allow space fox XY to expand to XX&#58;YY&#58; */
	dst = malloc(binlen * 7);

	if (dst != NULL) {
		out = dst;
		for (idx = 0; idx < binlen; idx++) {
			*out++ = hex[(bin[idx] & 0xf0) >> 4];
			*out++ = hex[bin[idx] & 0xf];

			*out++ = '&';
			*out++ = '#';
			*out++ = '5';
			*out++ = '8';
			*out++ = ';';
		}
		out -= 5;
		*out = 0;
	}
	return dst;
}


/**
 * extract RSA key information to info structure
 *
 * \param pkey The RSA key to examine.
 * \param ikey The public key info structure to fill
 * \rerun NSERROR_OK on success else error code.
 */
static nserror
rsa_to_info(EVP_PKEY *pkey, struct ns_cert_pkey *ikey)
{
	BIGNUM *n = NULL, *e = NULL;
	char *tmp;

	if (ns_EVP_PKEY_get_bn_param(pkey, "n", &n) != 1) {
		return NSERROR_BAD_PARAMETER;
	}

	if (ns_EVP_PKEY_get_bn_param(pkey, "e", &e) != 1) {
		BN_free(n);
		return NSERROR_BAD_PARAMETER;
	}

	ikey->algor = strdup("RSA");

	ikey->size = EVP_PKEY_bits(pkey);

	tmp = BN_bn2hex(n);
	if (tmp != NULL) {
		ikey->modulus = hexdup(tmp);
		OPENSSL_free(tmp);
	}

	tmp = BN_bn2dec(e);
	if (tmp != NULL) {
		ikey->exponent = strdup(tmp);
		OPENSSL_free(tmp);
	}

	BN_free(e);
	BN_free(n);

	return NSERROR_OK;
}


/**
 * extract DSA key information to info structure
 *
 * \param pkey The DSA key to examine.
 * \param ikey The public key info structure to fill
 * \rerun NSERROR_OK on success else error code.
 */
static nserror
dsa_to_info(EVP_PKEY *pkey, struct ns_cert_pkey *ikey)
{
	ikey->algor = strdup("DSA");

	ikey->size = EVP_PKEY_bits(pkey);

	return NSERROR_OK;
}


/**
 * extract DH key information to info structure
 *
 * \param pkey The DH key to examine.
 * \param ikey The public key info structure to fill
 * \rerun NSERROR_OK on success else error code.
 */
static nserror
dh_to_info(EVP_PKEY *pkey, struct ns_cert_pkey *ikey)
{
	ikey->algor = strdup("Diffie Hellman");

	ikey->size = EVP_PKEY_bits(pkey);

	return NSERROR_OK;
}


/**
 * extract EC key information to info structure
 *
 * \param pkey The EC key to examine.
 * \param ikey The public key info structure to fill
 * \rerun NSERROR_OK on success else error code.
 */
static nserror
ec_to_info(EVP_PKEY *pkey, struct ns_cert_pkey *ikey)
{
	size_t len;

	ikey->algor = strdup("Elliptic Curve");

	ikey->size = EVP_PKEY_bits(pkey);

	len = 0;
	ns_EVP_PKEY_get_utf8_string_param(pkey, "group", NULL, 0, &len);
	if (len != 0) {
		ikey->curve = malloc(len + 1);
		if (ikey->curve != NULL) {
			if (ns_EVP_PKEY_get_utf8_string_param(pkey, "group",
					ikey->curve, len + 1, NULL) == 0) {
				free(ikey->curve);
				ikey->curve = NULL;
			}
		}
	}

	len = 0;
	ns_EVP_PKEY_get_octet_string_param(pkey, "encoded-pub-key",
			NULL, 0, &len);
	if (len != 0) {
		unsigned char *point = malloc(len);
		if (point != NULL) {
			if (ns_EVP_PKEY_get_octet_string_param(pkey,
					"encoded-pub-key", point, len,
					NULL) == 1) {
				ikey->public = bindup(point, len);
			}
			free(point);
		}
	}

	return NSERROR_OK;
}

/**
 * extract public key information to info structure
 *
 * \param pkey the public key to examine. The reference is dropped on return
 * \param ikey The public key info structure to fill
 * \rerun NSERROR_OK on success else error code.
 */
static nserror
pkey_to_info(EVP_PKEY *pkey, struct ns_cert_pkey *ikey)
{
	nserror res;

	if (pkey == NULL) {
		return NSERROR_BAD_PARAMETER;
	}

	switch (EVP_PKEY_base_id(pkey)) {
	case EVP_PKEY_RSA:
		res = rsa_to_info(pkey, ikey);
		break;

	case EVP_PKEY_DSA:
		res = dsa_to_info(pkey, ikey);
		break;

	case EVP_PKEY_DH:
		res = dh_to_info(pkey, ikey);
		break;

	case EVP_PKEY_EC:
		res = ec_to_info(pkey, ikey);
		break;

	default:
		res = NSERROR_NOT_IMPLEMENTED;
		break;
	}

	EVP_PKEY_free(pkey);

	return res;
}

static nserror san_to_info(X509 *cert, struct ns_cert_san **prev_next)
{
	int idx;
	int san_names_nb = -1;
	const GENERAL_NAME *current_name;
	const unsigned char *dns_name;
	struct ns_cert_san *isan;

	STACK_OF(GENERAL_NAME) *san_names = NULL;

	san_names = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
	if (san_names == NULL) {
		return NSERROR_OK;
	}

	san_names_nb = sk_GENERAL_NAME_num(san_names);

	/* Check each name within the extension */
	for (idx = 0; idx < san_names_nb; idx++) {
		current_name = sk_GENERAL_NAME_value(san_names, idx);

		if (current_name->type == GEN_DNS) {
			/* extract DNS name into info structure */
			dns_name = ns_ASN1_STRING_get0_data(current_name->d.dNSName);

			isan = malloc(sizeof(struct ns_cert_san));
			if (isan != NULL) {
				isan->name = strdup((const char *)dns_name);
				isan->next = NULL;
				*prev_next = isan;
				prev_next = &isan->next;
			}
		}
	}

	/* AmiSSL can't cope with the "correct" mechanism of freeing
	 * the GENERAL_NAME stack, which is:
	 * sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
	 * So instead we do this open-coded loop which does the same:
	 */
	for (idx = 0; idx < san_names_nb; idx++) {
		GENERAL_NAME *entry = sk_GENERAL_NAME_pop(san_names);
		GENERAL_NAME_free(entry);
	}
	sk_GENERAL_NAME_free(san_names);

	return NSERROR_OK;
}

static nserror
der_to_certinfo(const uint8_t *der,
		size_t der_length,
		struct ns_cert_info *info)
{
	BIO *mem;
	BUF_MEM *buf;
	const ASN1_INTEGER *asn1_num;
	BIGNUM *bignum;
	X509 *cert;		/**< Pointer to certificate */

	if (der == NULL) {
		return NSERROR_OK;
	}

	cert = d2i_X509(NULL, &der, der_length);
	if (cert == NULL) {
		return NSERROR_INVALID;
	}

	/*
	 * get certificate version
	 *
	 * \note this is defined by standards (X.509 et al) to be one
	 *        less than the certificate version.
	 */
	info->version = X509_get_version(cert) + 1;

	/* not before date */
	mem = BIO_new(BIO_s_mem());
	ASN1_TIME_print(mem, X509_get_notBefore(cert));
	BIO_get_mem_ptr(mem, &buf);
	(void) BIO_set_close(mem, BIO_NOCLOSE);
	BIO_free(mem);
	info->not_before = calloc(1, buf->length + 1);
	if (info->not_before != NULL) {
		memcpy(info->not_before, buf->data, (unsigned)buf->length);
	}
	BUF_MEM_free(buf);

	/* not after date */
	mem = BIO_new(BIO_s_mem());
	ASN1_TIME_print(mem,
			X509_get_notAfter(cert));
	BIO_get_mem_ptr(mem, &buf);
	(void) BIO_set_close(mem, BIO_NOCLOSE);
	BIO_free(mem);
	info->not_after = calloc(1, buf->length + 1);
	if (info->not_after != NULL) {
		memcpy(info->not_after, buf->data, (unsigned)buf->length);
	}
	BUF_MEM_free(buf);

	/* signature type */
	info->sig_type = X509_get_signature_type(cert);

	/* signature algorithm */
	int pkey_nid = ns_X509_get_signature_nid(cert);
	if (pkey_nid != NID_undef) {
		const char* sslbuf = OBJ_nid2ln(pkey_nid);
		if (sslbuf != NULL) {
			info->sig_algor = strdup(sslbuf);
		}
	}

	/* serial number */
	asn1_num = X509_get_serialNumber(cert);
	if (asn1_num != NULL) {
		bignum = ASN1_INTEGER_to_BN(asn1_num, NULL);
		if (bignum != NULL) {
			char *tmp = BN_bn2hex(bignum);
			if (tmp != NULL) {
				info->serialnum = hexdup(tmp);
				OPENSSL_free(tmp);
			}
			BN_free(bignum);
			bignum = NULL;
		}
	}

	/* fingerprints */
	const EVP_MD *digest;
	unsigned int dig_len;
	unsigned char *buff;
	int rc;

	digest = EVP_sha1();
	buff = malloc(EVP_MD_size(digest));
	if (buff != NULL) {
		rc = X509_digest(cert, digest, buff, &dig_len);
		if ((rc == 1) && (dig_len == (unsigned int)EVP_MD_size(digest))) {
			info->sha1fingerprint = bindup(buff, dig_len);
		}
		free(buff);
	}

	digest = EVP_sha256();
	buff = malloc(EVP_MD_size(digest));
	if (buff != NULL) {
		rc = X509_digest(cert, digest, buff, &dig_len);
		if ((rc == 1) && (dig_len == (unsigned int)EVP_MD_size(digest))) {
			info->sha256fingerprint = bindup(buff, dig_len);
		}
		free(buff);
	}

	/* subject alternative names */
	san_to_info(cert, &info->san);

	/* issuer name */
	xname_to_info(X509_get_issuer_name(cert), &info->issuer_name);

	/* subject */
	xname_to_info(X509_get_subject_name(cert), &info->subject_name);

	/* public key */
	pkey_to_info(X509_get_pubkey(cert), &info->public_key);

	X509_free(cert);

	return NSERROR_OK;
}

/* copy certificate data */
static nserror
convert_chain_to_cert_info(const struct cert_chain *chain,
			   struct ns_cert_info **cert_info_out)
{
	struct ns_cert_info *certs;
	size_t depth;
	nserror res;

	certs = calloc(chain->depth, sizeof(struct ns_cert_info));
	if (certs == NULL) {
		return NSERROR_NOMEM;
	}

	for (depth = 0; depth < chain->depth;depth++) {
		res = der_to_certinfo(chain->certs[depth].der,
				      chain->certs[depth].der_length,
				      certs + depth);
		if (res != NSERROR_OK) {
			free(certs);
			return res;
		}
		certs[depth].err = chain->certs[depth].err;
	}

	*cert_info_out = certs;
	return NSERROR_OK;
}

#elif defined(WITH_MBEDTLS)

/* Onyx: the certificate viewer on mbedTLS (the Pi's TLS library: user/tls/onyx_tls.hpp) --
 * the same information as the OpenSSL code above extracts */
#include <mbedtls/x509_crt.h>
#include <mbedtls/oid.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/ecp.h>

/* binary to "XX&#58;YY..." (as bindup above) */
static char *mb_bindup(const unsigned char *bin, size_t binlen)
{
	static const char hex[] = "0123456789ABCDEF";
	char *dst, *out;
	size_t idx;

	if (binlen == 0)
		return NULL;
	dst = malloc(binlen * 7);
	if (dst == NULL)
		return NULL;
	out = dst;
	for (idx = 0; idx < binlen; idx++) {
		*out++ = hex[(bin[idx] & 0xf0) >> 4];
		*out++ = hex[bin[idx] & 0xf];
		memcpy(out, "&#58;", 5);
		out += 5;
	}
	out -= 5;
	*out = 0;
	return dst;
}

static char *mb_strndup(const unsigned char *p, size_t n)
{
	char *s = malloc(n + 1);
	if (s != NULL) {
		memcpy(s, p, n);
		s[n] = 0;
	}
	return s;
}

static void mb_name_to_info(const mbedtls_x509_name *n, struct ns_cert_name *iname)
{
	for (; n != NULL; n = n->next) {
		char **field = NULL;
		if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &n->oid) == 0)
			field = &iname->common_name;
		else if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_COUNTRY, &n->oid) == 0)
			field = &iname->country;
		else if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_LOCALITY, &n->oid) == 0)
			field = &iname->locality;
		else if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_STATE, &n->oid) == 0)
			field = &iname->province;
		else if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_ORGANIZATION, &n->oid) == 0)
			field = &iname->organisation;
		else if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_ORG_UNIT, &n->oid) == 0)
			field = &iname->organisation_unit;
		if (field != NULL && *field == NULL)
			*field = mb_strndup(n->val.p, n->val.len);
	}
	if (iname->common_name == NULL)
		iname->common_name = strdup("Unknown");
}

/* "Mar 21 20:44:09 2036 GMT", as OpenSSL's ASN1_TIME_print */
static char *mb_time(const mbedtls_x509_time *t)
{
	static const char *const mon[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	char b[48];
	snprintf(b, sizeof b, "%s %2d %02d:%02d:%02d %d GMT",
		 mon[t->mon >= 1 && t->mon <= 12 ? t->mon - 1 : 0], t->day,
		 t->hour, t->min, t->sec, t->year);
	return strdup(b);
}

static void mb_pkey_to_info(const mbedtls_pk_context *pk, struct ns_cert_pkey *ikey)
{
	mbedtls_pk_type_t type = mbedtls_pk_get_type(pk);

	ikey->size = (int) mbedtls_pk_get_bitlen(pk);
	if (type == MBEDTLS_PK_RSA || type == MBEDTLS_PK_RSASSA_PSS) {
		const mbedtls_rsa_context *rsa = mbedtls_pk_rsa(*pk);
		size_t nlen = mbedtls_rsa_get_len(rsa);
		unsigned char *nb = malloc(nlen), eb[8];
		ikey->algor = strdup("RSA");
		if (nb != NULL && mbedtls_rsa_export_raw(rsa, nb, nlen, NULL, 0, NULL, 0,
				NULL, 0, eb, sizeof eb) == 0) {
			unsigned long long e = 0;
			char es[24];
			size_t i;
			ikey->modulus = mb_bindup(nb, nlen);
			for (i = 0; i < sizeof eb; i++)
				e = (e << 8) | eb[i];
			snprintf(es, sizeof es, "%llu", e);
			ikey->exponent = strdup(es);
		}
		free(nb);
	} else if (type == MBEDTLS_PK_ECKEY || type == MBEDTLS_PK_ECKEY_DH ||
		   type == MBEDTLS_PK_ECDSA) {
		const mbedtls_ecp_keypair *ec = mbedtls_pk_ec(*pk);
		const mbedtls_ecp_curve_info *ci =
			mbedtls_ecp_curve_info_from_grp_id(mbedtls_ecp_keypair_get_group_id(ec));
		unsigned char pt[MBEDTLS_ECP_MAX_PT_LEN];
		size_t olen = 0;
		ikey->algor = strdup("Elliptic Curve");
		if (ci != NULL)
			ikey->curve = strdup(ci->name);
		if (mbedtls_ecp_write_public_key(ec, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen,
				pt, sizeof pt) == 0)
			ikey->public = mb_bindup(pt, olen);
	} else {
		ikey->algor = strdup(mbedtls_pk_get_name(pk));
	}
}

static nserror
der_to_certinfo(const uint8_t *der, size_t der_length, struct ns_cert_info *info)
{
	mbedtls_x509_crt crt;
	const mbedtls_x509_sequence *san;
	struct ns_cert_san **prev_next = &info->san;
	const char *desc = NULL;
	unsigned char dig[32];

	if (der == NULL)
		return NSERROR_OK;
	mbedtls_x509_crt_init(&crt);
	if (mbedtls_x509_crt_parse_der(&crt, der, der_length) != 0) {
		mbedtls_x509_crt_free(&crt);
		return NSERROR_INVALID;
	}
	info->version = crt.version;
	info->not_before = mb_time(&crt.valid_from);
	info->not_after = mb_time(&crt.valid_to);
	if (mbedtls_oid_get_sig_alg_desc(&crt.sig_oid, &desc) == 0 && desc != NULL)
		info->sig_algor = strdup(desc);
	info->serialnum = mb_bindup(crt.serial.p, crt.serial.len);
	if (mbedtls_sha1(der, der_length, dig) == 0)
		info->sha1fingerprint = mb_bindup(dig, 20);
	if (mbedtls_sha256(der, der_length, dig, 0) == 0)
		info->sha256fingerprint = mb_bindup(dig, 32);
	for (san = &crt.subject_alt_names; san != NULL; san = san->next) {
		struct ns_cert_san *isan;
		if (san->buf.p == NULL || san->buf.tag !=
				(MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_X509_SAN_DNS_NAME))
			continue;
		isan = malloc(sizeof(struct ns_cert_san));
		if (isan != NULL) {
			isan->name = mb_strndup(san->buf.p, san->buf.len);
			isan->next = NULL;
			*prev_next = isan;
			prev_next = &isan->next;
		}
	}
	mb_name_to_info(&crt.issuer, &info->issuer_name);
	mb_name_to_info(&crt.subject, &info->subject_name);
	mb_pkey_to_info(&crt.pk, &info->public_key);
	mbedtls_x509_crt_free(&crt);
	return NSERROR_OK;
}

/* copy certificate data */
static nserror
convert_chain_to_cert_info(const struct cert_chain *chain,
			   struct ns_cert_info **cert_info_out)
{
	struct ns_cert_info *certs;
	size_t depth;
	nserror res;

	certs = calloc(chain->depth, sizeof(struct ns_cert_info));
	if (certs == NULL) {
		return NSERROR_NOMEM;
	}

	for (depth = 0; depth < chain->depth;depth++) {
		res = der_to_certinfo(chain->certs[depth].der,
				      chain->certs[depth].der_length,
				      certs + depth);
		if (res != NSERROR_OK) {
			free(certs);
			return res;
		}
		certs[depth].err = chain->certs[depth].err;
	}

	*cert_info_out = certs;
	return NSERROR_OK;
}

#else
static nserror
convert_chain_to_cert_info(const struct cert_chain *chain,
			   struct ns_cert_info **cert_info_out)
{
	return NSERROR_NOT_IMPLEMENTED;
}
#endif


static nserror
format_certificate_name(struct fetch_about_context *ctx,
			struct ns_cert_name *cert_name)
{
	nserror res;
	res = fetch_about_ssenddataf(ctx,
			 "<tr><th>Common Name</th><td>%s</td></tr>\n",
			 cert_name->common_name);
	if (res != NSERROR_OK) {
		return res;
	}

	if (cert_name->organisation != NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<tr><th>Organisation</th><td>%s</td></tr>\n",
				 cert_name->organisation);
		if (res != NSERROR_OK) {
			return res;
		}
	}

	if (cert_name->organisation_unit != NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<tr><th>Organisation Unit</th><td>%s</td></tr>\n",
				 cert_name->organisation_unit);
		if (res != NSERROR_OK) {
			return res;
		}
	}

	if (cert_name->locality != NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<tr><th>Locality</th><td>%s</td></tr>\n",
				 cert_name->locality);
		if (res != NSERROR_OK) {
			return res;
		}
	}

	if (cert_name->province != NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<tr><th>Province</th><td>%s</td></tr>\n",
				 cert_name->province);
		if (res != NSERROR_OK) {
			return res;
		}
	}

	if (cert_name->country != NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<tr><th>Country</th><td>%s</td></tr>\n",
				 cert_name->country);
		if (res != NSERROR_OK) {
			return res;
		}
	}

	return res;
}

/**
 * output formatted certificate subject alternate names
 */
static nserror
format_certificate_san(struct fetch_about_context *ctx,
			      struct ns_cert_san *san)
{
	nserror res;

	if (san == NULL) {
		return NSERROR_OK;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<table class=\"info\">\n"
			 "<tr><th>Alternative Names</th><td><hr></td></tr>\n");
	if (res != NSERROR_OK) {
		return res;
	}

	while (san != NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<tr><th>DNS Name</th><td>%s</td></tr>\n",
				 san->name);
		if (res != NSERROR_OK) {
			return res;
		}

		san = san->next;
	}

	res = fetch_about_ssenddataf(ctx, "</table>\n");

	return res;

}


static nserror
format_certificate_public_key(struct fetch_about_context *ctx,
			      struct ns_cert_pkey *public_key)
{
	nserror res;

	if (public_key->algor == NULL) {
		/* skip the table if no algorithm name */
		return NSERROR_OK;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<table class=\"info\">\n"
			 "<tr><th>Public Key</th><td><hr></td></tr>\n"
			 "<tr><th>Algorithm</th><td>%s</td></tr>\n"
			 "<tr><th>Key Size</th><td>%d</td></tr>\n",
			 public_key->algor,
			 public_key->size);
	if (res != NSERROR_OK) {
		return res;
	}


	if (public_key->exponent != NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<tr><th>Exponent</th><td>%s</td></tr>\n",
				 public_key->exponent);
		if (res != NSERROR_OK) {
			return res;
		}
	}

	if (public_key->modulus != NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<tr><th>Modulus</th><td class=\"data\">%s</td></tr>\n",
				 public_key->modulus);
		if (res != NSERROR_OK) {
			return res;
		}
	}

	if (public_key->curve != NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<tr><th>Curve</th><td>%s</td></tr>\n",
				 public_key->curve);
		if (res != NSERROR_OK) {
			return res;
		}
	}

	if (public_key->public != NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<tr><th>Public Value</th><td>%s</td></tr>\n",
				 public_key->public);
		if (res != NSERROR_OK) {
			return res;
		}
	}

	res = fetch_about_ssenddataf(ctx, "</table>\n");

	return res;
}

static nserror
format_certificate_fingerprint(struct fetch_about_context *ctx,
			      struct ns_cert_info *cert_info)
{
	nserror res;

	if ((cert_info->sha1fingerprint == NULL) &&
	    (cert_info->sha256fingerprint == NULL))  {
		/* skip the table if no fingerprints */
		return NSERROR_OK;
	}


	res = fetch_about_ssenddataf(ctx,
			 "<table class=\"info\">\n"
			 "<tr><th>Fingerprints</th><td><hr></td></tr>\n");
	if (res != NSERROR_OK) {
		return res;
	}

	if (cert_info->sha256fingerprint != NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<tr><th>SHA-256</th><td class=\"data\">%s</td></tr>\n",
				 cert_info->sha256fingerprint);
		if (res != NSERROR_OK) {
			return res;
		}
	}

	if (cert_info->sha1fingerprint != NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<tr><th>SHA-1</th><td class=\"data\">%s</td></tr>\n",
				 cert_info->sha1fingerprint);
		if (res != NSERROR_OK) {
			return res;
		}
	}

	res = fetch_about_ssenddataf(ctx, "</table>\n");

	return res;
}

static nserror
format_certificate(struct fetch_about_context *ctx,
		   struct ns_cert_info *cert_info,
		   size_t depth)
{
	nserror res;

	res = fetch_about_ssenddataf(ctx,
			 "<h2 id=\"%"PRIsizet"\" class=\"ns-border\">%s</h2>\n",
			 depth, cert_info->subject_name.common_name);
	if (res != NSERROR_OK) {
		return res;
	}

	if (cert_info->err != SSL_CERT_ERR_OK) {
		res = fetch_about_ssenddataf(ctx,
				 "<table class=\"info\">\n"
				 "<tr class=\"ns-even-fg-bad\">"
				 "<th>Fault</th>"
				 "<td>%s</td>"
				 "</tr>"
				 "</table>\n",
				 messages_get_sslcode(cert_info->err));
		if (res != NSERROR_OK) {
			return res;
		}
	}

	res = fetch_about_ssenddataf(ctx,
			 "<table class=\"info\">\n"
			 "<tr><th>Issued To</th><td><hr></td></tr>\n");
	if (res != NSERROR_OK) {
		return res;
	}

	res = format_certificate_name(ctx, &cert_info->subject_name);
	if (res != NSERROR_OK) {
		return res;
	}

	res = fetch_about_ssenddataf(ctx,
			 "</table>\n");
	if (res != NSERROR_OK) {
		return res;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<table class=\"info\">\n"
			 "<tr><th>Issued By</th><td><hr></td></tr>\n");
	if (res != NSERROR_OK) {
		return res;
	}

	res = format_certificate_name(ctx, &cert_info->issuer_name);
	if (res != NSERROR_OK) {
		return res;
	}

	res = fetch_about_ssenddataf(ctx,
			 "</table>\n");
	if (res != NSERROR_OK) {
		return res;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<table class=\"info\">\n"
			 "<tr><th>Validity</th><td><hr></td></tr>\n"
			 "<tr><th>Valid From</th><td>%s</td></tr>\n"
			 "<tr><th>Valid Until</th><td>%s</td></tr>\n"
			 "</table>\n",
			 cert_info->not_before,
			 cert_info->not_after);
	if (res != NSERROR_OK) {
		return res;
	}

	res = format_certificate_san(ctx, cert_info->san);
	if (res != NSERROR_OK) {
		return res;
	}

	res = format_certificate_public_key(ctx, &cert_info->public_key);
	if (res != NSERROR_OK) {
		return res;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<table class=\"info\">\n"
			 "<tr><th>Miscellaneous</th><td><hr></td></tr>\n");
	if (res != NSERROR_OK) {
		return res;
	}

	if (cert_info->serialnum != NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<tr><th>Serial Number</th><td>%s</td></tr>\n",
				 cert_info->serialnum);
		if (res != NSERROR_OK) {
			return res;
		}
	}

	if (cert_info->sig_algor != NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<tr><th>Signature Algorithm</th>"
				 "<td>%s</td></tr>\n",
				 cert_info->sig_algor);
		if (res != NSERROR_OK) {
			return res;
		}
	}

	res = fetch_about_ssenddataf(ctx,
			 "<tr><th>Version</th><td>%ld</td></tr>\n"
			 "</table>\n",
			 cert_info->version);
	if (res != NSERROR_OK) {
		return res;
	}

	res = format_certificate_fingerprint(ctx, cert_info);
	if (res != NSERROR_OK) {
		return res;
	}

	return res;
}

/**
 * Handler to generate about:certificate page.
 *
 * Shows details of a certificate chain
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
bool fetch_about_certificate_handler(struct fetch_about_context *ctx)
{
	int code = 200;
	nserror res;
	struct cert_chain *chain = NULL;

	/* content is going to return ok */
	fetch_about_set_http_code(ctx, code);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/html"))
		goto fetch_about_certificate_handler_aborted;

	/* page head */
	res = fetch_about_ssenddataf(ctx,
			"<html>\n<head>\n"
			"<title>NetSurf Browser Certificate Viewer</title>\n"
			"<link rel=\"stylesheet\" type=\"text/css\" "
					"href=\"resource:internal.css\">\n"
			"</head>\n"
			"<body id=\"certificate\" class=\"ns-even-bg ns-even-fg ns-border\">\n"
			"<h1 class=\"ns-border\">Certificate</h1>\n");
	if (res != NSERROR_OK) {
		goto fetch_about_certificate_handler_aborted;
	}

	res = cert_chain_from_query(fetch_about_get_url(ctx), &chain);
	if (res != NSERROR_OK) {
		res = fetch_about_ssenddataf(ctx, "<p>Could not process that</p>\n");
		if (res != NSERROR_OK) {
			goto fetch_about_certificate_handler_aborted;
		}
	} else {
		struct ns_cert_info *cert_info;
		res = convert_chain_to_cert_info(chain, &cert_info);
		if (res == NSERROR_OK) {
			size_t depth;
			res = fetch_about_ssenddataf(ctx, "<ul>\n");
			if (res != NSERROR_OK) {
				free_ns_cert_info(cert_info);
				goto fetch_about_certificate_handler_aborted;
			}

			for (depth = 0; depth < chain->depth; depth++) {
				res = fetch_about_ssenddataf(ctx, "<li><a href=\"#%"PRIsizet"\">%s</a></li>\n",
						depth, (cert_info + depth)
							->subject_name
								.common_name);
				if (res != NSERROR_OK) {
					free_ns_cert_info(cert_info);
					goto fetch_about_certificate_handler_aborted;
				}

			}

			res = fetch_about_ssenddataf(ctx, "</ul>\n");
			if (res != NSERROR_OK) {
				free_ns_cert_info(cert_info);
				goto fetch_about_certificate_handler_aborted;
			}

			for (depth = 0; depth < chain->depth; depth++) {
				res = format_certificate(ctx, cert_info + depth,
						depth);
				if (res != NSERROR_OK) {
					free_ns_cert_info(cert_info);
					goto fetch_about_certificate_handler_aborted;
				}

			}
			free_ns_cert_info(cert_info);

		} else {
			res = fetch_about_ssenddataf(ctx,
					 "<p>Invalid certificate data</p>\n");
			if (res != NSERROR_OK) {
				goto fetch_about_certificate_handler_aborted;
			}
		}
	}


	/* page footer */
	res = fetch_about_ssenddataf(ctx, "</body>\n</html>\n");
	if (res != NSERROR_OK) {
		goto fetch_about_certificate_handler_aborted;
	}

	fetch_about_send_finished(ctx);

	cert_chain_free(chain);

	return true;

fetch_about_certificate_handler_aborted:
	cert_chain_free(chain);
	return false;
}
