//
// pc/macOS/cocoa.mm -- the screen's half of the Onyx kernel's table on macOS (hostkapi.cpp is the rest):
// the app's window, its events, its menu bar, the clipboard, files dropped or opened from the Finder.
//
//   the window     a Mac window; its content is the app's canvas (no Onyx frame drawn: get_chrome says
//                  "borderless"), drawn 1 point a pixel (sharp on a Retina screen: each pixel doubled).
//                  The app is maximised in it at once and follows its size (GUI_EVENT_DISPLAY_RESIZE, as
//                  pc/Koton on Windows); macOS remembers the window's place and size.
//   the keyboard   Cmd+letter is the Onyx Ctrl+letter (Cmd+S saves, Cmd+C copies...; the real Ctrl too);
//                  Cmd+Left / Right are Home / End, Cmd+Up / Down Ctrl+Home / End; Option is Alt; the
//                  text typed goes through macOS's input (dead keys: ^ then e is ê). Cmd+Q, H, M, W and
//                  Ctrl+Cmd+F are the Mac's own.
//   the mouse      a right click (or Ctrl+click, a two-finger click) is the right button; the trackpad's
//                  scrolling is made notches.
//   the menu bar   the app's menus (kapi_set_menu) after the application menu (About, Hide, Quit), then
//                  Window and Help (the app's manual, when the card has one).
//   files          dropped on the window or opened from the Finder (a .ledger double-clicked): the app's
//                  GUI_EVENT_DROP, the paths made Onyx ones (MAC:/Users/...).
//
#import <Cocoa/Cocoa.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "host.h"

@interface OnyxView : NSView <NSTextInputClient>
@end
@interface OnyxDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end

// (plain data only at file scope: this file's C++ objects would be made after gui_setup runs -- the
//  vectors and strings live in functions)
static NSWindow *g_win;
static OnyxView *g_view;
static OnyxDelegate *g_delegate;
static unsigned *g_canvas; static int g_cw, g_ch, g_stride;
static char g_title[64];
static gui_handler g_ptr, g_key, g_click, g_menuFn;
static volatile bool g_quit;
static int g_btn;				// the buttons held (1 left, 2 right, 4 middle)
static bool g_ctrlClick;			// (the left button pressed with Ctrl: the right one)
static int g_dispMods = -1;			// the modifiers of the event being handled (-1: the keyboard's now)
static double g_wheelAcc;
static pthread_mutex_t g_postLock = PTHREAD_MUTEX_INITIALIZER;

struct Ev { int to; int ev; long v; int mods; };	// to: 0 pointer, 1 key, 2 menu, 3 click
struct Post { void (*fn) (void *, long); void *ctx; long v; };
static std::vector<Ev> &evq () { static std::vector<Ev> q; return q; }
static std::vector<Post> &posts () { static std::vector<Post> p; return p; }
static std::string &drop_text () { static std::string s; return s; }
static std::string &menu_spec () { static std::string s; return s; }

static int live_mods ()
{
	NSEventModifierFlags f = [NSEvent modifierFlags];
	int m = 0;
	if (f & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) m |= MOD_CTRL;
	if (f & NSEventModifierFlagShift) m |= MOD_SHIFT;
	if (f & NSEventModifierFlagOption) m |= MOD_ALT;
	return m;
}
static void push_m (int to, int ev, long v, int mods) { evq ().push_back (Ev { to, ev, v, mods }); }
static void push (int to, int ev, long v) { push_m (to, ev, v, live_mods ()); }
static long ptrval (int x, int y, int buttons, int changed, int wheel)
{
	if (x < 0) x = 0; if (y < 0) y = 0; if (x > 0xFFFF) x = 0xFFFF; if (y > 0xFFFF) y = 0xFFFF;
	return ((long) (wheel & 0xFF) << 48) | ((long) (changed & 0xFF) << 40) | ((long) (buttons & 0xFF) << 32) | ((long) x << 16) | (long) y;
}
static NSString *ns (const std::string &s) { return [NSString stringWithUTF8String:s.c_str ()] ?: @""; }

// ---- the view: the canvas drawn, the mouse and the keyboard ------------------------------------------------------
static void mouse (int ev, NSEvent *e, int changed, int wheel)
{
	NSPoint p = [g_view convertPoint:[e locationInWindow] fromView:nil];
	int x = (int) floor (p.x), y = (int) floor (p.y);
	push (0, ev, ptrval (x, y, g_btn, changed, wheel));
	if (ev == GUI_EVENT_PTR_DOWN)		// (the legacy click handler too)
		push (3, GUI_EVENT_CANVAS_CLICK, ((long) g_btn << 32) | ((long) (x & 0xFFFF) << 16) | (y & 0xFFFF));
	else if (ev == GUI_EVENT_PTR_MOVE && g_btn && x >= 0 && y >= 0)
		push (3, GUI_EVENT_CANVAS_MOTION, ((long) g_btn << 32) | ((long) (x & 0xFFFF) << 16) | (y & 0xFFFF));
}
// a key macOS names (an arrow, a function key...) -> the Onyx code (0: a character)
static long special_key (unichar c)
{
	switch (c)
	{
	case NSUpArrowFunctionKey: return KEY_UP; case NSDownArrowFunctionKey: return KEY_DOWN;
	case NSLeftArrowFunctionKey: return KEY_LEFT; case NSRightArrowFunctionKey: return KEY_RIGHT;
	case NSHomeFunctionKey: return KEY_HOME; case NSEndFunctionKey: return KEY_END;
	case NSPageUpFunctionKey: return KEY_PGUP; case NSPageDownFunctionKey: return KEY_PGDN;
	case NSDeleteFunctionKey: return KEY_DEL;
	case 0x7F: case 0x08: return KEY_BACKSPACE;
	case '\r': case 0x03: return KEY_ENTER;				// (Return, the keypad's Enter)
	case '\t': case 0x19: return KEY_TAB;				// (Tab, Shift+Tab)
	case 0x1B: return 27;
	}
	if (c >= NSF1FunctionKey && c <= NSF12FunctionKey) return KEY_F1 + (long) (c - NSF1FunctionKey);
	return 0;
}

@implementation OnyxView
{
	NSTrackingArea *_track;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (void)drawRect:(NSRect)dirty
{
#ifdef __APPLE__
	CGContextRef cg = [[NSGraphicsContext currentContext] CGContext];
	NSRect b = [self bounds];
	unsigned bg = g_canvas ? g_canvas[(size_t) (g_ch - 1) * g_stride + g_cw - 1] : 0x1B1E24;	// (beyond the canvas: its corner's colour)
	CGContextSetRGBFillColor (cg, ((bg >> 16) & 255) / 255.0, ((bg >> 8) & 255) / 255.0, (bg & 255) / 255.0, 1);
	CGContextFillRect (cg, NSRectToCGRect (b));
	if (!g_canvas) return;
	CFDataRef data = CFDataCreate (0, (const UInt8 *) g_canvas, (CFIndex) g_stride * g_ch * 4);	// (a copy: the canvas may move)
	CGDataProviderRef dp = CGDataProviderCreateWithCFData (data);
	CGColorSpaceRef cs = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
	CGImageRef im = CGImageCreate (g_cw, g_ch, 8, 32, (size_t) g_stride * 4, cs,
				       (CGBitmapInfo) kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little, dp, 0, false, kCGRenderingIntentDefault);
	CGContextSaveGState (cg);
	CGContextSetInterpolationQuality (cg, kCGInterpolationNone);
	CGContextTranslateCTM (cg, 0, g_ch);
	CGContextScaleCTM (cg, 1, -1);
	CGContextDrawImage (cg, CGRectMake (0, 0, g_cw, g_ch), im);
	CGContextRestoreGState (cg);
	CGImageRelease (im); CGColorSpaceRelease (cs); CGDataProviderRelease (dp); CFRelease (data);
#endif
}
- (void)updateTrackingAreas
{
	if (_track) [self removeTrackingArea:_track];
	_track = [[NSTrackingArea alloc] initWithRect:NSZeroRect
		options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
		owner:self userInfo:nil];
	[self addTrackingArea:_track];
	[super updateTrackingAreas];
}
- (void)mouseEntered:(NSEvent *)e { mouse (GUI_EVENT_PTR_ENTER, e, 0, 0); }
- (void)mouseExited:(NSEvent *)e { if (!g_btn) push (0, GUI_EVENT_PTR_LEAVE, 0); }
- (void)mouseMoved:(NSEvent *)e { mouse (GUI_EVENT_PTR_MOVE, e, 0, 0); }
- (void)mouseDragged:(NSEvent *)e { mouse (GUI_EVENT_PTR_MOVE, e, 0, 0); }
- (void)rightMouseDragged:(NSEvent *)e { mouse (GUI_EVENT_PTR_MOVE, e, 0, 0); }
- (void)otherMouseDragged:(NSEvent *)e { mouse (GUI_EVENT_PTR_MOVE, e, 0, 0); }
- (void)mouseDown:(NSEvent *)e
{
	g_ctrlClick = ([e modifierFlags] & NSEventModifierFlagControl) != 0;
	int b = g_ctrlClick ? 2 : 1;
	g_btn |= b; mouse (GUI_EVENT_PTR_DOWN, e, b, 0);
}
- (void)mouseUp:(NSEvent *)e
{
	int b = g_ctrlClick ? 2 : 1;
	g_ctrlClick = false;
	g_btn &= ~b; mouse (GUI_EVENT_PTR_UP, e, b, 0);
}
- (void)rightMouseDown:(NSEvent *)e { g_btn |= 2; mouse (GUI_EVENT_PTR_DOWN, e, 2, 0); }
- (void)rightMouseUp:(NSEvent *)e { g_btn &= ~2; mouse (GUI_EVENT_PTR_UP, e, 2, 0); }
- (void)otherMouseDown:(NSEvent *)e { g_btn |= 4; mouse (GUI_EVENT_PTR_DOWN, e, 4, 0); }
- (void)otherMouseUp:(NSEvent *)e { g_btn &= ~4; mouse (GUI_EVENT_PTR_UP, e, 4, 0); }
- (void)scrollWheel:(NSEvent *)e
{
	double dy = [e scrollingDeltaY];
	if ([e phase] == NSEventPhaseBegan) g_wheelAcc = 0;
	g_wheelAcc += [e hasPreciseScrollingDeltas] ? dy / 16.0 : dy;	// (a trackpad: a notch every 16 points)
	int n = (int) g_wheelAcc;
	if (!n) return;
	g_wheelAcc -= n;
	if (n > 127) n = 127; if (n < -127) n = -127;
	mouse (GUI_EVENT_PTR_WHEEL, e, 0, n);
}
- (void)keyDown:(NSEvent *)e
{
	NSString *c = [e charactersIgnoringModifiers];
	unichar ch = [c length] ? [c characterAtIndex:0] : 0;
	long k = special_key (ch);
	if (k) { push (1, GUI_EVENT_KEY, k); return; }
	if ([e modifierFlags] & NSEventModifierFlagControl)		// the real Ctrl: Ctrl+letter
	{
		if (ch < 128 && isalpha (ch)) push (1, GUI_EVENT_KEY, ch & 0x1F);
		else if (ch >= 32) push (1, GUI_EVENT_KEY, ch);
		return;
	}
	[self interpretKeyEvents:@[e]];
}
- (BOOL)performKeyEquivalent:(NSEvent *)e
{
	if ([e type] != NSEventTypeKeyDown || [[self window] firstResponder] != self) return NO;
	NSEventModifierFlags f = [e modifierFlags];
	if (!(f & NSEventModifierFlagCommand) || (f & NSEventModifierFlagControl)) return NO;	// (Ctrl+Cmd+F: full screen)
	NSString *c = [e charactersIgnoringModifiers];
	if (![c length]) return NO;
	unichar ch = [c characterAtIndex:0];
	unichar lc = ch < 128 ? (unichar) tolower (ch) : ch;
	if (lc == 'q' || lc == 'h' || lc == 'm' || lc == 'w' || lc == ',' || lc == '`') return NO;	// (the Mac's own)
	int mods = live_mods ();
	switch (ch)
	{
	case NSLeftArrowFunctionKey: push_m (1, GUI_EVENT_KEY, KEY_HOME, mods & ~MOD_CTRL); return YES;
	case NSRightArrowFunctionKey: push_m (1, GUI_EVENT_KEY, KEY_END, mods & ~MOD_CTRL); return YES;
	case NSUpArrowFunctionKey: push_m (1, GUI_EVENT_KEY, KEY_HOME, mods | MOD_CTRL); return YES;
	case NSDownArrowFunctionKey: push_m (1, GUI_EVENT_KEY, KEY_END, mods | MOD_CTRL); return YES;
	}
	long k = special_key (ch);
	if (k) push (1, GUI_EVENT_KEY, k);
	else if (lc < 128 && isalpha (lc)) push (1, GUI_EVENT_KEY, lc & 0x1F);	// Cmd+S: the Onyx Ctrl+S
	else if (ch >= 32 && ch < 0xF700) push (1, GUI_EVENT_KEY, ch);
	else return NO;
	return YES;
}
// NSTextInputClient: the text typed (dead keys composed by macOS: nothing is kept "marked" here)
- (void)insertText:(id)s replacementRange:(NSRange)r
{
	NSString *t = [s isKindOfClass:[NSAttributedString class]] ? [s string] : s;
	NSUInteger n = [t length];
	for (NSUInteger i = 0; i < n; i++)
	{
		unsigned c = [t characterAtIndex:i];
		if (c >= 0xD800 && c < 0xDC00 && i + 1 < n)
		{
			unsigned lo = [t characterAtIndex:i + 1];
			if (lo >= 0xDC00 && lo < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); i++; }
		}
		if (c == '\r' || c == '\n') push (1, GUI_EVENT_KEY, KEY_ENTER);
		else if (c >= 32 && c != 0x7F) push (1, GUI_EVENT_KEY, (long) c);
	}
}
- (void)doCommandBySelector:(SEL)sel { }
- (void)setMarkedText:(id)s selectedRange:(NSRange)sel replacementRange:(NSRange)r { }
- (void)unmarkText { }
- (NSRange)selectedRange { return NSMakeRange (NSNotFound, 0); }
- (NSRange)markedRange { return NSMakeRange (NSNotFound, 0); }
- (BOOL)hasMarkedText { return NO; }
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)r actualRange:(NSRangePointer)a { return nil; }
- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText { return @[]; }
- (NSRect)firstRectForCharacterRange:(NSRange)r actualRange:(NSRangePointer)a
{
	NSRect w = [[self window] convertRectToScreen:[self convertRect:NSMakeRect (0, 0, 1, 1) toView:nil]];
	return w;
}
- (NSUInteger)characterIndexForPoint:(NSPoint)p { return NSNotFound; }
// files dropped on the window
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)s { return NSDragOperationCopy; }
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)s { return NSDragOperationCopy; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)s
{
	NSArray *urls = [[s draggingPasteboard] readObjectsForClasses:@[[NSURL class]] options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
	if (![urls count]) return NO;
	std::string all;
	for (NSURL *u in urls) { if (!all.empty ()) all += '\n'; all += onyx_path ([[u path] UTF8String]); }
	NSPoint p = [self convertPoint:[s draggingLocation] fromView:nil];
	int x = p.x < 0 ? 0 : (int) p.x, y = p.y < 0 ? 0 : (int) p.y;
	drop_text () = all;
	push (0, GUI_EVENT_DROP, ((long) DND_F_COPY << 32) | ((long) (x & 0xFFFF) << 16) | (y & 0xFFFF));
	return YES;
}
@end

// ---- the application: its menus, the window's closing and resizing, files opened from the Finder -------------------
@implementation OnyxDelegate
- (void)onyxMenu:(NSMenuItem *)i { push (2, GUI_EVENT_MENU, (long) [i tag]); }
- (void)onyxQuit:(id)s { g_quit = true; }
- (void)onyxManual:(NSMenuItem *)i { host_open ([[i representedObject] UTF8String]); }
- (BOOL)windowShouldClose:(NSWindow *)w { g_quit = true; return NO; }	// (the app ends its loop and returns)
- (void)windowDidResize:(NSNotification *)n
{
	NSSize s = [g_view bounds].size;
	push (0, GUI_EVENT_DISPLAY_RESIZE, ((long) (int) s.width << 16) | (int) s.height);
	[g_view setNeedsDisplay:YES];
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)a { g_quit = true; return NSTerminateNow; }
- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)a { return YES; }
- (void)application:(NSApplication *)a openURLs:(NSArray<NSURL *> *)urls
{
	std::string all;
	for (NSURL *u in urls) if ([u isFileURL]) { if (!all.empty ()) all += '\n'; all += onyx_path ([[u path] UTF8String]); }
	if (all.empty ()) return;
	drop_text () = all;					// (as a file dropped on the window: the app opens it)
	push (0, GUI_EVENT_DROP, ((long) DND_F_COPY << 32) | (10l << 16) | 10);
	if (g_win) [g_win makeKeyAndOrderFront:nil];
}
@end

static NSString *app_name ()
{
	NSString *n = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"];
	return [n length] ? n : [NSString stringWithUTF8String:g_title];
}
static NSMenuItem *add_item (NSMenu *m, NSString *title, SEL action, NSString *key, id target)
{
	NSMenuItem *i = [m addItemWithTitle:title action:action keyEquivalent:key ?: @""];
	if (target) [i setTarget:target];
	return i;
}
// the menu bar: the application menu, the app's ("M<title>" / "I<id>\t<label>\t<shortcut>" / "-"), Window, Help
static void apply_menu ()
{
	if (!NSApp) return;
	NSMenu *bar = [[NSMenu alloc] init];
	NSString *name = app_name ();
	NSMenuItem *ai = [bar addItemWithTitle:@"" action:NULL keyEquivalent:@""];
	NSMenu *am = [[NSMenu alloc] initWithTitle:name];
	add_item (am, [@"About " stringByAppendingString:name], @selector (orderFrontStandardAboutPanel:), nil, nil);
	[am addItem:[NSMenuItem separatorItem]];
	NSMenu *services = [[NSMenu alloc] init];
	[[am addItemWithTitle:@"Services" action:NULL keyEquivalent:@""] setSubmenu:services];
	[NSApp setServicesMenu:services];
	[am addItem:[NSMenuItem separatorItem]];
	add_item (am, [@"Hide " stringByAppendingString:name], @selector (hide:), @"h", nil);
	[add_item (am, @"Hide Others", @selector (hideOtherApplications:), @"h", nil) setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
	add_item (am, @"Show All", @selector (unhideAllApplications:), nil, nil);
	[am addItem:[NSMenuItem separatorItem]];
	add_item (am, [@"Quit " stringByAppendingString:name], @selector (onyxQuit:), @"q", g_delegate);
	[ai setSubmenu:am];

	const std::string &spec = menu_spec ();
	NSMenu *cur = nil;
	size_t i = 0;
	while (i < spec.size ())
	{
		size_t j = spec.find ('\n', i); if (j == std::string::npos) j = spec.size ();
		std::string l = spec.substr (i, j - i); i = j + 1;
		if (l.empty ()) continue;
		if (l[0] == 'M')
		{
			cur = [[NSMenu alloc] initWithTitle:ns (l.substr (1))];
			[cur setAutoenablesItems:NO];
			[[bar addItemWithTitle:ns (l.substr (1)) action:NULL keyEquivalent:@""] setSubmenu:cur];
		}
		else if (l[0] == '-' && cur) [cur addItem:[NSMenuItem separatorItem]];
		else if (l[0] == 'I' && cur)
		{
			size_t t1 = l.find ('\t'); if (t1 == std::string::npos) continue;
			size_t t2 = l.find ('\t', t1 + 1);
			int id = atoi (l.c_str () + 1);
			std::string label = l.substr (t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1);
			std::string sc = t2 == std::string::npos ? "" : l.substr (t2 + 1);
			NSString *key = @"";
			if (sc.size () == 2 && sc[0] == '^' && isalpha ((unsigned char) sc[1])) key = [NSString stringWithFormat:@"%c", tolower ((unsigned char) sc[1])];
			NSMenuItem *it = add_item (cur, ns (label), @selector (onyxMenu:), key, g_delegate);	// (shown as Cmd+key: the view
			[it setTag:id];											//  hands the key to the app first)
		}
	}

	NSMenuItem *wi = [bar addItemWithTitle:@"Window" action:NULL keyEquivalent:@""];
	NSMenu *wm = [[NSMenu alloc] initWithTitle:@"Window"];
	add_item (wm, @"Minimize", @selector (performMiniaturize:), @"m", nil);
	add_item (wm, @"Zoom", @selector (performZoom:), nil, nil);
	[wm addItem:[NSMenuItem separatorItem]];
	[add_item (wm, @"Enter Full Screen", @selector (toggleFullScreen:), @"f", nil) setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagControl];
	[wm addItem:[NSMenuItem separatorItem]];
	add_item (wm, @"Bring All to Front", @selector (arrangeInFront:), nil, nil);
	[wi setSubmenu:wm];
	[NSApp setWindowsMenu:wm];

	// Help: the app's manual on the card (SD:/manuals/<app>/<App>[.fr|.nl].pdf, the Mac's language first)
	std::string app = g_title;
	for (auto &c : app) c = (char) tolower ((unsigned char) c);
	std::string App = g_title;
	NSString *lang = [[NSLocale preferredLanguages] firstObject] ?: @"en";
	std::string lg = [[lang substringToIndex:MIN ((NSUInteger) 2, [lang length])] UTF8String];
	std::vector<std::string> tries;
	if (lg == "fr" || lg == "nl") tries.push_back ("SD:/manuals/" + app + "/" + App + "." + lg + ".pdf");
	tries.push_back ("SD:/manuals/" + app + "/" + App + ".pdf");
	for (auto &t : tries)
	{
		std::string h = host_path (t.c_str ());
		if (access (h.c_str (), R_OK) != 0) continue;
		NSMenuItem *hi = [bar addItemWithTitle:@"Help" action:NULL keyEquivalent:@""];
		NSMenu *hm = [[NSMenu alloc] initWithTitle:@"Help"];
		NSMenuItem *it = add_item (hm, [name stringByAppendingString:@" Manual"], @selector (onyxManual:), @"?", g_delegate);
		[it setRepresentedObject:ns (h)];
		[hi setSubmenu:hm];
		[NSApp setHelpMenu:hm];
		break;
	}
	[NSApp setMainMenu:bar];
}

static void app_init ()
{
	if (g_delegate) return;
	[NSApplication sharedApplication];
	[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
	g_delegate = [[OnyxDelegate alloc] init];
	[NSApp setDelegate:g_delegate];
	apply_menu ();
	[NSApp finishLaunching];
}

// ---- the window ----------------------------------------------------------------------------------------------------
static unsigned *make_canvas (int w, int h, unsigned fill)
{
	unsigned *px = (unsigned *) malloc ((size_t) w * h * 4);
	if (!px) return 0;
	for (long i = 0; i < (long) w * h; i++) px[i] = fill;
	return px;
}
static unsigned *create_ex (int x, int y, int w, int h, const char *title, unsigned flags)
{
	(void) x; (void) y; (void) flags;
	if (g_win || w < 1 || h < 1) return 0;				// (one window a process)
	snprintf (g_title, sizeof g_title, "%s", title ? title : "Onyx");
	@autoreleasepool
	{
		app_init ();
		g_canvas = make_canvas (w, h, 0x1B1E24); g_cw = w; g_ch = h; g_stride = w;
		if (!g_canvas) return 0;
		NSRect r = NSMakeRect (0, 0, w, h);
		g_win = [[NSWindow alloc] initWithContentRect:r
			styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
			backing:NSBackingStoreBuffered defer:NO];
		[g_win setReleasedWhenClosed:NO];
		[g_win setTitle:[NSString stringWithUTF8String:g_title] ?: @"Onyx"];
		g_view = [[OnyxView alloc] initWithFrame:r];
		[g_win setContentView:g_view];
		[g_win setContentMinSize:NSMakeSize (w < 900 ? w : 900, h < 600 ? h : 600)];
		[g_win setAcceptsMouseMovedEvents:YES];
		[g_win setDelegate:g_delegate];
		[g_win center];
		[g_win setFrameAutosaveName:[NSString stringWithFormat:@"Onyx %s", g_title]];	// (its last place and size)
		[g_view registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
		[g_win makeKeyAndOrderFront:nil];
		[g_win makeFirstResponder:g_view];
#if defined (__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
		if (@available (macOS 14.0, *)) [NSApp activate];
		else
#endif
		[NSApp activateIgnoringOtherApps:YES];
		apply_menu ();						// (the title known: Help's manual)
	}
	push (0, GUI_EVENT_WINCTL, KAPI_FRAME_MAXIMISE);		// the app fills the window (and follows it)
	return g_canvas;
}
static unsigned *create (int w, int h, const char *t) { return create_ex (0, 0, w, h, t, 0); }
static unsigned *resize2 (int w, int h, int *stride)
{
	if (w < 1 || h < 1) return 0;
	unsigned *px = make_canvas (w, h, g_canvas ? g_canvas[(size_t) (g_ch - 1) * g_stride + g_cw - 1] : 0x1B1E24);
	if (!px) return 0;
	unsigned *old = g_canvas;
	g_canvas = px; g_cw = w; g_ch = h; g_stride = w;
	free (old);
	if (stride) *stride = w;
	return px;
}
static unsigned *resize (int w, int h) { return resize2 (w, h, 0); }
static void move_window (int, int) { }
static void h_present (void)
{
	if (!g_view) return;
	@autoreleasepool { [g_view setNeedsDisplay:YES]; [g_view displayIfNeeded]; }
}
static void screen_size (int *w, int *h)
{
	NSRect r = [[NSScreen mainScreen] visibleFrame];
	if (w) *w = (int) r.size.width;
	if (h) *h = (int) r.size.height;
}
static int win_geometry (struct kapi_win_geom *g)
{
	if (!g_win) return -1;
	NSSize s = [g_view bounds].size;
	g->x = 0; g->y = 0; g->w = g_cw; g->h = g_ch; g->cw = g_cw; g->ch = g_ch;	// (no frame of Onyx's)
	g->ax = 0; g->ay = 0; g->aw = (int) s.width; g->ah = (int) s.height;	// the work area: the window's content
	g->state = [g_win isKeyWindow] ? KAPI_WIN_KEYS : 0;
	return 0;
}
static int win_minimise (unsigned) { if (g_win) [g_win miniaturize:nil]; return 0; }
static int desk (int, int) { return 1 << 8 | 0; }
static int win_desk (unsigned, int) { return 0; }
static int get_chrome (struct kapi_chrome *o)
{
	if (!g_win) return 0;
	memset (o, 0, sizeof *o);
	o->content = g_canvas; o->content_w = g_cw; o->content_h = g_ch;	// active = 0: no Onyx frame drawn
	snprintf (o->title, sizeof o->title, "%s", g_title);
	return 1;
}
static void set_ptr (gui_handler f) { g_ptr = f; }
static void set_key (gui_handler f) { g_key = f; }
static void set_click (gui_handler f) { g_click = f; }
static unsigned get_mods (void) { return (unsigned) (g_dispMods >= 0 ? g_dispMods : live_mods ()); }
static void cursor_pos (int *x, int *y)
{
	int px = 0, py = 0;
	if (g_win)
	{
		NSPoint p = [g_view convertPoint:[g_win mouseLocationOutsideOfEventStream] fromView:nil];
		px = (int) floor (p.x); py = (int) floor (p.y);
	}
	if (x) *x = px; if (y) *y = py;
}
static int font_w (void) { return 8; }
static int font_h (void) { return 16; }
static void draw_text_buf (unsigned *, int, int, int, int, const char *, unsigned) { }
static void draw_text (int, int, const char *, unsigned) { }

static void dispatch ()
{
	std::vector<Ev> q; q.swap (evq ());
	for (const Ev &e : q)
	{
		g_dispMods = e.mods;
		if (e.to == 0 && g_ptr) g_ptr (0, e.ev, (gui_value) e.v);
		else if (e.to == 1 && g_key) g_key (0, e.ev, (gui_value) e.v);
		else if (e.to == 2 && g_menuFn) g_menuFn (0, e.ev, (gui_value) e.v);
		else if (e.to == 3 && g_click) g_click (0, e.ev, (gui_value) e.v);
		g_dispMods = -1;
	}
	std::vector<Post> p;
	pthread_mutex_lock (&g_postLock); p.swap (posts ()); pthread_mutex_unlock (&g_postLock);
	for (const Post &x : p) x.fn (x.ctx, x.v);
}
static void pump (void)
{
	if (g_delegate)
	{
		@autoreleasepool
		{
			NSEvent *e;
			while ((e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES]))
				[NSApp sendEvent:e];
			[NSApp updateWindows];
		}
	}
	dispatch ();
}
static int pump_wait (unsigned timeout)
{
	pthread_mutex_lock (&g_postLock); bool idle = posts ().empty () && evq ().empty (); pthread_mutex_unlock (&g_postLock);
	if (idle && g_delegate)			// (an event, or the time out: macOS's own wait)
	{
		@autoreleasepool
		{
			NSDate *until = timeout == KAPI_WAIT_FOREVER ? [NSDate distantFuture] : [NSDate dateWithTimeIntervalSinceNow:timeout / 1000.0];
			NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:until inMode:NSDefaultRunLoopMode dequeue:YES];
			if (e) [NSApp sendEvent:e];
		}
	}
	else if (idle && timeout) usleep ((useconds_t) (timeout == KAPI_WAIT_FOREVER || timeout > 10 ? 10 : timeout) * 1000);
	pump ();
	return 0;
}
static int post (void (*fn) (void *, long), void *ctx, long v)
{
	pthread_mutex_lock (&g_postLock); posts ().push_back (Post { fn, ctx, v }); pthread_mutex_unlock (&g_postLock);
	if (g_delegate)		// (the main thread woken if it waits for an event)
	{
		@autoreleasepool
		{
			NSEvent *e = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0
						    windowNumber:0 context:nil subtype:0 data1:0 data2:0];
			[NSApp postEvent:e atStart:NO];
		}
	}
	return 0;
}
static int h_should_exit (void) { return g_quit ? 1 : 0; }

static int set_menu (const char *spec, gui_handler fn) { menu_spec () = spec ? spec : ""; g_menuFn = fn; apply_menu (); return 1; }
static unsigned get_menu (char *, unsigned, char *, unsigned) { return 0; }
static int menu_command (int id)
{
	if (id == MENU_QUIT) { g_quit = true; return 1; }
	push (2, GUI_EVENT_MENU, id);
	return 1;
}
static int drag_data (int *type, void *buf, unsigned cap)
{
	if (type) *type = DND_FILES;
	const std::string &d = drop_text ();
	unsigned n = (unsigned) d.size ();
	memcpy (buf, d.data (), n < cap ? n : cap);
	return (int) n;
}
static int drag_begin (int, const void *, unsigned, const char *) { return 0; }

// ---- the clipboard: the Mac's ---------------------------------------------------------------------------------------
static int clipboard_set (int type, const void *data, unsigned len)
{
	@autoreleasepool
	{
		NSPasteboard *pb = [NSPasteboard generalPasteboard];
		[pb clearContents];
		if (!data || !len) return 1;
		NSString *s = [[NSString alloc] initWithBytes:data length:len encoding:NSUTF8StringEncoding];
		if (!s) s = [[NSString alloc] initWithBytes:data length:len encoding:NSISOLatin1StringEncoding];
		if (type == CLIP_FILES || type == CLIP_FILES_CUT)		// (paths: the Finder's files)
		{
			NSMutableArray *urls = [NSMutableArray array];
			for (NSString *l in [s componentsSeparatedByString:@"\n"])
			{
				std::string h = host_path ([l UTF8String]);
				if (!h.empty () && access (h.c_str (), F_OK) == 0) [urls addObject:[NSURL fileURLWithPath:ns (h)]];
			}
			if ([urls count]) return [pb writeObjects:urls] ? 1 : 0;
		}
		return [pb setString:s forType:NSPasteboardTypeString] ? 1 : 0;
	}
}
static int clipboard_get (int *type, void *buf, unsigned cap, unsigned *serial)
{
	@autoreleasepool
	{
		NSPasteboard *pb = [NSPasteboard generalPasteboard];
		if (serial) *serial = (unsigned) [pb changeCount];
		if (type) *type = 0;
		NSString *s = [pb stringForType:NSPasteboardTypeString];
		if (![s length]) return 0;
		std::string o;
		for (const char *p = [s UTF8String]; p && *p; p++) if (*p != '\r') o += *p;
		if (type) *type = CLIP_TEXT;
		memcpy (buf, o.data (), o.size () < cap ? o.size () : cap);
		return (int) o.size ();
	}
}

void gui_fatal (const char *msg)
{
	fprintf (stderr, "onyx: %s\n", msg);
	@autoreleasepool
	{
		[NSApplication sharedApplication];
		NSAlert *a = [[NSAlert alloc] init];
		[a setMessageText:@"Onyx"];
		[a setInformativeText:[NSString stringWithUTF8String:msg] ?: @""];
		[a setAlertStyle:NSAlertStyleCritical];
		[a runModal];
	}
	_exit (2);
}

void gui_setup (TKApiTable *T)
{
	T->create_window = create; T->create_window_ex = create_ex; T->resize_window = resize; T->resize_window2 = resize2; T->move_window = move_window;
	T->set_pointer_handler = set_ptr; T->set_key_handler = set_key; T->set_click_handler = set_click; T->screen_size = screen_size;
	T->font_width = font_w; T->font_height = font_h; T->present = h_present; T->pump_events = pump; T->pump_wait = pump_wait; T->post = post;
	T->should_exit = h_should_exit;
	T->draw_text = draw_text; T->draw_text_buf = draw_text_buf; T->get_chrome = get_chrome; T->win_geometry = win_geometry;
	T->win_minimise = win_minimise; T->desk = desk; T->win_desk = win_desk; T->cursor_pos = cursor_pos; T->get_modifiers = get_mods;
	T->set_menu = set_menu; T->get_menu = get_menu; T->menu_command = menu_command; T->drag_data = drag_data; T->drag_begin = drag_begin;
	T->clipboard_set = clipboard_set; T->clipboard_get = clipboard_get;
}
