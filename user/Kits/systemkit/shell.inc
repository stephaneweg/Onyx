//
// shell.inc -- the code of shell.h (SystemKit: compiled into systemkit.so; a PC build has it inline). C and C++.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifdef SK_IMPL
#define SK_FN	extern "C"
#else
#define SK_FN	static inline
#endif
SK_FN int shell_running (void)
{
	return kapi_ipc_lookup (SHELL_SERVICE) > 0 ? 1 : 0;
}
SK_FN int shell_ask (int what)
{
	int pid = kapi_ipc_lookup (SHELL_SERVICE);
	if (pid <= 0) return 0;
	return kapi_mailbox_send (pid, what, 0, 0) ? 1 : 0;
}
#undef SK_FN
