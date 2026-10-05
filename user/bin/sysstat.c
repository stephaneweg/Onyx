//
// sysstat -- the system calls of the apps (kapi v74 proc_stats): how many each one makes, and
// which kapis. Every app runs at EL0 and reaches the kernel by `svc`: a call costs a few hundred
// nanoseconds, so an app making tens of thousands a second is worth a look.
//
//   sysstat                    every app: pid, calls per second, calls in all, emulated ID reads
//   sysstat <pid | name>       one app: the same, then its 8 most called kapis (user/kapi_names.h)
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
#include "kapi.h"
#include "applib.h"
#include "kapi_names.h"

static char s_procs[4096];		// kapi_list_procs: "<pid> <a|k> <state> <pages> <name>" a line

// An unsigned 64-bit number in decimal -> its length.
static int u64toa (unsigned long long v, char *b)
{
	char t[24]; int n = 0;
	do { t[n++] = (char) ('0' + v % 10); v /= 10; } while (v != 0);
	for (int i = 0; i < n; i++) b[i] = t[n - 1 - i];
	b[n] = '\0';
	return n;
}

// s right-justified in a field of w.
static void put_r (const char *s, int w)
{
	for (int k = ax_strlen (s); k < w; k++) ax_puts (" ");
	ax_puts (s);
}

// The next app line of s_procs from *pi: its pid and name (the first line of each pid: one per
// process, its threads after it) -> 0 at the end.
static int next_app (int *pi, int *pid, char *name, int cap)
{
	static int last = -1;
	if (*pi == 0) last = -1;
	while (s_procs[*pi] != '\0')
	{
		int i = *pi, ls = i;
		while (s_procs[i] != '\0' && s_procs[i] != '\n') i++;
		int le = i;
		*pi = s_procs[i] == '\n' ? i + 1 : i;

		int k = ls, p = 0;
		while (k < le && s_procs[k] >= '0' && s_procs[k] <= '9') p = p * 10 + (s_procs[k++] - '0');
		while (k < le && s_procs[k] == ' ') k++;
		char kind = k < le ? s_procs[k] : '?';
		for (int f = 0; f < 3 && k < le; f++)		// past kind, state, pages
		{
			while (k < le && s_procs[k] != ' ') k++;
			while (k < le && s_procs[k] == ' ') k++;
		}
		if (kind != 'a' || p <= 0 || p == last) continue;
		last = p;
		int n = 0;
		while (k < le && n < cap - 1) name[n++] = s_procs[k++];
		name[n] = '\0';
		*pid = p;
		return 1;
	}
	return 0;
}

static void line (int pid, const char *name, const struct kapi_syscall_stats *s)
{
	char b[24];
	ax_itoa (pid, b);             put_r (b, 5);
	ax_itoa ((int) s->rate, b);   put_r (b, 9);
	u64toa (s->syscalls, b);      put_r (b, 13);
	u64toa (s->emulated, b);      put_r (b, 9);
	ax_puts ("  ");
	ax_putln (name);
}

int main (void)
{
	if (kapi_abi_version () < 74)
	{
		ax_putln ("sysstat: needs a kernel with kapi v74 (proc_stats)");
		return 1;
	}
	char args[96];
	kapi_get_args (args, sizeof (args));
	int a = 0;
	while (args[a] == ' ') a++;
	int e = a; while (args[e] != '\0' && args[e] != ' ') e++;
	args[e] = '\0';
	const char *want = args + a;

	kapi_list_procs (s_procs, sizeof (s_procs));
	ax_putln ("  PID   CALLS/s     CALLS ALL  EMULATED  NAME");

	int i = 0, pid, found = 0;
	char name[64];
	struct kapi_syscall_stats s;
	while (next_app (&i, &pid, name, sizeof (name)))
	{
		if (*want != '\0')
		{
			int num = 0, isnum = 1;
			for (int k = 0; want[k]; k++)
				if (want[k] >= '0' && want[k] <= '9') num = num * 10 + (want[k] - '0'); else isnum = 0;
			if (isnum ? num != pid : !ax_streq (want, name)) continue;
		}
		if (kapi_proc_stats (pid, &s) != 0) continue;
		line (pid, name, &s);
		found++;
		if (*want == '\0') continue;

		ax_putln ("");
		ax_putln ("  the kapis most called:");
		for (int t = 0; t < KAPI_SYSCALL_STATS_TOP && s.top_slot[t] != 0; t++)
		{
			char b[24];
			u64toa (s.top_count[t], b);
			put_r (b, 13);
			ax_puts ("  ");
			if (s.syscalls != 0)
			{
				ax_itoa ((int) ((unsigned long long) s.top_count[t] * 100 / s.syscalls), b);
				put_r (b, 3);
				ax_puts ("%  ");
			}
			ax_putln (kapi_slot_name (s.top_slot[t]));
		}
		break;
	}
	if (found == 0 && *want != '\0')
	{
		ax_puts ("sysstat: no app ");
		ax_putln (want);
		return 1;
	}
	return 0;
}
