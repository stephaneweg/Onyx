/*
 * curl/curl.h -- stub for the NetSurf port on Onyx.
 *
 * NetSurf's content/fetch.c unconditionally #includes content/fetchers/curl.h (which pulls
 * <curl/curl.h> and declares CURLM *fetch_curl_multi), but only *registers* the curl fetcher
 * under #ifdef WITH_CURL -- which we leave off (HTTP comes from onyx_fetch). We don't link
 * libcurl, so this just provides the opaque types the header references, enough to parse.
 */
#ifndef _ONYX_COMPAT_CURL_H
#define _ONYX_COMPAT_CURL_H 1

typedef void CURL;
typedef void CURLM;

#endif /* _ONYX_COMPAT_CURL_H */
