//
// shellend.h -- the remote shell's client is gone: its shell ends with the session. telnetd runs
// a /bin/cmd per client between two pipes, and letting go of the pipes is not enough -- a pipe
// has no "the other end is gone": with a program still running, the shell stayed, then both
// blocked for good writing an output nobody read. Such leftovers piled up after dropped
// sessions and slowed everything down (2026-10-05). (The terminal needs none of this: it ends
// with its window, and the kernel ends a dead process's children with it.)
//
//   shell_end (proc, to_cmd, from_cmd)
//       Ctrl-C, as typed (cmd stops what it runs in the foreground: its stages, a script's cmd),
//       then the end of its input (cmd leaves at the prompt). Its output is read and dropped
//       meanwhile, so that it never waits on a full pipe. Still there after SHELLEND_KILL_MS
//       (stuck in a call): terminated -- the kernel then ends what it started too.
//       -> 1: the shell has ended (the caller reaps it: kapi_wait), 0: it has not after
//       SHELLEND_GIVEUP_MS (left alone; the caller closes its pipes all the same).
//
// A daemon meant to outlive the session is started detached (`run SD:/bin/ftpd SD:/`), not in
// the foreground. Needs kapi.h before it; the PC test tools/tests/run_telnetd_test.sh drives it
// through telnetd.c against a mock kapi.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
// hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
// do so, subject to the following conditions: The above copyright notice and this permission
// notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
// IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef _shellend_h
#define _shellend_h

#define SHELLEND_KILL_MS	3000		// (cmd gives a script's cmd 2 s to stop by itself)
#define SHELLEND_GIVEUP_MS	5000

static inline int shell_end (void *proc, void *to_cmd, void *from_cmd)
{
	char b[256];
	if (proc == 0) return 0;
	if (kapi_proc_done (proc)) return 1;
	// (not waited for: a full pipe -- 8 KB typed that nobody read -- must not hold us here)
	if (kapi_stream_write_nb (to_cmd, "\x03", 1) == -KAPI_ENOSYS) kapi_stream_write (to_cmd, "\x03", 1);
	kapi_stream_eof (to_cmd);
	for (int t = 0; t < SHELLEND_GIVEUP_MS && !kapi_proc_done (proc); t += 10)
	{
		while (kapi_stream_read_nb (from_cmd, b, sizeof b) > 0) {}
		if (t == SHELLEND_KILL_MS)
		{
			struct kapi_proc_status ps;
			if (kapi_proc_wait (proc, KAPI_WAIT_NOHANG | KAPI_WAIT_KEEP, &ps) == 0 && ps.pid > 0)
				kapi_kill_pid (ps.pid, 1);
		}
		kapi_msleep (10);
	}
	return kapi_proc_done (proc);
}

#endif
