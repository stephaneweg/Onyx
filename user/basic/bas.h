//
// basic/bas.h -- Onyx BASIC: a QBasic-style language compiled to bytecode for a small VM.
//
// The core (bascore.cpp, basnum.cpp) is plain portable C++ -- no libc, only new/delete --
// so it builds both into Onyx programs (/bin/basic, the qbasic editor) and on a PC for the
// host tests (tools/tests/run_basic_test.sh). Everything the language does to the outside
// world (screen, keyboard, graphics, wtk controls, files, notifications...) goes through a
// bas::Host the program supplies.
//
//   bas::Program *p = bas::compile (source, &err);     // 0 + err.line / err.msg on error
//   int rc = bas::run (p, host, &err);                   // 0 ok, -1 runtime error (err)
//   bas::destroy (p);
//
#ifndef _bas_h
#define _bas_h

namespace bas {

struct Error { int line; char msg[120]; };	// line: 1-based source line (0 = none)

// ---- the outside world -------------------------------------------------------------------
// Colours: 0..15 = the QBasic palette; RGB(r,g,b) values have bit 24 set (0x1RRGGBB).
struct Host
{
	virtual ~Host () {}
	// Text screen (PRINT / INPUT / CLS / LOCATE / COLOR). out() gets text with '\n'.
	virtual void out (const char *s, int n) = 0;
	virtual int  inputLine (char *buf, int cap) = 0;	// a typed line (no '\n'); -1 = quit
	virtual int  inkey (char *out2) { (void) out2; return 0; }	// 0 none, else 1-2 chars
	virtual void cls (int mode) { (void) mode; }		// -1 / 0 all, 1 graphics viewport, 2 text viewport
	virtual void locate (int row, int col) { (void) row; (void) col; }
	virtual int  column () { return 1; }			// POS(0), 1-based
	virtual int  row () { return 1; }			// CSRLIN
	virtual void color (int fg, int bg) { (void) fg; (void) bg; }
	virtual int  width () { return 80; }			// text columns
	virtual int  screenChar (int row, int col, bool color) { (void) row; (void) col; (void) color; return 32; }	// SCREEN (r, c[, 1])
	virtual void viewPrint (int top, int bottom) { (void) top; (void) bottom; }	// 0, 0 = the whole screen
	// Graphics (the same window as the text). SCREEN mode[, , apage, vpage] (pages: -1 = keep).
	virtual void screen (int mode, int apage, int vpage) { (void) mode; (void) apage; (void) vpage; }
	virtual void screenSize (int *w, int *h) { *w = 640; *h = 400; }
	virtual void setClip (int x1, int y1, int x2, int y2) { (void) x1; (void) y1; (void) x2; (void) y2; }	// x1 < 0: none
	virtual void paint (int x, int y, int c, int border) { (void) x; (void) y; (void) c; (void) border; }
	// Rectangles of pixels (GET / PUT): colours as POINT returns them.
	virtual void readRect (int x, int y, int w, int h, int *out) { for (int i = 0; i < w * h; i++) out[i] = 0; (void) x; (void) y; }
	virtual void writeRect (int x, int y, int w, int h, const int *in) { (void) x; (void) y; (void) w; (void) h; (void) in; }
	virtual void palette (int attr, int rgb) { (void) attr; (void) rgb; }	// rgb 0xRRGGBB; attr < 0: reset all
	virtual void pcopy (int src, int dst) { (void) src; (void) dst; }
	virtual int  keyPending (char *out2) { (void) out2; return 0; }	// the next key (as INKEY$), not taken
	virtual void pset (int x, int y, int c) { (void) x; (void) y; (void) c; }
	virtual int  point (int x, int y) { (void) x; (void) y; return 0; }
	virtual void line (int x1, int y1, int x2, int y2, int c, int box, int style) { (void) x1; (void) y1; (void) x2; (void) y2; (void) c; (void) box; (void) style; }
	virtual void circle (int x, int y, int r, int c, int fill) { (void) x; (void) y; (void) r; (void) c; (void) fill; }
	virtual void drawText (int x, int y, const char *s, int c) { (void) x; (void) y; (void) s; (void) c; }
	virtual int  mouse (int what) { (void) what; return 0; }	// 0 x, 1 y, 2 buttons
	// Time.
	virtual void sleepMs (int ms) { (void) ms; }
	virtual bool poll () { return true; }			// keep the window alive; false = quit
	virtual double timer () { return 0; }			// seconds since midnight
	virtual void date (char *out11) { out11[0] = 0; }	// "mm-dd-yyyy"
	virtual void time (char *out9) { out9[0] = 0; }	// "hh:mm:ss"
	virtual unsigned seed () { return 1; }
	// Sound: voice 0..15 plays freq Hz (0 = stop) with wave 0 square 1 sine 2 triangle
	// 3 saw 4 noise, volume 0..255. 0 ok, -1 no audio / the output is used elsewhere.
	virtual int  note (int voice, double freq, int wave, int volume) { (void) voice; (void) freq; (void) wave; (void) volume; return 0; }
	// GUI (Onyx): WINDOW, controls and their events.
	virtual void window (const char *title, int w, int h) { (void) title; (void) w; (void) h; }
	virtual int  control (int kind, int x, int y, int w, int h, const char *text, int val)
	{ (void) kind; (void) x; (void) y; (void) w; (void) h; (void) text; (void) val; return 0; }
	virtual void setText (int id, const char *s) { (void) id; (void) s; }
	virtual int  getText (int id, char *buf, int cap) { (void) id; (void) cap; buf[0] = 0; return 0; }
	virtual int  getValue (int id) { (void) id; return 0; }
	virtual void setValue (int id, int v) { (void) id; (void) v; }
	virtual int  event (bool wait) { (void) wait; return 0; }	// control id; -1 closed; 0 none
	// System.
	virtual void notify (const char *title, const char *text) { (void) title; (void) text; }
	virtual int  msgbox (const char *title, const char *text, int buttons) { (void) title; (void) text; (void) buttons; return 1; }
	virtual int  clipboard (char *buf, int cap) { (void) cap; buf[0] = 0; return 0; }
	virtual void setClipboard (const char *s) { (void) s; }
	virtual bool fileDialog (bool save, const char *dir, char *out, int cap) { (void) save; (void) dir; (void) cap; out[0] = 0; return false; }
	virtual bool exec (const char *path, const char *args) { (void) path; (void) args; return false; }
	virtual bool launch (const char *app) { (void) app; return false; }
	virtual bool chdir (const char *path) { (void) path; return false; }
	virtual int  shell (const char *cmd) { (void) cmd; return -1; }	// SHELL: run a command (its output on the screen)
	// Files (whole-file: OPEN ... FOR INPUT reads it all, OUTPUT / APPEND write at CLOSE).
	virtual char *load (const char *path, int *len) { (void) path; *len = 0; return 0; }	// new[] buffer
	virtual bool save (const char *path, const char *data, int len) { (void) path; (void) data; (void) len; return false; }
	virtual bool remove (const char *path) { (void) path; return false; }
	virtual bool rename (const char *from, const char *to) { (void) from; (void) to; return false; }
	virtual bool makeDir (const char *path) { (void) path; return false; }
	virtual bool exists (const char *path) { (void) path; return false; }
	virtual int  listDir (const char *pattern, int index, char *out, int cap) { (void) pattern; (void) index; (void) out; (void) cap; return 0; }
	// Program arguments (COMMAND$) and the end of the run.
	virtual const char *command () { return ""; }
	virtual void finished (bool error) { (void) error; }
};

// Control kinds for Host::control.
enum { CTL_BUTTON = 1, CTL_LABEL, CTL_TEXTBOX, CTL_CHECKBOX, CTL_LISTBOX, CTL_DROPDOWN, CTL_PROGRESS, CTL_SLIDER };

struct Program;
Program *compile (const char *src, Error *err);
int      run (Program *p, Host &host, Error *err);
void     destroy (Program *p);

// Number <-> text, QBasic style (also used by the editor / hosts).
int    formatNum (double v, char *out, bool dbl = false);	// "3.5", "-2", "1.234568E+08" (no leading space);
						// dbl: double precision (15 digits, "D" exponent)
double parseNum (const char *s, int *used);	// VAL(): leading number, 0 if none

} // namespace bas

#endif
