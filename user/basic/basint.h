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

enum { TY_NUM = 0, TY_STR = 1 };

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
	OP_NOP
};

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

	S_CLS = 100, S_LOCATE, S_COLOR, S_SCREEN, S_PSET, S_PRESET, S_LINE, S_CIRCLE, S_DRAWTEXT,
	S_SLEEP, S_PAUSE, S_BEEP, S_SOUND, S_RANDOMIZE, S_WINDOW, S_SETTEXT, S_SETVALUE, S_NOTIFY,
	S_SETCLIPBOARD, S_EXEC, S_LAUNCH, S_KILL, S_NAME, S_MKDIR, S_RMDIR, S_WIDTH, S_SWAPNUM,
	S_PLAY, S_NOTEON, S_NOTEOFF
};

struct LineMark { int pc, line; };
struct DataItem { char *text; bool isStr; };
// Slot kinds (Program::gkind / lkind): what a variable holds before its first store.
enum { K_NUM = 0, K_STR = 1, K_NUMARR = 2, K_STRARR = 3 };
struct ProcInfo { char name[48]; bool isFunc; int retTy; int nparams; int entry; int nlocals; int kindOff; };

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
	int nglobals;
	Program () : nglobals (0) {}
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
};

} // namespace bas

#endif
