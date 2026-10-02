/*
 * netdb.c -- name resolution and address conversions (libonyxposix, docs/POSIX-PLAN.md §3.4).
 *
 * getaddrinfo: a numeric address is parsed; "localhost" is 127.0.0.1; any other name goes to the
 * kernel's DNS (kapi net_resolve, v43: its cache, IPv4, one address) -- it blocks the calling
 * thread (curl's threaded resolver runs it on a worker). One AF_INET addrinfo per socket type
 * asked (both TCP and UDP when hints leave it open). AF_INET6 -> EAI_FAMILY; a name that does
 * not resolve -> EAI_NONAME (EAI_AGAIN when the network is down). Services: numeric, and the
 * small table below. getnameinfo is numeric only (no reverse DNS: NI_NAMEREQD -> EAI_NONAME).
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "posix_internal.h"

int h_errno;

const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

/* ---- addresses ---- */
int inet_aton (const char *s, struct in_addr *out)
{
	/* dotted quad, as inet_aton: a, a.b, a.b.c, a.b.c.d; decimal, 0x hex, 0 octal */
	unsigned long parts[4];
	int n = 0;
	const char *p = s;
	if (s == 0)
		return 0;
	for (;;)
	{
		if (!isdigit ((unsigned char) *p))
			return 0;
		char *end;
		unsigned long v = strtoul (p, &end, 0);
		if (end == p || n == 4)
			return 0;
		parts[n++] = v;
		p = end;
		if (*p == '.')
		{
			p++;
			continue;
		}
		if (*p != '\0')
			return 0;
		break;
	}
	unsigned long a;
	switch (n)
	{
	case 1: a = parts[0]; if (a > 0xFFFFFFFFUL) return 0; break;
	case 2: if (parts[0] > 255 || parts[1] > 0xFFFFFF) return 0; a = parts[0] << 24 | parts[1]; break;
	case 3: if (parts[0] > 255 || parts[1] > 255 || parts[2] > 0xFFFF) return 0;
		a = parts[0] << 24 | parts[1] << 16 | parts[2]; break;
	default:
		for (int i = 0; i < 4; i++)
			if (parts[i] > 255)
				return 0;
		a = parts[0] << 24 | parts[1] << 16 | parts[2] << 8 | parts[3];
	}
	if (out)
		out->s_addr = htonl ((uint32_t) a);
	return 1;
}

in_addr_t inet_addr (const char *s)
{
	struct in_addr a;
	return inet_aton (s, &a) ? a.s_addr : INADDR_NONE;
}

in_addr_t inet_network (const char *s)
{
	struct in_addr a;
	return inet_aton (s, &a) ? ntohl (a.s_addr) : INADDR_NONE;
}

char *inet_ntoa (struct in_addr a)
{
	static char buf[INET_ADDRSTRLEN];
	const unsigned char *b = (const unsigned char *) &a.s_addr;
	snprintf (buf, sizeof buf, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
	return buf;
}

static int pton4 (const char *s, unsigned char *out)
{
	/* strict: four decimal parts */
	unsigned v[4];
	int n = 0;
	for (;;)
	{
		if (!isdigit ((unsigned char) *s))
			return 0;
		unsigned x = 0;
		int digits = 0;
		while (isdigit ((unsigned char) *s))
		{
			x = x * 10 + (unsigned) (*s++ - '0');
			if (++digits > 3 || x > 255)
				return 0;
		}
		v[n++] = x;
		if (n == 4)
			break;
		if (*s++ != '.')
			return 0;
	}
	if (*s != '\0')
		return 0;
	for (int i = 0; i < 4; i++)
		out[i] = (unsigned char) v[i];
	return 1;
}

static int pton6 (const char *s, unsigned char *out)
{
	unsigned short w[8];
	int n = 0, gap = -1;
	memset (w, 0, sizeof w);
	if (s[0] == ':' && s[1] == ':')
	{
		gap = 0;
		s += 2;
		if (*s == '\0')
			goto done;
	}
	for (;;)
	{
		const char *start = s;
		unsigned x = 0;
		int digits = 0;
		while (isxdigit ((unsigned char) *s))
		{
			x = x * 16 + (unsigned) (isdigit ((unsigned char) *s) ? *s - '0' : (tolower ((unsigned char) *s) - 'a' + 10));
			s++;
			if (++digits > 4)
				return 0;
		}
		if (*s == '.' && n <= 6)
		{
			unsigned char v4[4];
			if (!pton4 (start, v4))
				return 0;
			w[n++] = (unsigned short) (v4[0] << 8 | v4[1]);
			w[n++] = (unsigned short) (v4[2] << 8 | v4[3]);
			break;
		}
		if (digits == 0 || n == 8)
			return 0;
		w[n++] = (unsigned short) x;
		if (*s == '\0')
			break;
		if (*s != ':')
			return 0;
		s++;
		if (*s == ':')
		{
			if (gap >= 0)
				return 0;
			gap = n;
			s++;
			if (*s == '\0')
				break;
		}
	}
done:
	if (gap >= 0)
	{
		int tail = n - gap;
		memmove (w + 8 - tail, w + gap, (size_t) tail * sizeof *w);
		for (int i = gap; i < 8 - tail; i++)
			w[i] = 0;
	}
	else if (n != 8)
		return 0;
	for (int i = 0; i < 8; i++)
	{
		out[i * 2] = (unsigned char) (w[i] >> 8);
		out[i * 2 + 1] = (unsigned char) w[i];
	}
	return 1;
}

int inet_pton (int af, const char *s, void *dst)
{
	if (af == AF_INET)
		return pton4 (s, (unsigned char *) dst);
	if (af == AF_INET6)
		return pton6 (s, (unsigned char *) dst);
	errno = EAFNOSUPPORT;
	return -1;
}

const char *inet_ntop (int af, const void *src, char *dst, socklen_t size)
{
	char buf[INET6_ADDRSTRLEN];
	const unsigned char *b = (const unsigned char *) src;
	if (af == AF_INET)
		snprintf (buf, sizeof buf, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
	else if (af == AF_INET6)
	{
		unsigned short w[8];
		for (int i = 0; i < 8; i++)
			w[i] = (unsigned short) (b[i * 2] << 8 | b[i * 2 + 1]);
		/* the longest run of zero words becomes "::" */
		int best = -1, bestlen = 0;
		for (int i = 0; i < 8;)
		{
			if (w[i] == 0)
			{
				int j = i;
				while (j < 8 && w[j] == 0)
					j++;
				if (j - i > bestlen && j - i > 1)
				{
					best = i;
					bestlen = j - i;
				}
				i = j;
			}
			else
				i++;
		}
		char *o = buf;
		for (int i = 0; i < 8; i++)
		{
			if (i == best)
			{
				*o++ = ':';
				if (i == 0)
					*o++ = ':';
				i += bestlen - 1;
				continue;
			}
			o += sprintf (o, "%x", w[i]);
			if (i < 7)
				*o++ = ':';
		}
		*o = '\0';
	}
	else
	{
		errno = EAFNOSUPPORT;
		return 0;
	}
	if (strlen (buf) + 1 > size)
	{
		errno = ENOSPC;
		return 0;
	}
	strcpy (dst, buf);
	return dst;
}

/* ---- services ---- */
static const struct { const char *name; int port; } s_services[] =
{
	{ "ftp", 21 }, { "ssh", 22 }, { "telnet", 23 }, { "smtp", 25 }, { "domain", 53 },
	{ "dns", 53 }, { "http", 80 }, { "www", 80 }, { "pop3", 110 }, { "ntp", 123 },
	{ "imap", 143 }, { "imap2", 143 }, { "ldap", 389 }, { "https", 443 }, { "submission", 587 },
	{ "smtps", 465 }, { "imaps", 993 }, { "pop3s", 995 }, { "socks", 1080 }, { "rdp", 3389 },
	{ "vnc", 5900 }, { "http-alt", 8080 }, { 0, 0 }
};

static int service_port (const char *s, int numeric_only, int *port)
{
	if (s == 0)
	{
		*port = 0;
		return 0;
	}
	char *end;
	long v = strtol (s, &end, 10);
	if (*s != '\0' && *end == '\0')
	{
		if (v < 0 || v > 65535)
			return EAI_SERVICE;
		*port = (int) v;
		return 0;
	}
	if (numeric_only)
		return EAI_NONAME;
	for (int i = 0; s_services[i].name; i++)
		if (strcasecmp (s, s_services[i].name) == 0)
		{
			*port = s_services[i].port;
			return 0;
		}
	return EAI_SERVICE;
}

struct servent *getservbyname (const char *name, const char *proto)
{
	static struct servent se;
	static char *aliases[1];
	static char protobuf[8];
	int port;
	if (service_port (name, 0, &port) != 0)
		return 0;
	se.s_name = (char *) name;
	se.s_aliases = aliases;
	se.s_port = htons ((uint16_t) port);
	snprintf (protobuf, sizeof protobuf, "%s", proto ? proto : "tcp");
	se.s_proto = protobuf;
	return &se;
}

struct servent *getservbyport (int port, const char *proto)
{
	static struct servent se;
	static char *aliases[1];
	for (int i = 0; s_services[i].name; i++)
		if (htons ((uint16_t) s_services[i].port) == port)
		{
			se.s_name = (char *) s_services[i].name;
			se.s_aliases = aliases;
			se.s_port = port;
			se.s_proto = (char *) (proto ? proto : "tcp");
			return &se;
		}
	return 0;
}

struct protoent *getprotobyname (const char *name)
{
	static struct protoent pe;
	static char *aliases[1];
	static const struct { const char *n; int p; } t[] = { { "ip", 0 }, { "icmp", 1 }, { "tcp", 6 }, { "udp", 17 }, { 0, 0 } };
	for (int i = 0; t[i].n; i++)
		if (strcmp (name, t[i].n) == 0)
		{
			pe.p_name = (char *) t[i].n;
			pe.p_aliases = aliases;
			pe.p_proto = t[i].p;
			return &pe;
		}
	return 0;
}

struct protoent *getprotobynumber (int n)
{
	return n == 0 ? getprotobyname ("ip") : n == 1 ? getprotobyname ("icmp") :
	       n == 6 ? getprotobyname ("tcp") : n == 17 ? getprotobyname ("udp") : 0;
}

/* ---- resolution ---- */
/* a host name -> an IPv4 address (network order). 0, or an EAI_* code */
static int resolve (const char *host, int numeric_only, uint32_t *out)
{
	struct in_addr a;
	if (pton4 (host, (unsigned char *) &a.s_addr))
	{
		*out = a.s_addr;
		return 0;
	}
	unsigned char v6[16];
	if (pton6 (host, v6))
		return EAI_FAMILY;
	if (numeric_only)
		return EAI_NONAME;
	if (strcasecmp (host, "localhost") == 0 || strcasecmp (host, "localhost.") == 0)
	{
		*out = htonl (INADDR_LOOPBACK);
		return 0;
	}
	if (kapi__core () != 0)
		return EAI_SYSTEM;
	char ip[64];
	if (kapi_net_resolve (host, ip, sizeof ip) == 1 && pton4 (ip, (unsigned char *) &a.s_addr))
	{
		*out = a.s_addr;
		return 0;
	}
	char st[32];
	if (kapi_net_status (st, sizeof st) == 0)
		return EAI_AGAIN;			/* (no network) */
	return EAI_NONAME;
}

struct ai_block
{
	struct addrinfo ai;
	struct sockaddr_in sin;
	char canon[256];
};

int getaddrinfo (const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
	int family = hints ? hints->ai_family : AF_UNSPEC;
	int socktype = hints ? hints->ai_socktype : 0;
	int protocol = hints ? hints->ai_protocol : 0;
	int flags = hints ? hints->ai_flags : 0;
	if (res == 0)
		return EAI_SYSTEM;
	*res = 0;
	if (node == 0 && service == 0)
		return EAI_NONAME;
	if (family != AF_UNSPEC && family != AF_INET)
		return EAI_FAMILY;
	if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM)
		return EAI_SOCKTYPE;

	int port;
	int e = service_port (service, (flags & AI_NUMERICSERV) != 0, &port);
	if (e)
		return e;

	uint32_t addr;
	if (node == 0)
		addr = htonl ((flags & AI_PASSIVE) ? INADDR_ANY : INADDR_LOOPBACK);
	else if ((e = resolve (node, (flags & AI_NUMERICHOST) != 0, &addr)) != 0)
		return e;

	int types[2], nt = 0;
	if (socktype == 0 || socktype == SOCK_STREAM) types[nt++] = SOCK_STREAM;
	if (socktype == 0 || socktype == SOCK_DGRAM) types[nt++] = SOCK_DGRAM;

	struct addrinfo *head = 0, **tail = &head;
	for (int i = 0; i < nt; i++)
	{
		struct ai_block *b = (struct ai_block *) calloc (1, sizeof *b);
		if (b == 0)
		{
			freeaddrinfo (head);
			return EAI_MEMORY;
		}
		b->sin.sin_family = AF_INET;
		b->sin.sin_port = htons ((uint16_t) port);
		b->sin.sin_addr.s_addr = addr;
		b->ai.ai_family = AF_INET;
		b->ai.ai_socktype = types[i];
		b->ai.ai_protocol = protocol ? protocol : types[i] == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
		b->ai.ai_addrlen = sizeof b->sin;
		b->ai.ai_addr = (struct sockaddr *) &b->sin;
		if ((flags & AI_CANONNAME) && i == 0)
		{
			snprintf (b->canon, sizeof b->canon, "%s", node ? node : "localhost");
			b->ai.ai_canonname = b->canon;
		}
		*tail = &b->ai;
		tail = &b->ai.ai_next;
	}
	*res = head;
	return 0;
}

void freeaddrinfo (struct addrinfo *ai)
{
	while (ai)
	{
		struct addrinfo *next = ai->ai_next;
		free (ai);				/* (the addrinfo is the start of its block) */
		ai = next;
	}
}

const char *gai_strerror (int e)
{
	switch (e)
	{
	case 0:			return "Success";
	case EAI_BADFLAGS:	return "Invalid flags";
	case EAI_NONAME:	return "Name or service not known";
	case EAI_AGAIN:		return "Temporary failure in name resolution";
	case EAI_FAIL:		return "Non-recoverable failure in name resolution";
	case EAI_FAMILY:	return "Address family not supported";
	case EAI_SOCKTYPE:	return "Socket type not supported";
	case EAI_SERVICE:	return "Service not supported for socket type";
	case EAI_MEMORY:	return "Memory allocation failure";
	case EAI_SYSTEM:	return "System error";
	case EAI_OVERFLOW:	return "Argument buffer overflow";
	case EAI_NODATA:	return "No address associated with hostname";
	default:		return "Unknown error";
	}
}

int getnameinfo (const struct sockaddr *sa, socklen_t len, char *host, socklen_t hostlen,
		 char *serv, socklen_t servlen, int flags)
{
	if (sa == 0 || sa->sa_family != AF_INET || len < sizeof (struct sockaddr_in))
		return EAI_FAMILY;
	const struct sockaddr_in *in = (const struct sockaddr_in *) sa;
	if (host && hostlen)
	{
		if (flags & NI_NAMEREQD)
			return EAI_NONAME;		/* (no reverse DNS) */
		if (inet_ntop (AF_INET, &in->sin_addr, host, hostlen) == 0)
			return EAI_OVERFLOW;
	}
	if (serv && servlen)
	{
		int port = ntohs (in->sin_port);
		const char *name = 0;
		if (!(flags & NI_NUMERICSERV))
			for (int i = 0; s_services[i].name; i++)
				if (s_services[i].port == port)
				{
					name = s_services[i].name;
					break;
				}
		int n = name ? snprintf (serv, servlen, "%s", name) : snprintf (serv, servlen, "%d", port);
		if (n < 0 || (socklen_t) n >= servlen)
			return EAI_OVERFLOW;
	}
	return 0;
}

int gethostbyname_r (const char *name, struct hostent *he, char *buf, size_t len,
		     struct hostent **result, int *err)
{
	*result = 0;
	struct layout { char *aliases[1]; char *addrs[2]; uint32_t addr; };
	size_t need = sizeof (struct layout) + strlen (name) + 1;
	if (len < need)
		return ERANGE;
	uint32_t a;
	int e = resolve (name, 0, &a);
	if (e)
	{
		*err = e == EAI_AGAIN ? TRY_AGAIN : HOST_NOT_FOUND;
		return e == EAI_AGAIN ? EAGAIN : ENOENT;
	}
	struct layout *l = (struct layout *) (((unsigned long) buf + 7) & ~7UL);
	if ((char *) (l + 1) + strlen (name) + 1 > buf + len)
		return ERANGE;
	l->aliases[0] = 0;
	l->addr = a;
	l->addrs[0] = (char *) &l->addr;
	l->addrs[1] = 0;
	char *n = (char *) (l + 1);
	strcpy (n, name);
	he->h_name = n;
	he->h_aliases = l->aliases;
	he->h_addrtype = AF_INET;
	he->h_length = 4;
	he->h_addr_list = l->addrs;
	*result = he;
	*err = 0;
	return 0;
}

struct hostent *gethostbyname (const char *name)
{
	static struct hostent he;
	static char buf[512];
	struct hostent *r;
	int e = gethostbyname_r (name, &he, buf, sizeof buf, &r, &h_errno);
	(void) e;
	return r;
}

struct hostent *gethostbyaddr (const void *addr, socklen_t len, int type)
{
	static struct hostent he;
	static char name[INET_ADDRSTRLEN];
	static char *aliases[1];
	static char *addrs[2];
	static uint32_t a;
	if (type != AF_INET || len != 4)
	{
		h_errno = HOST_NOT_FOUND;
		return 0;
	}
	memcpy (&a, addr, 4);
	inet_ntop (AF_INET, &a, name, sizeof name);
	addrs[0] = (char *) &a;
	addrs[1] = 0;
	he.h_name = name;
	he.h_aliases = aliases;
	he.h_addrtype = AF_INET;
	he.h_length = 4;
	he.h_addr_list = addrs;
	return &he;
}

const char *hstrerror (int e)
{
	switch (e)
	{
	case HOST_NOT_FOUND:	return "Unknown host";
	case TRY_AGAIN:		return "Host name lookup failure";
	case NO_RECOVERY:	return "Unknown server error";
	case NO_DATA:		return "No address associated with name";
	default:		return "Resolver error";
	}
}

void herror (const char *s) { fprintf (stderr, "%s%s%s\n", s ? s : "", s ? ": " : "", hstrerror (h_errno)); }
