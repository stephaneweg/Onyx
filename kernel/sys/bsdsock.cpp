//
// bsdsock.cpp -- BSD sockets over the kernel's socket slots (sys/net.cpp): sock_open / connect /
// bind / listen / accept / send / recv / shutdown / close / getopt / setopt / name, and poll over
// sockets, streams and files (kapi v75 slots 229..241, docs/POSIX-PLAN.md §3.3).
//
// Owner: WP-NET. This is the WP-0 skeleton: every entry returns -KAPI_ENOSYS until WP-NET lands.
//
// ---------------------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// ---------------------------------------------------------------------------------------------
//
#include <kern/kapi_abi.h>

extern "C" {

int kapi_sock_open (int nType, unsigned nFlags)
{
	return -KAPI_ENOSYS;
}

int kapi_sock_connect (int s, const struct kapi_sockaddr *pTo)
{
	return -KAPI_ENOSYS;
}

int kapi_sock_bind (int s, const struct kapi_sockaddr *pAddr)
{
	return -KAPI_ENOSYS;
}

int kapi_sock_listen (int s, int nBacklog)
{
	return -KAPI_ENOSYS;
}

int kapi_sock_accept (int s, struct kapi_sockaddr *pPeer, unsigned nFlags)
{
	return -KAPI_ENOSYS;
}

long long kapi_sock_send (int s, const void *pBuf, unsigned long long nLen, unsigned nFlags,
			 const struct kapi_sockaddr *pTo)
{
	return -KAPI_ENOSYS;
}

long long kapi_sock_recv (int s, void *pBuf, unsigned long long nLen, unsigned nFlags,
			 struct kapi_sockaddr *pFrom)
{
	return -KAPI_ENOSYS;
}

int kapi_sock_shutdown (int s, int nHow)
{
	return -KAPI_ENOSYS;
}

int kapi_sock_close (int s)
{
	return -KAPI_ENOSYS;
}

int kapi_sock_getopt (int s, int nOpt, int *pValue)
{
	return -KAPI_ENOSYS;
}

int kapi_sock_setopt (int s, int nOpt, int nValue)
{
	return -KAPI_ENOSYS;
}

int kapi_sock_name (int s, int nPeer, struct kapi_sockaddr *pOut)
{
	return -KAPI_ENOSYS;
}

int kapi_poll (struct kapi_pollfd *pFds, unsigned n, int nTimeoutMs)
{
	return -KAPI_ENOSYS;
}

}
