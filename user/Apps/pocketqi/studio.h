//
// pocketqi/studio.h -- PocketQI on Onyx: PocketQI Studio reaches Pocket (included by main.cpp). PocketQI's own
// administration server's portable core, unchanged (core/admin_http.cpp: HTTP; core/admin_apps.cpp: the skills' API;
// core/admin_tls.cpp: TLS 1.2 / 1.3 over mbedTLS, a self-signed ECDSA P-256 certificate made at the first start), the
// API PocketQI Studio speaks to a "Pocket QI physique" (its RobotClient.cs) -- on Onyx at https://<the Pi's address>
// (port 443):
//   GET    /api/apps            the BASIC skills installed: [{"id", "name", "description", "actions"}]
//   POST   /api/apps/<id>       a skill installed or replaced (POCKETAPP 1: its files; main.bas compiled with
//                               lib/galo.bi first, nothing written on an error), Pocket knows it at the next sentence
//   GET    /api/apps/<id>       a skill fetched (Studio's "Récupérer...")
//   DELETE /api/apps/<id>       a skill removed
//   POST   /api/apps/<id>/run   a skill run at once, without the LLM (Studio's F5): the action, an argument a line
//   GET    /api/face            Pocket's mood and state (Studio's "Visage" window)
//   GET    /                    a page: the certificate's SHA-256 fingerprint (to compare with Studio's)
// Here, Onyx's side: the core's platform (its "SD:/" is PocketQI's folder -- SD:/apps/<id>/ the skills, SD:/lib/galo.bi,
// SD:/etc/https.key and https.crt, SD:/etc/admin.pass the optional password, as on PocketQI's robot), the routing
// (PocketQI's other pages -- Wi-Fi, keys, mail -- are Onyx's Control Panel's and Pocket > Settings' here), the runner
// (the skill run by the brain's thread, as a sentence is) and the face. A thread listens on port 443 while Pocket >
// PocketQI Studio... says so (`studio = on`), in the window or in the background (`pocketqi --tray`): whichever has
// the port first.
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
#ifndef POCKETQI_STUDIO_H
#define POCKETQI_STUDIO_H

#include "Apps/pocketqi/core/admin_core.h"
#include <stdarg.h>

#define STUDIO_PORT 443

// ---- the core's platform: "SD:/..." is PocketQI's folder -------------------------------------------------------------
static std::string studio_path (const char *p)
{
	if (strncasecmp (p, "SD:/", 4) == 0) return g_home + "/" + (p + 4);
	if (strncasecmp (p, "SD:", 3) == 0) return g_home + "/" + (p + 3);
	return g_home + "/" + p;
}

int AdminFileRead (const char *pPath, char *pBuffer, unsigned nSize)
{
	if (nSize == 0) return -1;
	void *f = kapi_open (studio_path (pPath).c_str ());
	if (!f) return -1;
	int n = 0;
	while ((unsigned) n < nSize - 1)
	{
		int r = kapi_read (f, pBuffer + n, nSize - 1 - (unsigned) n);
		if (r <= 0) break;
		n += r;
	}
	kapi_close (f);
	pBuffer[n] = 0;
	return n;
}
bool AdminFileWrite (const char *pPath, const void *pData, unsigned nLength)
{
	std::string p = studio_path (pPath);
	size_t slash = p.find_last_of ('/');
	if (slash != std::string::npos) make_dirs (p.substr (0, slash));
	return kapi_save_file (p.c_str (), pData, nLength) == (int) nLength;
}
bool AdminFileDelete (const char *pPath) { std::string p = studio_path (pPath); kapi_remove (p.c_str ()); return !file_exists (p); }
bool AdminFileExists (const char *pPath) { return file_exists (studio_path (pPath)); }
bool AdminMakeDir (const char *pPath) { std::string p = studio_path (pPath); make_dirs (p); return file_exists (p); }
bool AdminRename (const char *pFrom, const char *pTo) { return kapi_rename (studio_path (pFrom).c_str (), studio_path (pTo).c_str ()) >= 0; }
bool AdminRemoveDir (const char *pPath) { return kapi_remove (studio_path (pPath).c_str ()) >= 0; }
int AdminListDir (const char *pPath, TAdminDirEntry *pEntry, void *pParam)
{
	void *d = kapi_opendir (studio_path (pPath).c_str ());
	if (!d) return -1;
	struct kapi_dirent e;
	int n = 0;
	while (kapi_readdir (d, &e) > 0)
	{
		if (!strcmp (e.name, ".") || !strcmp (e.name, "..")) continue;
		pEntry (e.name, e.is_dir != 0, pParam);
		n++;
	}
	kapi_closedir (d);
	return n;
}
void AdminLog (const char *pFormat, ...)
{
	char b[300];
	va_list ap;
	va_start (ap, pFormat);
	vsnprintf (b, sizeof b, pFormat, ap);
	va_end (ap);
	printf ("pocketqi: studio: %s\n", b);
	fflush (stdout);
}
void AdminAppsChanged (const char *) { g_skillsDirty = true; }

// ---- the runner: a skill run by the brain's thread, as a sentence is (refused while Pocket works) ------------------
static bool studio_runner (const char *pId, const char *pAction, const char *const *ppArgs, unsigned nArgs, char *pError, unsigned nErrorSize)
{
	if (g_busy) { snprintf (pError, nErrorSize, "PocketQI est occupé"); return false; }
	g_runArgs.clear ();
	g_runArgs.push_back (pId);
	g_runArgs.push_back (pAction);
	for (unsigned i = 0; i < nArgs; i++) g_runArgs.push_back (ppArgs[i]);
	g_runJob = true;
	g_busy = true;
	if (g_tray) post (M_WAKE, 0, 0, "");
	kapi_event_set (g_jobEv);
	return true;
}

// ---- the face: {"humeur", "intensite", "etat", "seq"} ------------------------------------------------------------
static unsigned studio_face (char *pJson, unsigned nSize)
{
	int intensity = 1;
	pocket_humeur h = pocket_humeur_get (&intensity);
	int etat = g_talking ? VISAGE_PARLE : g_bgState;
	int n = snprintf (pJson, nSize, "{\"humeur\": \"%s\", \"intensite\": %d, \"etat\": \"%s\", \"seq\": %u}",
			  pocket_humeur_name (h), intensity, pocket_visage_etat_nom (etat), g_faceSeq);
	return n > 0 && (unsigned) n < nSize ? (unsigned) n : 0;
}

// ---- the routing: Studio's API, and a page with the certificate's fingerprint ------------------------------------
struct StudioHost : CAdminHost
{
	void GetStatus (TAdminStatus *s) override { memset (s, 0, sizeof *s); kapi_net_status (s->IP, sizeof s->IP); snprintf (s->Hostname, sizeof s->Hostname, "onyx"); snprintf (s->Version, sizeof s->Version, "PocketQI on Onyx"); }
	unsigned GetScan (TWifiAP *, unsigned, unsigned *pAge, bool) override { if (pAge) *pAge = 0; return 0; }
	void RequestReboot (unsigned) override {}
};

void AdminHandleRequest (CAdminHost *, const THttpRequest &rRequest, CAdminText *pResponse)
{
	if (AdminIsAppsPath (rRequest.Path)) { AdminAppsRequest (rRequest, pResponse); return; }
	if (AdminIsFacePath (rRequest.Path)) { AdminFaceRequest (rRequest, pResponse); return; }
	if (AdminIsScreenPath (rRequest.Path)) { AdminScreenRequest (rRequest, pResponse); return; }
	if (!strcmp (rRequest.Path, "/") && (!strcmp (rRequest.Method, "GET") || !strcmp (rRequest.Method, "HEAD")))
	{
		CAdminText b;
		b.Append ("<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\">"
			  "<title>Pocket (Onyx)</title><style>body{font-family:sans-serif;max-width:40em;margin:2em auto;padding:0 1em}"
			  "code{word-break:break-all}</style></head><body><h1>Pocket, sur Onyx</h1>"
			  "<p>PocketQI Studio peut envoyer ses apps &agrave; ce Pocket : cible &laquo;&nbsp;Pocket QI physique&nbsp;&raquo;, "
			  "cette adresse.</p><p>Empreinte SHA-256 du certificat :<br><code>");
		b.AppendHtml (AdminTlsFingerprint ());
		b.Append ("</code></p><p>Valide : ");
		b.AppendHtml (AdminTlsValidity ());
		b.Append ("</p></body></html>\n");
		HttpResponse (pResponse, 200, "text/html; charset=utf-8", b.Get (), b.GetLength ());
		return;
	}
	static const char nf[] = "Pocket : page inconnue\n";
	HttpResponse (pResponse, 404, "text/plain; charset=utf-8", nf, sizeof nf - 1);
}

// ---- the connection: AppKit's TCP --------------------------------------------------------------------------------
static int studio_send (void *c, const void *b, unsigned n) { return kapi_tcp_send (*(int *) c, b, n); }
static int studio_recv (void *c, void *b, unsigned n) { return kapi_tcp_recv (*(int *) c, b, n); }
static void studio_sleep (unsigned ms) { kapi_msleep (ms); }
static unsigned studio_millis () { return kapi_get_ticks () * 10; }

static volatile bool g_studioOn;			// the setting (`studio = on`)
static volatile int  g_studioLs = -1;			// the listening socket (-1: none)
static int g_studioTid;
static std::string g_studioError;			// why it does not serve ("": it serves, or it is off)

static int studio_thread (void *)
{
	static StudioHost host;
	char err[200] = "";
	if (!AdminTlsInit (err, sizeof err)) { g_studioError = err; AdminLog ("TLS: %s", err); g_studioTid = 0; return 0; }
	AdminSetAppRunner (studio_runner);
	AdminSetFaceSource (studio_face);
	AdminLog ("certificate SHA-256 %s", AdminTlsFingerprint ());
	while (!g_quit && g_studioOn)
	{
		int ls = kapi_tcp_listen (STUDIO_PORT);
		if (ls < 0)
		{
			// (the port taken: the other Pocket -- the window or the background -- serves; tried again later)
			g_studioError = ls == -6 ? "the port is used (another Pocket serves PocketQI Studio)" : "no network";
			for (int i = 0; i < 100 && !g_quit && g_studioOn; i++) kapi_msleep (100);
			continue;
		}
		g_studioLs = ls;
		g_studioError.clear ();
		while (!g_quit && g_studioOn)
		{
			char ip[48];
			int s = kapi_tcp_accept (ls, ip, sizeof ip);
			if (s < 0) break;
			if (!g_studioOn) { kapi_tcp_close (s); break; }
			TAdminConnIO io = { &s, studio_send, studio_recv, studio_sleep, studio_millis };
			AdminServeTls (&host, &io);
			kapi_tcp_close (s);
		}
		g_studioLs = -1;
		kapi_tcp_close (ls);
	}
	g_studioTid = 0;
	return 0;
}

// the setting applied: the server started or stopped
static void studio_apply (bool on)
{
	g_studioOn = on;
	if (on && g_studioTid <= 0) g_studioTid = kapi_thread_create (studio_thread, 0, 512 * 1024, "studio");
	if (!on && g_studioLs >= 0) kapi_tcp_close (g_studioLs);		// (accept returns: the thread ends)
}

#endif
