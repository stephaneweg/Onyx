//
// volutil.h -- what df, mount, eject and mkfs share (kapi v91's volumes): a size as text, a state's
// name, an error's text. Header-only (the four are freestanding C tools).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see docs/LICENSING.md)
//
#ifndef _volutil_h
#define _volutil_h
#include "appkit/appkit.h"

static __attribute__ ((unused)) void vu_put_u (unsigned long long v)
{
	char t[24]; int k = 0;
	do { t[k++] = (char) ('0' + v % 10); v /= 10; } while (v);
	char o[24]; int n = 0;
	while (k) o[n++] = t[--k];
	o[n] = 0;
	ax_puts (o);
}

static __attribute__ ((unused)) void vu_put_size (unsigned long long b)		// "512 KB", "37.5 MB", "29.7 GB"
{
	const char *unit = " KB"; unsigned long long d = 1024;
	if (b >= (1ull << 40)) { unit = " TB"; d = 1ull << 40; }
	else if (b >= (1ull << 30)) { unit = " GB"; d = 1ull << 30; }
	else if (b >= (1ull << 20)) { unit = " MB"; d = 1ull << 20; }
	unsigned long long whole = b / d, tenth = (b % d) * 10 / d;
	vu_put_u (whole);
	if (d > 1024 && whole < 100) { ax_puts ("."); vu_put_u (tenth); }
	ax_puts (unit);
}

static __attribute__ ((unused)) void vu_pad (int n) { while (n-- > 0) ax_puts (" "); }

static __attribute__ ((unused)) const char *vu_state (const struct kapi_volume *v)
{
	switch (v->state)
	{
	case KAPI_VST_MOUNTED:		return "mounted";
	case KAPI_VST_EJECTED:		return "ejected (can be removed)";
	case KAPI_VST_UNREADABLE:	return (v->flags & KAPI_VF_IOERR) ? "unreadable (the device does not answer)" : "not formatted (no FAT / exFAT)";
	case KAPI_VST_REMOVED:		return (v->flags & KAPI_VF_UNSAFE) ? "removed without an eject" : "removed";
	default:			return "?";
	}
}

static __attribute__ ((unused)) const char *vu_err (int e)
{
	switch (-e)
	{
	case KAPI_EBUSY:	return "busy: files are open on it";
	case KAPI_EPERM:	return "not allowed on this volume";
	case KAPI_EINVAL:	return "invalid (not removable, or a bad label / cluster size)";
	case KAPI_ENOENT:	return "no such volume, or not mounted";
	case KAPI_ENODEV:	return "no device there";
	case KAPI_ENOSPC:	return "the volume is too small or too big for that file system";
	case KAPI_EROFS:	return "the device is write-protected";
	case KAPI_EIO:		return "input / output error";
	case KAPI_ENOMEM:	return "out of memory";
	case KAPI_ENOSYS:	return "this kernel has no volume calls (kapi v91)";
	default:		return "error";
	}
}

// "usb" / "USB:" / "usb2:/x" -> "USB:" / "USB2:" in out (the volume's name, upper case, with ':')
static __attribute__ ((unused)) void vu_volname (const char *in, char *out, int cap)
{
	int n = 0;
	while (in[n] && in[n] != ':' && in[n] != '/' && n < cap - 2) { char c = in[n]; out[n] = c >= 'a' && c <= 'z' ? (char) (c - 32) : c; n++; }
	out[n++] = ':'; out[n] = 0;
}

// the next argument of the argument string at *i -> its length (0: none)
static __attribute__ ((unused)) int vu_arg (const char *args, int len, int *i, char *tok, int cap)
{
	while (*i < len && args[*i] == ' ') (*i)++;
	int p = 0;
	if (*i < len && args[*i] == '"')
	{
		(*i)++;
		while (*i < len && args[*i] != '"' && p < cap - 1) tok[p++] = args[(*i)++];
		if (*i < len) (*i)++;
	}
	else while (*i < len && args[*i] != ' ' && p < cap - 1) tok[p++] = args[(*i)++];
	tok[p] = 0;
	return p;
}

#endif
