//
// notify.inc -- the code of notify.h (SystemKit: compiled into systemkit.so; a PC build has it inline).
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
SK_FN int notify_action (const char *title, const char *text, const char *action)
{
	int pid = kapi_ipc_lookup (NOTIFY_SERVICE);
	if (pid == 0)
	{
		kapi_launch ("notifyd");			// start the service, wait for it
		for (int i = 0; i < 40 && pid == 0; i++) { kapi_msleep (50); pid = kapi_ipc_lookup (NOTIFY_SERVICE); }
		if (pid == 0) return 0;
	}
	char msg[NOTIFY_MAX];
	int n = 0;
	for (int i = 0; title && title[i] && n < 80; i++) msg[n++] = title[i];
	msg[n++] = '\0';
	for (int i = 0; text && text[i] && n < NOTIFY_MAX - 130; i++) msg[n++] = text[i];
	msg[n++] = '\0';
	if (action && *action) { for (int i = 0; action[i] && i < 120; i++) msg[n++] = action[i]; msg[n++] = '\0'; }
	return kapi_mailbox_send (pid, NOTIFY_MSG_SHOW, msg, (unsigned) n);
}
SK_FN int notify (const char *title, const char *text) { return notify_action (title, text, 0); }

#undef SK_FN
