//
// lisa/main.cpp -- Lisa, a modern "Eliza": a chat window backed by a large language model
// (the Groq chat API). The conversation on top (read-only, word-wrapped), a message box and
// a Send button below: Enter sends, Shift+Enter starts a new line.
//
// Every request sends the role (the system prompt, from the config) + the WHOLE chat
// history, so Lisa keeps the context of the conversation. The HTTPS work is done by the
// console tool /bin/groq (newlib + mbedTLS), spawned with the history as a JSON array on
// its stdin; its stdout (the answer) is polled from onTick, so the window stays live while
// Lisa "thinks".
//
// Config: SD:/apps/lisa.app/config.ini (key = the Groq API key, model, role, ...). It is
// NEVER committed; config.ini.example shows the format. Settings > Edit Configuration opens
// it in tinypad.
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "clipboard.h"

using namespace wtk;

#define W		600
#define H		470
#define INPUT_H		62
#define BTN_W		70
#define STATUS_H	20
#define CONFIG		"SD:/apps/lisa.app/config.ini"
#define GROQ		"SD:/bin/groq"
#define LOG_CAP		65536			// transcript buffer
#define HIST_CAP	24576			// history kept for the context (oldest turns dropped)
#define ANS_CAP		32768

static Textarea *g_log;			// the conversation (read-only)
static Textarea *g_input;		// the message being typed
static Label    *g_status;
static Button   *g_send;

// ---- history: alternating user / assistant turns (NUL-separated, role char first) ------
struct Turn { char role; int off, len; };
#define MAXTURN 128
static Turn g_turn[MAXTURN];
static int  g_nturn;
static char g_hist[HIST_CAP];
static int  g_hlen;

static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }

static void hist_drop_oldest (void)
{
	if (g_nturn == 0) return;
	int n = g_turn[0].len + 1;
	for (int i = n; i < g_hlen; i++) g_hist[i - n] = g_hist[i];
	g_hlen -= n;
	for (int i = 1; i < g_nturn; i++) { g_turn[i - 1] = g_turn[i]; g_turn[i - 1].off -= n; }
	g_nturn--;
}
static void hist_add (char role, const char *s)
{
	int n = slen (s);
	if (n > HIST_CAP / 2) n = HIST_CAP / 2;
	while (g_nturn > 0 && (g_hlen + n + 1 > HIST_CAP || g_nturn >= MAXTURN)) hist_drop_oldest ();
	// keep the history starting with a user turn (the API expects user first after system)
	while (g_nturn > 0 && g_turn[0].role != 'u') hist_drop_oldest ();
	Turn &t = g_turn[g_nturn++];
	t.role = role; t.off = g_hlen; t.len = n;
	for (int i = 0; i < n; i++) g_hist[g_hlen++] = s[i];
	g_hist[g_hlen++] = '\0';
}

// ---- transcript -----------------------------------------------------------------------
static int log_cols (void) { int c = (g_log->width - 8 - WK_SBW) / wk_fw (); return c < 20 ? 20 : c; }

// Append text word-wrapped to the transcript width; every line after the first is
// indented under the speaker's name.
static void log_append (const char *who, const char *text)
{
	static char out[ANS_CAP + 4096];
	int n = 0, cols = log_cols (), ind = slen (who), col = 0;
	auto putc_ = [&] (char c) { if (n < (int) sizeof out - 2) out[n++] = c; };
	if (g_log->len > 0) { putc_ ('\n'); }
	for (int i = 0; who[i]; i++) { putc_ (who[i]); col++; }
	const char *p = text;
	while (*p)
	{
		if (*p == '\n')
		{
			putc_ ('\n'); for (int i = 0; i < ind; i++) putc_ (' ');
			col = ind; p++; continue;
		}
		int wl = 0; while (p[wl] && p[wl] != ' ' && p[wl] != '\n') wl++;	// next word
		if (col + wl > cols && col > ind)
		{
			putc_ ('\n'); for (int i = 0; i < ind; i++) putc_ (' ');
			col = ind;
			while (*p == ' ') p++;
			continue;
		}
		if (wl == 0) { putc_ (*p++); col++; continue; }			// a space
		if (wl > cols - ind) wl = cols - ind;					// a very long word: cut
		for (int i = 0; i < wl; i++) { putc_ (*p++); col++; }
	}
	putc_ ('\n');
	out[n] = '\0';
	// Keep the transcript bounded: drop the oldest half when full.
	if (g_log->len + n >= LOG_CAP - 1)
	{
		int cut = g_log->len / 2;
		while (cut < g_log->len && g_log->buf[cut] != '\n') cut++;
		static char keep[LOG_CAP];
		int k = 0;
		for (int i = cut + 1; i < g_log->len; i++) keep[k++] = g_log->buf[i];
		keep[k] = '\0';
		g_log->setContent (keep);
	}
	g_log->anchor = -1;
	g_log->caret = g_log->len;
	g_log->insertText (out);
	g_log->gotoLine (1 << 30);				// scroll to the end
}

// Light Markdown clean-up for plain-text display: **bold**, __x__, `code`, "### " headings.
static void demarkdown (char *s)
{
	int j = 0; bool bol = true;
	for (int i = 0; s[i]; )
	{
		if ((s[i] == '*' && s[i + 1] == '*') || (s[i] == '_' && s[i + 1] == '_')) { i += 2; continue; }
		if (s[i] == '`') { i++; continue; }
		if (bol && s[i] == '#') { while (s[i] == '#') i++; if (s[i] == ' ') i++; continue; }
		bol = s[i] == '\n';
		s[j++] = s[i++];
	}
	while (j > 0 && (s[j - 1] == '\n' || s[j - 1] == ' ')) j--;
	s[j] = '\0';
}

// ---- the request (a /bin/groq child) ----------------------------------------------------
static void *g_proc, *g_in, *g_out;
static char  g_ans[ANS_CAP];
static int   g_alen;
static unsigned g_t0;
static char  g_pending[4096];		// the message in flight (restored to the box on error)

static void set_busy (bool b)
{
	g_send->disabled = b; g_send->invalidate (true);
	if (!b) g_status->setText ("");
}

static int json_esc (char *d, int cap, const char *s)
{
	static const char hex[] = "0123456789abcdef";
	int n = 0;
	for (; *s && n < cap - 8; s++)
	{
		unsigned char c = (unsigned char) *s;
		if (c == '"' || c == '\\') { d[n++] = '\\'; d[n++] = (char) c; }
		else if (c == '\n') { d[n++] = '\\'; d[n++] = 'n'; }
		else if (c < 0x20 || c >= 0x7F)
		{ d[n++] = '\\'; d[n++] = 'u'; d[n++] = '0'; d[n++] = '0'; d[n++] = hex[c >> 4]; d[n++] = hex[c & 15]; }
		else d[n++] = (char) c;
	}
	d[n] = '\0';
	return n;
}

static bool config_ok (void)
{
	void *f = kapi_open (CONFIG);
	if (f == 0) return false;
	static char b[4096];
	int n = kapi_read (f, b, sizeof b - 1);
	kapi_close (f);
	if (n <= 0) return false;
	b[n] = '\0';
	for (int i = 0; i < n; i++)				// a non-empty "key = ..." line
	{
		if ((i == 0 || b[i - 1] == '\n') && b[i] == 'k' && b[i + 1] == 'e' && b[i + 2] == 'y')
		{
			int j = i + 3; while (b[j] == ' ' || b[j] == '\t') j++;
			if (b[j] != '=') continue;
			j++; while (b[j] == ' ' || b[j] == '\t') j++;
			if (b[j] && b[j] != '\r' && b[j] != '\n') return true;
		}
	}
	return false;
}

static void show_setup (void)
{
	log_append ("Lisa: ", "I need a Groq API key to talk. Create one (free) at console.groq.com, "
		    "then put it in " CONFIG " as a line:\n    key = gsk_...\n"
		    "Settings > Edit Configuration opens that file (config.ini.example shows every "
		    "option: model, role, temperature).");
}

static void send_now (void)
{
	if (g_proc) return;
	const char *msg = g_input->content ();
	int s = 0; while (msg[s] == ' ' || msg[s] == '\n') s++;
	if (msg[s] == '\0') return;
	if (!config_ok ()) { show_setup (); return; }

	int k = 0; for (; msg[s + k] && k < (int) sizeof g_pending - 1; k++) g_pending[k] = msg[s + k];
	while (k > 0 && (g_pending[k - 1] == '\n' || g_pending[k - 1] == ' ')) k--;
	g_pending[k] = '\0';
	log_append ("You:  ", g_pending);
	hist_add ('u', g_pending);
	g_input->setContent ("");

	// The history as a JSON array of messages (the tool adds the system role).
	static char req[HIST_CAP * 2 + 4096];
	int n = 0;
	req[n++] = '[';
	for (int i = 0; i < g_nturn; i++)
	{
		const char *hdr = g_turn[i].role == 'u' ? "{\"role\":\"user\",\"content\":\"" : "{\"role\":\"assistant\",\"content\":\"";
		if (i) req[n++] = ',';
		for (int j = 0; hdr[j]; j++) req[n++] = hdr[j];
		n += json_esc (req + n, (int) sizeof req - n - 8, g_hist + g_turn[i].off);
		req[n++] = '"'; req[n++] = '}';
	}
	req[n++] = ']'; req[n] = '\0';

	g_in = kapi_pipe (); g_out = kapi_pipe ();
	g_proc = kapi_spawn (GROQ, "-j", g_in, g_out);
	if (g_proc == 0)
	{
		kapi_stream_close (g_in); kapi_stream_close (g_out); g_in = g_out = 0;
		log_append ("      ", "[cannot start " GROQ "]");
		return;
	}
	for (int off = 0; off < n; )
	{
		int w = kapi_stream_write (g_in, req + off, (unsigned) (n - off));
		if (w <= 0) break;
		off += w;
	}
	kapi_stream_eof (g_in);
	g_alen = 0; g_ans[0] = '\0';
	g_t0 = kapi_get_ticks ();
	set_busy (true);
}

static void finish (void)
{
	kapi_wait (g_proc); g_proc = 0;
	kapi_stream_close (g_in); kapi_stream_close (g_out); g_in = g_out = 0;
	set_busy (false);
	g_ans[g_alen] = '\0';
	if (g_alen == 0 || (g_ans[0] == 'e' && g_ans[1] == 'r' && g_ans[2] == 'r' && g_ans[3] == 'o' && g_ans[4] == 'r' && g_ans[5] == ':'))
	{
		// Failed: forget the question (so it can be sent again) and give it back.
		if (g_nturn > 0 && g_turn[g_nturn - 1].role == 'u') { g_hlen = g_turn[g_nturn - 1].off; g_nturn--; }
		while (g_alen > 0 && g_ans[g_alen - 1] == '\n') g_ans[--g_alen] = '\0';
		log_append ("      ", g_alen ? g_ans : "error: no answer");
		if (g_input->len == 0) g_input->setContent (g_pending);
		g_input->setFocus ();
		return;
	}
	demarkdown (g_ans);
	hist_add ('a', g_ans);
	log_append ("Lisa: ", g_ans);
	g_input->setFocus ();
}

static void poll_child (void)
{
	if (g_proc == 0) return;
	int r;
	while (g_alen < ANS_CAP - 1 && (r = kapi_stream_read_nb (g_out, g_ans + g_alen, (unsigned) (ANS_CAP - 1 - g_alen))) > 0)
		g_alen += r;
	if (kapi_proc_done (g_proc))
	{
		while (g_alen < ANS_CAP - 1 && (r = kapi_stream_read_nb (g_out, g_ans + g_alen, (unsigned) (ANS_CAP - 1 - g_alen))) > 0)
			g_alen += r;
		finish ();
		return;
	}
	static const char *dots[] = { "Lisa is thinking", "Lisa is thinking.", "Lisa is thinking..", "Lisa is thinking..." };
	static int last = -1;
	int d = (int) ((kapi_get_ticks () - g_t0) / 400) % 4;
	if (d != last) { last = d; g_status->setText (dots[d]); }
}

// ---- widgets / menu ---------------------------------------------------------------------
// The message box: Enter sends, Shift+Enter is a new line.
class InputBox : public Textarea
{
public:
	InputBox (int l, int t, int w, int h) : Textarea (l, t, w, h, 4000) {}
	bool onKey (long k) override
	{
		if (k == KEY_ENTER && !(kapi_get_modifiers () & MOD_SHIFT)) { send_now (); return true; }
		return Textarea::onKey (k);
	}
};

class LisaRoot : public Root
{
public:
	LisaRoot () : Root (W, H, "Lisa") {}
	void onTick () override { poll_child (); }
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{ if (type == DND_TEXT) { g_input->insertText (data); g_input->setFocus (); } }
};

static void on_send (Widget &) { send_now (); g_input->setFocus (); }
static void on_new (void)
{
	if (g_proc) return;
	g_nturn = 0; g_hlen = 0;
	g_log->setContent ("");
	log_append ("Lisa: ", "Hello! I'm Lisa. What would you like to talk about?");
	g_input->setFocus ();
}
static void on_save (void)
{
	char path[100];
	if (wk_file_save (path, sizeof path, "SD:/", "SD:/lisa-chat.txt"))
		kapi_save_file (path, g_log->content (), (unsigned) g_log->len);
	g_input->setFocus ();
}
static void on_copy (void)
{
	if (g_log->hasSelection ()) g_log->copy (); else g_input->copy ();
}
static void on_paste (void) { g_input->paste (); g_input->setFocus (); }
static void on_copy_answer (void)
{
	for (int i = g_nturn - 1; i >= 0; i--)
		if (g_turn[i].role == 'a') { clip_set_text_n (g_hist + g_turn[i].off, g_turn[i].len); return; }
}
static void on_config (void)
{
	void *f = kapi_open (CONFIG);
	if (f) kapi_close (f);
	else
	{
		static const char tmpl[] =
			"# Lisa -- Groq chat API settings. KEEP THIS FILE PRIVATE (it holds your API key).\n"
			"# Get a key at https://console.groq.com (API Keys).\n"
			"key =\n"
			"model = llama-3.3-70b-versatile\n"
			"temperature = 0.7\n"
			"max_tokens = 1024\n"
			"role = You are Lisa, a friendly and concise assistant living inside Onyx, a small\n"
			"role = operating system for the Raspberry Pi 4. Answer in the user's language.\n"
			"role = Write plain text for a small window: short paragraphs, no Markdown tables.\n";
		kapi_save_file (CONFIG, tmpl, sizeof tmpl - 1);
	}
	kapi_exec ("SD:/apps/tinypad.app/main", CONFIG);
}

int main (void)
{
	LisaRoot root;
	if (root.canvas.px == 0) return 1;
	root.setBg (0x00303840);

	int logH = H - INPUT_H - STATUS_H - 12;
	g_log = new Textarea (4, 4, W - 8, logH, LOG_CAP);
	g_log->readonly = true;
	root.addChild (g_log);
	g_status = new Label (8, 4 + logH + 2, W - 16, STATUS_H - 4, "", 0x00A8D8B8, 0x00303840);
	root.addChild (g_status);
	int iy = 4 + logH + STATUS_H;
	g_input = new InputBox (4, iy, W - 8 - BTN_W - 6, INPUT_H);
	root.addChild (g_input);
	g_send = new Button (W - 4 - BTN_W, iy, BTN_W, INPUT_H, "Send", on_send);
	root.addChild (g_send);

	static Menu menu;
	menu.menu ("Chat");
	menu.item ("New Conversation",  "^N", WK_CTRL ('N'), on_new);
	menu.item ("Save Transcript...", "^S", WK_CTRL ('S'), on_save);
	menu.menu ("Edit");
	menu.item ("Copy",              "^C", WK_CTRL ('C'), on_copy);
	menu.item ("Paste",             "^V", WK_CTRL ('V'), on_paste);
	menu.item ("Copy Last Answer",  "",   0,             on_copy_answer);
	menu.menu ("Settings");
	menu.item ("Edit Configuration...", "", 0, on_config);
	menu.publish ();

	on_new ();
	if (!config_ok ()) show_setup ();
	g_input->setFocus ();

	char args[512];
	if (kapi_get_args (args, sizeof args) > 0 && args[0]) { g_input->setContent (args); send_now (); }

	root.run ();
	if (g_proc) { kapi_wait (g_proc); }
	return 0;
}
