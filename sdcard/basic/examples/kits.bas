' kits.bas -- a BASIC program that uses the system's kits.
'
' #import <kit> makes the functions of a kit (a shared library of SD:/lib) callable by their
' name: Kit.name (arguments). What a kit offers is in its reference (the documents 10 to 18) and
' in SD:/lib/<kit>.bi, the list BASIC reads.
'
'   - a string goes as a string, a number as a number; a handle or a pointer is a number
'     (keep it in a plain or a # variable, not in a % or & one);
'   - BYREF variable: where the function fills a number;
'   - ADDRESSOF (Name): a SUB or a FUNCTION of the program the kit calls back;
'   - ALLOC (bytes) / DEALLOC: memory for what a function writes; CSTR$ (pointer): its text;
'     PEEKB / PEEKW / PEEKL / PEEKQ / PEEKF / PEEKD and POKEB ... POKES read and write it.
'
#import AppKit
#import FileKit
#import SystemKit

DECLARE FUNCTION Copying (user, done, total, file$)

PRINT "The kernel's interface is version"; AppKit.abi_version

' The kernel says what it is: a text written into a buffer of ours
info# = ALLOC(512)
n = AppKit.kernel_info(info#, 512)
i$ = CSTR$(info#)
PRINT "The kernel says: "; LEFT$(i$, INSTR(i$, CHR$(10)) - 1)
DEALLOC info#

' FileKit: a size, as people read it
size# = FileKit.file_size("SD:/config.txt")
text# = ALLOC(32)
FileKit.human_size size#, text#, 32
PRINT "config.txt is "; CSTR$(text#)
DEALLOC text#

' ... a whole file in memory: the kit allocates it, we free it with the kit's function
IF FileKit.load("SD:/config.txt", BYREF buf#, BYREF length) = 0 THEN
	PRINT "Its first line: "; LEFT$(CSTR$(buf#), INSTR(CSTR$(buf#), CHR$(10)) - 1)
	FileKit.free buf#
END IF

' ... a tree measured: two numbers come back through BYREF
nfiles = 0: nfolders = 0
bytes# = FileKit.tree_size("SD:/basic", BYREF nfiles, BYREF nfolders)
PRINT "SD:/basic:"; nfiles; "files in"; nfolders; "folders,"; bytes#; "bytes"

' ... a copy that reports its progress to a FUNCTION of ours (0 = go on)
IF FileKit.copy("SD:/config.txt", "SD:/tmp/kits-demo.txt", ADDRESSOF(Copying), 42) = 0 THEN PRINT "Copied."
r = FileKit.remove("SD:/tmp/kits-demo.txt")

' SystemKit: a notification on the desktop
SystemKit.notify "BASIC", "Hello from a kit"
END

FUNCTION Copying (user, done, total, file$)
	PRINT "  copying "; file$; ":"; done; "of"; total; "(our number:"; user; ")"
	Copying = 0
END FUNCTION
