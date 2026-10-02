//
// Apps/mail/app.h -- what Mail's window parts share: the model, the parts themselves, the colours and sizes, the
// helpers the parts call one another through (main.cpp defines them).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _mail_app_h
#define _mail_app_h

#include "ft/fonts.h"
#include "Apps/mail/ui.h"
#include "Apps/mail/model.h"
#include "mail/html_ft.h"
#include "img/imgload.hpp"
#include "notify.h"
#include "clipboard.h"
#include "fileassoc.h"

namespace mailapp {

static const int TB_H = 50, SIDE_W = 214, LIST_W = 316;

static Model g_m;
static Root *g_root;
class ToolBar; class Sidebar; class ListPane; class ReadPane; class ComposePane; class ContactsPane; class Welcome;
static ToolBar *g_tb; static Sidebar *g_side; static ListPane *g_list; static ReadPane *g_read; static ComposePane *g_compose; static ContactsPane *g_contacts; static Welcome *g_welcome;
static int g_conv = -1;				// the conversation shown (view.convs index), -1 none
static bool g_showContacts;
static unsigned g_tick;

// the colours (from the theme: Mail follows the desktop's)
static unsigned col_side () { return wk_mix (C_BG, C_FIELD, 80); }
static unsigned col_list () { return C_FIELD; }
static unsigned col_dim () { return wk_mix (C_FIELD, C_FIELD_TEXT, 140); }
static unsigned col_faint () { return wk_mix (C_FIELD, C_FIELD_TEXT, 90); }
static unsigned col_line () { return wk_mix (C_FIELD, C_FIELD_TEXT, 30); }
static unsigned col_sel () { return wk_mix (C_FIELD, C_ACCENT, 70); }
static unsigned col_hover () { return wk_mix (C_FIELD, C_FIELD_TEXT, 14); }

// main.cpp
static void refresh_all ();
static void layout_parts ();
static void select_view (int kind, int acct = 0, int folder = 0);
static void open_conv (int k);
static void compose_new (int mode, const Ref *about = 0);	// mode: 0 new, 1 reply, 2 reply all, 3 forward, 4 a draft again
static void compose_to (const char *name, const char *email);
static void act_archive (); static void act_delete (); static void act_junk (); static void act_star (); static void act_unread ();
static void act_check ();
static void open_wizard ();
static void open_settings ();
static void show_contacts (bool on);
static void status_note (const char *s);
static void on_result (void *ctx, long);

// the conversation's messages shown (refs into the model)
static int conv_refs (Ref *out, int max)
{
	if (g_conv < 0 || g_conv >= g_m.view.convs.n) return 0;
	const Conv &c = g_m.view.convs[g_conv];
	int n = c.n < max ? c.n : max;
	for (int i = 0; i < n; i++) out[i] = g_m.view.refs[c.first + i];
	return n;
}

// "Marie Dubois" of '"Marie Dubois" <m@x>' (the address's local part when no name)
static void who (const char *addrs, char *out, int cap, bool all = false)
{
	Addr a[16]; int n = parse_addrs (addrs ? addrs : "", a, 16);
	out[0] = 0;
	Buf o;
	for (int i = 0; i < n && (all || i == 0); i++) { char s[160]; addr_show (a[i], s, sizeof s); if (o.n) o.add (", "); o.add (s); }
	scpy (out, o.c (), cap);
}
static void first_email (const char *addrs, char *out, int cap) { Addr a[4]; int n = parse_addrs (addrs ? addrs : "", a, 4); scpy (out, n ? a[0].email : "", cap); }

// a date as the list shows it: 11:42 today, "Yesterday", Mon, 12 Sep, 12/09/2025
static long long local_of (long long utc) { return utc + tz_minutes () * 60LL; }
static void day_of (long long utc, int *y, int *m, int *d) { long long l = local_of (utc); long long day = l >= 0 ? l / 86400 : (l - 86399) / 86400; civil (day, y, m, d); }
static long long day_num (long long utc) { long long l = local_of (utc); return l >= 0 ? l / 86400 : (l - 86399) / 86400; }
static void fmt_short (long long t, char *b, int cap)
{
	long long now = now_utc ();
	long long dn = day_num (now), dt = day_num (t);
	long long l = local_of (t); int sec = (int) (l - dt * 86400);
	static const char *const WD[7] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };
	int y, m, d; civil (dt, &y, &m, &d);
	int ny, nm, nd; civil (dn, &ny, &nm, &nd);
	if (dt == dn) snprintf (b, cap, "%02d:%02d", sec / 3600, sec / 60 % 60);
	else if (dt == dn - 1) snprintf (b, cap, "Yesterday");
	else if (dn - dt < 7) snprintf (b, cap, "%s", WD[((dt % 7) + 7) % 7]);
	else if (y == ny) snprintf (b, cap, "%d %s", d, MONTHS3[m - 1]);
	else snprintf (b, cap, "%02d/%02d/%04d", d, m, y);
}
static void fmt_long (long long t, char *b, int cap)
{
	long long dt = day_num (t), dn = day_num (now_utc ());
	long long l = local_of (t); int sec = (int) (l - dt * 86400);
	int y, m, d; civil (dt, &y, &m, &d);
	static const char *const WD[7] = { "Thursday", "Friday", "Saturday", "Sunday", "Monday", "Tuesday", "Wednesday" };
	if (dt == dn) snprintf (b, cap, "Today, %02d:%02d", sec / 3600, sec / 60 % 60);
	else if (dt == dn - 1) snprintf (b, cap, "Yesterday, %02d:%02d", sec / 3600, sec / 60 % 60);
	else snprintf (b, cap, "%s %d %s %d, %02d:%02d", WD[((dt % 7) + 7) % 7], d, MONTHS3[m - 1], y, sec / 3600, sec / 60 % 60);
}
// the list's day groups: 0 today, 1 yesterday, 2 this week, 3 this month, 4 older
static int day_group (long long t)
{
	long long dn = day_num (now_utc ()), dt = day_num (t);
	if (dt >= dn) return 0;
	if (dt == dn - 1) return 1;
	if (dn - dt < 7) return 2;
	int y, m, d, ny, nm, nd; civil (dt, &y, &m, &d); civil (dn, &ny, &nm, &nd);
	if (y == ny && m == nm) return 3;
	return 4;
}
static void fmt_size (long n, char *b, int cap)
{
	if (n < 1024) snprintf (b, cap, "%ld bytes", n);
	else if (n < 1024 * 1024) snprintf (b, cap, "%ld KB", (n + 512) / 1024);
	else snprintf (b, cap, "%ld.%ld MB", n / 1048576, n % 1048576 * 10 / 1048576);
}
static const char *folder_label (const Folder &f) { return f.show[0] ? f.show : f.name; }
static int folder_icon (const Folder &f)
{
	switch (f.special) { case SP_INBOX: return I_INBOX; case SP_SENT: return I_SENT; case SP_DRAFTS: return I_DRAFTS; case SP_TRASH: return I_TRASH;
	case SP_JUNK: return I_JUNK; case SP_ARCHIVE: case SP_ALL: return I_ARCHIVE; case SP_FLAGGED: return I_STAR; default: return I_FOLDER; }
}

} // namespace mailapp

#endif
