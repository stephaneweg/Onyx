//
// basic/basint.h -- internals shared by the compiler (bascomp.cpp) and the VM (basvm.cpp).
//
#ifndef _basint_h
#define _basint_h

#include "basic/bas.h"

namespace bas {

// ---- tiny portable helpers (no libc) ----------------------------------------------------
static inline int  bslen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline void bscpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static inline char bup (char c) { return (c >= 'a' && c <= 'z') ? (char) (c - 32) : c; }
static inline bool bseq (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static inline bool bseqi (const char *a, const char *b) { while (*a && bup (*a) == bup (*b)) { a++; b++; } return bup (*a) == bup (*b); }
static inline void bmcpy (void *d, const void *s, int n) { char *dd = (char *) d; const char *ss = (const char *) s; for (int i = 0; i < n; i++) dd[i] = ss[i]; }

// A growable array (T copyable).
template <class T> struct Vec
{
	T *d; int n, cap;
	Vec () : d (0), n (0), cap (0) {}
	~Vec () { delete [] d; }
	void push (const T &v)
	{
		if (n == cap)
		{
			int nc = cap ? cap * 2 : 16;
			T *nd = new T[nc];
			for (int i = 0; i < n; i++) nd[i] = d[i];
			delete [] d; d = nd; cap = nc;
		}
		d[n++] = v;
	}
	T &operator[] (int i) { return d[i]; }
	const T &operator[] (int i) const { return d[i]; }
private:
	Vec (const Vec &);
	Vec &operator= (const Vec &);
};

// Value types at compile time: TY_NUM, TY_STR, or a user TYPE (TY_REC + its index).
enum { TY_NUM = 0, TY_STR = 1, TY_NIL = 2, TY_REC = 16 };		// (TY_NIL: the literal NOTHING)
// Numeric sub-types (the storage of a variable): single (the default), INTEGER, LONG, DOUBLE.
enum { NT_SNG = 0, NT_INT, NT_LNG, NT_DBL };

// ---- bytecode ------------------------------------------------------------------------------
enum Op
{
	OP_NUM = 1, OP_STR, OP_LDG, OP_STG, OP_LDL, OP_STL,
	OP_ALDG, OP_ASTG, OP_ALDL, OP_ASTL,			// slot nd (indices on the stack)
	OP_DIMG, OP_DIML,					// slot nd isStr (lo,hi pairs on the stack)
	OP_ERASEG, OP_ERASEL,
	OP_REFG, OP_REFL,					// push a reference (by-ref argument)
	OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_IDIV, OP_MOD, OP_POW, OP_NEG, OP_CAT,
	OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,
	OP_SEQ, OP_SNE, OP_SLT, OP_SGT, OP_SLE, OP_SGE,
	OP_AND, OP_OR, OP_XOR, OP_NOT, OP_EQV, OP_IMP,
	OP_JMP, OP_JZ, OP_JNZ,
	OP_CALL, OP_RET, OP_RETF,				// proc argc / - / -
	OP_GOSUB, OP_RETSUB, OP_POP,
	OP_BI, OP_ST,						// builtin function / statement: id argc
	OP_PRINT, OP_PRSEP, OP_PRTAB, OP_PRSPC, OP_CHAN,	// PRSEP kind: 1 zone, 2 newline
	OP_INPUT, OP_INFIELD, OP_LINPUT,			// prompt flags / type / prompt
	OP_READ, OP_RESTORE,					// type / data index
	OP_OPEN, OP_CLOSE, OP_END, OP_STOP,
	OP_NOP,
	// --- QBasic completion ---
	OP_CONV,						// nt: INTEGER / LONG store (round, overflow)
	OP_FIXSTR,						// len: pad / cut to a fixed-length string
	OP_ADDRG, OP_ADDRL,					// slot: push a reference to a variable
	OP_AADDRG, OP_AADDRL,					// slot nd: reference to an array element
	OP_FADDR, OP_FLD,					// field: reference -> field ref / record -> field value
	OP_STREF, OP_LDREF,					// store through / load from a reference
	OP_MIDSET, OP_LSET, OP_RSET,				// MID$ / LSET / RSET statements (ref ...)
	OP_ONERR, OP_RESUME,					// target (-1 off) / mode target
	OP_USING,						// n items: PRINT USING
	OP_FGET, OP_FPUT,					// hasVar kind ext: GET / PUT # (file rec [ref])
	OP_FIELD,						// n: FIELD #f, (width ref) x n
	OP_SEEK,						// SEEK #f, pos
	OP_ONEVENT,						// kind(0 timer, 1..31 key) target: ON TIMER / KEY GOSUB
	OP_EVSTATE,						// kind state(0 off 1 on 2 stop)
	OP_CHAIN, OP_RUN, OP_CLEAR, OP_TRON,			// CHAIN file / RUN mode target / CLEAR / TRON on
	OP_FREADY,						// (reserved)
	OP_NEWREC,						// type: push a fresh record (NEW Type) / object (NEW Class)
	// --- classes ---
	OP_NIL,							// push NOTHING
	OP_VCALL,						// slot argc: a virtual method, by the object's class (the object under the arguments)
	OP_ICALL,						// interface slot argc: an interface's method, by the object's class
	OP_ISTYPE,						// type (-1: NOTHING): object -> -1 / 0 (x IS Class, x IS NOTHING)
	OP_SAMEOBJ,						// a IS b: the same object
	OP_CAST,						// type: the object must be NOTHING or of this class / interface
	// --- kits (#import) ---
	OP_KCALL,						// function argc: a kit's function (Program::kfns), its arguments on the stack
	OP_COUNT_						// (the number of opcodes + 1: the .bax header)
};

// The number of operands (code words) after an opcode: what the native translator walks the code with.
static inline int opLen (int op)
{
	switch (op)
	{
	case OP_NUM: case OP_STR: case OP_LDG: case OP_STG: case OP_LDL: case OP_STL: case OP_ERASEG: case OP_ERASEL:
	case OP_REFG: case OP_REFL: case OP_JMP: case OP_JZ: case OP_JNZ: case OP_GOSUB: case OP_PRINT: case OP_PRSEP:
	case OP_INFIELD: case OP_LINPUT: case OP_READ: case OP_RESTORE: case OP_OPEN: case OP_CONV: case OP_FIXSTR:
	case OP_ADDRG: case OP_ADDRL: case OP_FADDR: case OP_FLD: case OP_ONERR: case OP_USING: case OP_FIELD:
	case OP_TRON: case OP_NEWREC: case OP_ISTYPE: case OP_CAST:
		return 1;
	case OP_ALDG: case OP_ASTG: case OP_ALDL: case OP_ASTL: case OP_AADDRG: case OP_AADDRL: case OP_CALL: case OP_BI:
	case OP_ST: case OP_INPUT: case OP_RESUME: case OP_ONEVENT: case OP_EVSTATE: case OP_RUN: case OP_VCALL:
	case OP_KCALL:
		return 2;
	case OP_FGET: case OP_FPUT: case OP_ICALL:
		return 3;
	case OP_DIMG: case OP_DIML:
		return 4;
	}
	return 0;
}

// GET / PUT # layout of a variable: a scalar kind, or a record (ext = its type).
enum { LK_SNG = 1, LK_INT, LK_LNG, LK_DBL, LK_VSTR, LK_FSTR, LK_REC };

// Builtin functions (OP_BI) and statements (OP_ST).
enum Builtin
{
	B_LEN = 1, B_ASC, B_CHR, B_LEFT, B_RIGHT, B_MID, B_INSTR, B_UCASE, B_LCASE, B_LTRIM, B_RTRIM, B_TRIM,
	B_STR, B_VAL, B_SPACE, B_STRING, B_HEX, B_OCT,
	B_ABS, B_SGN, B_INT, B_FIX, B_SQR, B_SIN, B_COS, B_TAN, B_ATN, B_EXP, B_LOG, B_RND, B_CINT, B_CLNG, B_CDBL,
	B_MIN, B_MAX,
	B_TIMER, B_DATE, B_TIME, B_INKEY, B_COMMAND, B_POS, B_CSRLIN, B_POINT, B_RGB, B_EOF, B_LOF, B_FREEFILE,
	B_FILEEXISTS, B_DIR,
	B_BUTTON, B_LABEL, B_TEXTBOX, B_CHECKBOX, B_LISTBOX, B_DROPDOWN, B_PROGRESS, B_SLIDER,
	B_GETTEXT, B_VALUE, B_EVENT, B_WAITEVENT, B_MSGBOX, B_CLIPBOARD, B_OPENFILE, B_SAVEFILE,
	B_MOUSEX, B_MOUSEY, B_MOUSEB,
	B_TICKS, B_LBOUND, B_UBOUND,
	B_ERR, B_ERL, B_MKI, B_MKL, B_MKS, B_MKD, B_CVI, B_CVL, B_CVS, B_CVD, B_INPUTS, B_SEEK, B_LOC,
	B_ENVIRON, B_FRE, B_PMAP, B_SCREEN, B_STRD, B_POINT1, B_CINTR, B_CLNGR, B_KEYDOWN, B_PLAYN,
	B_STICK, B_STRIG, B_PAD, B_GRAB3D, B_GPU3D,			// (the last below 100: S_CLS)

	S_CLS = 100, S_LOCATE, S_COLOR, S_SCREEN, S_PSET, S_PRESET, S_LINE, S_CIRCLE, S_DRAWTEXT,
	S_SLEEP, S_PAUSE, S_BEEP, S_SOUND, S_RANDOMIZE, S_WINDOW, S_SETTEXT, S_SETVALUE, S_NOTIFY,
	S_SETCLIPBOARD, S_EXEC, S_LAUNCH, S_KILL, S_NAME, S_MKDIR, S_RMDIR, S_WIDTH, S_SWAPNUM,
	S_PLAY, S_NOTEON, S_NOTEOFF,
	S_ERROR, S_RESET, S_FILES, S_CHDIR, S_ENVIRON, S_SHELL, S_PAINT, S_DRAW, S_GGET, S_GPUT,
	S_VIEW, S_VIEWPRINT, S_GWINDOW, S_PALETTE, S_PALUSING, S_PCOPY, S_KEYDEF, S_CLSN, S_FULLSCREEN,
	S_SCENE3D, S_RENDER3D, S_CAMERA3D, S_LIGHT3D, S_IDENTITY3D, S_TRANSLATE3D, S_ROTATE3D, S_SCALE3D,
	S_PUSH3D, S_POP3D, S_COLOR3D, S_TEXTURE3D, S_BLEND3D, S_DEPTH3D, S_CULL3D, S_VERTEX3D,
	S_CUBE3D, S_SPHERE3D, S_CYLINDER3D, S_PLANE3D,
	S_MOVECONTROL, S_SHOWCONTROL, S_ENABLECONTROL, S_FOCUSCONTROL,	// (the controls' place, state: QBStudio's code)
	S_PLAYFILE, S_STOPFILE, S_PAUSEFILE, S_FILEVOLUME,		// (AudioKit: a sound file in the background)
	S_MIDINOTE, S_MIDIPROGRAM, S_MIDICONTROL, S_MIDIOFF,		// (... notes on the General MIDI synthesizer)
	S_DEALLOC, S_POKEB, S_POKEW, S_POKEL, S_POKEQ, S_POKEF, S_POKED, S_POKES,	// (kits: memory a program shares with a kit)
	S_PEEKT, S_POKET,						// (... a kit's structure read from / written at an address)
	S_EXT,								// (a dialect's statement: its id, then its arguments)
	S_LAST,
	B_MENUITEM = 300, B_WINDOWWIDTH, B_WINDOWHEIGHT,		// (the built-in functions past 100)
	B_FILEPLAYING, B_FILEPOS, B_FILELENGTH, B_NOTEFREQ, B_NOTENUMBER,	// (AudioKit)
	B_ALLOC, B_CSTR, B_PEEKB, B_PEEKW, B_PEEKL, B_PEEKQ, B_PEEKF, B_PEEKD, B_ADDRESSOF,	// (kits; known after an #import)
	B_EXT,								// (a dialect's function: its id, then its arguments)
	B_LAST
};

struct LineMark { int pc, line; };
struct DataItem { char *text; bool isStr; };
// Slot kinds (Program::gkind / lkind): what a variable holds before its first store.
// gext / lext: the fixed length of a K_STR / K_STRARR (0 = variable), the TYPE of a K_REC /
// K_RECARR.
enum { K_NUM = 0, K_STR = 1, K_NUMARR = 2, K_STRARR = 3, K_REC = 4, K_RECARR = 5 };

// User TYPEs at run time: the field layout (GET / PUT, LEN, new records).
enum { FK_SNG = LK_SNG, FK_INT = LK_INT, FK_LNG = LK_LNG, FK_DBL = LK_DBL, FK_VSTR = LK_VSTR, FK_FSTR = LK_FSTR, FK_REC = LK_REC };
struct FieldInfo { int kind; int len; int sub; };	// len: FSTR length / REC type (sub)
// A TYPE (a value), a CLASS (a reference: kind TK_CLASS) or an INTERFACE. A class: its parent (-1), its
// virtual methods' procedures at vtab[vt .. vt + nvt) (-1: abstract), the interfaces it implements at
// itab[it .. it + 2 * nit) (pairs: the interface, where its methods' procedures start in vtab), its own
// destructor (SUB Class.delete; -1).
enum { TK_TYPE = 0, TK_CLASS, TK_IFACE };
struct TypeInfo
{
	char name[48]; int first, nf, size;
	int kind, parent, vt, nvt, it, nit, dtor;
	TypeInfo () : first (0), nf (0), size (0), kind (TK_TYPE), parent (-1), vt (0), nvt (0), it (0), nit (0), dtor (-1) { name[0] = 0; }
};
struct StmtRange { int start, end; };
struct NumLabel { int pc; int value; };
struct ProcInfo { char name[48]; bool isFunc; int retTy; int nparams; int entry; int nlocals; int kindOff; };

// The kits a program imports (#import filekit) and their functions it calls (OP_KCALL). A kit is a
// shared library (SD:/lib/<name>.so) whose functions are known by their place in its table; what their
// names and types are is said by its description (SD:/lib/<name>.bi, tools/kitbi/kitbi.py), read when the
// program is compiled -- the compiled program keeps what it uses of it. A function's types, one letter each:
//   the result  v none, i / u a 32-bit number (signed / not), l a 64-bit number or a pointer, b / c a byte
//               (unsigned / signed), h / w 16 bits (signed / not), f a float, d a double, s a C string
//               (const char *: copied into a BASIC string; never freed);
//   an argument i a whole number, p a pointer (a number; 0), c a function (ADDRESSOF), s a C string (a
//               BASIC string, or a number: a pointer), f a float, d a double, I / L / F / D a pointer
//               to numbers (int *, a 64-bit number's or a pointer's address, float *, double *: a number
//               -- the address --, or BYREF variable: the variable gets what the function wrote).
//
// A kit's structures (the .bi's "struct" and "field" lines) are TYPEs of the program, named KIT.NAME: a
// variable of one given where a function takes a pointer (p) goes as the C structure -- its fields
// written at their places before the call, read back after it. KitStruct: the TYPE, the C structure's
// size, where its fields' places start in Program::kflds (one per field of the TYPE, in order).
// KitFld: the field's place, its C kind -- b c h w i u l f d as a result's, a: a text in a char array of
// n bytes (a 0 after it), t: the structure n of Program::kstructs.
enum { KIT_MAXARGS = 16 };
struct KitRef { char name[32]; int minVer; };		// minVer: the table must have this many entries
struct KitFn { int kit, slot; char ret; char args[KIT_MAXARGS + 1]; char name[64]; };
struct KitStruct { int type, size, first; };
struct KitFld { int off; char kind; int n; };

struct Program
{
	Vec<int> code;
	Vec<double> nums;
	Vec<char *> strs; Vec<int> strl;
	Vec<LineMark> lines;
	Vec<DataItem> data;
	Vec<ProcInfo> procs;
	Vec<unsigned char> gkind;		// per global slot (K_*)
	Vec<unsigned char> lkind;		// per local slot, procs[i].kindOff.. (K_*)
	Vec<int> gext, lext;			// fixed string length / TYPE per slot
	Vec<TypeInfo> types;
	Vec<FieldInfo> fields;
	Vec<StmtRange> stmts;			// every statement's code range (RESUME)
	Vec<NumLabel> numLabels;		// line-number labels (ERL)
	Vec<int> common;			// COMMON global slots, in order (CHAIN)
	Vec<int> vtab, itab;			// the classes' method tables (TypeInfo)
	Vec<KitRef> kits;			// the kits imported
	Vec<KitFn> kfns;			// their functions the program calls
	Vec<KitStruct> kstructs;		// their structures (TYPEs of the program)
	Vec<KitFld> kflds;
	int kitStruct (int type) const { for (int i = 0; i < kstructs.n; i++) if (kstructs[i].type == type) return i; return -1; }
	int nglobals;
	bool managed;				// OPTION MANAGED / the compile dialogs' "Managed": run by the VM, not in machine code
	Program () : nglobals (0), managed (false) {}
	~Program ()
	{
		for (int i = 0; i < strs.n; i++) delete [] strs[i];
		for (int i = 0; i < data.n; i++) delete [] data[i].text;
	}
	int lineAt (int pc) const
	{
		int best = 0;
		for (int i = 0; i < lines.n; i++) { if (lines[i].pc > pc) break; best = lines[i].line; }
		return best;
	}
	// An object of class t: is it a `want` (its class, an ancestor, an interface it implements)?
	bool isA (int t, int want) const
	{
		if (types[want].kind == TK_IFACE)
		{
			for (int i = 0; i < types[t].nit; i++) if (itab[types[t].it + 2 * i] == want) return true;
			return false;
		}
		for (; t >= 0; t = types[t].parent) if (t == want) return true;
		return false;
	}
};

} // namespace bas

#endif
