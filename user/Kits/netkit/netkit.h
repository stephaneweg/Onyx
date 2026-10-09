//
// netkit.h -- NetKit (SD:/lib/netkit.so): the network for the programs. THE header a program includes:
//
//     #include "netkit/netkit.h"
//
// It brings (each has its own header beside this one):
//   httpc.h    a small HTTP/1.0 client, no allocation     http_get, http_post, http_request
//   ftpfs.h    the FTP volumes (FTP:host/path)            ftpfs_login, ftpfs_load_sites...
//   wifi.h     the known Wi-Fi networks, joining one       wifi_known_load, wifi_join, wifi_forget...
// C and C++. Link lib/netkit.imp.a (C++) or lib/netkit.imp_c.a (C).
// The HTTP/1.1 class with its TLS transport (HttpClient) is "netkit/http.hpp", included apart: it is
// still a header with its code.
//
#ifndef _netkit_h
#define _netkit_h
#include "httpc.h"
#include "ftpfs.h"
#include "wifi.h"
#endif
