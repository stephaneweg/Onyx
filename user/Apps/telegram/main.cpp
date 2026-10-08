//
// telegram -- Telegram for Onyx: an instant messenger in the way of Windows Live Messenger, on Telegram's
// open API (MTProto 2.0, written here: tl.h, tgcrypto.h, mtproto.h, client.h -- no TDLib).
//
// The window: on the left the contact list (me and my status on the sky, the search, the Favourites, the
// Conversations, the Contacts and who is online), on the right the conversation (its header, the messages
// "Alice says:", the display pictures, the line to write in with its emoticons). Before the account is
// there, the sign-in's steps (signin.h). A new message in another conversation: a notification.
//
//   telegram            the account in SD:/apps/telegram.app/session.dat (else the sign-in)
//   telegram --demo     made-up conversations, no network (demo.h)
//
// config.ini ([telegram]): api_id, api_hash (another key than the app's own, built in), test (1: Telegram's
// test servers), pictures (0: the display pictures' column hidden). log.txt: what the connection did.
//
// MIT licence.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "uikit/menu.h"
#include "uikit/lang.h"
#include "fontkit/uikitface.h"
#include "systemkit/systemkit.h"
#include "imagekit/img/pngsave.hpp"		// (the JPEG writer: ImageKit's on Onyx, PNGSAVE_USE_IMAGEKIT)
#include "tgplat.h"
#include "client.h"
#include "look.h"
#include "emoticons.h"
#include "rich.h"
#include "buddylist.h"
#include "chatview.h"
#include "chatinput.h"
#include "signin.h"
#include "demo.h"

using namespace uikit;

#define W0		940
#define H0		640
#define LIST_W		300
#define HEAD_H		76
#define INPUT_H		112
#define DP_W		150
#define SB_H		22

#define BAR_H		34
#define LIST_WIN_W	360		// the main window when the conversations have their own windows
#define CW_W		640		// a conversation's window
#define CW_H		560

static bool g_demo = false, g_pictures = true;
static bool g_windowed = true;		// (config.ini windows=) a conversation opens in its own window
static Widget *g_welcome, *g_status;
static Root *g_root;

// ---- the settings ----------------------------------------------------------------------------------------

// The app's own key at my.telegram.org (the project's, public: the user's choice, 2026-10-07). config.ini's
// api_id / api_hash, when they are there, take its place.
#define TG_API_ID	35701384
#define TG_API_HASH	"e33e9fae5727447538e9aea5c0f5a0fc"

static void save_config ()
{
	char t[400];
	int n;
	if (g_c.apiId == TG_API_ID && !strcmp (g_c.apiHash, TG_API_HASH))	// (the app's own key: not written)
		n = snprintf (t, sizeof t, "[telegram]\ntest=%d\npictures=%d\nwindows=%d\n", (int) g_c.test, (int) g_pictures, (int) g_windowed);
	else n = snprintf (t, sizeof t, "[telegram]\napi_id=%d\napi_hash=%s\ntest=%d\npictures=%d\nwindows=%d\n", g_c.apiId, g_c.apiHash, (int) g_c.test, (int) g_pictures, (int) g_windowed);
	tg_save (TG_DIR "config.ini", t, n);
}

static void load_config ()
{
	g_c.apiId = TG_API_ID;
	snprintf (g_c.apiHash, sizeof g_c.apiHash, "%s", TG_API_HASH);
	if (app_ini_load ("config.ini") < 0) return;
	g_c.apiId = app_ini_get_int ("telegram", "api_id", TG_API_ID);
	snprintf (g_c.apiHash, sizeof g_c.apiHash, "%s", app_ini_get ("telegram", "api_hash", TG_API_HASH));
	if (!g_c.apiId || !g_c.apiHash[0]) { g_c.apiId = TG_API_ID; snprintf (g_c.apiHash, sizeof g_c.apiHash, "%s", TG_API_HASH); }
	g_c.test = app_ini_get_int ("telegram", "test", 0) != 0;
	g_pictures = app_ini_get_int ("telegram", "pictures", 1) != 0;
	g_windowed = app_ini_get_int ("telegram", "windows", 1) != 0;
}
static void save_api (int id, const char *hash)
{
	g_c.setApi (id, hash, g_c.test);
	save_config ();
}

// ---- the layout -----------------------------------------------------------------------------------------

static void place (Widget *w, int x, int y, int ww, int hh) { w->left = x; w->top = y; w->resizeTo (ww < 1 ? 1 : ww, hh < 1 ? 1 : hh); w->invalidate (true); }

// A conversation's pane (buddylist.h, TgPane) placed at x in a window w x h (its height from the top): the
// header, the bar (a person who is not a contact), the messages, the line to write in, the pictures' column.
static void pane_layout (TgPane *p, int x, int w, int h, bool conv)
{
	TgPane *was = g_pane;
	tg_use (p);
	bool dp = conv && g_pictures && w >= 640;
	int cw = dp ? w - DP_W : w;
	g_head->hidden = g_chat->hidden = g_input->hidden = !conv;
	g_dp->hidden = !dp;
	bool bar = conv && g_c.showBar (g_c.conv (g_open));
	int bh = bar ? BAR_H : 0;
	g_bar->hidden = !bar;
	place (g_head, x, 0, w, HEAD_H);
	place (g_bar, x, HEAD_H, cw, BAR_H);
	place (g_chat, x, HEAD_H + bh, cw, h - HEAD_H - INPUT_H - bh);
	place (g_input, x, h - INPUT_H, cw, INPUT_H);
	place (g_dp, x + cw, HEAD_H, DP_W, h - HEAD_H);
	g_pane = was;
}

static void relayout ()
{
	Root &r = *g_root;
	int w = r.width, h = r.height - SB_H;
	bool ready = g_c.state == tg::AS_READY;
	bool list_only = ready && g_windowed;		// (the conversations in their own windows)
	g_signin->hidden = ready;
	place (g_signin, 0, 0, w, r.height);
	g_list->hidden = !ready;
	place (g_list, 0, 0, list_only ? w : LIST_W, h);
	bool conv = ready && !list_only && g_mainPane.open;
	pane_layout (&g_mainPane, LIST_W, w - LIST_W, h, conv);
	g_welcome->hidden = !ready || conv || list_only;
	place (g_welcome, LIST_W, 0, w - LIST_W, h);
	g_status->hidden = !ready;
	place (g_status, 0, h, w, SB_H);
	r.invalidate (true);
}

// The main window made w x h (its client area): the frame dragged, as the frame does it (uikit's Root).
static void main_size (int cw, int ch)
{
	Root &r = *g_root;
	if (r.maximised ()) r.maximise (false);
	if (cw == r.width && ch == r.height) return;
	r.winSelect ();
	int stride = cw;
	unsigned *fb = uk_win_resize2 (cw, ch, &stride);
	if (!fb) return;
	r.canvas.adopt (fb, cw, ch, stride);
	r.width = cw; r.height = ch;
	r.layout ();
	uk_decorate_window ();
	relayout ();
	r.fitWorkArea ();
}

// The main window as the mode wants it: the list alone (its conversations in their own windows), or the
// list and the conversation beside it.
static void apply_mode ()
{
	Root &r = *g_root;
	bool list_only = g_windowed && g_c.state == tg::AS_READY;
	if (list_only && r.width > LIST_WIN_W + 40) { r.setMinSize (300, 400); main_size (LIST_WIN_W, r.height); }
	else if (!list_only && r.width < 720) { main_size (W0, r.height < H0 ? H0 : r.height); r.setMinSize (720, 500); }
	relayout ();
}

// ---- the actions ----------------------------------------------------------------------------------------

static bool open_window (long long peer);
static void close_windows ();

static void open_conversation (long long peer)
{
	if (!peer) return;
	if (g_windowed && g_c.state == tg::AS_READY && open_window (peer)) return;
	tg_use (&g_mainPane);
	g_open = peer;
	if (g_demo) { tg::Conv *c = g_c.conv (peer); if (c) { c->unread = 0; c->rev++; } g_c.rev++; }
	else g_c.open (peer);
	g_picker->hide ();
	relayout ();
	g_chat->reset ();
	g_list->refresh ();
	g_list->scrollToOpen ();
	g_input->edit->setFocus ();
}

// ---- the pictures sent ------------------------------------------------------------------------------------

#define PIC_MAX		1280		// the longest side sent (Telegram's photos go to 2560; this keeps them light)
#define PIC_QUALITY	87

// Pixels (0x00RRGGBB / 0xAARRGGBB) made the picture to send: brought down to PIC_MAX, JPEG; its view for
// the strip. False: no memory.
static bool attach_pixels (const unsigned *px, int w, int h, const char *name)
{
	if (!px || w <= 0 || h <= 0) return false;
	int dw = w, dh = h;
	if (dw > PIC_MAX || dh > PIC_MAX)
	{
		if (dw >= dh) { dh = (int) ((long) h * PIC_MAX / w); dw = PIC_MAX; }
		else { dw = (int) ((long) w * PIC_MAX / h); dh = PIC_MAX; }
		if (dw < 1) dw = 1;
		if (dh < 1) dh = 1;
	}
	unsigned *small = (dw != w || dh != h) ? scale_box (px, w, h, dw, dh) : 0;
	const unsigned *src = small ? small : px;
	// (the clear pixels laid on white: JPEG has no transparency)
	unsigned *opaque = new unsigned[(size_t) dw * dh];
	for (int i = 0; i < dw * dh; i++)
	{
		unsigned c = src[i], a = c >> 24;
		opaque[i] = a == 0 && (c & 0xFFFFFF) == 0 ? 0xFF000000u : a == 255 || a == 0 ? (c | 0xFF000000u) : (0xFF000000u | uk_mix (0xFFFFFF, c & 0xFFFFFF, (int) a));
	}
	unsigned n = 0;
	unsigned char *jpg = pngsave::jpeg_encode (opaque, dw, dh, PIC_QUALITY, &n);
	delete [] opaque;
	if (!jpg || !n) { delete [] small; return false; }
	attach_clear ();
	g_att.jpg = jpg; g_att.n = (int) n; g_att.w = dw; g_att.h = dh;
	g_att.th = 20; g_att.tw = dw * 20 / dh; if (g_att.tw < 8) g_att.tw = 8; if (g_att.tw > 40) g_att.tw = 40;
	g_att.thumb = scale_box (src, dw, dh, g_att.tw, g_att.th);
	snprintf (g_att.name, sizeof g_att.name, "%s", name);
	delete [] small;
	g_input->invalidate (true);
	g_input->edit->setFocus ();
	return true;
}
static bool picture_name (const char *p) { return img_is_image_name (p); }
static bool attach_file (const char *path)
{
	ImgFrames im;
	if (!img_load (path, &im)) { notify ("Telegram", TR ("This file is not a picture Onyx can read.")); return false; }
	const char *base = path;
	for (const char *q = path; *q; q++) if (*q == '/' || *q == ':') base = q + 1;
	bool ok = attach_pixels (im.px[0], im.w, im.h, base);
	img_free (&im);
	return ok;
}
// Ctrl+V: a picture copied (Paint, Screenshot), or a picture file copied (the File Viewer) -> attached.
static bool paste_picture ()
{
	if (!g_open) return false;
	int w = 0, h = 0;
	unsigned *px = clip_get_image (&w, &h);
	if (px)
	{
		bool ok = attach_pixels (px, w, h, TR ("the clipboard's picture"));
		delete [] px;
		return ok;
	}
	char path[300]; int cut = 0;
	if (clip_get_file (path, sizeof path, &cut) && picture_name (path)) return attach_file (path);
	return false;
}
static void choose_picture ()
{
	if (!g_open) return;
	static char dir[260] = "SD:/Pictures";
	char path[300] = "";
	if (!ft_file_open (path, sizeof path, dir, TR ("Pictures|*.jpg;*.jpeg;*.png;*.gif;*.bmp;*.webp;*.pcx|All files|*"))) return;
	snprintf (dir, sizeof dir, "%s", path);
	for (int i = (int) strlen (dir) - 1; i > 0; i--) if (dir[i] == '/') { dir[i] = 0; break; }
	attach_file (path);
}
static void open_picture (const char *path)
{
	if (!lx_launch ("imageview", path)) notify ("Telegram", TR ("The Image Viewer is not installed."));
}

static void send_current ()
{
	ChatEdit *e = g_input->edit;
	if (!g_open || (!e->len && !g_att.jpg)) return;
	// (the spaces and line breaks around the text dropped)
	int a = 0, b = e->len;
	while (a < b && (e->buf[a] == ' ' || e->buf[a] == '\n')) a++;
	while (b > a && (e->buf[b - 1] == ' ' || e->buf[b - 1] == '\n')) b--;
	if (g_att.jpg)					// a picture, the text its caption
	{
		char *cap = tg::sdup (e->buf + a, b - a);
		g_c.sendPhoto (g_open, g_att.jpg, g_att.n, g_att.w, g_att.h, cap);
		if (g_demo)
		{
			tg::Conv *c = g_c.conv (g_open);
			if (c && c->n) { c->m[c->n - 1].progress = 60; c->rev++; }
		}
		free (cap);
		attach_clear ();
		e->clear ();
		g_input->invalidate (true);
		return;
	}
	if (a == b) return;
	char *t = tg::sdup (e->buf + a, b - a);
	if (g_demo)
	{
		static int id = 1000;
		demo_msg (g_open, id++, tg::pkey (tg::P_USER, g_c.selfId), true, g_c.serverTime (), t);
		tg::Conv *c = g_c.conv (g_open); c->typingUntil = 0; c->rev++; g_c.rev++; g_c.dialogs ();
	}
	else g_c.send (g_open, t);
	free (t);
	e->clear ();
}

static void typed_something ()
{
	tgc::entropy_add (0, 0);			// (the keys' timing: a little more randomness)
	if (g_open && !g_demo) g_c.typing (g_open);
}

static void open_link (const char *url)
{
	char u[600];
	if (!strncmp (url, "www.", 4) || !strncmp (url, "t.me/", 5)) snprintf (u, sizeof u, "https://%s", url);
	else snprintf (u, sizeof u, "%s", url);
	if (!lx_launch ("jet", u)) notify ("Telegram", TR ("Jet Browser is not installed: install it from the Package Manager."));
}

// ---- the status's little menu (a click on "(Online)") ---------------------------------------------------------

class PopMenu : public Widget
{
public:
	enum { ROW = 26 };
	PopMenu () : Widget (0, 0, 200, 3 * ROW + 12), m_hot (-1), m_down (true) { hidden = true; catchOutside = true; transparent = true; }
	void show (int x, int y) { left = x; top = y; hidden = false; m_down = true; m_hot = -1; bringToFront (); invalidate (true); }
	void hide () { hidden = true; if (parent) parent->invalidate (true); }
	void onDraw () override
	{
		Canvas &cv = canvas;
		cv.clear (UK_TRANSPARENT_KEY);
		uk_rbox (cv, 0, 0, width, height, 5, 0xFFFFFF, 0xF1F6FB);
		uk_rline (cv, 0, 0, width, height, 5, TC_SEL_RIM);
		const char *items[3] = { TR ("Online"), TR ("Appear offline"), TR ("Sign out") };
		for (int i = 0; i < 3; i++)
		{
			int y = 6 + i * ROW;
			if (i == 2) cv.fillRect (8, y - 1, width - 16, 1, TC_LINE);
			if (i == m_hot) { uk_rbox (cv, 4, y, width - 8, ROW - 2, 3, TC_SEL_TOP, TC_SEL_BOT); uk_rline (cv, 4, y, width - 8, ROW - 2, 3, TC_SEL_RIM); }
			if (i < 2) buddy (cv, 10, y + 4, 16, i == 0 ? TC_ONLINE : TC_OFFLINE);
			bool cur = i < 2 && ((i == 0) == g_c.wantOnline ());
			ftext (cv, g_face.ui, 34, y + (ROW - 2 - g_face.ui->height ()) / 2, items[i], TC_INK, cur ? 2 : 0);
		}
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) br; (void) bm; (void) wheel;
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		int i = in ? (my - 6) / ROW : -1;
		if (i > 2) i = -1;
		if (i != m_hot) { m_hot = i; invalidate (true); }
		if (bl && !m_down)
		{
			hide ();
			if (i == 0) g_c.setOnline (true);
			else if (i == 1) g_c.setOnline (false);
			else if (i == 2) sign_out ();
			g_list->invalidate (true);
		}
		m_down = bl != 0;
		return in || bl;
	}
	static void sign_out ()
	{
		if (g_demo) return;
		if (ft_messagebox (TR ("Sign out"), TR ("Sign out of Telegram on this Onyx? Your conversations stay on Telegram."), MB_YESNO) != 1) return;
		g_c.logOut ();
		close_windows ();
		g_mainPane.open = 0;
		relayout ();
	}
private:
	int m_hot;
	bool m_down;
};
static PopMenu *g_pop;
static void status_menu (int x, int y) { g_pop->show (x, y); }

// ---- adding a contact by phone number ----------------------------------------------------------------------

class AddContact : public Modal
{
public:
	Textbox *phone, *first, *last;
	AddContact () : Modal (380, 250)
	{
		Root *r = Root::current ();
		left = ((r ? r->width : 940) - width) / 2; top = ((r ? r->height : 640) - height) / 2;
		int y = titleH () + 34;
		phone = new Textbox (130, y, 230, 26, ""); phone->maxLen = 30; addChild (phone);
		first = new Textbox (130, y + 38, 230, 26, ""); first->maxLen = 60; addChild (first);
		last = new Textbox (130, y + 76, 230, 26, ""); last->maxLen = 60; addChild (last);
		phone->cb = first->cb = last->cb = [] (Widget &w) { ((AddContact *) w.parent)->ok (); };
		Button *b = new Button (width - 196, height - 40, 90, 28, TR ("Add"), [] (Widget &w) { ((AddContact *) w.parent)->ok (); });
		addChild (b);
		b = new Button (width - 98, height - 40, 88, 28, TR ("Cancel"), [] (Widget &w) { ((Modal *) w.parent)->close (0); });
		addChild (b);
	}
	void ok ()
	{
		int digits = 0;
		for (const char *p = phone->text; *p; p++) if (*p >= '0' && *p <= '9') digits++;
		if (digits < 6) { m_err = true; invalidate (true); phone->setFocus (); return; }
		close (1);
	}
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == KEY_TAB)				// (the next field)
		{
			Textbox *f[3] = { phone, first, last };
			int i = phone->hasFocus ? 0 : first->hasFocus ? 1 : last->hasFocus ? 2 : -1;
			bool back = (kapi_get_modifiers () & MOD_SHIFT) != 0;
			f[((i < 0 ? 0 : i) + (back ? 2 : 1)) % 3]->setFocus ();
			invalidate (true);
			return true;
		}
		return false;
	}
	void onDraw () override
	{
		drawBox (TR ("Add a Contact"));
		int y = titleH () + 10;
		ftext (canvas, g_face.small, 16, y, m_err ? TR ("The phone number, with its country code: +33 6 12 34 56 78") : TR ("Their phone number, with its country code."), m_err ? TC_BUSY : TC_GREY, m_err ? 2 : 0, width - 32);
		const char *l[3] = { TR ("Phone number"), TR ("First name"), TR ("Last name") };
		for (int i = 0; i < 3; i++) ftext (canvas, g_face.ui, 16, titleH () + 39 + i * 38, l[i], TC_INK);
	}
private:
	bool m_err = false;
};

static void add_contact_dialog ()
{
	if (g_c.state != tg::AS_READY) return;
	AddContact *d = new AddContact ();
	int r;
	{ UkFaceScope f (ft_dialog_face ()); r = d->run (); }
	if (r == 1)
	{
		if (g_demo)				// (the demo: the person made up)
		{
			static long long id = 2000;
			demo_user (id, d->first->text[0] ? d->first->text : d->phone->text, d->last->text, "", tg::ST_RECENTLY, g_c.serverTime (), true);
			long long peer = tg::pkey (tg::P_USER, id++);
			tg::Conv *c = g_c.convs.add (peer); c->peer = peer; c->inList = true; c->loaded = c->complete = true; c->topDate = g_c.serverTime ();
			g_c.added = peer; g_c.rev++; g_c.dialogs ();
		}
		else g_c.addContact (d->phone->text, d->first->text, d->last->text);
	}
	delete d;
}
static void m_add_contact () { add_contact_dialog (); }

// ---- the welcome (no conversation open) and the status bar ------------------------------------------------------

class Welcome : public Widget
{
public:
	Welcome (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		Canvas &cv = canvas;
		fill_grad (cv, 0, 0, width, height, 0xFFFFFF, 0xE8F2FB);
		VPath p; p.ellipse (V (width / 2), V (height + height / 4), V (width), V (height / 2)); p.fill (cv, TC_SKY_TOP, 120);
		int s = 120, cx = width / 2, y = height / 2 - 120;
		avatar_default (cv, cx - s / 2, y, s, TC_ONLINE);
		y += s + 26;
		const char *t = TR ("Pick a conversation");
		ftext (cv, g_face.title, cx - ftw (g_face.title, t) / 2, y, t, TC_TITLE);
		y += 30;
		const char *u = TR ("Choose someone in your list to write to them. Their new messages come in at once.");
		Rich r; rich_layout (r, g_face.ui, u, 360, 16, false);
		rich_draw (cv, r, g_face.ui, u, cx - r.widest / 2, y, TC_GREY);
	}
};

class StatusBar : public Widget
{
public:
	StatusBar (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		Canvas &cv = canvas;
		fill_grad (cv, 0, 0, width, height, 0xF0F5FA, 0xDFE9F3);
		cv.fillRect (0, 0, width, 1, TC_LINE);
		bool on = g_c.online;
		VPath p; p.circle (V (12), V (height / 2), V (4)); p.fill (cv, on ? TC_ONLINE : TC_AWAY);
		ftext (cv, g_face.small, 22, (height - g_face.small->height ()) / 2, g_demo ? TR ("Demonstration: nothing is sent") : on ? TR ("Connected to Telegram") : TR ("Connecting to Telegram..."), TC_GREY);
		const char *r = "Telegram for Onyx";
		if (width >= 520) ftext (cv, g_face.small, width - ftw (g_face.small, r) - 10, (height - g_face.small->height ()) / 2, r, TC_LIGHT, 1);
	}
};

// ---- the menus ------------------------------------------------------------------------------------------

static Menu g_menu;
static void m_online () { g_c.setOnline (true); g_list->invalidate (true); }
static void m_offline () { g_c.setOnline (false); g_list->invalidate (true); }
static void m_signout () { PopMenu::sign_out (); }
static void m_quit () { kapi_exit (0); }
static void relayout_windows ();
static void m_pictures () { g_pictures = !g_pictures; save_config (); relayout (); relayout_windows (); extern void build_menu (); build_menu (); }
static void close_windows ();
static void m_windows ()
{
	g_windowed = !g_windowed;
	save_config ();
	if (!g_windowed) close_windows ();
	apply_mode ();
	extern void build_menu (); build_menu ();
}
static void m_search () { if (g_c.state == tg::AS_READY) g_list->search->setFocus (); }
static void m_about ()
{
	ft_messagebox (TR ("About Telegram for Onyx"),
		       TR ("Telegram for Onyx 1.0 - an unofficial Telegram client in the way of the old Messenger, written for Onyx on Telegram's open API (MTProto 2.0). MIT licence."), MB_OK);
}
void build_menu ()
{
	g_menu = Menu ();
	g_menu.menu ("Telegram");
	g_menu.item (TR ("Search"), "^F", UK_CTRL ('F'), m_search);
	g_menu.item (TR ("Add a Contact..."), "^N", UK_CTRL ('N'), m_add_contact);
	g_menu.separator ();
	g_menu.item (TR ("Sign Out"), "", 0, m_signout);
	g_menu.item (TR ("Quit"), "^Q", UK_CTRL ('Q'), m_quit);
	g_menu.menu (TR ("Status"));
	g_menu.item (TR ("Online"), "", 0, m_online);
	g_menu.item (TR ("Appear Offline"), "", 0, m_offline);
	g_menu.menu (TR ("View"));
	g_menu.item (g_pictures ? TR ("Hide the Display Pictures") : TR ("Show the Display Pictures"), "", 0, m_pictures);
	g_menu.item (g_windowed ? TR ("Conversations Beside the List") : TR ("Conversations in Their Own Windows"), "", 0, m_windows);
	g_menu.menu (TR ("Help"));
	g_menu.item (TR ("About Telegram for Onyx"), "", 0, m_about);
	g_menu.publish ();
}

// ---- a conversation in its own window ------------------------------------------------------------------------
// (Onyx's windows of a program, uikit's Root (NewWindow): docs/MULTI-WINDOW-STUDY.md.) The main window keeps
// the list; a conversation opened there comes in a window of its own -- one for each, brought forward when
// it is opened again. Its close box closes it (Telegram goes on); closing the main window ends Telegram.

class ConvWindow : public Root
{
public:
	TgPane pane;
	bool gone = false;				// closed: deleted by the main window's next tick
	ConvWindow (long long peer, const char *title) : Root (NewWindow (), -1, -1, CW_W, CW_H, title)
	{
		memset (&pane, 0, sizeof pane);
		pane.open = peer; pane.root = this;
		if (!winOpened ()) return;
		TgPane *was = g_pane;
		g_pane = &pane;					// (the widgets made now are this pane's)
		setBg (TC_BG);
		int h = CW_H;
		g_head = new ConvHeader (0, 0, CW_W, HEAD_H);
		addChild (g_head);
		g_chat = new ChatView (0, HEAD_H, CW_W - DP_W, h - HEAD_H - INPUT_H);
		addChild (g_chat);
		g_bar = new PeerBar (0, HEAD_H, CW_W - DP_W, BAR_H);
		g_bar->hidden = true;
		addChild (g_bar);
		g_input = new InputBar (0, h - INPUT_H, CW_W - DP_W, INPUT_H);
		addChild (g_input);
		g_dp = new DpColumn (CW_W - DP_W, HEAD_H, DP_W, h - HEAD_H, INPUT_H);
		addChild (g_dp);
		g_picker = new EmoPicker (g_input->edit);
		addChild (g_picker);
		setResizable (true);
		setMinSize (420, 360);
		if (g_demo) { tg::Conv *c = g_c.conv (peer); if (c) { c->unread = 0; c->rev++; } g_c.rev++; }
		else g_c.open (peer);
		place_me ();
		g_chat->reset ();
		g_input->edit->setFocus ();
		g_pane = was;
	}
	void place_me () { pane_layout (&pane, 0, width, height, true); invalidate (true); }
	void onResized () override { place_me (); }
	void onTick () override
	{
		tg_use (&pane);
		if (g_c.state != tg::AS_READY || gone) return;
		g_chat->tick ();
		if (g_c.rev != m_rev)
		{
			m_rev = g_c.rev;
			g_head->invalidate (true); g_dp->invalidate (true); g_bar->invalidate (true);
			bool bar = g_c.showBar (g_c.conv (g_open));
			if (bar == g_bar->hidden) place_me ();		// (the bar came or went)
		}
		g_input->edit->tickBlink ();
	}
	void onClose () override
	{
		tg_use (&pane);
		attach_clear ();
		closeWindow ();
		gone = true;
		tg_use (&g_mainPane);
		g_list->invalidate (true);
	}
	bool onKey (long k) override
	{
		tg_use (&pane);
		if (k == 27 && !g_picker->hidden) { g_picker->hide (); return true; }
		if (k == KEY_PGUP) { g_chat->pageUp (); return true; }
		if (k == KEY_PGDN) { g_chat->pageDown (); return true; }
		return false;
	}
	void onDrop (int x, int y, int type, const char *data, int len, unsigned flags) override;
private:
	unsigned m_rev = 0;
};

static ConvWindow *g_wins[KAPI_WS_WINDOWS_MORE];

// A conversation in its window: the one it has brought forward, else a new one -> false: no window could be
// made (the conversation is then shown beside the list).
static bool open_window (long long peer)
{
	for (int i = 0; i < KAPI_WS_WINDOWS_MORE; i++)
	{
		ConvWindow *w = g_wins[i];
		if (!w || w->gone || w->pane.open != peer) continue;
		int was = uk_win_select (-1);
		w->winSelect ();
		uk_win_raise (0);
		uk_win_select (was);
		return true;
	}
	int slot = -1;
	for (int i = 0; i < KAPI_WS_WINDOWS_MORE && slot < 0; i++) if (!g_wins[i]) slot = i;
	if (slot < 0) { notify ("Telegram", TR ("Too many conversations are open: close one of their windows.")); return true; }
	char name[160];
	g_c.peerName (peer, name, sizeof name);
	ConvWindow *w = new ConvWindow (peer, name);
	if (!w->winOpened ())				// (no window: the conversations beside the list again)
	{
		delete w;
		g_windowed = false;
		apply_mode ();
		extern void build_menu (); build_menu ();
		return false;
	}
	g_wins[slot] = w;
	g_list->invalidate (true);
	return true;
}

// Is this conversation on the screen? (its window, or beside the list)
static bool peer_shown (long long peer)
{
	if (!g_windowed && peer == g_mainPane.open) return true;
	for (int i = 0; i < KAPI_WS_WINDOWS_MORE; i++)
		if (g_wins[i] && !g_wins[i]->gone && g_wins[i]->pane.open == peer) return true;
	return false;
}

static void reap_windows ()
{
	for (int i = 0; i < KAPI_WS_WINDOWS_MORE; i++)
		if (g_wins[i] && g_wins[i]->gone) { delete g_wins[i]; g_wins[i] = 0; }
	tg_use (&g_mainPane);
}

static void close_windows ()
{
	for (int i = 0; i < KAPI_WS_WINDOWS_MORE; i++)
		if (g_wins[i] && !g_wins[i]->gone) g_wins[i]->onClose ();
	tg_use (&g_mainPane);
}

static void relayout_windows ()
{
	for (int i = 0; i < KAPI_WS_WINDOWS_MORE; i++)
		if (g_wins[i] && !g_wins[i]->gone) g_wins[i]->place_me ();
}

// ---- the icon of the menu bar's status area ---------------------------------------------------------------
// (kapi v95, UIKit's uk_tray) Telegram's icon there, the unread messages in its tip; a double click shows the
// main window again, even minimised (Elegant does it).
static void tray_update (bool force)
{
	static int s_unread = -1;
	int n = 0;
	if (g_c.state == tg::AS_READY)
		for (int i = 0; i < g_c.convs.n; i++) if (g_c.convs.a[i]->inList) n += g_c.convs.a[i]->unread;
	if (n == s_unread && !force) return;
	s_unread = n;
	char tip[56];
	if (n == 0) snprintf (tip, sizeof tip, "Telegram");
	else if (n == 1) snprintf (tip, sizeof tip, "%s", TR ("Telegram - 1 unread message"));
	else snprintf (tip, sizeof tip, TR ("Telegram - %d unread messages"), n);
	uk_tray (TG_DIR "icon.bmp", tip);
}

// ---- the window -----------------------------------------------------------------------------------------

class TgRoot : public Root
{
public:
	TgRoot (int w, int h) : Root (w, h, "Telegram"), m_state (-1), m_title (0) {}
	void onResized () override { relayout (); }
	void onTick () override
	{
		tg_use (&g_mainPane);
		g_c.tick ();
		reap_windows ();
		if ((int) g_c.state != m_state)
		{
			bool was = m_state == tg::AS_READY;
			m_state = (int) g_c.state;
			if (m_state == tg::AS_READY && !was && g_signin && g_signin->offline ()) g_c.setOnline (false);
			if (m_state != tg::AS_READY) { g_open = 0; close_windows (); }
			apply_mode ();
			g_list->refresh ();
		}
		if (g_c.state == tg::AS_READY)
		{
			g_list->tick ();
			g_chat->tick ();
			if (g_c.rev != m_rev)
			{
				m_rev = g_c.rev;
				tray_update (false);
				g_head->invalidate (true); g_dp->invalidate (true); g_status->invalidate (true); g_bar->invalidate (true);
				bool bar = g_open && g_c.showBar (g_c.conv (g_open));
				if (bar == g_bar->hidden) relayout ();		// (the bar came or went)
			}
			if (g_c.barError)
			{
				g_c.barError = false;
				char e[200]; friendly_error (g_c.error, g_c.floodWait, e, sizeof e);
				ft_messagebox ("Telegram", e, MB_OK);
			}
			g_input->edit->tickBlink ();
			notifications ();
			if (g_c.added)					// (a contact added: their conversation; or why not)
			{
				long long a = g_c.added;
				g_c.added = 0;
				if (a > 0) { g_list->refresh (); open_conversation (a); }
				else if (a == -1) ft_messagebox (TR ("Add a Contact"), TR ("This phone number has no Telegram account (or its owner does not let others find them by their number)."), MB_OK);
				else { char t[300], e[200]; friendly_error (g_c.error, g_c.floodWait, e, sizeof e); snprintf (t, sizeof t, TR ("The contact could not be added. %s"), e); ft_messagebox (TR ("Add a Contact"), t, MB_OK); }
			}
		}
		else g_signin->tick ();
#ifndef TG_NO_LOG
		tg_log_flush (false);
#endif
	}
	// pictures dropped from the File Viewer: the first one attached
	void onDrop (int x, int y, int type, const char *data, int len, unsigned flags) override
	{
		(void) x; (void) y; (void) len; (void) flags;
		tg_use (&g_mainPane);
		drop_pictures (type, data);
	}
	static void drop_pictures (int type, const char *data)
	{
		if (type != DND_FILES || !g_open || g_c.state != tg::AS_READY) return;
		char path[300];
		for (const char *p = data; *p; )
		{
			int n = 0;
			while (p[n] && p[n] != '\n') n++;
			if (n > 0 && n < (int) sizeof path)
			{
				memcpy (path, p, (size_t) n); path[n] = 0;
				if (picture_name (path)) { attach_file (path); return; }
			}
			p += n; if (*p) p++;
		}
	}
	bool onKey (long k) override
	{
		tg_use (&g_mainPane);
		return keys (k);
	}
	static bool keys (long k)
	{
		if (k == 27 && !g_picker->hidden) { g_picker->hide (); return true; }
		if (g_c.state == tg::AS_READY)
		{
			unsigned mods = kapi_get_modifiers ();
			if (k == KEY_PGUP) { g_chat->pageUp (); return true; }
			if (k == KEY_PGDN) { g_chat->pageDown (); return true; }
			if ((mods & MOD_CTRL) && (k == KEY_UP || k == KEY_DOWN)) return g_list->moveSel (k == KEY_UP ? -1 : 1);
		}
		return false;
	}
private:
	int m_state;
	unsigned m_rev = 0;
	int m_title;

	// a new message elsewhere than the conversation looked at: a notification
	void notifications ()
	{
		while (g_c.ninbox > 0)
		{
			tg::Client::Incoming in = g_c.inbox[0];
			memmove (g_c.inbox, g_c.inbox + 1, sizeof g_c.inbox[0] * (size_t) (g_c.ninbox - 1));
			g_c.ninbox--;
			if (g_demo) continue;
			if (peer_shown (in.peer)) { tg::Conv *c = g_c.conv (in.peer); if (c) g_c.markRead (c); continue; }
			tg::Conv *c = g_c.conv (in.peer);
			if (!c) continue;
			char name[160], pv[200], title[200];
			g_c.peerName (in.peer, name, sizeof name);
			preview (c, pv, sizeof pv);
			snprintf (title, sizeof title, TR ("%s says:"), name);
			notify_action (title, pv, "telegram");
		}
	}
};

void ConvWindow::onDrop (int x, int y, int type, const char *data, int len, unsigned flags)
{
	(void) x; (void) y; (void) len; (void) flags;
	tg_use (&pane);
	TgRoot::drop_pictures (type, data);
}

int main ()
{
	char args[200] = "";
	kapi_get_args (args, sizeof args);
	g_demo = strstr (args, "--demo") != 0;
	// One Telegram at a time (a notification's click starts "telegram"): the one running comes forward.
	if (!g_demo)
	{
		if (kapi_ipc_lookup ("telegram") > 0) { uk_win_app_raise ("telegram"); return 0; }
		kapi_ipc_register ("telegram");
	}
	ft_uikit_install ("DejaVu Sans", 13);
	uk_lang_init ();
	faces_init ();
	tl::load ();

	TgRoot root (W0, H0);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	g_mainPane.root = &root;
	root.setBg (TC_BG);
	root.setResizable (true);
	root.setMinSize (720, 500);

	snprintf (g_c.dir, sizeof g_c.dir, "%s", TG_DIR);
	kapi_mkdir (TG_DIR "cache");			// (the profile photos)
	g_c.lang = uk_lang ();
	load_config ();
	int h = H0 - SB_H;
	g_list = new BuddyList (0, 0, LIST_W, h);
	root.addChild (g_list);
	g_welcome = new Welcome (LIST_W, 0, W0 - LIST_W, h);
	root.addChild (g_welcome);
	g_head = new ConvHeader (LIST_W, 0, W0 - LIST_W, HEAD_H);
	root.addChild (g_head);
	g_chat = new ChatView (LIST_W, HEAD_H, W0 - LIST_W - DP_W, h - HEAD_H - INPUT_H);
	root.addChild (g_chat);
	g_bar = new PeerBar (LIST_W, HEAD_H, W0 - LIST_W - DP_W, BAR_H);
	g_bar->hidden = true;
	root.addChild (g_bar);
	g_input = new InputBar (LIST_W, h - INPUT_H, W0 - LIST_W - DP_W, INPUT_H);
	root.addChild (g_input);
	g_dp = new DpColumn (W0 - DP_W, HEAD_H, DP_W, h - HEAD_H, INPUT_H);
	root.addChild (g_dp);
	g_status = new StatusBar (0, h, W0, SB_H);
	root.addChild (g_status);
	g_signin = new SignIn (0, 0, W0, H0);
	root.addChild (g_signin);
	g_picker = new EmoPicker (g_input->edit);
	root.addChild (g_picker);
	g_pop = new PopMenu ();
	root.addChild (g_pop);
	build_menu ();
	tray_update (true);

	if (g_demo)
	{
		g_c.demo = true;
		demo_fill ();
		g_list->refresh ();
		open_conversation (tg::pkey (tg::P_USER, 1001));
	}
	else g_c.begin ();
	relayout ();
	root.fitWorkArea ();
	apply_mode ();
	root.run ();
	g_c.end ();
	tg_log_flush (true);
	return 0;
}
