//
// ui/ai_dialog.h -- "Compose with AI": Koton's AI dialog. What to ask (a whole piece, a development
// of the theme, a new instrument over the song, drums, a polyrhythmic piece), the style and the
// intention in words, the length and the options (melody as riffs, drums, the AI voicing the
// chords, poly chords / poly drums), the provider, its model and the API key. Generate runs the
// helper SD:/bin/llm (a TLS program: the prompt in, the model's text out) with the dialog left open,
// its progress at the bottom (Cancel stops it); the reply is checked, its summary shown, and "Apply as
// a new song" places it by engine/ai (aiApplyReply) on a new song -- the piece alone, or a copy of the
// song with the addition. Without a key: "Copy the prompt" (to paste in any chat) and "Paste a reply"
// (the chat's answer, checked the same way).
//
#ifndef _koton_ai_dialog_h
#define _koton_ai_dialog_h

#include "ui/dialogs.h"
#include "engine/ai.h"
#include "systemkit/clipboard.h"

namespace kui {

// the dialog's last request (what, style, intention, bars, the options): the next opening starts from it
// (kept in SD:/koton/settings.json with the provider)
struct AiLast
{
	int kind; Str style, intention; int bars; bool full, drums, voice, pchords, pdrums;
	AiLast () : kind (AI_COMPOSE), style ("bossa nova, warm, acoustic"), intention ("A calm intro, a verse, a brighter chorus, an ending."),
		bars (32), full (false), drums (true), voice (false), pchords (false), pdrums (false) {}
};
struct AiSettings { char *provider; int provCap; char *model; int modelCap; char *key; int keyCap; AiLast *last; void (*save) ();
	bool (*askSave) (); };		// (askSave: the song's unsaved changes before a new song -- false: cancelled)

static const int s_aiKinds[5] = { AI_COMPOSE, AI_DEVELOP, AI_ADD_TRACK, AI_ADD_DRUMS, AI_POLYRHYTHM };
static const char *const s_aiKindNames[5] = { "Compose a piece", "Develop the theme (after the end)", "Add an instrument over the song",
	"Add drums over the song", "A polyrhythmic piece" };

// run the helper: the request in, its output -> out, its last progress line ("llm: ...") into `line`;
// `view` is redrawn while it runs. False when *cancel became true (or the window closed): llm killed.
static bool runLlmIn (const char *req, unsigned reqLen, Vec<char> &out, char *line, int lineCap, volatile bool *cancel, Widget *view)
{
	void *in = kapi_pipe (), *pout = kapi_pipe ();
	void *proc = kapi_spawn ("SD:/bin/llm", "", in, pout);
	if (!proc) { kapi_stream_close (in); kapi_stream_close (pout); return false; }
	for (unsigned off = 0; off < reqLen; )
	{
		int w = kapi_stream_write (in, req + off, reqLen - off);
		if (w <= 0) break;
		off += (unsigned) w;
	}
	kapi_stream_eof (in);
	Root *r = Root::current ();
	char buf[4096];
	bool cancelled = false;
	for (;;)
	{
		int n;
		while ((n = kapi_stream_read_nb (pout, buf, sizeof buf)) > 0) for (int i = 0; i < n; i++) out.push (buf[i]);
		if (kapi_proc_done (proc)) { while ((n = kapi_stream_read_nb (pout, buf, sizeof buf)) > 0) for (int i = 0; i < n; i++) out.push (buf[i]); break; }
		// the last progress line ("llm: ..." -- a line of its own)
		{
			int i = out.size () - 1;
			while (i >= 0 && (out[i] == '\n' || out[i] == '\r')) i--;
			int e2 = i + 1;
			while (i >= 0 && out[i] != '\n') i--;
			if (e2 - (i + 1) > 5 && out[i + 1] == 'l' && out[i + 2] == 'l' && out[i + 3] == 'm')
				snprintf (line, lineCap, "%.*s", imin (lineCap - 1, e2 - (i + 1)), out.data () + i + 1);
		}
		uk_pump ();
		if (*cancel || uk_quit ()) { cancelled = true; break; }
		view->invalidate (true);
		if (!r->valid) { r->draw (); uk_present (); }
		msleep (30);
	}
	if (cancelled) kapi_kill ("llm");			// (by its name: the only llm is ours)
	kapi_wait (proc);
	kapi_stream_close (in); kapi_stream_close (pout);
	return !cancelled;
}

// the progress box of a helper run outside the AI dialog (the SoundFont's download; Cancel kills it)
class AiBusy : public KDialog
{
public:
	char line[160], m_titleBuf[48];
	AiBusy () : KDialog (460, 150, "Composing...", true)
	{
		for (Widget *c = firstChild; c; c = c->nextSib) if (c->tag == 1) c->hidden = true;
		snprintf (line, sizeof line, "Starting SD:/bin/llm...");
	}
	void drawMore (Canvas &cv) override
	{
		textL (cv, 16, top0 (), 22, line, C_TEXT);
		int t = (int) (kapi_get_ticks () / 8) % 24;
		for (int i = 0; i < 24; i++) box (cv, 16 + i * 17, top0 () + 36, 12, 8, 3, i == t ? ACC : mixc (ACC, C_FACE, 190));
	}
};
static bool runLlm (const char *req, unsigned reqLen, Vec<char> &out, AiBusy *busy)
{
	Root *r = Root::current ();
	r->addChild (busy); busy->done = false;
	bool ran = runLlmIn (req, reqLen, out, busy->line, sizeof busy->line, &busy->done, busy);
	r->removeChild (busy); r->invalidate (true);
	return ran;
}

// The dialog stays open while the model composes: its progress at the bottom, Cancel stops the request.
// The reply is checked and its summary shown with "Apply as a new song" -- or Generate again. "Copy the
// prompt" / "Paste a reply" (a chat's answer, through the clipboard) keep it open and end the same way.
class AiDialog : public KDialog
{
public:
	enum { B_COPY = 10, B_PASTE, B_APPLY };
	AiSettings &S;
	Dropdown *kind, *prov;
	Textbox *style, *model, *key;
	Textarea *intent;
	NumericUpDown *bars;
	Checkbox *full, *drums, *voice, *pchords, *pdrums;
	Button *genB, *copyB, *pasteB, *applyB;
	const char *provNames[AI_PROVIDER_COUNT];
	bool running; volatile bool cancelReq;
	char status[400]; unsigned statusCol;
	Str reply; AiRequest replyReq;		// the reply waiting for Apply, and the request it answers
	Project result;				// the new song (Apply)
	AiDialog (AiSettings &s, int initialKind) : KDialog (640, 520, "Compose with AI"), S (s), running (false), cancelReq (false), statusCol (C_TEXT)
	{
		status[0] = 0;
		int y = top0 (), x = 150, w = width - x - 16;
		lab (16, y + 1, 130, "What");
		static const AiLast s_def;
		const AiLast &L = s.last ? *s.last : s_def;
		if (initialKind < 0) initialKind = L.kind;	// (the menu: the last kind; the browser's entries: theirs)
		int ks = 0; for (int i = 0; i < 5; i++) if (s_aiKinds[i] == initialKind) ks = i;
		kind = new Dropdown (x, y, w, 24, s_aiKindNames, 5, ks, 0); addChild (kind);
		y += 32;
		lab (16, y + 1, 130, "Style");
		style = new Textbox (x, y, w, 26, L.style.c ()); addChild (style);
		y += 34;
		lab (16, y + 1, 130, "Intention");
		intent = new Textarea (x, y, w, 70, 1000); addChild (intent);
		intent->setContent (L.intention.c ());
		y += 78;
		lab (16, y + 1, 130, "Bars (about)");
		bars = new NumericUpDown (x, y, 90, 24, 4, 128, iclamp (L.bars, 4, 128), 4, 0); addChild (bars);
		y += 34;
		full = new Checkbox (x, y, 230, 22, "The melody as notes (riffs)", L.full, 0, C_FACE); addChild (full);
		drums = new Checkbox (x + 240, y, 200, 22, "Drums", L.drums, 0, C_FACE); addChild (drums);
		y += 26;
		voice = new Checkbox (x, y, 230, 22, "The AI voices the chords", L.voice, 0, C_FACE); addChild (voice);
		pchords = new Checkbox (x + 240, y, 200, 22, "Poly chords", L.pchords, 0, C_FACE); addChild (pchords);
		y += 26;
		pdrums = new Checkbox (x + 240, y, 200, 22, "Poly drums", L.pdrums, 0, C_FACE); addChild (pdrums);
		y += 36;
		lab (16, y + 1, 130, "Provider");
		int ps = 0;
		for (int i = 0; i < AI_PROVIDER_COUNT; i++) { provNames[i] = aiProviderLabel (g_aiProviders[i]); if (!strcmp (g_aiProviders[i], s.provider)) ps = i; }
		prov = new Dropdown (x, y, 170, 24, provNames, AI_PROVIDER_COUNT, ps, provChanged); addChild (prov);
		model = new Textbox (x + 180, y, w - 180, 26, s.model); addChild (model);
		y += 34;
		lab (16, y + 1, 130, "API key");
		key = new Textbox (x, y, w, 26, s.key); key->password = true; key->tip = "Kept in SD:/koton/settings.json, in plain text"; addChild (key);
		genB = 0;
		for (Widget *c = firstChild; c; c = c->nextSib) if (c->tag == 1) { genB = (Button *) c; break; }
		snprintf (genB->text, 64, "Generate");
		copyB = new Button (16, height - 40, 140, 28, "Copy the prompt", dlgButton); copyB->tag = B_COPY; addChild (copyB);
		pasteB = new Button (164, height - 40, 130, 28, "Paste a reply", dlgButton); pasteB->tag = B_PASTE; addChild (pasteB);
		applyB = new Button (width - 196, height - 86, 180, 28, "Apply as a new song", dlgButton); applyB->tag = B_APPLY; applyB->hidden = true; addChild (applyB);
	}
	static void provChanged (Widget &w)
	{
		AiDialog *d = (AiDialog *) w.parent;
		d->model->setText (aiProviderDefaultModel (g_aiProviders[iclamp (d->prov->sel, 0, AI_PROVIDER_COUNT - 1)]));
	}
	void setStatus (unsigned col, const char *fmt, ...)
	{
		va_list ap; va_start (ap, fmt); vsnprintf (status, sizeof status, fmt, ap); va_end (ap);
		statusCol = col; invalidate (true);
	}
	void request (AiRequest &r)
	{
		r.kind = s_aiKinds[iclamp (kind->sel, 0, 4)];
		r.style = style->text; r.intention = intent->content ();
		r.measures = bars->value;
		r.fullMelody = full->checked; r.drums = drums->checked; r.chordsVoice = voice->checked;
		r.polyChords = pchords->checked; r.polyDrums = pdrums->checked;
		r.english = true;
	}
	// the provider, model, key and this request kept for the next time (SD:/koton/settings.json)
	void remember (const AiRequest &r)
	{
		snprintf (S.provider, S.provCap, "%s", g_aiProviders[iclamp (prov->sel, 0, AI_PROVIDER_COUNT - 1)]);
		snprintf (S.model, S.modelCap, "%s", model->text);
		snprintf (S.key, S.keyCap, "%s", key->text);
		if (S.last)
		{
			AiLast &L = *S.last;
			L.kind = r.kind; L.style = r.style; L.intention = r.intention; L.bars = r.measures;
			L.full = r.fullMelody; L.drums = r.drums; L.voice = r.chordsVoice; L.pchords = r.polyChords; L.pdrums = r.polyDrums;
		}
		if (S.save) S.save ();
	}
	bool prompt (AiRequest &r, Str &sys, Str &usr)
	{
		request (r); remember (r);
		char err[200] = "";
		if (!aiBuildPrompt (g_doc.p, r, sys, usr, err, sizeof err)) { setStatus (REC, "%s", err); return false; }
		return true;
	}
	// a reply received (or pasted): checked, then waiting for Apply
	void gotReply (const AiRequest &r, const char *text)
	{
		char sum[200];
		if (!aiCheckReply (r, text, sum, sizeof sum)) { applyB->hidden = true; setStatus (REC, "The reply cannot be used: %s", sum); return; }
		reply = text; replyReq = r;
		applyB->hidden = false;
		setStatus (GREEN, "Reply received -- %s", sum);
	}
	void busy (bool on)
	{
		running = on;
		genB->hidden = copyB->hidden = pasteB->hidden = on;
		if (on) applyB->hidden = true;
		invalidate (true);
	}
	void generate ()
	{
		AiRequest r; Str sys, usr;
		if (!prompt (r, sys, usr)) return;
		if (!S.key[0]) { setStatus (REC, "An API key is needed (Gemini: aistudio.google.com, free). Or Copy the prompt / Paste a reply."); return; }
		json::Writer w (false);
		aiBuildRequestJson (S.provider, S.model, S.key, sys, usr, 0.7, -1, w);
		if (!w.ok ()) { setStatus (REC, "The request could not be built."); return; }
		Vec<char> out;
		cancelReq = false; busy (true);
		setStatus (C_TEXT, "Starting SD:/bin/llm...");
		bool ran = runLlmIn (w.data (), (unsigned) w.size (), out, status, sizeof status, &cancelReq, this);
		busy (false);
		if (!ran) { setStatus (REC, "Cancelled (or SD:/bin/llm is missing)."); return; }
		out.push (0);
		Str text, error;
		if (!aiParseLlmOutput (out.data (), out.size () - 1, text, error)) { setStatus (REC, "The AI did not answer: %s", error.c ()); return; }
		gotReply (r, text.c ());
	}
	void copyPrompt ()
	{
		AiRequest r; Str sys, usr;
		if (!prompt (r, sys, usr)) return;
		Str all; aiFullPrompt (sys, usr, all);
		clip_set_text (all.c ());
		setStatus (C_TEXT, "The prompt is on the clipboard: paste it in a chat with any AI, copy its whole answer, then Paste a reply.");
	}
	void pasteReply ()
	{
		AiRequest r; request (r); remember (r);
		int cap = 1 << 20; char *b = (char *) malloc (cap);
		int n = b ? clip_get_text (b, cap) : 0;
		if (n <= 0) { free (b); setStatus (REC, "The clipboard holds no text."); return; }
		gotReply (r, b);
		free (b);
	}
	// Apply: the reply placed on a new song (a piece: the reply alone; an addition: a copy of the song
	// with it); the current song's unsaved changes are asked about first
	void apply ()
	{
		Project np;
		if (replyReq.kind != AI_COMPOSE && replyReq.kind != AI_POLYRHYTHM) np = g_doc.p;
		char err[200] = "";
		if (!aiApplyReply (np, replyReq, reply.c (), err, sizeof err)) { applyB->hidden = true; setStatus (REC, "%s", err); return; }
		if (S.askSave && !S.askSave ()) return;
		result = np;
		close (1);
	}
	void onButton (int tag) override
	{
		if (running) { if (tag == 0) { cancelReq = true; setStatus (C_TEXT, "Cancelling..."); } return; }
		switch (tag)
		{
		case 1: generate (); break;
		case B_COPY: copyPrompt (); break;
		case B_PASTE: pasteReply (); break;
		case B_APPLY: apply (); break;
		default: close (0); break;
		}
	}
	bool onKey (long k) override
	{
		if (k == 27) { if (running) cancelReq = true; else close (0); return true; }
		return false;				// (Enter: a new line of the intention)
	}
	void drawMore (Canvas &cv) override
	{
		// the status: two lines at most, above the buttons (left of Apply when it shows)
		int y = height - 90, maxW = applyB->hidden ? width - 32 : applyB->left - 28;
		const char *s = status;
		for (int l = 0; l < 2 && *s; l++)
		{
			char ln[400]; int n = 0, lastSp = -1;
			while (s[n] && n < (int) sizeof ln - 1)
			{
				ln[n] = s[n]; ln[n + 1] = 0;
				if (uk_text_w (ln) > maxW) break;
				if (s[n] == ' ') lastSp = n;
				n++;
			}
			if (s[n] && l == 0 && lastSp > 0) n = lastSp;
			ln[n] = 0;
			textL (cv, 16, y + l * 20, 20, ln, statusCol);
			s += n; while (*s == ' ') s++;
		}
		if (running)
		{
			int t = (int) (kapi_get_ticks () / 8) % 24;
			for (int i = 0; i < 24; i++) box (cv, 16 + i * 17, height - 30, 12, 8, 3, i == t ? ACC : mixc (ACC, C_FACE, 190));
		}
	}
};

// the whole "Compose with AI": -> true when a new song was made of the reply
static bool aiCompose (AiSettings &s, int initialKind)
{
	AiDialog *d = new AiDialog (s, initialKind);
	int ok = d->run ();
	if (ok != 1) { delete d; return false; }
	char sum[200]; if (!aiCheckReply (d->replyReq, d->reply.c (), sum, sizeof sum)) sum[0] = 0;
	g_doc.adopt (d->result);
	delete d;
	snprintf (g_status, sizeof g_status, "AI: a new song -- %s", sum);
	return true;
}

} // namespace kui

#endif
