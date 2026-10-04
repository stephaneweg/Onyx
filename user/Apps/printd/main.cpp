//
// printd -- the print service (the IPC service "print"; docs/03 "Printing"): the queue of the jobs the apps
// recorded (SD:/var/spool/print: <id>.opj the pages, <id>.job the ticket -- print/print.cpp writes them),
// printed one after the other; the printers (SD:/etc/printers.ini: added, removed, asked what they can do).
//
// A job, by its printer's kind:
//   pdf  the pages replayed as a PDF (print/pdfsink.h), saved where the user said;
//   ipp  a network printer (IPP Everywhere / AirPrint; print/ipp.h): the pages rendered at its resolution
//        (print/raster.h) and streamed as PWG Raster while they are made -- or sent as a PDF to a printer
//        that takes PDF --, then the printer asked until it has printed.
// The user is told when a job is printed, or why it was not (notifyd). One thread: while a job is rendered
// and sent, the requests (the queue's list, a cancel) are answered between the rows.
//
// No window. Started at boot (SD:/etc/autostart: the jobs left in the spool are printed) and by the
// library when an app prints.
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "kapi.h"
#include "notify.h"
#include "print/printers.h"
#include "print/job.h"
#include "print/pdfsink.h"
#include "print/raster.h"
#include "print/ippnet.h"

using namespace pprt;

struct Job
{
	unsigned id; int state, pages, page;
	char printer[64], title[80], message[96], media[64], output[256];
	int copies, mono, quality;
	int remote;				// its number at the printer (while it prints)
	char uri[160];
	unsigned polled; int polls;
	bool cancel;
};
enum { MAXJOB = 32 };
static Job g_job[MAXJOB]; static int g_njob;
static Job *g_cur;				// the job being rendered / sent
static bool g_forget;				// the finished jobs are to leave the list (done between two jobs)

static void spool (char *out, int cap, unsigned id, const char *ext) { snprintf (out, cap, PRINT_SPOOL "/%u%s", id, ext); }
static void spool_drop (unsigned id)
{
	char p[160];
	spool (p, sizeof p, id, ".opj"); kapi_remove (p);
	spool (p, sizeof p, id, ".job"); kapi_remove (p);
}
static Job *job_by_id (unsigned id) { for (int i = 0; i < g_njob; i++) if (g_job[i].id == id) return &g_job[i]; return 0; }
static bool finished (const Job &j) { return j.state >= PJ_DONE; }

// a job from its ticket -> queued
static bool enqueue (unsigned id)
{
	if (job_by_id (id)) return true;
	char p[160]; spool (p, sizeof p, id, ".job");
	char *t = pio_load (p, 0);
	if (!t) return false;
	if (g_njob == MAXJOB)
	{	// the list is full: the oldest finished job leaves it (not while a job runs: it is held by its place)
		int k = -1;
		if (g_cur) { delete[] t; return false; }
		for (int i = 0; i < g_njob && k < 0; i++) if (finished (g_job[i])) k = i;
		if (k < 0) { delete[] t; return false; }
		for (int i = k; i + 1 < g_njob; i++) g_job[i] = g_job[i + 1];
		g_njob--;
	}
	Job &j = g_job[g_njob]; memset (&j, 0, sizeof j);
	j.id = id; j.copies = 1; j.quality = 4;
	Ini ini (t);
	while (ini.next ())
	{
		const char *k = ini.key, *v = ini.val;
		if (seq (k, "printer")) scpy (j.printer, sizeof j.printer, v);
		else if (seq (k, "title")) scpy (j.title, sizeof j.title, v);
		else if (seq (k, "media")) scpy (j.media, sizeof j.media, v);
		else if (seq (k, "output")) scpy (j.output, sizeof j.output, v);
		else if (seq (k, "copies")) j.copies = sint (v);
		else if (seq (k, "mono")) j.mono = sint (v);
		else if (seq (k, "quality")) j.quality = sint (v);
		else if (seq (k, "pages")) j.pages = sint (v);
	}
	delete[] t;
	j.state = PJ_QUEUED;
	g_njob++;
	return true;
}

// ---- the requests ----
static void answer (unsigned token, const void *b, unsigned n)
{
	char p[64], q[72];
	snprintf (p, sizeof p, PRINT_REPLY "/%u", token); snprintf (q, sizeof q, "%s.part", p);
	if (kapi_save_file (q, b, n) < 0) return;
	kapi_remove (p); kapi_rename (q, p);
}
static void answer_ok (unsigned token, bool ok, const char *text)
{
	PdAnswer a; memset (&a, 0, sizeof a);
	a.ok = ok; scpy (a.text, sizeof a.text, text ? text : "");
	answer (token, &a, sizeof a);
}
static void clean_name (char *n)
{
	for (char *p = n; *p; p++) if (*p == '[' || *p == ']' || *p == '=' || *p == ',' || (unsigned char) *p < 32) *p = ' ';
	int l = slen (n); while (l > 0 && n[l - 1] == ' ') n[--l] = 0;
}
// a printer asked what it can do -> its entry
static bool probe (const char *address, const char *name, Printer &p, char *why, int cap, ipp::Caps *out = 0)
{
	ipp::Caps *c = new ipp::Caps;
	bool ok = ipp::get_caps (&ipp::KAPI_NET, address, *c, why, cap);
	if (ok && !c->pwg && !c->pdf) { scpy (why, cap, "this printer takes neither PWG Raster nor PDF (not an IPP Everywhere / AirPrint printer)"); ok = false; }
	if (ok)
	{
		ipp::Uri u; ipp::parse_uri (address, u);
		memset (&p, 0, sizeof p);
		scpy (p.name, sizeof p.name, name && name[0] ? name : c->name[0] ? c->name : c->model); clean_name (p.name);
		scpy (p.kind, sizeof p.kind, "ipp"); scpy (p.uri, sizeof p.uri, u.ipp); scpy (p.model, sizeof p.model, c->model);
		scpy (p.format, sizeof p.format, c->pwg ? "image/pwg-raster" : "application/pdf");
		p.color = c->color; p.dpi = c->dpi ? c->dpi : 300; p.copies = c->copies > 0 ? c->copies : 1; p.quality = c->quality;
		scpy (p.media, sizeof p.media, c->media[0] ? c->media : "iso_a4_210x297mm,na_letter_8.5x11in");
		scpy (p.media_default, sizeof p.media_default, c->media_default[0] ? c->media_default : "iso_a4_210x297mm");
		for (int i = 0; i < 4; i++) p.margin[i] = c->margin[i];
		if (out) *out = *c;
	}
	delete c;
	return ok;
}
// The local network searched for printers, the way AirPrint finds them (mDNS / DNS-SD): one question -- who
// offers "_ipp._tcp.local"? -- sent to the multicast address 224.0.0.251:5353 from an ordinary port: every
// printer answers to that port (RFC 6762, a "one-shot" query: the answers come to us alone, no multicast
// group to join). Whoever answers is then asked who it is, over IPP. One datagram sent, twice: nothing
// the network notices -- unlike trying every address, which the Pi's network stack does not stand (it
// restarted the Pi: 2026-10-05).
static void scan (unsigned token)
{
	PdFound *found = new PdFound[16]; int nf = 0;
	unsigned char who[16][4]; int nwho = 0;
	int u = kapi_net_status (0, 0) ? kapi_sock_open (KAPI_SOCK_DGRAM, 0) : -1;
	if (u >= 0)
	{
		static const unsigned char Q[] = { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
			4, '_', 'i', 'p', 'p', 4, '_', 't', 'c', 'p', 5, 'l', 'o', 'c', 'a', 'l', 0, 0, 12, 0x80, 1 };	// PTR, IN, unicast answer
		struct kapi_sockaddr to; memset (&to, 0, sizeof to);
		to.family = KAPI_AF_INET; to.port = 5353; to.addr[0] = 224; to.addr[3] = 251;
		kapi_sock_setopt (u, KAPI_SO_RCVTIMEO_MS, 250);
		for (int round = 0; round < 2; round++)
		{
			if (kapi_sock_send (u, Q, sizeof Q, 0, &to) != (long long) sizeof Q) break;
			for (int k = 0; k < 6; k++)					// (a second and a half of answers)
			{
				static unsigned char ans[1500]; struct kapi_sockaddr from; memset (&from, 0, sizeof from);
				long long r = kapi_sock_recv (u, ans, sizeof ans, 0, &from);
				if (r < 12 || !(ans[2] & 0x80)) continue;		// (not an answer)
				bool have = false;
				for (int i = 0; i < nwho; i++) if (!memcmp (who[i], from.addr, 4)) have = true;
				if (!have && nwho < 16) memcpy (who[nwho++], from.addr, 4);
			}
		}
		kapi_sock_close (u);
	}
	ipp::Caps *cp = new ipp::Caps;
	for (int i = 0; i < nwho && nf < 16; i++)
	{
		char addr[32], why[96]; snprintf (addr, sizeof addr, "%d.%d.%d.%d", who[i][0], who[i][1], who[i][2], who[i][3]);
		if (!ipp::get_caps (&ipp::KAPI_NET, addr, *cp, why, sizeof why)) continue;
		PdFound &f = found[nf++]; memset (&f, 0, sizeof f);
		scpy (f.address, sizeof f.address, addr); scpy (f.name, sizeof f.name, cp->name[0] ? cp->name : cp->model);
		scpy (f.model, sizeof f.model, cp->model); f.usable = cp->pwg || cp->pdf;
	}
	delete cp;
	answer (token, found, (unsigned) nf * sizeof (PdFound));
	delete[] found;
}
static void handle (int type, const PdReq &rq)
{
	switch (type)
	{
	case PD_SUBMIT: answer_ok (rq.token, enqueue (rq.id), 0); break;
	case PD_SCAN: scan (rq.token); break;
	case PD_LIST:
	{
		PdJob *o = new PdJob[MAXJOB]; int n = 0;
		for (int i = 0; i < g_njob; i++)
		{
			const Job &j = g_job[i]; PdJob &d = o[n++]; memset (&d, 0, sizeof d);
			d.id = j.id; d.state = j.state; d.pages = j.pages; d.page = j.page;
			scpy (d.printer, sizeof d.printer, j.printer); scpy (d.title, sizeof d.title, j.title); scpy (d.message, sizeof d.message, j.message);
		}
		answer (rq.token, o, (unsigned) n * sizeof (PdJob));
		delete[] o;
		break;
	}
	case PD_CANCEL:
	{
		Job *j = job_by_id (rq.id);
		if (j && !finished (*j))
		{
			if (j == g_cur) j->cancel = true;				// (the sender sees it between two rows)
			else
			{
				if (j->state == PJ_PRINTING && j->remote > 0) ipp::cancel_job (&ipp::KAPI_NET, j->uri, j->remote);
				j->state = PJ_CANCELED; scpy (j->message, sizeof j->message, "Cancelled"); spool_drop (j->id);
			}
		}
		answer_ok (rq.token, j != 0, 0);
		break;
	}
	case PD_FORGET: g_forget = true; answer_ok (rq.token, true, 0); break;
	case PD_ADD:
	{
		List *l = new List; load (*l);
		Printer p; char why[160] = "";
		bool ok = probe (rq.uri, rq.name, p, why, sizeof why);
		if (ok)
		{
			int at = -1;
			for (int i = 1; i < l->n; i++) if (seq (l->p[i].name, p.name)) at = i;
			if (at < 0 && l->n >= MAXPRINTERS) { ok = false; scpy (why, sizeof why, "the list of printers is full"); }
			else
			{
				if (at < 0) at = l->n++;
				l->p[at] = p;
				if (l->n == 2) scpy (l->def, sizeof l->def, p.name);		// the first real printer: the default
				ok = save (*l);
				if (!ok) scpy (why, sizeof why, "SD:/etc/printers.ini cannot be written");
				else scpy (why, sizeof why, p.name);
			}
		}
		answer_ok (rq.token, ok, why);
		delete l;
		break;
	}
	case PD_REMOVE: case PD_DEFAULT:
	{
		List *l = new List; load (*l);
		int at = -1;
		for (int i = 0; i < l->n; i++) if (seq (l->p[i].name, rq.name)) at = i;
		bool ok = at >= 0;
		if (ok && type == PD_DEFAULT) scpy (l->def, sizeof l->def, rq.name);
		else if (ok && at > 0)
		{
			for (int i = at; i + 1 < l->n; i++) l->p[i] = l->p[i + 1];
			l->n--;
			if (seq (l->def, rq.name)) scpy (l->def, sizeof l->def, l->n > 1 ? l->p[1].name : PRINT_PDF_NAME);
		}
		else ok = false;						// (the PDF printer stays)
		if (ok) ok = save (*l);
		answer_ok (rq.token, ok, 0);
		delete l;
		break;
	}
	case PD_REFRESH:
	{
		List *l = new List; load (*l);
		char text[160] = ""; bool ok = false;
		int at = -1;
		for (int i = 0; i < l->n; i++) if (seq (l->p[i].name, rq.name)) at = i;
		if (at == 0) { ok = true; scpy (text, sizeof text, "Ready"); }
		else if (at > 0)
		{
			Printer p; ipp::Caps *c = new ipp::Caps;
			ok = probe (l->p[at].uri, l->p[at].name, p, text, sizeof text, c);
			if (ok)
			{
				l->p[at] = p; save (*l);
				bool trouble = c->state_reasons[0] && !seq (c->state_reasons, "none");
				snprintf (text, sizeof text, "%s", c->state == 5 ? "Stopped" : c->state == 4 ? "Printing" : "Ready");
				if (trouble) { scat (text, sizeof text, " ("); scat (text, sizeof text, c->state_reasons); scat (text, sizeof text, ")"); }
				for (int i = 0; i < c->ninks; i++)
				{
					char t[64]; snprintf (t, sizeof t, " - %s %d %%", c->ink_name[i], c->ink[i]);
					scat (text, sizeof text, t);
				}
				ok = c->state != 5;
			}
			delete c;
		}
		else scpy (text, sizeof text, "no such printer");
		answer_ok (rq.token, ok, text);
		delete l;
		break;
	}
	}
}
// the mailbox emptied (block: wait for a message when there is nothing to do)
static void pump (bool block)
{
	for (;;)
	{
		PdReq rq; int from = 0, type = 0;
		memset (&rq, 0, sizeof rq);
		int n = kapi_mailbox_recv (&from, &type, &rq, sizeof rq, block ? 1 : 0);
		if (n < 0) return;
		block = false;
		rq.name[sizeof rq.name - 1] = 0; rq.uri[sizeof rq.uri - 1] = 0;
		if (n >= (int) sizeof (unsigned)) handle (type, rq);
	}
}
static void idle () { pump (false); }
static bool stop () { return g_cur && g_cur->cancel; }

// ---- a job printed ----
static void done (Job &j, int state, const char *message, bool tell = true)
{
	j.state = state; scpy (j.message, sizeof j.message, message);
	spool_drop (j.id);
	if (!tell) return;
	char t[200];
	if (state == PJ_DONE) snprintf (t, sizeof t, "%s: %s", j.title, message);
	else snprintf (t, sizeof t, "%s was not printed: %s", j.title, message);
	notify_action (state == PJ_DONE ? "Printed" : "Printing", t, "control printconf");
}

struct RasterOut : praster::Raster
{
	ipp::Send *send; Job *job; const char *media;
	RasterOut (float w, float h, int dpi, bool gray, ipp::Send *s, Job *j, const char *m) : Raster (w, h, dpi, gray), send (s), job (j), media (m) {}
	static bool wr (void *c, const void *b, unsigned n) { return ((ipp::Send *) c)->write (b, n); }
	bool page_ready () { job->page++; return praster::pwg_page (*this, media, job->quality, job->pages, wr, send); }
};

static void print_pdf (Job &j, const Printer &)
{
	char p[160]; spool (p, sizeof p, j.id, ".opj");
	pjob::Reader r; pjob::PdfSink s;
	if (!r.open (p)) { done (j, PJ_FAILED, "its pages are missing from the queue"); return; }
	int k;
	while ((k = r.page (s)) == 1) { j.page++; pump (false); if (j.cancel) { done (j, PJ_CANCELED, "Cancelled", false); return; } }
	if (k < 0) { done (j, PJ_FAILED, "the job's file is damaged"); return; }
	s.w.info (j.title, 0, 0, "Onyx");
	unsigned len = 0; unsigned char *pdf = s.w.finish (&len);
	bool ok = pdf && kapi_save_file (j.output, pdf, len) == (int) len;
	delete[] pdf;
	if (!ok) { done (j, PJ_FAILED, "the PDF file cannot be written"); return; }
	j.state = PJ_DONE; snprintf (j.message, sizeof j.message, "Saved as %s", j.output);
	spool_drop (j.id);
	char t[360], act[300]; snprintf (t, sizeof t, "%s: saved as %s", j.title, j.output); snprintf (act, sizeof act, "pdf %s", j.output);
	notify_action ("PDF saved", t, act);
}
static void print_ipp (Job &j, const Printer &pr)
{
	char p[160]; spool (p, sizeof p, j.id, ".opj");
	pjob::Reader r;
	if (!r.open (p)) { done (j, PJ_FAILED, "its pages are missing from the queue"); return; }
	bool pdf = seq (pr.format, "application/pdf");
	const char *media = j.media[0] ? j.media : pr.media_default;
	float pw = 595.28f, ph = 841.89f; media_size (media, &pw, &ph);
	ipp::Send *send = new ipp::Send;
	ipp::JobSetup js = { j.title, "onyx", pr.format, media, j.copies, j.quality, j.mono != 0 };
	scpy (j.uri, sizeof j.uri, pr.uri);
	j.state = PJ_SENDING;
	bool ok = send->begin (&ipp::KAPI_NET, pr.uri, js);
	int k = 0;
	if (ok && pdf)
	{
		pjob::PdfSink s;
		while ((k = r.page (s)) == 1) { j.page++; pump (false); if (j.cancel) break; }
		unsigned len = 0; unsigned char *d = s.w.finish (&len);
		ok = k == 0 && !j.cancel && d && send->write (d, len);
		delete[] d;
	}
	else if (ok)
	{
		ok = send->write ("RaS2", 4);
		RasterOut *out = new RasterOut (pw, ph, pr.dpi > 0 ? pr.dpi : 300, j.mono != 0, send, &j, media);
		while (ok && (k = r.page (*out)) == 1) { if (out->failed || j.cancel) ok = false; pump (false); }
		if (out->failed) ok = false;
		delete out;
	}
	if (j.cancel) { delete send; done (j, PJ_CANCELED, "Cancelled", false); return; }
	if (ok && k < 0) { delete send; done (j, PJ_FAILED, "the job's file is damaged"); return; }
	int remote = ok ? send->end () : -1;
	char why[96]; scpy (why, sizeof why, send->err[0] ? send->err : "the printer did not take the job");
	delete send;
	if (remote < 0) { done (j, PJ_FAILED, why); return; }
	// sent: the printer prints; it is asked until it is done (main's loop)
	j.remote = remote; j.state = PJ_PRINTING; j.polled = kapi_get_ticks (); j.polls = 0;
	scpy (j.message, sizeof j.message, "Sent to the printer");
	char q[160]; spool (q, sizeof q, j.id, ".opj"); kapi_remove (q);		// (the pages are at the printer; the ticket stays until the end)
}
static void run (Job &j)
{
	List *l = new List; load (*l);
	const Printer *pr = find (*l, j.printer);
	g_cur = &j; j.state = PJ_PREPARING; j.page = 0;
	if (!pr) done (j, PJ_FAILED, "its printer was removed");
	else if (seq (pr->kind, "pdf")) print_pdf (j, *pr);
	else print_ipp (j, *pr);
	g_cur = 0;
	delete l;
}
// the jobs at their printers: asked every few seconds
static void follow ()
{
	for (int i = 0; i < g_njob; i++)
	{
		Job &j = g_job[i];
		if (j.state != PJ_PRINTING) continue;
		char why[96];
		int st = j.remote > 0 ? ipp::job_state (&ipp::KAPI_NET, j.uri, j.remote, why, sizeof why) : ipp::JOB_COMPLETED;
		j.polls++;
		if (st == ipp::JOB_COMPLETED) { char m[96]; snprintf (m, sizeof m, "printed on %s", j.printer); done (j, PJ_DONE, m); }
		else if (st == ipp::JOB_CANCELED) done (j, PJ_CANCELED, "Cancelled at the printer", false);
		else if (st == ipp::JOB_ABORTED) done (j, PJ_FAILED, why[0] ? why : "the printer gave up the job");
		else if (st < 0 && j.polls > 100) done (j, PJ_DONE, "sent (the printer does not say more)", false);
		else if (st == ipp::JOB_STOPPED || (why[0] && st >= 0)) snprintf (j.message, sizeof j.message, "At the printer: %s", why[0] ? why : "stopped");
		else if (st >= 0) scpy (j.message, sizeof j.message, st == ipp::JOB_PROCESSING ? "Printing" : "Waiting at the printer");
	}
}

int main (void)
{
	if (!kapi_ipc_register (PRINTD_SERVICE)) return 0;		// (it runs already)
	kapi_mkdir ("SD:/var"); kapi_mkdir ("SD:/var/spool"); kapi_mkdir (PRINT_SPOOL); kapi_mkdir (PRINT_REPLY);
	ipp::net_idle = idle; ipp::net_stop = stop;
	// the jobs left in the spool (the Pi was turned off before they were printed)
	void *d = kapi_opendir (PRINT_SPOOL);
	if (d)
	{
		struct kapi_dirent e;
		while (kapi_readdir (d, &e))
		{
			int n = slen (e.name);
			if (n > 4 && !strcmp (e.name + n - 4, ".job")) enqueue ((unsigned) strtoul (e.name, 0, 10));
		}
		kapi_closedir (d);
	}
	unsigned last = 0;
	for (;;)
	{
		if (g_forget)
		{
			int k = 0;
			for (int i = 0; i < g_njob; i++) if (!finished (g_job[i])) g_job[k++] = g_job[i];
			g_njob = k; g_forget = false;
		}
		Job *next = 0; bool printing = false;
		for (int i = 0; i < g_njob; i++)
		{
			if (g_job[i].state == PJ_QUEUED && !next) next = &g_job[i];
			if (g_job[i].state == PJ_PRINTING) printing = true;
		}
		if (next) { run (*next); continue; }
		if (!printing) { pump (true); continue; }
		pump (false);
		kapi_msleep (100);
		if (++last >= 30) { last = 0; follow (); }
	}
}
