//
// basic/bas.h -- Onyx BASIC: a QBasic-style language compiled to bytecode for a small VM.
//
// The core (bascore.cpp, basnum.cpp) is plain portable C++ -- no libc, only new/delete --
// so it builds both into Onyx programs (/bin/basic, the qbasic editor) and on a PC for the
// host tests (tools/tests/run_basic_test.sh). Everything the language does to the outside
// world (screen, keyboard, graphics, uikit controls, files, notifications...) goes through a
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
struct G3Vertex; struct G3Batch;		// (basic/bas3d.h)

// ---- a host's own words: a dialect (setDialect) ---------------------------------------------
// A program that hosts BASIC may add statements and functions of its own (a game's "FORWARD 3", "WALL ()"),
// word aliases (another language's keywords: "AVANCE" for FORWARD, "SI" for IF) and the block REPEAT n ...
// END REPEAT. The words are reserved while the dialect is set; they are compiled into calls of Host::ext.
//   kind  's' a statement, 'n' a function giving a number, '$' a function giving a string;
//   args  as the built-ins' (N a number, S a string, '[' optional from here) -- at most 7.
// A dialect's word takes precedence over a built-in of the same name (a turtle's COLOR).
struct ExtWord { const char *name; int id; char kind; const char *args; };
struct Dialect
{
	const ExtWord *words;				// ended by a 0 name; 0: none
	const char *const *aliases;			// pairs "ALIAS", "WORD" (capitals), ended by 0; 0: none
	bool repeat;					// REPEAT n ... END REPEAT
};
// An argument or a result of Host::ext: a number, or a string (s: len bytes, code page 437 as BASIC keeps them).
struct ExtVal { bool str; double n; const char *s; int len; };

// ---- the outside world -------------------------------------------------------------------
// Colours: 0..15 = the QBasic palette; RGB(r,g,b) values have bit 24 set (0x1RRGGBB).
struct Profile
{
	unsigned long long ops, prims;				// instructions run; of which primitives
	unsigned long long usTotal, usPrim;			// microseconds: in all, in the primitives (their waits included)
	unsigned long long usWait, usWaitPrim;			// waiting / pumping the window: outside a primitive, inside one
	bool inPrim;
	Profile () : ops (0), prims (0), usTotal (0), usPrim (0), usWait (0), usWaitPrim (0), inPrim (false) {}
	// The three shares, in microseconds: the VM's instructions, the primitives' work, the waits.
	unsigned long long vm () const { return usTotal - usPrim - usWait; }
	unsigned long long work () const { return usPrim - usWaitPrim; }
	unsigned long long waits () const { return usWait + usWaitPrim; }
	// "VM 12.3 s (41 %), primitives 3.1 s (10 %), waits 14.6 s (49 %); 1234567 k instructions, 0.010 us each; 4567 k primitives, 0.68 us each"
	int text (char *o, int cap) const
	{
		int n = 0;
		auto str = [&] (const char *s) { while (*s && n < cap - 1) o[n++] = *s++; };
		auto num = [&] (unsigned long long v) { char t[24]; int k = 0; do { t[k++] = (char) ('0' + v % 10); v /= 10; } while (v); while (k && n < cap - 1) o[n++] = t[--k]; };
		auto fix = [&] (unsigned long long v1000) { num (v1000 / 1000); str ("."); unsigned long long f = v1000 % 1000; if (f < 100) str ("0"); if (f < 10) str ("0"); num (f); };
		unsigned long long tot = usTotal ? usTotal : 1;
		auto part = [&] (const char *name, unsigned long long us) { str (name); fix (us / 1000); str (" s ("); num (us * 100 / tot); str (" %)"); };
		part ("VM ", vm ()); part (", primitives ", work ()); part (", waits ", waits ());
		str ("; "); num (ops / 1000); str (" k instructions, "); fix (ops ? vm () * 1000 / ops : 0); str (" us each; ");
		num (prims / 1000); str (" k primitives, "); fix (prims ? work () * 1000 / prims : 0); str (" us each");
		o[n] = 0;
		return n;
	}
};

struct Host
{
	virtual ~Host () {}
	// Profiling (basic -p): where the time goes -- the VM's own instructions, the runtime's primitives
	// (built-in functions and statements, PRINT, INPUT, files), and in those the waits (SLEEP, PAUSE,
	// the window's pump and display). prof = 0: no measure. clockUs: a free-running microsecond clock.
	Profile *prof = 0;
	virtual unsigned clockUs () { return 0; }
	// Native execution (basjit.h, AArch64): memory the program's machine code is written to and run from
	// (0: none -- the program runs on the VM); managed = true (basic -m): the VM even where there is.
	bool managed = false;
	virtual void *codeAlloc (unsigned size) { (void) size; return 0; }
	virtual void profReport () {}				// (called every few seconds and at the end)
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
	// Rectangles of pixels (GET / PUT): raw = 0xRRGGBB values, else palette indices (the
	// nearest entry). Outside the screen: 0 / not written.
	virtual void readRect (int x, int y, int w, int h, int *out, bool raw) { for (int i = 0; i < w * h; i++) out[i] = 0; (void) x; (void) y; (void) raw; }
	virtual void writeRect (int x, int y, int w, int h, const int *in, bool raw) { (void) x; (void) y; (void) w; (void) h; (void) in; (void) raw; }
	virtual void fullscreen (bool on) { (void) on; }	// FULLSCREEN: the screen scaled to the display
	virtual void palette (int attr, int rgb) { (void) attr; (void) rgb; }	// rgb 0xRRGGBB; attr < 0: reset all
	virtual void pcopy (int src, int dst) { (void) src; (void) dst; }
	virtual int  keyPending (char *out2) { (void) out2; return 0; }	// the next key (as INKEY$), not taken
	virtual bool keyDown (const char *k, int n) { (void) k; (void) n; return false; }	// KEYDOWN(k$): held now?
	// USB gamepads (PAD, STICK, STRIG): the PAD_* buttons of pad 0..3 (-1: all; user/Include/gamepad.h
	// bits: 1 up, 2 down, 4 left, 8 right, 16 A, 32 B, 64 X, 128 Y, 256 L, 512 R, 1024 L2,
	// 2048 R2, 4096 select, 8192 start ...), a stick axis (0 lx, 1 ly, 2 rx, 3 ry: -1000..1000).
	virtual unsigned padButtons (int pad) { (void) pad; return 0; }
	virtual int  padAxis (int pad, int axis) { (void) pad; (void) axis; return 0; }
	virtual void pset (int x, int y, int c) { (void) x; (void) y; (void) c; }
	virtual int  point (int x, int y) { (void) x; (void) y; return 0; }
	virtual void line (int x1, int y1, int x2, int y2, int c, int box, int style) { (void) x1; (void) y1; (void) x2; (void) y2; (void) c; (void) box; (void) style; }
	virtual void circle (int x, int y, int r, int c, int fill) { (void) x; (void) y; (void) r; (void) c; (void) fill; }
	virtual void drawText (int x, int y, const char *s, int c) { (void) x; (void) y; (void) s; (void) c; }
	virtual int  mouse (int what) { (void) what; return 0; }	// 0 x, 1 y, 2 buttons
	// 3D (SCENE3D ... RENDER3D, basic/bas3d.h): a texture from w x h pixels 0xAARRGGBB -> its
	// number (1..; 0 = none left); a frame of vertices + batches drawn on the active page
	// (cleared to clear 0xRRGGBB unless keep) -> 0 ok, < 0 not possible; the 0xRRGGBB of a
	// colour value (palette index or RGB ()); the width / height of a displayed pixel; true
	// when the GPU draws.
	virtual int  texture3d (const unsigned *px, int w, int h) { (void) px; (void) w; (void) h; return 0; }
	virtual int  render3d (const G3Vertex *v, int nv, const G3Batch *b, int nb, unsigned clear, bool keep)
	{ (void) v; (void) nv; (void) b; (void) nb; (void) clear; (void) keep; return -1; }
	virtual unsigned rgbColor (int c) { return (c & 0x1000000) ? (unsigned) c & 0xFFFFFF : 0xFFFFFF; }
	virtual double pixelAspect () { return 1; }
	virtual bool gpu3d () { return false; }
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
	// PLAY "MB": queue a note (freq 0 = a rest) sounding onMs then silent offMs, played while the
	// program goes on; false = no background player (the VM plays it in the foreground).
	virtual bool bgNote (double freq, int onMs, int offMs, int wave) { (void) freq; (void) onMs; (void) offMs; (void) wave; return false; }
	virtual int  bgNotes () { return 0; }			// PLAY(n): notes still queued
	// AudioKit (audiokit/audiokit.h; the Onyx runtime): a sound file played in the background --
	// akPlay 0 / -1 (akError: why); akCommand 0 stop, 1 pause (v), 2 volume (v: 0..100), 3 every
	// note off; akQuery 0 its state (0 stopped, 1 playing, 2 paused, 3 waiting for the output),
	// 1 its place in seconds, 2 its length --, and notes on the General MIDI synthesizer (akMidi: a
	// MIDI command, 0x90 note on, 0x80 off, 0xC0 program, 0xB0 controller -> 0 / -1).
	virtual int  akPlay (const char *path, int loop) { (void) path; (void) loop; return -1; }
	virtual void akCommand (int what, int v) { (void) what; (void) v; }
	virtual double akQuery (int what) { (void) what; return 0; }
	virtual int  akMidi (int cmd, int ch, int d1, int d2) { (void) cmd; (void) ch; (void) d1; (void) d2; return -1; }
	virtual const char *akError () { return ""; }
	virtual double akNoteHz (int key) { (void) key; return 0; }		// a MIDI key's frequency
	virtual int  akNoteKey (const char *name) { (void) name; return -1; }	// "C4", "F#3" -> the key
	// GPIOKit (gpiokit/gpiokit.h; the Onyx runtime): the 40-pin header. gpio (GP_*, a, b, c, bytes in,
	// bytes out) -> >= 0, or GPIOKit's negative code (gpioError: its text). No GPIOKit: GP_NODEV.
	enum { GP_MODE, GP_WRITE, GP_READ, GP_PWM, GP_SERVO, GP_EDGES, GP_EVENTS, GP_FREE, GP_SIM, GP_I2C_OPEN,
	       GP_I2C_REG_READ, GP_I2C_REG_WRITE, GP_I2C_XFER, GP_I2C_SCAN, GP_SPI_OPEN, GP_SPI_XFER, GP_NODEV = -19 };
	// GP_MODE (pin, mode 0 free / 1 input / 2 pull-up / 3 pull-down / 4 output), GP_WRITE (pin, level), GP_READ (pin),
	// GP_PWM (pin, Hz, duty in 1/10000), GP_SERVO (pin, pulse us), GP_EDGES (pin, 1 rising | 2 falling),
	// GP_EVENTS -> how many edges came, out[2 k] their pin, out[2 k + 1] their edge (outCap / 2 at most),
	// GP_FREE (pin; -1: every one), GP_SIM (on), GP_I2C_OPEN (Hz), GP_I2C_REG_READ (addr, reg) -> its value,
	// GP_I2C_REG_WRITE (addr, reg, value), GP_I2C_XFER (addr: in written, then b bytes read into out) -> b,
	// GP_I2C_SCAN -> how many answered (out: 16 bytes, bit a = address a), GP_SPI_OPEN (Hz, mode),
	// GP_SPI_XFER (chip select: in sent, as many bytes into out) -> how many.
	virtual int  gpio (int op, int a, int b, int c, const char *in, int inLen, char *out, int outCap)
	{ (void) op; (void) a; (void) b; (void) c; (void) in; (void) inLen; (void) out; (void) outCap; return GP_NODEV; }
	virtual const char *gpioError (int code) { (void) code; return "no GPIO on this system"; }
	// GUI (Onyx): WINDOW, controls and their events.
	virtual void window (const char *title, int w, int h) { (void) title; (void) w; (void) h; }
	virtual int  control (int kind, int x, int y, int w, int h, const char *text, int val)
	{ (void) kind; (void) x; (void) y; (void) w; (void) h; (void) text; (void) val; return 0; }
	virtual void setText (int id, const char *s) { (void) id; (void) s; }
	virtual int  getText (int id, char *buf, int cap) { (void) id; (void) cap; buf[0] = 0; return 0; }
	virtual int  getValue (int id) { (void) id; return 0; }
	virtual void setValue (int id, int v) { (void) id; (void) v; }
	virtual int  event (bool wait) { (void) wait; return 0; }	// control id; -1 closed; -2 resized; 0 none
	// The controls' place and state, the window's flags (1: resizable), its menus (QBStudio's code).
	virtual void moveControl (int id, int x, int y, int w, int h) { (void) id; (void) x; (void) y; (void) w; (void) h; }
	virtual void showControl (int id, bool on) { (void) id; (void) on; }
	virtual void enableControl (int id, bool on) { (void) id; (void) on; }
	virtual void focusControl (int id) { (void) id; }
	virtual void windowFlags (int flags) { (void) flags; }
	virtual int  menuItem (const char *title, const char *item, const char *key) { (void) title; (void) item; (void) key; return 0; }
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
	// Kits (#import): the shared library `name` (lower case) opened for this program, its table having at
	// least minVersion entries -> its entries (entry n: the function of place n), or 0 with `why` said.
	virtual void *const *kitOpen (const char *name, int minVersion, char *why, int cap)
	{ (void) name; (void) minVersion; bscpyHost (why, "no kits on this system", cap); return 0; }
	static void bscpyHost (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; if (cap > 0) d[i] = 0; }
	// A dialect's word (setDialect): its id, its arguments -> true (a function: *result set; a string result's
	// bytes stay the host's until the next call), or false: a run-time error, why says it (at the word's line).
	virtual bool ext (int id, const ExtVal *args, int argc, ExtVal *result, char *why, int cap)
	{ (void) id; (void) args; (void) argc; (void) result; bscpyHost (why, "Unknown word", cap); return false; }
	// The statement hook (a debugger, a step-by-step run): with lineHook set before run (), onStatement is called
	// before each statement the program starts (its source line, 1-based) -- false stops the program there (run
	// returns 0, no error). The program then runs on the VM (no machine code). A loop with no statement inside
	// ("DO: LOOP") calls poll () only: a host that limits a run counts there too.
	bool lineHook = false;
	virtual bool onStatement (int line) { (void) line; return true; }
	// Program arguments (COMMAND$) and the end of the run.
	virtual const char *command () { return ""; }
	virtual void finished (bool error) { (void) error; }
};

// Control kinds for Host::control.
enum { CTL_BUTTON = 1, CTL_LABEL, CTL_TEXTBOX, CTL_CHECKBOX, CTL_LISTBOX, CTL_DROPDOWN, CTL_PROGRESS, CTL_SLIDER };

struct Program;
Program *compile (const char *src, Error *err);
// Kits (#import name): where the compiler reads a kit's description -- the text of SD:/lib/<name>.bi
// (name in lower case) as a new[] buffer and its length, or 0: no such kit. None set: #import fails.
void     setKitSource (char *(*source) (const char *name, int *len));
// The dialect the next compile () and wordList () use (0: plain BASIC). The structure is kept, not copied.
void     setDialect (const Dialect *d);
int      run (Program *p, Host &host, Error *err);
void     destroy (Program *p);
// Compiled programs (.bax, basbax.cpp): the bytecode as a file -- it runs without parsing.
void     setManaged (Program *p, bool on);		// "Managed": the program runs on the VM even where machine code is possible
bool     isManaged (const Program *p);
int      saveBax (const Program *p, char **out);
// A standalone app (Make App > Standalone): the runtime's executable (SD:/bin/basic) with a compiled program
// after it and a 16-byte trailer -- "OBAXAPP1", the program's offset and length (little-endian). The runtime
// started from such a file runs that program. attachBax: the whole file (new[]), its size. attachedBax: from a
// file's last 16 bytes and its size, where the program is (false: a plain runtime).
enum { BAX_TRAILER = 16 };
int      attachBax (const char *runtime, int rlen, const char *bax, int blen, char **out);
bool     attachedBax (const char *tail16, unsigned fileSize, unsigned *off, unsigned *len);	// its bytes (new[]), their count
bool     isBax (const char *buf, int len);
Program *loadBax (const char *buf, int len, Error *err);
Program *load (const char *buf, int len, Error *err);	// a .bax as it is, else source (compiled)
// The language's words (keywords, then the built-in functions), separated by spaces: for an
// editor that capitalises them. Returns the length (the list is cut at cap - 1).
int      wordList (char *buf, int cap);

// Number <-> text, QBasic style (also used by the editor / hosts).
int    formatNum (double v, char *out, bool dbl = false);	// "3.5", "-2", "1.234568E+08" (no leading space);
						// dbl: double precision (15 digits, "D" exponent)
double parseNum (const char *s, int *used);	// VAL(): leading number, 0 if none

} // namespace bas

#endif
