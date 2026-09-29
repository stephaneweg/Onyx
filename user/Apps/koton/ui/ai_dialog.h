//
// ui/ai_dialog.h -- "Compose with AI": Koton's AI dialog. What to ask (a whole piece, a development
// of the theme, a new instrument over the song, drums, a polyrhythmic piece), the style and the
// intention in words, the length and the options (melody as riffs, drums, the AI voicing the
// chords, poly chords / poly drums), the provider, its model and the API key. Generate runs the
// helper SD:/bin/llm (a TLS program: the prompt in, the model's text out) while a progress box shows
// what it does; the reply is placed on the song by engine/ai (aiApplyReply) -- one undo step. Without
// a key: "Copy the prompt" (to paste in any chat) and "Paste a reply" (the chat's answer applied).
//
#ifndef _koton_ai_dialog_h
#define _koton_ai_dialog_h

#include "ui/dialogs.h"
#include "engine/ai.h"
#include "clipboard.h"

namespace kui {

struct AiSettings { char *provider; int provCap; char *model; int modelCap; char *key; int keyCap; void (*save) (); };

static const int s_aiKinds[5] = { AI_COMPOSE, AI_DEVELOP, AI_ADD_TRACK, AI_ADD_DRUMS, AI_POLYRHYTHM };
static const char *const s_aiKindNames[5] = { "Compose a piece", "Develop the theme (after the end)", "Add an instrument over the song",
	"Add drums over the song", "A polyrhythmic piece" };

class AiDialog : public KDialog
{
public:
	enum { B_COPY = 10, B_PASTE };
	Dropdown *kind, *prov;
	Textbox *style, *model, *key;
	Textarea *intent;
	NumericUpDown *bars;
	Checkbox *full, *drums, *voice, *pchords, *pdrums;
	int action;			// 1 generate, B_COPY, B_PASTE
	const char *provNames[AI_PROVIDER_COUNT];
	AiDialog (const AiSettings &s, int initialKind) : KDialog (640, 470, "Compose with AI"), action (0)
	{
		int y = top0 (), x = 150, w = width - x - 16;
		lab (16, y + 1, 130, "What");
		int ks = 0; for (int i = 0; i < 5; i++) if (s_aiKinds[i] == initialKind) ks = i;
		kind = new Dropdown (x, y, w, 24, s_aiKindNames, 5, ks, 0); addChild (kind);
		y += 32;
		lab (16, y + 1, 130, "Style");
		style = new Textbox (x, y, w, 26, "bossa nova, warm, acoustic"); addChild (style);
		y += 34;
		lab (16, y + 1, 130, "Intention");
		intent = new Textarea (x, y, w, 70, 1000); addChild (intent);
		intent->setContent ("A calm intro, a verse, a brighter chorus, an ending.");
		y += 78;
		lab (16, y + 1, 130, "Bars (about)");
		bars = new NumericUpDown (x, y, 90, 24, 4, 128, 32, 4, 0); addChild (bars);
		y += 34;
		full = new Checkbox (x, y, 230, 22, "The melody as notes (riffs)", false, 0, C_FACE); addChild (full);
		drums = new Checkbox (x + 240, y, 200, 22, "Drums", true, 0, C_FACE); addChild (drums);
		y += 26;
		voice = new Checkbox (x, y, 230, 22, "The AI voices the chords", false, 0, C_FACE); addChild (voice);
		pchords = new Checkbox (x + 240, y, 200, 22, "Poly chords", false, 0, C_FACE); addChild (pchords);
		y += 26;
		pdrums = new Checkbox (x + 240, y, 200, 22, "Poly drums", false, 0, C_FACE); addChild (pdrums);
		y += 36;
		lab (16, y + 1, 130, "Provider");
		int ps = 0;
		for (int i = 0; i < AI_PROVIDER_COUNT; i++) { provNames[i] = aiProviderLabel (g_aiProviders[i]); if (!strcmp (g_aiProviders[i], s.provider)) ps = i; }
		prov = new Dropdown (x, y, 170, 24, provNames, AI_PROVIDER_COUNT, ps, provChanged); addChild (prov);
		model = new Textbox (x + 180, y, w - 180, 26, s.model); addChild (model);
		y += 34;
		lab (16, y + 1, 130, "API key");
		key = new Textbox (x, y, w, 26, s.key); key->password = true; key->tip = "Kept in SD:/koton/settings.json, in plain text"; addChild (key);
		Button *b = new Button (16, height - 40, 140, 28, "Copy the prompt", dlgButton); b->tag = B_COPY; addChild (b);
		b = new Button (164, height - 40, 130, 28, "Paste a reply", dlgButton); b->tag = B_PASTE; addChild (b);
		for (Widget *c = firstChild; c; c = c->nextSib) if (c->tag == 1 && c != b) { snprintf (((Button *) c)->text, 64, "Generate"); break; }
	}
	static void provChanged (Widget &w)
	{
		AiDialog *d = (AiDialog *) w.parent;
		d->model->setText (aiProviderDefaultModel (g_aiProviders[iclamp (d->prov->sel, 0, AI_PROVIDER_COUNT - 1)]));
	}
	void onOther (int tag) override { action = tag; close (1); }
	void onButton (int tag) override { if (tag == 1) action = 1; KDialog::onButton (tag); }
	bool onKey (long k) override { if (k == KEY_ENTER) return false; return KDialog::onKey (k); }	// (Enter: a new line of the intention)
	void request (AiRequest &r)
	{
		r.kind = s_aiKinds[iclamp (kind->sel, 0, 4)];
		r.style = style->text; r.intention = intent->content ();
		r.measures = bars->value;
		r.fullMelody = full->checked; r.drums = drums->checked; r.chordsVoice = voice->checked;
		r.polyChords = pchords->checked; r.polyDrums = pdrums->checked;
		r.english = true;
	}
};

// the progress box while the helper runs (Cancel kills it)
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

// run the helper: the request in, its output -> out (false: cancelled / could not start)
static bool runLlm (const char *req, unsigned reqLen, Vec<char> &out, AiBusy *busy)
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
	r->addChild (busy); busy->done = false;
	char buf[4096];
	bool cancelled = false;
	for (;;)
	{
		int n;
		while ((n = kapi_stream_read_nb (pout, buf, sizeof buf)) > 0) for (int i = 0; i < n; i++) out.push (buf[i]);
		if (kapi_proc_done (proc)) { while ((n = kapi_stream_read_nb (pout, buf, sizeof buf)) > 0) for (int i = 0; i < n; i++) out.push (buf[i]); break; }
		// the last progress line ("llm: ..." -- a line of its own)
		Str last;
		{
			int end = out.size (), i = end - 1;
			while (i >= 0 && (out[i] == '\n' || out[i] == '\r')) i--;
			int e2 = i + 1;
			while (i >= 0 && out[i] != '\n') i--;
			if (e2 - (i + 1) > 5 && out[i + 1] == 'l' && out[i + 2] == 'l' && out[i + 3] == 'm') last = Str (out.data () + i + 1, imin (150, e2 - (i + 1)));
			(void) end;
		}
		if (!last.empty ()) snprintf (busy->line, sizeof busy->line, "%s", last.c ());
		wk_pump ();
		if (busy->done || wk_quit ()) { cancelled = true; break; }
		busy->invalidate (true);
		if (!r->valid) { r->draw (); wk_present (); }
		msleep (30);
	}
	if (cancelled) kapi_kill ("llm");			// (by its name: the only llm is ours)
	kapi_wait (proc);
	r->removeChild (busy); r->invalidate (true);
	kapi_stream_close (in); kapi_stream_close (pout);
	return !cancelled;
}

// the whole "Compose with AI": -> true when the song changed
static bool aiCompose (AiSettings &s, int initialKind)
{
	AiDialog *d = new AiDialog (s, initialKind);
	int ok = d->run ();
	if (ok != 1) { delete d; return false; }
	AiRequest r; d->request (r);
	snprintf (s.provider, s.provCap, "%s", g_aiProviders[iclamp (d->prov->sel, 0, AI_PROVIDER_COUNT - 1)]);
	snprintf (s.model, s.modelCap, "%s", d->model->text);
	snprintf (s.key, s.keyCap, "%s", d->key->text);
	int action = d->action;
	delete d;
	if (s.save) s.save ();
	Str sys, usr; char err[200] = "";
	if (!aiBuildPrompt (g_doc.p, r, sys, usr, err, sizeof err)) { wk_messagebox ("Compose with AI", err, MB_OK); return false; }
	Str reply;
	if (action == AiDialog::B_COPY)
	{
		Str full; aiFullPrompt (sys, usr, full);
		clip_set_text (full.c ());
		wk_messagebox ("Compose with AI", "The prompt is on the clipboard: paste it in a chat with any AI, copy its whole answer, then use \"Paste a reply\".", MB_OK);
		return false;
	}
	if (action == AiDialog::B_PASTE)
	{
		int cap = 1 << 20; char *b = (char *) malloc (cap);
		int n = b ? clip_get_text (b, cap) : 0;
		if (n <= 0) { free (b); wk_messagebox ("Compose with AI", "The clipboard holds no text.", MB_OK); return false; }
		reply = b; free (b);
	}
	else
	{
		if (!s.key[0]) { wk_messagebox ("Compose with AI", "An API key is needed (Gemini: aistudio.google.com, free). Or use Copy the prompt / Paste a reply.", MB_OK); return false; }
		json::Writer w (false);
		aiBuildRequestJson (s.provider, s.model, s.key, sys, usr, 0.7, -1, w);
		if (!w.ok ()) return false;
		Vec<char> out;
		AiBusy *busy = new AiBusy ();
		bool ran = runLlm (w.data (), (unsigned) w.size (), out, busy);
		delete busy;
		if (!ran) { snprintf (g_status, sizeof g_status, "The AI request was cancelled (or SD:/bin/llm is missing)."); return false; }
		out.push (0);
		Str text, error;
		if (!aiParseLlmOutput (out.data (), out.size () - 1, text, error)) { char m[300]; snprintf (m, sizeof m, "The AI did not answer: %s", error.c ()); wk_messagebox ("Compose with AI", m, MB_OK); return false; }
		reply = text;
	}
	char sum[200];
	if (!aiCheckReply (r, reply.c (), sum, sizeof sum)) { char m[300]; snprintf (m, sizeof m, "The reply cannot be used: %s", sum); wk_messagebox ("Compose with AI", m, MB_OK); return false; }
	Project np = g_doc.p;					// (placed on a copy: a failure leaves the song alone)
	if (!aiApplyReply (np, r, reply.c (), err, sizeof err)) { wk_messagebox ("Compose with AI", err, MB_OK); return false; }
	g_doc.checkpoint ();
	g_doc.p = np;
	g_doc.changed ();
	g_doc.sel = Selection ();
	snprintf (g_status, sizeof g_status, "AI: %s", sum);
	return true;
}

} // namespace kui

#endif
