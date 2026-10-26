//
// pc/3DForge/core/forgecore.cpp -- 3DForge for Windows: the core the WPF program (pc/3DForge/*.cs) drives through a C
// interface, forgecore.dll. It is Onyx's 3DForge itself (user/Apps/3dforge: the document and its history replayed by
// Manifold, the sketch, the fillets, Manufacture -- the router's G-code, the resin printer's layers, the filament
// printer's path --, the view and its tools, fview.h, compiled as they are over forgeshim.h); what is new here is the
// window's part that UIKit drew on Onyx (fui.h, main.cpp) told to the host instead:
//
//   * the view: drawn into the host's pixels (fc_view_draw), its mouse (fc_view_mouse); the frame's triangles by the
//     processor on every core (kapi_gpu_render below: frender.h's z-buffer, cut in bands of rows);
//   * the right panel: its controls as a list of items (fc_form: what fui.h's Props built), the host makes them with
//     WPF and gives back what is typed, checked, chosen (fc_form_*);
//   * the timeline, the tools' bar, the status: their state (fc_tl_*, fc_state, fc_text);
//   * the commands (fc_cmd: main.cpp's cmd), the files, the exports, the G-code and the print file (the dialogs are
//     the host's; what they show is computed here).
//
// Strings are UTF-8 both ways; a string is returned into (buf, cap), the result its length.
//
// MIT licence (Onyx).
//
#define _USE_MATH_DEFINES
#include <math.h>
#include <thread>
#include <vector>
#include <string>
#include <algorithm>
#include "fview.h"
#include "fdraw.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef MOD_CTRL
#undef MOD_SHIFT
#undef MOD_ALT

using namespace uikit;
using namespace forge;

#define FC extern "C" __declspec (dllexport)

// ---- the host's ---------------------------------------------------------------------------------------------------------
static int g_mods;
int kapi_get_modifiers () { return g_mods; }
static int g_threads = 1;
int kapi_gpu_info (char *out, int cap) { if (out && cap > 0) snprintf (out, cap, "processor, %d threads", g_threads); return 1; }

// frender.h's soft_render, for the rows [ya, yb) of the target: each band drawn by a thread of its own (a pixel is
// only ever touched by its band's, in the batches' order: the same picture as one thread's).
static void raster_band (const kapi_gpu_frame *f, const kapi_gpu_vertex3 *vv, const kapi_gpu_batch *bb, unsigned nb, float *zb, int ya, int yb)
{
	int W = f->w, H = f->h;
	for (int y = ya; y < yb; y++) { unsigned *r = f->pixels + (size_t) y * f->stride; float *z = zb + (size_t) y * W; for (int x = 0; x < W; x++) { r[x] = f->clear; z[x] = 2; } }
	for (unsigned bi = 0; bi < nb; bi++)
	{
		const kapi_gpu_batch &bt = bb[bi];
		int zf = bt.flags & 7; bool zwrite = !(bt.flags & KAPI_GPU_B_NOZWRITE), cull = (bt.flags & KAPI_GPU_B_CULL_BACK) != 0;
		bool blend = ((bt.flags >> 8) & 15) == KAPI_GPU_BLEND_ALPHA, mat = !(bt.flags & KAPI_GPU_B_NOMATRIX);
		const float *m = bt.matrix;
		for (unsigned i = bt.first; i + 2 < bt.first + bt.count; i += 3)
		{
			float X[3], Y[3], Z[3];
			for (int k = 0; k < 3; k++)
			{
				const kapi_gpu_vertex3 &q = vv[i + k]; float x = q.x, y = q.y, z = q.z;
				if (mat) { x = m[0] * q.x + m[1] * q.y + m[2] * q.z + m[3]; y = m[4] * q.x + m[5] * q.y + m[6] * q.z + m[7]; z = m[8] * q.x + m[9] * q.y + m[10] * q.z + m[11]; }
				X[k] = (x * 0.5f + 0.5f) * W; Y[k] = (0.5f - y * 0.5f) * H; Z[k] = z;
			}
			float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
			if (area == 0 || (cull && area > 0)) continue;
			int y0 = (int) floorf (std::min (Y[0], std::min (Y[1], Y[2]))), y1 = (int) ceilf (std::max (Y[0], std::max (Y[1], Y[2])));
			if (y0 < ya) y0 = ya; if (y1 > yb - 1) y1 = yb - 1;
			if (y0 > y1) continue;
			int x0 = (int) floorf (std::min (X[0], std::min (X[1], X[2]))), x1 = (int) ceilf (std::max (X[0], std::max (X[1], X[2])));
			if (x0 < 0) x0 = 0; if (x1 > W - 1) x1 = W - 1;
			if (x0 > x1) continue;
			const kapi_gpu_vertex3 &c0 = vv[i], &c1 = vv[i + 1], &c2 = vv[i + 2];
			bool flatc = c0.r == c1.r && c0.g == c1.g && c0.b == c1.b && c0.r == c2.r && c0.g == c2.g && c0.b == c2.b && c0.a == c1.a && c0.a == c2.a;
			float inv = 1 / area;
			for (int y = y0; y <= y1; y++)
			{
				float py = y + 0.5f; unsigned *row = f->pixels + (size_t) y * f->stride; float *zr = zb + (size_t) y * W;
				// (the row's span inside the triangle, from where its edges cross the row: a thin line's quad -- a
				//  grid's line across the view -- is not tested over its whole box)
				float xl = 1e30f, xr = -1e30f;
				for (int e = 0; e < 3; e++)
				{
					float ax = X[e], ay = Y[e], bx = X[(e + 1) % 3], by = Y[(e + 1) % 3];
					if ((py < ay && py < by) || (py > ay && py > by)) continue;
					float x = ay == by ? (py == ay ? std::min (ax, bx) : 1e30f) : ax + (py - ay) * (bx - ax) / (by - ay);
					float x2 = ay == by && py == ay ? std::max (ax, bx) : x;
					if (x < xl) xl = x; if (x2 > xr) xr = x2;
				}
				if (xl > xr) continue;
				int sx0 = std::max (x0, (int) floorf (xl - 0.5f)), sx1 = std::min (x1, (int) ceilf (xr - 0.5f));
				for (int x = sx0; x <= sx1; x++)
				{
					float px = x + 0.5f;
					float w0 = ((X[1] - px) * (Y[2] - py) - (X[2] - px) * (Y[1] - py)) * inv;
					float w1 = ((X[2] - px) * (Y[0] - py) - (X[0] - px) * (Y[2] - py)) * inv;
					float w2 = 1 - w0 - w1;
					if (w0 < 0 || w1 < 0 || w2 < 0) continue;
					float z = w0 * Z[0] + w1 * Z[1] + w2 * Z[2]; float &zz = zr[x];
					if (z < -1 || z > 1) continue;
					if (zf == KAPI_GPU_Z_LESS ? !(z < zz) : zf == KAPI_GPU_Z_LEQUAL ? !(z <= zz) : false) continue;
					int r, g, b, a;
					if (flatc) { r = c0.r; g = c0.g; b = c0.b; a = c0.a; }
					else
					{
						r = (int) (w0 * c0.r + w1 * c1.r + w2 * c2.r); g = (int) (w0 * c0.g + w1 * c1.g + w2 * c2.g);
						b = (int) (w0 * c0.b + w1 * c1.b + w2 * c2.b); a = (int) (w0 * c0.a + w1 * c1.a + w2 * c2.a);
					}
					unsigned &d = row[x];
					if (blend && a < 255)
					{
						int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
						r = (r * a + dr * (255 - a)) / 255; g = (g * a + dg * (255 - a)) / 255; b = (b * a + db * (255 - a)) / 255;
					}
					d = (unsigned) (r << 16 | g << 8 | b);
					if (zwrite) zz = z;
				}
			}
		}
	}
}
#include <chrono>
static double now_ms () { return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now ().time_since_epoch ()).count (); }
static double g_t0;
int kapi_gpu_render (const kapi_gpu_frame *f, const kapi_gpu_vertex3 *v, unsigned, const kapi_gpu_batch *b, unsigned nb)
{
	g_t0 = now_ms ();
	static std::vector<float> zb;
	if ((int) zb.size () < f->w * f->h) zb.resize ((size_t) f->w * f->h);
	int n = g_threads, band = (f->h + n - 1) / n;
	if (n <= 1) { raster_band (f, v, b, nb, &zb[0], 0, f->h); return 0; }
	std::vector<std::thread> t;
	for (int k = 0; k < n; k++)
	{
		int ya = k * band, yb = std::min (f->h, ya + band);
		if (ya < yb) t.emplace_back (raster_band, f, v, b, nb, &zb[0], ya, yb);
	}
	for (std::thread &q : t) q.join ();
	if (getenv ("FORGE_PROFILE")) fprintf (stderr, "raster %.1f ms\n", now_ms () - g_t0);
	return 0;
}

// ---- files: UTF-8 paths ----------------------------------------------------------------------------------------------------
static std::wstring wpath (const char *s)
{
	int n = MultiByteToWideChar (CP_UTF8, 0, s, -1, 0, 0); std::wstring w (n > 0 ? n - 1 : 0, 0);
	if (n > 1) MultiByteToWideChar (CP_UTF8, 0, s, -1, &w[0], n);
	return w;
}
static bool read_text (const char *path, std::string &s)
{
	FILE *f = _wfopen (wpath (path).c_str (), L"rb"); if (!f) return false;
	s.clear (); char b[65536]; size_t n;
	while ((n = fread (b, 1, sizeof b, f)) > 0) s.append (b, n);
	fclose (f); return true;
}
static bool write_file (const char *path, const void *data, size_t n)
{
	FILE *f = _wfopen (wpath (path).c_str (), L"wb"); if (!f) return false;
	bool ok = fwrite (data, 1, n, f) == n; ok = fclose (f) == 0 && ok;
	return ok;
}
static const char *base_name (const char *p) { const char *b = p; for (const char *q = p; *q; q++) if (*q == '/' || *q == '\\' || *q == ':') b = q + 1; return b; }
static int put (const std::string &s, char *buf, int cap)
{
	if (buf && cap > 0) { size_t n = std::min (s.size (), (size_t) cap - 1); memcpy (buf, s.data (), n); buf[n] = 0; }
	return (int) s.size ();
}

// ---- the window's state ------------------------------------------------------------------------------------------------------
static View *g_view;
static int g_flags;				// what changed since the host last asked (FL_*)
enum { FL_FIELDS = 1, FL_PANEL = 2, FL_UI = 4, FL_VIEW = 8, FL_PRINTFILE = 16 };
static std::string g_cfg, g_printers;		// the folder of the presets (cam.ini, print.ini, filament.ini); the printers' list
static std::string cfg (const char *name) { return g_cfg + "\\" + name; }

static void app_refresh (int what)
{
	g_flags |= FL_UI | FL_VIEW;
	if (what & R_PANELS) g_flags |= FL_PANEL;
	if (what & R_FIELDS) g_flags |= FL_FIELDS;
}
static void view_dirty () { if (g_view) g_view->invalidate (true); g_flags |= FL_VIEW; }

namespace forge {
static void fdm_presets_save () { A.fdm.infill = A.fdmPct / 100; std::string s = fdm_save (A.fdm, A.fmach); write_file (cfg ("filament.ini").c_str (), s.data (), s.size ()); }
static void print_presets_save () { PrintSetup t; t.on = true; t.resin = A.print.resin; t.machine = A.print.machine; std::string s = print_save (t); write_file (cfg ("print.ini").c_str (), s.data (), s.size ()); }
static void cam_presets_save () { std::string s = cam_presets (A.job); write_file (cfg ("cam.ini").c_str (), s.data (), s.size ()); }
}

// A part without a setup yet: Design, the tool and the machine as they were left (main.cpp's job_reset).
static void job_reset ()
{
	A.camMode = A.camSim = A.camDirty = false; A.camSel = A.camPage = 0; A.job = CamSetup (); A.paths = CamPaths ();
	std::string s; if (read_text (cfg ("cam.ini").c_str (), s)) cam_presets_take (A.job, s.c_str ());
	A.gen = 0; A.print = PrintSetup (); A.pjob = PrintJob (); A.slicer = PrintSlicer (); A.sliced = A.printDirty = A.wantFile = A.layerPlay = A.layer3d = false; A.layer = 0;
	A.placed = A.held = Manifold (); A.placedMesh = A.supMesh = A.cutMesh = RMesh ();
	if (read_text (cfg ("print.ini").c_str (), s)) { PrintSetup t; print_load (t, s.c_str ()); A.print.resin = t.resin; A.print.machine = t.machine; }
	A.fdm = FdmSettings (); A.fmach = FDM_MACHINE0; A.fjob = FdmJob (); A.fdmReady = false; A.fdmDrawn = -1;
	if (read_text (cfg ("filament.ini").c_str (), s)) fdm_load (A.fdm, A.fmach, s.c_str ());
	A.fdmPct = A.fdm.infill * 100;
}

// ---- the commands (main.cpp's) --------------------------------------------------------------------------------------------------
enum { CMD_NEW = 100, CMD_OPEN, CMD_SAVE, CMD_UNDO, CMD_REDO, CMD_EXPORT, CMD_SK_CANCEL, CMD_SK_FINISH, CMD_SK_CLOSE, CMD_OK, CMD_CANCEL,
       CMD_DELETE, CMD_EDIT_SKETCH, CMD_ROLL_HERE, CMD_ROLL_END, CMD_DEL_ELEMENT, CMD_SHAPES, CMD_SK_START,
       CMD_MODE_DESIGN, CMD_MODE_CAM, CMD_CAM_SETUP, CMD_CAM_TOOL, CMD_CAM_CLEAR, CMD_CAM_CONTOUR, CMD_CAM_SIM, CMD_GCODE, CMD_CAM_BODY, CMD_CAM_DELETE,
       CMD_CAM_VALIDATE, CMD_CAM_REVERT, CMD_CANVAS_IMPORT, CMD_CANVAS_DELETE, CMD_PR_SETUP, CMD_PR_RESIN, CMD_PR_SUPPORTS, CMD_PR_LAYERS, CMD_PR_FILE, CMD_PR_GENERATE, CMD_PR_CLEAR, CMD_PR_PRESET, CMD_GEN, CMD_SK_CONS,
       // (the Windows version's own: the View menu's)
       CMD_HOME = 200, CMD_FIT, CMD_TOP, CMD_FRONT, CMD_RIGHT, CMD_EDGES, CMD_GRID, CMD_SEETHROUGH, CMD_SNAP, CMD_PROC_MILL, CMD_PROC_RESIN, CMD_PROC_FDM };
enum { REQ_NONE, REQ_GCODE, REQ_PRINTFILE };	// what fc_cmd asks the host to show

static void confirm ()
{
	if (A.sketching)
	{
		SkEl &e = A.cur;
		if (A.selEl >= 0 || A.curStep == 0) return;
		if (e.kind == SK_SPLINE) { sk_spline_end (); return; }
		if (e.kind == SK_ARC && A.tool == T_ARC3 && A.curStep == 1) return;
		if (e.kind == SK_ARC && A.curStep == 1) { if (e.r > 1e-6) { A.curStep = 2; memset (A.typed, 0, sizeof A.typed); A.typed[0] = true; tool_hint (); ui (R_ALL); } return; }
		bool ok = e.kind == SK_LINE ? e.len > 1e-6 : e.kind == SK_ARC ? fabs (e.sweep) > 1e-6 && e.r > 1e-6 : e.kind == SK_CIRCLE ? e.w > 1e-6 : fabs (e.w) > 1e-6 && fabs (e.h) > 1e-6;
		if (ok) sk_add (e);
		return;
	}
	if (!A.hasPend) return;
	Feature &f = A.pend;
	if (f.kind == F_BOX || round_kind (f.kind)) { g_view->shapeNext (0, 0, false); return; }
	if (f.kind == F_COMBINE || (f.kind == F_MOVE && A.step == 0)) return;
	commit_pend ();
}
static void cancel ()
{
	if (printer ()) { if (A.layerPlay) { A.layerPlay = false; A.fdmDrawn = -1; view_dirty (); ui (0); } return; }
	if (A.camMode) { if (A.camSim) { A.camSim = false; ui (R_ALL); } else cam_revert (); return; }
	if (A.sketching)
	{
		if (A.selEl >= 0) { A.selEl = -1; ui (R_ALL); return; }
		A.chain = false; sk_arm (); tool_hint (); ui (R_ALL); return;
	}
	tool_set (T_SELECT);
}
// The print file asked: once the layers are cut (main.cpp's cmd_print_file).
static int print_file ()
{
	if (!resin ()) return REQ_NONE;
	if (A.printDirty) print_refresh ();
	if (A.sliced) return REQ_PRINTFILE;
	A.wantFile = true; print_slice_start (); set_hint (TR ("Cutting the layers...")); ui (0);
	return REQ_NONE;
}
static int command (int id)
{
	int req = REQ_NONE;
	switch (id)
	{
	case CMD_UNDO: if (!A.camMode) do_undo (false); break;
	case CMD_REDO: if (!A.camMode) do_undo (true); break;
	case CMD_MODE_DESIGN: cam_enter (false); break;
	case CMD_MODE_CAM: cam_enter (true); if (A.camMode) g_view->fit (); break;
	case CMD_PR_SETUP: print_page (0); break;
	case CMD_PR_RESIN: print_page (1); break;
	case CMD_PR_SUPPORTS: print_page (2); break;
	case CMD_PR_LAYERS: print_page (3); break;
	case CMD_PR_FILE: req = print_file (); break;
	case CMD_PR_GENERATE:
		if (!resin ()) break;
		if (A.printDirty) print_refresh ();
		print_supports_auto (A.placed, A.print, A.print.tips); A.doc.changes++; print_refresh ();
		if (A.print.tips.empty ()) set_hint (TR ("Nothing hangs: no support is needed as the body is."));
		ui (R_ALL); break;
	case CMD_PR_CLEAR: if (resin ()) { A.print.tips.clear (); A.doc.changes++; print_refresh (); ui (R_ALL); } break;
	case CMD_CANVAS_DELETE:
		if (!A.camMode && !A.sketching && A.selCanvas >= 0 && A.selCanvas < (int) A.doc.canvases.size ())
		{ undo_push (); A.doc.canvases.erase (A.doc.canvases.begin () + A.selCanvas); A.selCanvas = -1; A.doc.changes++; ui (R_ALL); }
		break;
	case CMD_CAM_VALIDATE: if (A.camMode && A.gen == 0) { cam_refresh (); ui (R_ALL); } break;
	case CMD_CAM_REVERT: cam_revert (); break;
	case CMD_CAM_SETUP: if (A.camMode) { A.camSel = 0; A.camPage = 0; A.camSim = false; cam_hint (); ui (R_ALL); } break;
	case CMD_CAM_TOOL: if (A.camMode) { A.camPage = 1; A.camSim = false; cam_hint (); ui (R_ALL); } break;
	case CMD_CAM_CLEAR: if (A.camMode) cam_add (CAM_CLEAR); break;
	case CMD_CAM_CONTOUR: if (A.camMode) cam_add (CAM_CONTOUR); break;
	case CMD_CAM_SIM:
		if (!A.camMode) break;
		if (A.camDirty) cam_refresh ();
		if (A.paths.hm.empty ()) { set_hint (TR ("Add a clearing or a contour first: there is nothing to simulate.")); ui (0); break; }
		A.camSim = !A.camSim; A.simPlay = false; if (A.camSim) sim_to (A.paths.moves.size ());
		set_hint (A.camSim ? TR ("What is left of the stock. Play (at the left) runs the cut; drag the bar to go through it. Esc: back to the moves.") : ""); if (!A.camSim) cam_hint ();
		ui (R_ALL); break;
	case CMD_GCODE:
		if (A.gen == 1) req = print_file ();
		else if (A.gen == 0 && A.camMode) { if (A.camDirty) cam_refresh (); req = REQ_GCODE; }
		break;
	case CMD_CAM_DELETE: if (A.camMode) cam_delete_op (); break;
	case CMD_SK_CANCEL: sketch_end (false); break;
	case CMD_SK_FINISH: sketch_end (true); break;
	case CMD_SK_CLOSE: sk_close (); break;
	case CMD_SK_CONS: sk_cons (); break;
	case CMD_SK_START:
	{
		double o = A.skOffset;
		Plane pl = A.skPlane == 1 ? plane_of (V3 (0, o, 0), V3 (0, -1, 0)) : A.skPlane == 2 ? plane_of (V3 (o, 0, 0), V3 (1, 0, 0)) : plane_of (V3 (0, 0, o), V3 (0, 0, 1));
		sketch_begin (pl, -1); break;
	}
	case CMD_OK: confirm (); break;
	case CMD_CANCEL: cancel (); break;
	case CMD_DELETE:
		if (printer ()) break;
		if (!A.camMode && !A.sketching && A.selCanvas >= 0 && A.selFeat < 0 && A.selBody < 0) { command (CMD_CANVAS_DELETE); break; }
		if (A.camMode) cam_delete_op (); else if (!A.sketching && A.selFeat >= 0) delete_feature (A.selFeat);
		break;
	case CMD_DEL_ELEMENT:
		if (A.sketching && A.selEl >= 0 && A.selEl < (int) A.sk.els.size ()) { A.sk.els.erase (A.sk.els.begin () + A.selEl); A.selEl = -1; sk_eval (); A.chain = false; sk_arm (); ui (R_ALL); }
		break;
	case CMD_EDIT_SKETCH:
		if (!A.camMode && !A.sketching && A.selFeat >= 0 && A.doc.feats[A.selFeat].kind == F_SKETCH) sketch_begin (A.doc.feats[A.selFeat].pl, A.doc.feats[A.selFeat].target, A.selFeat);
		break;
	case CMD_ROLL_HERE: if (!A.camMode && A.selFeat >= 0) { A.doc.upto = A.selFeat + 1; A.doc.touch (); ui (R_ALL); } break;
	case CMD_ROLL_END: if (A.camMode) break; A.doc.upto = (int) A.doc.feats.size (); A.doc.touch (); ui (R_ALL); break;
	case CMD_HOME: if (A.sketching) cam_face (A.sk.pl); else g_view->home (); break;
	case CMD_FIT: g_view->fit (); break;
	case CMD_TOP: g_view->lookAlong (-90, 90); break;
	case CMD_FRONT: g_view->lookAlong (-90, 0); break;
	case CMD_RIGHT: g_view->lookAlong (0, 0); break;
	case CMD_EDGES: A.showEdges = !A.showEdges; ui (R_ALL); break;
	case CMD_GRID: A.showGrid = !A.showGrid; ui (R_ALL); break;
	case CMD_SEETHROUGH: A.seeThrough = !A.seeThrough; ui (R_ALL); break;
	case CMD_SNAP: A.snap = !A.snap; ui (R_ALL); break;
	case CMD_PROC_MILL: gen_set (0); if (A.camMode) g_view->fit (); break;
	case CMD_PROC_RESIN: gen_set (1); if (A.camMode) g_view->fit (); break;
	case CMD_PROC_FDM: gen_set (2); if (A.camMode) g_view->fit (); break;
	}
	view_dirty ();
	return req;
}

// ---- the right panel: its controls, as items ------------------------------------------------------------------------------
// An item: its kind (a letter) then its fields, separated by \x1F; the items by \x1E. Colours are 6 hex digits, "" the
// theme's. The host makes a control of each, in order, and answers through fc_form_* with the ids given here.
//   H icon|title|sub0|sub1|subAt|swatch        the panel's head (icon -1 none, -2 the body's colour: swatch)
//   L text|colour|bold|small                   a line of text
//   F id|name|value|unit|focus                 a value (a number), typed in a field
//   T id|name|text                             a name, typed
//   C tag|text|checked                         a check box
//   S tag|name|selected|option|option...       a segmented choice (name "": across the panel)
//   D tag|name|selected|option|option...       a drop-down list
//   B cmd|text|accent|bottom                   a button (bottom: kept at the panel's foot)
//   P bodyName|processName                     the body cut or printed, and the process (two buttons: CMD_CAM_BODY, CMD_GEN)
//   O selected|enabled0..3                     the operation: new body, union, subtract, intersect
//   W selected|c0|c1|c2|c3|c4                  the body's colour, among five
//   R text|name|colour                         the body a step applies to
//   K key|value|keyColour                      a measure: its name, its value at the right
//   I line|line...                             a note in a box
//   X state|text|colour                        a line with a mark (state 0 a check, 1 a warning)
//   Y state|line1|colour1|line2|colour2        a summary in a box, a mark at its left
//   G selected                                 the origin among the stock's 27 points (k = i + 3 (2 - row) + 9 (2 - grid))
struct Bind { double *val; int typed; bool mag; char *name; };
static std::vector<std::string> g_items;
static std::vector<Bind> g_binds;
static Feature *g_feat; static SkEl *g_el;
static const char *CDIM = "6A6E78", *CRED = "B03A30";

static std::string hex (unsigned c) { char t[8]; snprintf (t, sizeof t, "%06X", c & 0xFFFFFF); return t; }
static void item (char kind, std::initializer_list<std::string> f)
{
	std::string s (1, kind);
	for (const std::string &x : f) { s += '\x1F'; s += x; }
	g_items.push_back (s);
}
static void item_v (char kind, const std::vector<std::string> &f)
{
	std::string s (1, kind);
	for (const std::string &x : f) { s += '\x1F'; s += x; }
	g_items.push_back (s);
}
static std::string num (double v) { char t[32]; fmt (v, t); return t; }
static void head (int ic, const char *t, const char *s0 = 0, const char *s1 = 0, int at = -1, unsigned sw = 0)
{ item ('H', { std::to_string (ic), t, s0 ? s0 : "", s1 ? s1 : "", std::to_string (at), hex (sw) }); }
static void label (const char *s, const char *col = "", bool bold = false, bool small = false) { item ('L', { s, col, bold ? "1" : "0", small ? "1" : "0" }); }
static void section (const char *s) { label (s, "", true); }
static void note (const char *s) { label (s, CDIM, false, true); }
static int field (const char *name, double *val, const char *unit, int typed, bool mag = false, bool focus = false)
{
	Bind b = { val, typed, mag, 0 }; g_binds.push_back (b); int id = (int) g_binds.size () - 1;
	item ('F', { std::to_string (id), name, num (mag ? fabs (*val) : *val), unit ? unit : "", focus ? "1" : "0" });
	return id;
}
static void textbind (const char *name, char *text)
{
	Bind b = { 0, -1, false, text }; g_binds.push_back (b);
	item ('T', { std::to_string ((int) g_binds.size () - 1), name, text });
}
static void check (const char *s, bool on, int tag) { item ('C', { std::to_string (tag), s, on ? "1" : "0" }); }
static void seg (int tag, const char *name, int sel, std::initializer_list<const char *> opts)
{
	std::vector<std::string> f = { std::to_string (tag), name ? name : "", std::to_string (sel) };
	for (const char *o : opts) f.push_back (o);
	item_v ('S', f);
}
static void drop (int tag, const char *name, int sel, std::initializer_list<const char *> opts)
{
	std::vector<std::string> f = { std::to_string (tag), name, std::to_string (sel) };
	for (const char *o : opts) f.push_back (o);
	item_v ('D', f);
}
static void drop_v (int tag, const char *name, int sel, const std::vector<std::string> &opts)
{
	std::vector<std::string> f = { std::to_string (tag), name, std::to_string (sel) };
	f.insert (f.end (), opts.begin (), opts.end ());
	item_v ('D', f);
}
static std::vector<int> g_loftIdx;		// (a loft's lists: their lines -> the steps)
static void button (int cmd, const char *text, bool accent = false, bool bottom = false) { item ('B', { std::to_string (cmd), text, accent ? "1" : "0", bottom ? "1" : "0" }); }
static void kv (const char *k, const char *v, const char *kc = CDIM) { item ('K', { k, v, kc }); }
static void mark (bool ok, const char *text, const char *col) { item ('X', { ok ? "0" : "1", text, col }); }
static void summary (bool ok, const char *l1, const char *c1, const char *l2, const char *c2) { item ('Y', { ok ? "0" : "1", l1, c1, l2 ? l2 : "", c2 ? c2 : CDIM }); }
static void info (std::initializer_list<const char *> lines) { std::vector<std::string> f; for (const char *l : lines) f.push_back (l); item_v ('I', f); }

static bool canvasPage () { return !A.camMode && !A.sketching && !A.hasPend && A.tool == T_SELECT && A.selCanvas >= 0 && A.selCanvas < (int) A.doc.canvases.size () && A.selFeat < 0 && A.selBody < 0; }
static void bodyProcess (int body) { item ('P', { body_name (body), A.gen == 2 ? TR ("Filament") : A.gen == 1 ? TR ("Resin print") : TR ("Milling") }); }

// The fields of a step (the one being made, or one of the history): fui.h's featureFields.
static void featureFields (Feature &f, bool making)
{
	char t[96];
	switch (f.kind)
	{
	case F_BOX:
		field (TR ("Width"), &f.w, TR ("mm"), 0, true, making && A.step != 2); field (TR ("Depth"), &f.d, TR ("mm"), 1, true);
		field (TR ("Height"), &f.h, TR ("mm"), 2, true, making && A.step == 2); break;
	case F_CYL:
		field (TR ("Diameter"), &f.w, TR ("mm"), 0, false, making && A.step != 2); field (f.h < 0 || f.through ? TR ("Depth") : TR ("Height"), &f.h, TR ("mm"), 1, true, making && A.step == 2);
		check (TR ("Through the whole body"), f.through, 2); break;
	case F_PYRAMID: case F_PRISM: case F_TAPER:
	{
		bool top = f.kind == F_TAPER;
		field (TR ("Sides"), &f.n, 0, -1, false, making && A.step <= 1);
		field (TR ("Base radius"), &f.w, TR ("mm"), 0);
		if (top) field (TR ("Top radius"), &f.d, TR ("mm"), 2, false, making && A.step == 2);
		field (TR ("Height"), &f.h, TR ("mm"), 1, true, making && A.step > 1 && !(A.step == 2 && top));
		break;
	}
	case F_SPHERE: field (TR ("Radius"), &f.w, TR ("mm"), 0, false, making); break;
	case F_TORUS: field (TR ("Ring radius"), &f.w, TR ("mm"), 0, false, making && A.step != 2); field (TR ("Tube radius"), &f.d, TR ("mm"), 1, false, making && A.step == 2); break;
	case F_LOFT:
	{
		// (the sketches before this step: its two ends are chosen among them)
		std::vector<std::string> nm; g_loftIdx.clear (); int s0 = 0, s1 = 0;
		int before = making ? (int) A.doc.feats.size () : A.selFeat;
		for (int i = 0; i < before && i < (int) A.doc.feats.size (); i++)
			if (A.doc.feats[i].kind == F_SKETCH) { if (i == f.sketch) s0 = (int) nm.size (); if (i == f.tool) s1 = (int) nm.size (); nm.push_back (A.doc.feats[i].name); g_loftIdx.push_back (i); }
		drop_v (3, TR ("From"), s0, nm); drop_v (4, TR ("To"), s1, nm);
		note (TR ("The largest closed outline of each sketch, joined by straight lines."));
		break;
	}
	case F_EXTRUDE:
		field (f.h < 0 || f.through ? TR ("Depth") : TR ("Height"), &f.h, TR ("mm"), 1, true, making);
		check (TR ("Through the whole body"), f.through, 2); break;
	case F_FILLET: case F_CHAMFER:
		if (making) seg (1, "", f.kind == F_FILLET ? 0 : 1, { TR ("Fillet"), TR ("Chamfer") });
		field (f.kind == F_FILLET ? TR ("Radius") : TR ("Size"), &f.r, TR ("mm"), 0, false, making);
		check (TR ("Two sizes"), f.r2 > 0, 17);		// (asymmetric: the first on the upper face -- a hole's rim's flat face --, the second on the other)
		if (f.r2 > 0) { field (f.kind == F_FILLET ? TR ("Radius 2") : TR ("Size 2"), &f.r2, TR ("mm"), -1); check (TR ("Swap the faces"), f.swap, 18); }
		snprintf (t, sizeof t, TR ("%d chosen"), (int) f.edges.size ()); kv (TR ("Edges"), t, "");
		if (f.edges.empty ()) note (TR ("Click an edge of a body"));
		else { snprintf (t, sizeof t, TR ("on %s"), body_name (f.target)); note (t); }
		if (making) info ({ TR ("Any edge can be rounded: straight ones, circles and arcs exactly, the others followed.") });
		break;
	case F_MOVE:
		section (TR ("Move"));
		field (TR ("Along X"), &f.mv.x, TR ("mm"), 0, false, making && A.step == 1); field (TR ("Along Y"), &f.mv.y, TR ("mm"), 1); field (TR ("Along Z"), &f.mv.z, TR ("mm"), 2);
		section (TR ("Turn, about its centre"));
		field (TR ("Around X"), &f.rot.x, "\xC2\xB0", -1); field (TR ("Around Y"), &f.rot.y, "\xC2\xB0", -1); field (TR ("Around Z"), &f.rot.z, "\xC2\xB0", -1);
		section (TR ("Scale (1: as it is)"));
		field (TR ("Along X"), &f.sc.x, "\xC3\x97", -1); field (TR ("Along Y"), &f.sc.y, "\xC3\x97", -1); field (TR ("Along Z"), &f.sc.z, "\xC3\x97", -1);
		check (TR ("Clone: the original stays"), f.clone, 8);
		break;
	case F_COMBINE: snprintf (t, sizeof t, TR ("%s, with another body"), TR (OP_NAME[f.op])); label (t, CDIM); break;
	case F_SKETCH: describe (A.doc, f, t, sizeof t); label (t, CDIM); break;
	}
	if (makes_solid (f.kind) && f.kind != F_EXTRUDE && f.kind != F_LOFT)		// where it is, on its plane
	{
		section (TR ("Place"));
		field (f.kind == F_BOX && !f.centred ? TR ("Corner X") : TR ("Centre X"), &f.x, TR ("mm"), -1); field ("Y", &f.y, TR ("mm"), -1);
		field (f.target >= 0 ? TR ("Off the face") : f.pl.n.z > 0.9999 && fabs (f.pl.o.z) < 1e-9 ? "Z" : TR ("Off the plane"), &f.z, TR ("mm"), -1);
		if (f.kind == F_BOX || ((f.kind == F_PYRAMID || f.kind == F_PRISM || f.kind == F_TAPER) && f.n >= 2.5)) field (TR ("Turned"), &f.turn, "\xC2\xB0", 3);
	}
	if (makes_solid (f.kind))
	{
		std::vector<std::string> o = { std::to_string (f.op) };
		for (int i = 0; i < 4; i++) o.push_back (i == OP_NEW || A.doc.body (f.target) || !A.doc.bodies.empty () ? "1" : "0");
		item_v ('O', o);
		if (f.op != OP_NEW && A.doc.body (f.target))
			item ('R', { f.op == OP_UNION ? TR ("Joined to") : f.op == OP_SUB ? TR ("Cut from") : TR ("Kept inside"), body_name (f.target), hex (A.doc.prop (f.target).colour) });
		if (f.kind == F_BOX) check (TR ("Centred on the first click"), f.centred, 1);
	}
}

// Manufacture's pages for a filament printer, a resin printer (fui.h's fdmPages, printPages, printDraw).
static void print_summary ()
{
	const PrintSetup &s = A.print; char t[120], a[24], b[24], c[24];
	V3 sz = A.placedMesh.tris () ? A.placedMesh.hi - A.placedMesh.lo : V3 (0, 0, 0); double hw, hh, hz; plate_half (&hw, &hh, &hz);
	bool fits = A.placedMesh.tris () && A.placedMesh.lo.x >= -hw - 1e-6 && A.placedMesh.hi.x <= hw + 1e-6 && A.placedMesh.lo.y >= -hh - 1e-6 && A.placedMesh.hi.y <= hh + 1e-6 && A.placedMesh.hi.z <= hz + 1e-6;
	double lh = A.gen == 2 ? A.fdm.layer : s.resin.layer;
	if (A.camPage == 0)
	{
		fmt (sz.x, a, 12); fmt (sz.y, b, 12); fmt (sz.z, c, 12); snprintf (t, sizeof t, "%s \xC3\x97 %s \xC3\x97 %s: %s", a, b, c, fits ? TR ("it fits") : TR ("too large"));
		char l2[64]; snprintf (l2, sizeof l2, TR ("%d layers of %.3g mm"), (int) ceil ((A.placedMesh.tris () ? A.placedMesh.hi.z : 0) / lh - 1e-6), lh);
		summary (fits, t, fits ? "286E38" : "8A5A10", l2, CDIM);
	}
	else
	{
		bool floats = A.placedMesh.tris () && A.placedMesh.lo.z > s.resin.layer && s.tips.empty ();
		if (s.tips.empty ()) snprintf (t, sizeof t, floats ? TR ("Nothing holds the body") : TR ("No supports")); else snprintf (t, sizeof t, TR ("%d pillars%s"), (int) s.tips.size (), s.raft ? TR (" \xC2\xB7 a raft") : "");
		summary (!floats, t, floats ? "8A5A10" : "286E38", floats ? TR ("it is lifted off the plate") : A.placedMesh.tris () && A.placedMesh.lo.z < 0.3 ? TR ("the body lies on the plate") : TR ("click the body to add one"), CDIM);
	}
}
static void fdm_info ()
{
	const FdmJob &j = A.fjob; int n = (int) j.layers.size (); char t[64], a[24];
	if (!A.fdmReady) { kv (TR ("Layers"), j.err[0] ? "\xE2\x80\x94" : "..."); return; }
	snprintf (t, sizeof t, "%d", n); kv (TR ("Layers"), t); fmt (n * A.fdm.layer, a, 12); snprintf (t, sizeof t, TR ("%s mm"), a); kv (TR ("Height"), t);
	snprintf (t, sizeof t, "%.1f m", j.length / 1000); kv (TR ("Line drawn"), t); snprintf (t, sizeof t, TR ("%.2f m \xC2\xB7 %.1f cm\xC2\xB3"), j.filament / 1000, j.volume / 1000); kv (TR ("Filament"), t);
}
static void resin_info ()
{
	const PrintSetup &s = A.print; const PrintJob &j = A.pjob; int n = (int) j.file.layers.size (); bool ok = A.sliced; char t[96], a[24];
	if (ok) snprintf (t, sizeof t, "%d", n); else snprintf (t, sizeof t, TR ("%d of %d..."), j.done, j.total); kv (TR ("Layers"), t);
	fmt (n * s.resin.layer, a, 12); snprintf (t, sizeof t, TR ("%s mm"), a); kv (TR ("Height"), ok ? t : "\xE2\x80\x94");
	snprintf (t, sizeof t, TR ("%.1f ml"), j.volume); kv (TR ("Resin"), ok ? t : "\xE2\x80\x94");
	int mn = (int) (j.minutes + 0.5); if (mn >= 60) snprintf (t, sizeof t, "%d h %02d", mn / 60, mn % 60); else snprintf (t, sizeof t, TR ("%d min"), mn); kv (TR ("Time"), ok ? t : "\xE2\x80\x94");
	section (TR ("Checked"));
	if (!ok) { label (j.err[0] ? j.err : TR ("cutting the layers..."), CDIM, false, true); return; }
	mark (!j.tooLarge, j.tooLarge ? TR ("It does not fit the printer") : TR ("It fits the plate and the room"), j.tooLarge ? "8A5A10" : "");
	mark (j.onPlate, j.onPlate ? TR ("The first layers lie on the plate") : TR ("Nothing lies on the plate"), j.onPlate ? "" : "8A5A10");
	if (j.islands) { fmt (j.islandAt, a, 12); snprintf (t, sizeof t, j.islands > 1 ? TR ("%d parts start in mid-air (at %s mm)") : TR ("%d part starts in mid-air (at %s mm)"), j.islands, a); mark (false, t, "8A5A10"); }
	else mark (true, TR ("Each layer rests on the last"), "");
}
static void fdm_pages ()
{
	PrintSetup &s = A.print; FdmSettings &f = A.fdm;
	if (A.camPage == 0)
	{
		head (I_PR_PLATE, TR ("Setup"), TR ("the body, on the bed"));
		bodyProcess (s.body);
		section (TR ("Bed")); field (TR ("Width"), &A.fmach.bedX, TR ("mm"), -1); field (TR ("Depth"), &A.fmach.bedY, TR ("mm"), -1); field (TR ("Height"), &A.fmach.sizeZ, TR ("mm"), -1);
		section (TR ("On the bed")); field ("X", &s.x, TR ("mm"), -1); field ("Y", &s.y, TR ("mm"), -1);
		field (TR ("Tilted, X"), &s.tiltX, "\xC2\xB0", -1); field (TR ("Tilted, Y"), &s.tiltY, "\xC2\xB0", -1); field (TR ("Turned"), &s.turn, "\xC2\xB0", -1);
		print_summary ();
	}
	else if (A.camPage == 1)
	{
		head (I_PR_FIL, TR ("Filament"), TR ("layers, shell, infill"));
		section (TR ("Layers")); field (TR ("Height"), &f.layer, TR ("mm"), -1); field (TR ("Line's width"), &f.width, TR ("mm"), -1);
		section (TR ("Shell")); field (TR ("Walls"), &f.walls, 0, -1); field (TR ("Solid above"), &f.top, TR ("lay."), -1); field (TR ("Solid below"), &f.bottom, TR ("lay."), -1);
		section (TR ("Infill")); field (TR ("Share"), &A.fdmPct, "%", -1); field (TR ("Turned"), &f.angle, "\xC2\xB0", -1);
		drop (1, TR ("As"), f.pattern < 0 || f.pattern > 2 ? 0 : f.pattern, { TR ("Lines"), TR ("Grid"), TR ("Honeycomb") });
		section (TR ("Skirt")); field (TR ("Loops"), &f.skirt, 0, -1); field (TR ("Away by"), &f.skirtGap, TR ("mm"), -1);
	}
	else
	{
		head (I_PR_LAYERS, TR ("Layers"), TR ("the nozzle's path"));
		fdm_info ();
		check (TR ("Show it in the 3D view"), A.layer3d, 33);
		note (TR ("Play draws the layer as the printer would, then the next."));
		note (TR ("The file for a printer comes later: the path is what is shown."));
	}
}
static void print_pages ()
{
	PrintSetup &s = A.print; char t[96];
	if (A.gen == 2) { fdm_pages (); return; }
	if (A.camPage == 0)
	{
		head (I_PR_PLATE, TR ("Setup"), TR ("the body, on the plate"));
		bodyProcess (s.body);
		section (TR ("Printer")); label (s.machine.name);
		snprintf (t, sizeof t, "%d \xC3\x97 %d \xC2\xB7 %.3g \xC2\xB5m \xC2\xB7 .%s", s.machine.resX, s.machine.resY, s.machine.pixel, s.machine.ext); note (t);
		snprintf (t, sizeof t, TR ("room %.4g \xC3\x97 %.4g \xC3\x97 %.4g mm"), s.machine.resX * s.machine.pixel / 1000, s.machine.resY * s.machine.pixel / 1000, s.machine.sizeZ); note (t);
		section (TR ("On the plate")); field ("X", &s.x, TR ("mm"), -1); field ("Y", &s.y, TR ("mm"), -1); field (TR ("Lifted"), &s.lift, TR ("mm"), -1);
		field (TR ("Tilted, X"), &s.tiltX, "\xC2\xB0", -1); field (TR ("Tilted, Y"), &s.tiltY, "\xC2\xB0", -1); field (TR ("Turned"), &s.turn, "\xC2\xB0", -1);
		check (TR ("Mirror the picture"), s.mirror, 30);
		print_summary ();
	}
	else if (A.camPage == 1)
	{
		head (I_PR_RESIN, TR ("Resin"), TR ("exposures, the lift"));
		PrintResin &r = s.resin;
		button (CMD_PR_PRESET, r.name);
		section (TR ("Layers")); field (TR ("Height"), &r.layer, TR ("mm"), -1); field (TR ("Exposure"), &r.exposure, "s", -1); field (TR ("Light off"), &r.off, "s", -1);
		section (TR ("First layers")); field (TR ("How many"), &r.bottomLayers, 0, -1); field (TR ("Exposure"), &r.bottomExposure, "s", -1); field (TR ("Then, over"), &r.transition, TR ("lay."), -1);
		section (TR ("Lift, after each layer")); field (TR ("Slowly"), &r.normal.h1, TR ("mm"), -1); field (TR ("... at"), &r.normal.up1, "/s", -1);
		field (TR ("Then"), &r.normal.h2, TR ("mm"), -1); field (TR ("... at"), &r.normal.up2, "/s", -1); field (TR ("Back down at"), &r.normal.down2, "/s", -1);
	}
	else if (A.camPage == 2)
	{
		head (I_PR_SUP, TR ("Supports"), TR ("what holds the body"));
		button (CMD_PR_GENERATE, TR ("Generate"), true);
		section (TR ("Where")); field (TR ("Overhang"), &s.supAngle, "\xC2\xB0", -1); field (TR ("A pillar every"), &s.supEvery, TR ("mm"), -1);
		check (TR ("Also under low points"), s.supLow, 31);
		section (TR ("Pillars")); field (TR ("Diameter"), &s.supDia, TR ("mm"), -1); field (TR ("Tip"), &s.supTip, TR ("mm"), -1); field (TR ("Into the body"), &s.supInto, TR ("mm"), -1);
		check (TR ("A raft under it all"), s.raft, 32); field (TR ("Thick"), &s.raftThick, TR ("mm"), -1); field (TR ("Wider by"), &s.raftMore, TR ("mm"), -1);
		print_summary ();
		button (CMD_PR_CLEAR, TR ("Remove them all"), false, true);
	}
	else
	{
		head (I_PR_LAYERS, TR ("Layers"), TR ("what the screen shows"));
		resin_info ();
		check (TR ("Show it in the 3D view"), A.layer3d, 33);
		button (CMD_PR_FILE, TR ("Write the print file..."), true, true);
	}
}
static void cam_pages ()
{
	CamSetup &c = A.job; CamOp *op = cam_op (); char t[96], a[24], b[24], d[24];
	if (A.gen >= 1) { print_pages (); return; }
	if (A.camPage == 0)
	{
		head (I_CAM_SETUP, TR ("Setup"), TR ("the body, its stock"));
		bodyProcess (c.body);
		section (TR ("Stock")); seg (10, "", c.fixed ? 1 : 0, { TR ("Around it"), TR ("Fixed size") });
		if (!c.fixed) { field (TR ("Sides, more"), &c.side, TR ("mm"), -1); field (TR ("Top, more"), &c.top, TR ("mm"), -1); field (TR ("Under, more"), &c.under, TR ("mm"), -1); }
		else
		{
			field (TR ("Along X"), &c.size.x, TR ("mm"), -1); field (TR ("Along Y"), &c.size.y, TR ("mm"), -1); field (TR ("Height"), &c.size.z, TR ("mm"), -1);
			field (TR ("Moved, X"), &c.off.x, TR ("mm"), -1); field (TR ("Moved, Y"), &c.off.y, TR ("mm"), -1); field (TR ("Moved, Z"), &c.off.z, TR ("mm"), -1);
		}
		if (!c.fixed) { V3 s = A.paths.hi - A.paths.lo; fmt (s.x, a, 12); fmt (s.y, b, 12); fmt (s.z, d, 12); snprintf (t, sizeof t, TR ("%s \xC3\x97 %s \xC3\x97 %s mm"), a, b, d); kv (TR ("Size"), t); }
		section (TR ("Origin")); item ('G', { std::to_string (c.origin) });
		section (TR ("Axes"));
		static const char *const DIR[4] = { TR ("right"), TR ("back"), TR ("left"), TR ("front") };
		drop (2, TR ("X goes"), c.xdir & 3, { DIR[0], DIR[1], DIR[2], DIR[3] });
		snprintf (t, sizeof t, TR ("Y goes %s; Z goes up."), DIR[(c.xdir + 1) & 3]); note (t);
		section (TR ("Heights above the stock"));
		field (TR ("Safe"), &c.safe, TR ("mm"), -1); field (TR ("Retract"), &c.retract, TR ("mm"), -1);
	}
	else if (A.camPage == 1)
	{
		head (I_CAM_TOOL, TR ("Tool"), TR ("a flat end mill"));
		section (TR ("Tool")); textbind (TR ("Name"), c.tool.name);
		field (TR ("Diameter"), &c.tool.dia, TR ("mm"), -1); field (TR ("Cutting length"), &c.tool.flute, TR ("mm"), -1);
		field (TR ("Spindle"), &c.tool.rpm, TR ("rpm"), -1); field (TR ("Cutting feed"), &c.tool.feed, TR ("/min"), -1);
		field (TR ("Plunge feed"), &c.tool.plunge, TR ("/min"), -1); field (TR ("Travel"), &c.tool.travel, TR ("/min"), -1);
		section (TR ("Machine")); textbind (TR ("Name"), c.machine.name);
		field (TR ("Travel X"), &c.machine.tx, TR ("mm"), -1); field (TR ("Travel Y"), &c.machine.ty, TR ("mm"), -1); field (TR ("Travel Z"), &c.machine.tz, TR ("mm"), -1);
		field (TR ("Spindle, most"), &c.machine.rpmMax, TR ("rpm"), -1); field (TR ("Spindle's wait"), &c.machine.dwell, "s", -1);
	}
	else if (op)
	{
		bool clear = op->kind == CAM_CLEAR;
		head (clear ? I_CAM_CLEAR : I_CAM_CONTOUR, op->name, clear ? TR ("roughing, by levels") : TR ("the outline, to depth"));
		seg (11, TR ("On"), op->useFace ? 1 : 0, { TR ("The body"), TR ("A face") });
		if (op->useFace)
		{
			const Body *bd = A.doc.body (c.body); int g = bd ? cam_face (bd->mesh, op->facePt) : -1; fmt (op->facePt.z, a, 12);
			if (g >= 0) snprintf (t, sizeof t, TR ("the face at height %s"), a); else snprintf (t, sizeof t, TR ("click a flat face turned up"));
			label (t, g >= 0 ? CDIM : CRED, false, true);
		}
		if (clear)
		{
			field (TR ("Step over"), &op->stepover, TR ("mm"), -1); field (TR ("Step down"), &op->stepdown, TR ("mm"), -1);
			field (TR ("Left on walls"), &op->leaveR, TR ("mm"), -1); field (TR ("Left on floors"), &op->leaveZ, TR ("mm"), -1);
			seg (12, TR ("Cut"), op->climb ? 0 : 1, { TR ("Climb"), TR ("Convent.") });
			if (!op->useFace)
			{
				check (TR ("To the lowest flat face"), op->lowestAuto, 20);
				if (!op->lowestAuto) field (TR ("Lowest (model Z)"), &op->lowest, TR ("mm"), -1);
			}
		}
		else
		{
			seg (13, TR ("Side"), op->inside ? 1 : 0, { TR ("Outside"), TR ("Inside") }); seg (12, TR ("Cut"), op->climb ? 0 : 1, { TR ("Climb"), TR ("Convent.") });
			field (TR ("Step down"), &op->stepdown, TR ("mm"), -1);
			check (TR ("From the stock's top"), op->fromAuto, 21);
			if (!op->fromAuto) field (TR ("From (model Z)"), &op->from, TR ("mm"), -1);
			if (!op->useFace)
			{
				field (TR ("Under it, by"), &op->under, TR ("mm"), -1);
				check (TR ("Tabs"), op->tabs, 22);
				if (op->tabs) { field (TR ("How many"), &op->ntabs, 0, -1); field (TR ("Width"), &op->tabw, TR ("mm"), -1); field (TR ("Height"), &op->tabh, TR ("mm"), -1); }
			}
		}
		// its sum: what it cuts, how long, how low
		bool bad = op->failed;
		if (A.camDirty) summary (false, TR ("changed"), "8A5A10", TR ("Validate to compute"), CDIM);
		else if (bad) summary (false, op->err, "8A5A10", 0, 0);
		else
		{
			snprintf (t, sizeof t, TR ("%.1f m cut \xC2\xB7 %d min"), op->length / 1000, (int) (op->minutes + 0.5));
			V3 o = cam_origin (A.job, A.paths.lo, A.paths.hi); double lowz = 1e30;
			for (const CamMove &m : A.paths.moves) if (m.op == A.camSel - 1 && m.p.z < lowz) lowz = m.p.z;
			char l2[48]; fmt (lowz - o.z, a, 12); snprintf (l2, sizeof l2, TR ("never below Z %s"), a);
			summary (true, t, "286E38", l2, CDIM);
		}
		button (CMD_CAM_DELETE, TR ("Delete this operation"), false, true);
	}
}
static void form_build ()
{
	g_items.clear (); g_binds.clear (); g_feat = 0; g_el = 0;
	char t[96];
	if (A.camMode) { cam_pages (); return; }
	if (A.sketching)
	{
		static const char *const EN[] = { TR ("Line"), TR ("Arc"), TR ("Circle"), TR ("Rectangle"), TR ("Close"), TR ("Spline"), TR ("Point") };
		static const int EI[] = { I_LINE, I_ARC, I_CIRCLE, I_RECT, I_CLOSE, I_SPLINE, I_POINT };
		bool edit = A.selEl >= 0 && A.selEl < (int) A.sk.els.size ();
		SkEl &e = edit ? A.sk.els[A.selEl] : A.cur; g_el = &e;
		if (e.kind == SK_ARC && !edit && A.tool == T_ARC3) head (I_ARC3, TR ("3-point arc"), TR ("Ends"), TR ("Middle"), A.curStep < 2 ? 0 : 1);
		else if (e.kind == SK_ARC && !edit && e.chain) head (EI[e.kind], TR ("Arc"), TR ("Centre"), TR ("End"), A.curStep < 2 ? 0 : 1);
		else if (e.kind == SK_ARC && !edit) head (EI[e.kind], TR ("Arc"), TR ("Centre"), TR ("Start \xC2\xB7 End"), A.curStep == 0 ? 0 : 1);
		else if (A.tool == T_MIDLINE && !edit) head (I_MIDLINE, TR ("Midpoint line"));
		else head (EI[e.kind], EN[e.kind], edit ? TR ("selected") : 0);
		bool chained = e.chain, fresh = !edit && A.curStep > 0;
		const char *xn = e.kind == SK_CIRCLE || (e.kind == SK_RECT && e.rel) || (e.kind == SK_ARC && !edit && A.tool == T_ARC && A.curStep == 0) ? TR ("Centre X")
				 : e.kind == SK_RECT ? TR ("Corner X") : e.kind == SK_POINT ? TR ("At X") : TR ("Starts at X");
		bool mid = A.tool == T_MIDLINE && !edit;		// (a line drawn from its middle: placed, its middle is what moves)
		if (mid) xn = TR ("Middle X");
		double *fx = mid && A.curStep == 1 ? &A.arcC.x : &e.x, *fy = mid && A.curStep == 1 ? &A.arcC.y : &e.y;
		if (e.kind != SK_CLOSE && !chained) { field (xn, fx, TR ("mm"), -1); field ("Y", fy, TR ("mm"), -1); }
		else if (e.kind != SK_CLOSE) kv (TR ("Starts at"), TR ("the end before"), "");
		if (e.kind == SK_LINE)
		{
			field (TR ("Length"), &e.len, TR ("mm"), 0, false, fresh); field (TR ("Angle"), &e.a, "\xC2\xB0", 1);
			if (chained) check (TR ("From the line before"), e.rel, 7);
		}
		else if (e.kind == SK_ARC)
		{
			bool swf = fresh && A.curStep == 2 && A.tool != T_ARC3;
			field (TR ("Radius"), &e.r, TR ("mm"), 0, false, fresh && !swf); field (TR ("Centre at"), &e.ca, "\xC2\xB0", -1); field (TR ("Angle"), &e.sweep, "\xC2\xB0", 1, false, swf);
			note (TR ("+ clockwise, \xE2\x88\x92 anticlockwise"));
		}
		else if (e.kind == SK_SPLINE)
		{
			snprintf (t, sizeof t, TR ("through %d points"), (int) e.pts.size () + (edit ? 1 : 0)); label (t);
			if (!edit) note (TR ("Enter, or the last point clicked again, ends it."));
		}
		else if (e.kind == SK_POINT) note (TR ("A mark to snap to: it is part of no outline."));
		else if (e.kind == SK_CIRCLE) field (TR ("Diameter"), &e.w, TR ("mm"), 0, false, fresh);
		else if (e.kind == SK_RECT)
		{
			field (TR ("Width"), &e.w, TR ("mm"), 0, false, fresh); field (TR ("Height"), &e.h, TR ("mm"), 1);
			seg (14, TR ("From its"), e.rel ? 1 : 0, { TR ("Corner"), TR ("Centre") });
		}
		if (e.kind != SK_CLOSE && e.kind != SK_POINT) check (TR ("Construction (part of no outline)"), e.cons, 19);
		check (TR ("Snap to points and angles"), A.snap, 3);
		info ({ TR ("Each element starts from a point and keeps its own values. Change one: what was drawn after it follows.") });
		if (edit) button (CMD_DEL_ELEMENT, TR ("Delete this element"), false, true);
		return;
	}
	if (A.tool == T_SKETCH)				// where the sketch goes: a face clicked, or a plane of the axes
	{
		head (I_SKETCH, TR ("Sketch"), TR ("its plane"));
		note (TR ("Click a flat face of a body, or draw on a plane of the axes:"));
		seg (3, TR ("Plane"), A.skPlane, { TR ("XY"), TR ("XZ"), TR ("YZ") });
		field (TR ("Offset"), &A.skOffset, TR ("mm"), -1, false, true);
		note (A.skPlane == 0 ? TR ("XY: the ground, seen from above; the offset is its height (Z).") : A.skPlane == 1 ? TR ("XZ: upright, seen from the front; the offset is along Y.") : TR ("YZ: upright, seen from the right; the offset is along X."));
		button (CMD_SK_START, TR ("Start the sketch"), true);
		button (CMD_CANCEL, TR ("Cancel"), false, true);
		return;
	}
	if (A.hasPend)
	{
		Feature &f = A.pend;
		snprintf (t, sizeof t, "%s %d", TR (KIND_NAME[f.kind]), A.doc.count (f.kind, (int) A.doc.feats.size ()) + 1);
		if (f.kind == F_BOX) head (I_BOX, t, TR ("Base"), TR ("Height"), A.step == 2 ? 1 : 0);
		else if (f.kind == F_CYL) head (I_CYL, t, TR ("Circle"), TR ("Depth"), A.step == 2 ? 1 : 0);
		else if (f.kind == F_TORUS) head (I_TORUS, t, TR ("Ring"), TR ("Tube"), A.step == 2 ? 1 : 0);
		else if (f.kind == F_SPHERE) head (I_SPHERE, t, TR ("Radius"));
		else if (f.kind == F_TAPER) head (I_TAPER, t, TR ("Base"), TR ("Top \xC2\xB7 Height"), A.step >= 2 ? 1 : 0);
		else if (round_kind (f.kind)) head (icon_of_kind (f.kind), t, TR ("Base"), TR ("Height"), A.step == 2 ? 1 : 0);
		else head (icon_of_kind (f.kind), t);
		featureFields (f, true);
		button (CMD_CANCEL, TR ("Cancel"), false, true); button (CMD_OK, TR ("OK"), true, true);
		return;
	}
	if (canvasPage ())				// a canvas: a picture on a plane, to draw over
	{
		Canvas3 &c = A.doc.canvases[A.selCanvas]; char a[24], d[24];
		head (I_SKETCH, c.name, TR ("a canvas, to draw over"));
		textbind (TR ("Name"), c.name);
		seg (16, TR ("Plane"), c.plane, { TR ("XY"), TR ("XZ"), TR ("YZ") });
		field (c.plane == 0 ? TR ("Height (Z)") : c.plane == 1 ? TR ("Along Y") : TR ("Along X"), &c.off, TR ("mm"), -1);
		section (TR ("On its plane")); field ("X", &c.x, TR ("mm"), -1); field ("Y", &c.y, TR ("mm"), -1); field (TR ("Width"), &c.w, TR ("mm"), -1); field (TR ("Turned"), &c.turn, "\xC2\xB0", -1);
		fmt (c.w, a, 12); fmt (c.w * c.aspect, d, 12); snprintf (t, sizeof t, TR ("%s \xC3\x97 %s mm"), a, d); kv (TR ("Size"), t); note (TR ("Its size follows its width."));
		section (TR ("Seen")); field (TR ("Opacity"), &c.opacity, "%", -1); check (TR ("Shown"), c.visible, 34);
		note (TR ("Drag it in the view to move it, a corner to size it."));
		button (CMD_CANVAS_DELETE, TR ("Remove this canvas"), false, true);
		return;
	}
	if (A.selFeat >= 0 && A.selFeat < (int) A.doc.feats.size ())
	{
		Feature &f = A.doc.feats[A.selFeat]; g_feat = &f;
		head (icon_of_kind (f.kind), f.name, A.selFeat >= A.doc.upto ? TR ("rolled back") : TR ("a step of the history"));
		featureFields (f, false);
		if (f.failed && A.selFeat < A.doc.upto) { label (TR ("This step could not be made:"), CRED, false, true); label (f.err, CRED, false, true); }
		if (f.kind == F_SKETCH) button (CMD_EDIT_SKETCH, TR ("Edit sketch"));
		button (CMD_DELETE, TR ("Delete this step"), false, true);
		return;
	}
	if (A.selBody >= 0 && A.doc.body (A.selBody))
	{
		Body &bd = *A.doc.body (A.selBody); BodyProp &p = A.doc.prop (A.selBody);
		int steps = 0; for (int i = 0; i < A.doc.upto; i++) if (A.doc.feats[i].target == bd.id || i == bd.id) steps++;
		snprintf (t, sizeof t, TR ("Body \xC2\xB7 %d step%s"), steps, steps == 1 ? "" : "s"); head (-2, p.name, t, 0, -1, p.colour);
		textbind (TR ("Name"), p.name);
		{ std::vector<std::string> w = { hex (p.colour) }; for (int i = 0; i < 5; i++) w.push_back (hex (BODY_COLOURS[i])); item_v ('W', w); }
		section (TR ("Measures"));
		char a[24], c[24], d[24]; V3 sz = bd.mesh.hi - bd.mesh.lo; fmt (sz.x, a, 12); fmt (sz.y, c, 12); fmt (sz.z, d, 12);
		snprintf (t, sizeof t, TR ("%s \xC3\x97 %s \xC3\x97 %s mm"), a, c, d); kv (TR ("Size"), t);
		snprintf (t, sizeof t, TR ("%.1f cm\xC2\xB3"), bd.m.Volume () / 1000); kv (TR ("Volume"), t);
		snprintf (t, sizeof t, TR ("%.0f cm\xC2\xB2"), bd.m.SurfaceArea () / 100); kv (TR ("Surface"), t);
		snprintf (t, sizeof t, "%d", bd.mesh.tris ()); kv (TR ("Triangles"), t);
		section (TR ("Display")); check (TR ("Edges"), A.showEdges, 4); check (TR ("Grid"), A.showGrid, 5); check (TR ("See through"), A.seeThrough, 6);
		section (TR ("Print check"));
		bool ok = bd.m.Status () == Manifold::Error::NoError && !bd.m.IsEmpty ();
		mark (ok, ok ? TR ("Closed solid, ready to print") : TR ("Not a closed solid"), ok ? "286E38" : "8A5A10");
		return;
	}
	head (-1, TR ("3DForge"), A.doc.feats.empty () ? TR ("A new part") : TR ("Nothing selected"));
	if (A.doc.feats.empty ())
	{
		section (TR ("Start with a shape:"));
		note (TR ("Box: click, move away, click, move up, click."));
		note (TR ("Sketch: draw an outline on a face, then Extrude it."));
		note (TR ("Each step stays in the history: click it to change its values."));
	}
	section (TR ("Display")); check (TR ("Edges"), A.showEdges, 4); check (TR ("Grid"), A.showGrid, 5); check (TR ("See through"), A.seeThrough, 6);
}

// A value changed (a field, a check box, a choice): what it belongs to is made again (fui.h's Props::applied).
static void applied ()
{
	if (canvasPage ()) { Canvas3 &c = A.doc.canvases[A.selCanvas]; if (c.w < 1) c.w = 1; if (c.opacity < 0) c.opacity = 0; if (c.opacity > 100) c.opacity = 100;
		A.doc.changes++; view_dirty (); ui (0); return; }
	if (!A.camMode && !A.sketching && A.hasPend && A.step == 0) A.ghost = true;
	if (printer ())
	{
		PrintSetup &s = A.print;
		if (A.gen == 2 && A.camPage == 1)
		{
			FdmSettings &f = A.fdm;
			if (f.layer < 0.05) f.layer = 0.05; if (f.width < 0.1) f.width = 0.1; if (f.walls < 1) f.walls = 1; if (f.top < 0) f.top = 0; if (f.bottom < 0) f.bottom = 0;
			if (A.fdmPct < 0) A.fdmPct = 0; if (A.fdmPct > 100) A.fdmPct = 100; if (f.skirt < 0) f.skirt = 0;
			fdm_presets_save (); print_touch (); view_dirty (); ui (0); return;
		}
		if (A.gen == 2 && A.camPage == 0) fdm_presets_save ();
		if (A.camPage == 0)
		{
			if (s.tiltX != A.pAt[3] || s.tiltY != A.pAt[4] || s.turn != A.pAt[5]) s.tips.clear ();
			else for (V3 &t : s.tips) t = t + V3 (s.x - A.pAt[0], s.y - A.pAt[1], s.lift - A.pAt[2]);
			A.pAt[0] = s.x; A.pAt[1] = s.y; A.pAt[2] = s.lift; A.pAt[3] = s.tiltX; A.pAt[4] = s.tiltY; A.pAt[5] = s.turn;
		}
		if (A.camPage == 1 && A.gen == 1)
		{
			s.resin.bottom = s.resin.normal; s.resin.liftSpeed = s.resin.normal.up2; s.resin.retractSpeed = s.resin.normal.down2;
			if (s.resin.layer < 0.01) s.resin.layer = 0.01;
			print_presets_save ();
		}
		print_touch (); view_dirty (); ui (0); return;
	}
	if (A.camMode) { cam_touch (); if (A.camPage == 1) cam_presets_save (); view_dirty (); ui (0); return; }
	if (A.sketching) sk_eval ();
	else if (A.hasPend) { auto_op (); preview_update (); if (A.prevErr) set_hint (A.prevErr); else tool_hint (); }
	else if (g_feat) A.doc.touch ();
	view_dirty (); ui (0);
}
static double parse_num (const char *s) { char t[64]; int n = 0; for (; *s && n < 62; s++) t[n++] = *s == ',' ? '.' : *s; t[n] = 0; return atof (t); }

// ---- the timeline -------------------------------------------------------------------------------------------------------------
static int tl_count () { return filament () ? 1 : resin () ? 2 : A.camMode ? 1 + (int) A.job.ops.size () : A.sketching ? (int) A.sk.els.size () : (int) A.doc.feats.size (); }
static int tl_sel () { return A.camMode ? A.camSel : A.sketching ? A.selEl : A.selFeat; }
static void tl_text (int i, char *out, int cap)
{
	char t[96], a[24], b[24], c[24];
	if (printer ())
	{
		if (i == 0) snprintf (out, cap, TR ("Setup  \xC2\xB7  %s \xC2\xB7 %s"), body_name (A.print.body), A.gen == 2 ? TR ("a filament printer") : A.print.machine.name);
		else if (A.print.tips.empty ()) snprintf (out, cap, TR ("Supports  \xC2\xB7  none"));
		else snprintf (out, cap, TR ("Supports  \xC2\xB7  %d pillars%s"), (int) A.print.tips.size (), A.print.raft ? TR (" \xC2\xB7 a raft") : "");
		return;
	}
	if (A.camMode)
	{
		if (i == 0) { V3 s = A.paths.hi - A.paths.lo; fmt (s.x, a, 12); fmt (s.y, b, 12); fmt (s.z, c, 12); snprintf (out, cap, TR ("Setup  \xC2\xB7  %s \xC2\xB7 stock %s \xC3\x97 %s \xC3\x97 %s"), body_name (A.job.body), a, b, c); return; }
		const CamOp &o = A.job.ops[i - 1]; fmt (A.job.tool.dia, a, 12);
		if (o.failed) snprintf (out, cap, "%s  \xC2\xB7  %s", o.name, o.err);
		else snprintf (out, cap, TR ("%s  \xC2\xB7  \xC3\x98 %s flat \xC2\xB7 %d min%s"), o.name, a, (int) (o.minutes + 0.5), o.useFace ? TR (" \xC2\xB7 one face") : "");
		return;
	}
	if (!A.sketching)
	{
		const Feature &f = A.doc.feats[i];
		if (f.failed && i < A.doc.upto) snprintf (t, sizeof t, "%s", f.err); else describe (A.doc, f, t, sizeof t);
		snprintf (out, cap, "%s  \xC2\xB7  %s%s", f.name, t, i >= A.doc.upto ? TR ("  (rolled back)") : ""); return;
	}
	static const char *const EN[] = { TR ("Line"), TR ("Arc"), TR ("Circle"), TR ("Rectangle"), TR ("Close"), TR ("Spline"), TR ("Point") };
	const SkEl &e = A.sk.els[i]; int k = 0; for (int j = 0; j <= i; j++) if (A.sk.els[j].kind == e.kind) k++;
	if (e.kind == SK_LINE) { fmt (e.len, a); fmt (e.a, b); snprintf (t, sizeof t, e.rel ? TR ("%s \xC2\xB7 turn %s\xC2\xB0") : TR ("%s \xC2\xB7 at %s\xC2\xB0"), a, b); }
	else if (e.kind == SK_ARC) { fmt (e.r, a); fmt (e.sweep, b); snprintf (t, sizeof t, "R %s \xC2\xB7 %s\xC2\xB0", a, b); }
	else if (e.kind == SK_SPLINE) snprintf (t, sizeof t, TR ("through %d points"), (int) e.pts.size () + 1);
	else if (e.kind == SK_POINT) { fmt (e.x, a); fmt (e.y, b); snprintf (t, sizeof t, TR ("a mark at %s, %s"), a, b); }
	else if (e.kind == SK_CIRCLE) { fmt (e.w, a); fmt (e.x, b); fmt (e.y, c); snprintf (t, sizeof t, TR ("\xC3\x98 %s \xC2\xB7 at %s, %s"), a, b, c); }
	else if (e.kind == SK_RECT) { fmt (fabs (e.w), a); fmt (fabs (e.h), b); snprintf (t, sizeof t, e.rel ? TR ("%s \xC3\x97 %s \xC2\xB7 from its centre") : "%s \xC3\x97 %s", a, b); }
	else snprintf (t, sizeof t, TR ("back to the start"));
	snprintf (out, cap, "%s %d  \xC2\xB7  %s%s", EN[e.kind], k, t, e.cons ? TR (" \xC2\xB7 construction") : "");
}

// ================================================================================================================================
// The interface
// ================================================================================================================================
FC void fc_init (const char *configDir, const char *printersIni)
{
	g_cfg = configDir ? configDir : "."; g_printers = printersIni ? printersIni : "";
	g_threads = (int) std::thread::hardware_concurrency (); if (g_threads < 1) g_threads = 1; if (g_threads > 32) g_threads = 32;
	static TextFace sm (11), bg (15), ti (9); g_small = &sm; g_big = &bg; g_tiny = &ti;
	A.refresh = app_refresh;
	job_reset ();
	if (!g_view) g_view = new View (0, 0, 800, 600);
	tool_set (T_SELECT);
	g_flags = FL_PANEL | FL_UI | FL_VIEW;
}
// A catalogue of words in the user's language (Onyx's lang/<code>.txt files: "English<TAB>translation"), before fc_init:
// the host loads 3DForge's and its own when Windows speaks that language. 0: it cannot be read.
FC int fc_lang (const char *path) { return forge_lang_load (path) ? 1 : 0; }
// What changed since the last call (FL_*): the host shows it again.
FC int fc_poll () { int f = g_flags | (g_view && g_view->dirty ? FL_VIEW : 0); g_flags = 0; return f; }
FC void fc_set_accent (unsigned rgb) { C_ACCENT = rgb & 0xFFFFFF; view_dirty (); }

// ---- the view ----
FC void fc_view_draw (unsigned *px, int w, int h, int stride)
{
	if (!g_view || w < 8 || h < 8) return;
	g_view->width = w; g_view->height = h; g_view->canvas.adopt (px, w, h, stride);
	double t0 = now_ms ();
	g_view->onDraw (); g_view->dirty = false;
	if (getenv ("FORGE_PROFILE")) fprintf (stderr, "frame %.1f ms\n", now_ms () - t0);
	g_view->canvas.px = 0;					// (the host's: not to be freed)
	g_view->canvas.owns = false;
}
// The pointer over the view: where, its buttons (1 left, 2 right, 4 middle), the wheel's notches, the keys (MOD_*).
FC int fc_view_mouse (int x, int y, int buttons, int wheel, int mods)
{
	if (!g_view) return 0;
	g_mods = mods;
	bool taken = g_view->onMouse (x, y, buttons & 1, buttons & 2, buttons & 4, wheel);
	return taken ? 1 : 0;
}
FC void fc_view_look (double az, double el) { if (g_view) g_view->lookAlong (az, el); }

// The clock: what plays (the cut, the layers, the bead), what is computed a little after the last change, the layers
// cut a few at a time (main.cpp's onTick). Called some 60 times a second.
FC void fc_tick ()
{
	if (A.camMode && A.gen == 0 && A.camSim && A.simPlay && kapi_get_ticks () - A.simT >= 3)
	{
		size_t n = A.paths.moves.size (); long by = play_steps (n / 300.0 < 1 ? 1 : n / 300.0); A.simT = kapi_get_ticks ();
		if (by > 0) sim_to (A.simAt + by);
		if (A.simAt >= n) A.simPlay = false;
		view_dirty ();
	}
	if (filament ())
	{
		if (A.printDirty && kapi_get_ticks () - A.camDirtyT > 40) { print_refresh (); if (A.camPage == 3) fdm_start (); ui (0); view_dirty (); }
		if (A.layerPlay && A.fdmReady && A.camPage == 3 && kapi_get_ticks () - A.layerT >= 2)
		{
			int n = (int) A.fjob.layers.size (); A.layerT = kapi_get_ticks ();
			double total = A.layer < n ? fdm_layer_length (A.fjob.layers[A.layer]) : 0, by = (total / 50 < 6 ? 6 : total / 50) * play_speed ();
			if (A.fdmDrawn < 0) A.fdmDrawn = 0;
			A.fdmDrawn += by;
			if (A.fdmDrawn >= total) { if (A.layer < n - 1) { A.layer++; A.fdmDrawn = 0; ui (0); } else { A.layerPlay = false; A.fdmDrawn = -1; ui (0); } }
			view_dirty ();
		}
	}
	if (resin ())
	{
		if (A.printDirty && kapi_get_ticks () - A.camDirtyT > 40) { print_refresh (); if (A.camPage == 3) print_slice_start (); ui (0); view_dirty (); }
		if (A.slicer.running ())
		{
			unsigned t0 = kapi_get_ticks (); bool done = false;
			for (int k = 0; k < 60 && !done; k++) { done = A.slicer.step (1); if (kapi_get_ticks () - t0 >= 3) break; }
			if (done)
			{
				A.sliced = true; int n = (int) A.pjob.file.layers.size (); if (A.layer > n - 1) A.layer = n - 1;
				if (A.wantFile) { A.wantFile = false; g_flags |= FL_PRINTFILE; }
				print_hint ();
			}
			ui (0); view_dirty ();
		}
		else if (A.layerPlay && A.sliced && kapi_get_ticks () - A.layerT >= 4)
		{
			int n = (int) A.pjob.file.layers.size (); A.layerT = kapi_get_ticks (); A.layer += (int) play_steps (n > 300 ? n / 150.0 : 1);
			if (A.layer >= n - 1) { A.layer = n - 1; A.layerPlay = false; }
			ui (0); view_dirty ();
		}
	}
}
// A key: 27 Esc (cancel), 13 Enter (confirm), 46 Delete.
FC void fc_key (int k)
{
	if (k == 27) cancel ();
	else if (k == 13) confirm ();
	else if (k == 46) command (A.sketching ? CMD_DEL_ELEMENT : CMD_DELETE);
	view_dirty ();
}
FC int fc_cmd (int id) { return command (id); }
// A tool of the bar: chosen, or put down when it is chosen again (a sketch's stay).
FC void fc_tool (int t, int toggle)
{
	if (toggle && A.tool == t && !A.sketching) t = T_SELECT;
	tool_set (t); if (shape_tool (t)) A.lastShape = t;
	view_dirty ();
}

// ---- the state: ints, then the texts ----
FC void fc_state (int *s, int n)
{
	int v[32] = { 0 };
	v[0] = A.tool; v[1] = A.sketching; v[2] = A.camMode; v[3] = A.gen; v[4] = A.camPage; v[5] = A.camSel; v[6] = A.camSim; v[7] = A.camDirty;
	v[8] = (int) A.undo.size (); v[9] = (int) A.redo.size (); v[10] = A.showEdges; v[11] = A.showGrid; v[12] = A.seeThrough; v[13] = A.snap;
	v[14] = A.doc.tris (); v[15] = A.doc.changes != A.saved; v[16] = cam_op () ? cam_op ()->kind : -1; v[17] = A.lastShape; v[18] = A.hasPend;
	v[19] = (int) A.doc.bodies.size (); v[20] = (int) A.doc.feats.size (); v[21] = A.doc.upto; v[22] = A.selFeat; v[23] = A.selBody; v[24] = A.step;
	v[25] = A.selCanvas; v[26] = g_threads; v[27] = A.sliced; v[28] = A.selEl;
	v[29] = A.selEl >= 0 && A.selEl < (int) A.sk.els.size () ? A.sk.els[A.selEl].cons : A.cons;		// (construction: the element selected's, else the mode)
	for (int i = 0; i < n && i < 32; i++) s[i] = v[i];
}
// 0 the hint, 1 the file's path, 2 the sketch's outlines (closed, open), 3 the build's name
FC int fc_text (int which, char *buf, int cap)
{
	char t[96];
	switch (which)
	{
	case 0: return put (A.hint, buf, cap);
	case 1: return put (A.path, buf, cap);
	case 2: snprintf (t, sizeof t, "%d\n%d", A.ev.nclosed, A.ev.nopen); return put (t, buf, cap);
	}
	return put ("", buf, cap);
}

// ---- the panel ----
FC int fc_form (char *buf, int cap)
{
	form_build ();
	std::string s; for (size_t i = 0; i < g_items.size (); i++) { if (i) s += '\x1E'; s += g_items[i]; }
	return put (s, buf, cap);
}
// What a field of the last form says (its id), typed.
FC void fc_form_text (int id, const char *text)
{
	if (id < 0 || id >= (int) g_binds.size ()) return;
	Bind &q = g_binds[id];
	if (q.name)
	{
		snprintf (q.name, 24, "%s", text); A.doc.changes++;
		if (resin ()) print_presets_save (); else if (A.camMode && A.gen == 0) cam_presets_save ();
		ui (0); view_dirty (); return;
	}
	double v = parse_num (text);
	if (q.mag && !strchr (text, '-')) v = (*q.val < 0 ? -1 : 1) * fabs (v);	// (a size keeps its way -- up, down --; "-5" turns it: Control.cs)
	*q.val = v; if (q.typed >= 0) A.typed[q.typed] = true;
	applied ();
}
// Enter in a field (fui.h's onEnter).
FC int fc_form_enter ()
{
	if (filament ()) { print_refresh (); if (A.camPage == 3) fdm_start (); ui (0); return REQ_NONE; }
	if (resin ()) { print_refresh (); if (A.camPage == 3) print_slice_start (); ui (0); return REQ_NONE; }
	if (A.camMode) { cam_refresh (); ui (0); return REQ_NONE; }
	return command (A.tool == T_SKETCH && !A.sketching ? CMD_SK_START : CMD_OK);
}
FC void fc_form_check (int tag, int on)
{
	Feature *f = A.hasPend ? &A.pend : g_feat; bool c = on != 0;
	switch (tag)
	{
	case 1: if (f) f->centred = c; break;
	case 2: if (f) { f->through = c; if (f->through && f->h > 0) f->h = -f->h; } break;
	case 3: A.snap = c; ui (R_PANELS); return;
	case 4: A.showEdges = c; break;
	case 5: A.showGrid = c; break;
	case 6: A.seeThrough = c; break;
	case 7: if (g_el) g_el->rel = c; break;
	case 8: if (f) f->clone = c; break;
	case 19: if (g_el) { g_el->cons = c; if (g_el == &A.cur) A.cons = c; } break;
	case 17: if (f) f->r2 = c ? (f->r2 > 0 ? f->r2 : f->r) : 0; break;		// (asymmetric: the second size starts as the first)
	case 18: if (f) f->swap = c; break;
	case 34: if (canvasPage ()) { A.doc.canvases[A.selCanvas].visible = c; A.doc.changes++; view_dirty (); ui (0); } return;
	case 30: A.print.mirror = c; break;
	case 31: A.print.supLow = c; break;
	case 32: A.print.raft = c; break;
	case 33: A.layer3d = c; A.layerPlay = false; view_dirty (); ui (0); return;
	case 20: if (cam_op ()) cam_op ()->lowestAuto = c; break;
	case 21: if (cam_op ()) cam_op ()->fromAuto = c; break;
	case 22: if (cam_op ()) cam_op ()->tabs = c; break;
	}
	if (tag >= 4 && tag <= 6) { view_dirty (); ui (R_PANELS); return; }
	if (tag >= 30) { print_touch (); view_dirty (); ui (R_PANELS); return; }
	if (tag >= 20) { cam_touch (); ui (R_PANELS); return; }
	if (!A.hasPend && !A.sketching && f) undo_push ();
	applied (); ui (R_PANELS);
}
FC void fc_form_seg (int tag, int sel)
{
	SkEl *el = g_el;
	if (tag == 1) { tool_set (sel == 0 ? T_FILLET : T_CHAMFER); return; }
	if (tag == 2 && el) { el->sweep = (sel == 0 ? 1 : -1) * fabs (el->sweep); applied (); }
	if (tag == 14 && el)
	{
		SkEl &e = *el; bool c = sel == 1;
		if (c != e.rel && (A.curStep > 0 || A.selEl >= 0))
		{
			double x0 = e.rel ? e.x - fabs (e.w) / 2 : e.w < 0 ? e.x + e.w : e.x, y0 = e.rel ? e.y - fabs (e.h) / 2 : e.h < 0 ? e.y + e.h : e.y;
			e.w = fabs (e.w); e.h = fabs (e.h); e.x = c ? x0 + e.w / 2 : x0; e.y = c ? y0 + e.h / 2 : y0;
		}
		e.rel = c; A.rectCentre = c; applied (); tool_hint (); ui (R_PANELS);
	}
	if (tag == 3) { A.skPlane = sel; ui (R_ALL); }
	if (tag == 16 && canvasPage ()) { A.doc.canvases[A.selCanvas].plane = sel; A.doc.changes++; view_dirty (); ui (R_ALL); }
	if (tag == 10) { A.job.fixed = sel == 1; if (A.job.fixed) A.job.size = A.paths.hi - A.paths.lo; cam_touch (); ui (R_ALL); }
	if (tag == 11 && cam_op ()) { cam_op ()->useFace = sel == 1; cam_touch (); if (sel == 1) set_hint (TR ("Click the flat face, turned up, to work on.")); ui (R_ALL); }
	if (tag == 12 && cam_op ()) { cam_op ()->climb = sel == 0; cam_touch (); ui (R_PANELS); }
	if (tag == 13 && cam_op ()) { cam_op ()->inside = sel == 1; cam_touch (); ui (R_PANELS); }
	view_dirty ();
}
FC void fc_form_drop (int tag, int sel)
{
	if (tag == 1) { A.fdm.pattern = sel; fdm_presets_save (); print_touch (); view_dirty (); ui (0); }
	if (tag == 2) { A.job.xdir = sel; A.doc.changes++; cam_hint (); view_dirty (); ui (R_ALL); }
	if ((tag == 3 || tag == 4) && sel >= 0 && sel < (int) g_loftIdx.size ())		// a loft's sketches
	{
		Feature *f = A.hasPend ? &A.pend : g_feat; if (!f || f->kind != F_LOFT) return;
		if (!A.hasPend) undo_push ();
		if (tag == 3) f->sketch = g_loftIdx[sel]; else f->tool = g_loftIdx[sel];
		applied (); ui (R_PANELS);
	}
}
// The operation (0..3), the body's colour (0..4), the origin (0..26).
FC void fc_form_op (int i)
{
	Feature *fp = A.hasPend ? &A.pend : g_feat; if (!fp || i < 0 || i > 3) return;
	Feature &f = *fp;
	if (i != OP_NEW && !A.doc.body (f.target))
	{
		if (A.doc.bodies.empty ()) return;
		f.target = A.doc.body (A.selBody) ? A.selBody : A.doc.bodies[0].id;
	}
	if (!A.hasPend) undo_push ();
	f.op = i; A.opAuto = false; applied (); ui (R_PANELS);
}
FC void fc_form_colour (int i)
{
	if (i < 0 || i > 4 || !A.doc.body (A.selBody)) return;
	A.doc.prop (A.selBody).colour = BODY_COLOURS[i]; A.doc.changes++; view_dirty (); ui (R_PANELS);
}
FC void fc_form_origin (int k) { if (k >= 0 && k < 27) { A.job.origin = k; A.doc.changes++; view_dirty (); ui (R_PANELS); } }

// ---- the lists the panel's buttons open: the bodies, the processes, the resins ----
FC int fc_list (int which, char *buf, int cap)
{
	std::string s; char t[96];
	if (which == 0) for (size_t i = 0; i < A.doc.bodies.size () && i < 40; i++) { s += body_name (A.doc.bodies[i].id); s += '\n'; }
	if (which == 1)
	{
		std::string ini; read_text (g_printers.c_str (), ini); std::vector<PrintMachine> ms = print_machines (ini.c_str ());
		s = std::string (TR ("Milling \xC2\xB7 G-code for a router (GRBL)")) + "\n" + TR ("Filament printing \xC2\xB7 the nozzle's path") + "\n";
		for (size_t i = 0; i < ms.size () && i < 30; i++) { snprintf (t, sizeof t, TR ("Resin printing \xC2\xB7 %s"), ms[i].name); s += t; s += "\n"; }
	}
	if (which == 2) for (int i = 0; i < 3; i++) { s += PRINT_RESINS[i].name; s += '\n'; }
	return put (s, buf, cap);
}
FC void fc_pick (int which, int i)
{
	if (which == 0 && i >= 0 && i < (int) A.doc.bodies.size ())
	{
		if (A.gen >= 1) { A.print.body = A.doc.bodies[i].id; A.print.tips.clear (); A.doc.changes++; print_refresh (); }
		else { A.job.body = A.doc.bodies[i].id; A.doc.changes++; cam_refresh (); }
		g_view->fit (); ui (R_ALL);
	}
	if (which == 1)
	{
		if (i == 0) gen_set (0);
		else if (i == 1) gen_set (2);
		else
		{
			std::string ini; read_text (g_printers.c_str (), ini); std::vector<PrintMachine> ms = print_machines (ini.c_str ());
			if (i - 2 < (int) ms.size ())
			{
				bool other = strcmp (A.print.machine.name, ms[i - 2].name) != 0; A.print.machine = ms[i - 2];
				if (other) print_presets_save ();
				gen_set (1);
			}
		}
		g_view->fit ();
	}
	if (which == 2 && i >= 0 && i < 3) { A.print.resin = PRINT_RESINS[i]; print_presets_save (); print_touch (); ui (R_ALL); }
	view_dirty ();
}

// ---- the timeline ----
FC int fc_tl_count () { return tl_count (); }
// An item: its icon, and flags: 1 selected, 2 rolled back, 4 failed, 8 a sketch. Its text into buf.
FC int fc_tl_item (int i, int *icon, char *buf, int cap)
{
	if (i < 0 || i >= tl_count ()) return 0;
	static const int EI[] = { I_LINE, I_ARC, I_CIRCLE, I_RECT, I_CLOSE, I_SPLINE, I_POINT };
	*icon = printer () ? (i == 0 ? I_PR_PLATE : I_PR_SUP) : A.camMode ? (i == 0 ? I_CAM_SETUP : A.job.ops[i - 1].kind == CAM_CLEAR ? I_CAM_CLEAR : I_CAM_CONTOUR)
		: A.sketching ? EI[A.sk.els[i].kind] : icon_of_kind (A.doc.feats[i].kind);
	bool off = !A.camMode && !A.sketching && i >= A.doc.upto;
	bool bad = printer () ? false : A.camMode ? i > 0 && A.job.ops[i - 1].failed : !A.sketching && A.doc.feats[i].failed && !off;
	int fl = (i == tl_sel () ? 1 : 0) | (off ? 2 : 0) | (bad ? 4 : 0) | (!A.camMode && !A.sketching && A.doc.feats[i].kind == F_SKETCH ? 8 : 0);
	char t[200]; tl_text (i, t, sizeof t); put (t, buf, cap);
	return fl;
}
// Where the part is rebuilt up to (the blue bar: Design only, else -1); moved.
FC int fc_tl_upto () { return !A.sketching && !A.camMode && tl_count () > 0 ? A.doc.upto : -1; }
FC void fc_tl_set_upto (int k)
{
	if (A.sketching || A.camMode) return;
	if (A.tool != T_SELECT) tool_set (T_SELECT);
	if (k < 0) k = 0; if (k > tl_count ()) k = tl_count ();
	if (k == A.doc.upto) return;
	A.doc.upto = k; A.doc.touch (); if (A.selFeat >= k) A.selFeat = -1; if (!A.doc.body (A.selBody)) A.selBody = -1;
	set_hint (k < tl_count () ? TR ("The part as it was after that step. Drag the bar back to the end for the rest.") : TR ("The whole history again."));
	ui (R_ALL); view_dirty ();
}
// A click on an item (dbl: twice; right: the right button -- the host then shows its menu).
FC void fc_tl_pick (int i, int dbl, int right)
{
	if (i < 0 || i >= tl_count ()) return;
	if (printer ()) { print_page (i == 0 ? 0 : 2); return; }
	if (A.camMode) { A.camSel = i; A.camPage = i == 0 ? 0 : 2; A.camSim = false; cam_hint (); ui (R_ALL); return; }
	if (A.sketching) { A.selEl = A.selEl == i && !right ? -1 : i; sk_arm (); ui (R_ALL); return; }
	if (A.tool != T_SELECT) tool_set (T_SELECT);
	A.selFeat = i; A.selBody = -1; A.selCanvas = -1; ui (R_ALL);
	if (dbl && !right && A.doc.feats[i].kind == F_SKETCH) command (CMD_EDIT_SKETCH);
	view_dirty ();
}

// ---- an icon: kind, s x s, its ink and accent over bg (the host draws it twice, over black and white, for its alpha) ----
FC void fc_icon (int kind, int s, unsigned ink, unsigned acc, unsigned bg, unsigned *px)
{
	Canvas c; c.adopt (px, s, s, s); c.clear (bg & 0xFFFFFF);
	if (kind >= 0) icon (c, kind, 0, 0, s, ink & 0xFFFFFF, acc & 0xFFFFFF, bg & 0xFFFFFF);
	c.px = 0;
}

// ---- the files ----
FC void fc_new ()
{
	if (A.sketching) sketch_end (false);
	A.doc = Doc (); A.saved = 0; A.path[0] = 0; A.undo.clear (); A.redo.clear (); A.selFeat = A.selBody = -1; A.selCanvas = -1;
	job_reset (); A.caption[0] = 0;
	tool_set (T_SELECT); g_view->home ();
}
FC int fc_open (const char *path)
{
	std::string s; Doc d;
	if (!read_text (path, s) || !d.load (s.c_str ())) return -1;
	if (A.sketching) sketch_end (false);
	A.doc = d; A.saved = A.doc.changes; A.undo.clear (); A.redo.clear (); A.selFeat = A.selBody = -1; A.selCanvas = -1;
	job_reset (); cam_load (A.job, s.c_str ()); print_load (A.print, s.c_str ());
	if (strstr (s.c_str (), "\nmanufacture resin")) A.gen = 1;
	if (strstr (s.c_str (), "\nmanufacture filament")) { A.gen = 2; fdm_load (A.fdm, A.fmach, s.c_str ()); A.fdmPct = A.fdm.infill * 100; }
	snprintf (A.path, sizeof A.path, "%s", path);
	if (!A.doc.bodies.empty ()) A.selBody = A.doc.bodies[0].id;
	A.sketching = false; A.caption[0] = 0; tool_set (T_SELECT); g_view->home ();
	return 0;
}
FC int fc_save (const char *path)
{
	std::string s = A.doc.save () + cam_save (A.job) + print_save (A.print) + (A.gen == 1 ? "manufacture resin\n" : A.gen == 2 ? "manufacture filament\n" + fdm_save (A.fdm, A.fmach) : std::string ());
	if (!write_file (path, s.data (), s.size ())) return -1;
	snprintf (A.path, sizeof A.path, "%s", path); A.saved = A.doc.changes; ui (0);
	return 0;
}
// A picture laid on a plane to draw over (main.cpp's canvas_add): "" done, else why not.
FC int fc_canvas_add (const char *path, char *err, int cap)
{
	if (A.camMode) return put (TR ("A canvas belongs to Design."), err, cap);
	const App::CanvasPic *pic = canvas_pic (path);
	if (!pic || !pic->px || pic->w < 1 || pic->h < 1) return put (TR ("That file is not a picture 3DForge can read (PNG, JPEG, BMP, GIF, TIFF)."), err, cap);
	if (A.doc.canvases.size () >= 32) return put (TR ("A part holds 32 canvases at most."), err, cap);
	Canvas3 c; snprintf (c.path, sizeof c.path, "%s", path); snprintf (c.name, sizeof c.name, "%.22s", base_name (path)); char *dot = strrchr (c.name, '.'); if (dot && dot != c.name) *dot = 0;
	c.aspect = (double) pic->h / pic->w;
	if (A.sketching)
	{
		const Plane &pl = A.sk.pl;
		if (fabs (pl.n.y) > 0.999) { c.plane = 1; c.off = pl.o.y; } else if (fabs (pl.n.x) > 0.999) { c.plane = 2; c.off = pl.o.x; } else if (fabs (pl.n.z) > 0.999) c.off = pl.o.z;
	}
	else { undo_push (); if (A.tool == T_SKETCH) { c.plane = A.skPlane; c.off = A.skOffset; } }
	A.doc.canvases.push_back (c); A.doc.changes++;
	if (!A.sketching) { tool_set (T_SELECT); A.selCanvas = (int) A.doc.canvases.size () - 1; A.selBody = A.selFeat = -1; set_hint (TR ("Drag the canvas to move it on its plane, a corner to size it; its values are at the right.")); }
	ui (R_ALL); view_dirty ();
	return put ("", err, cap);
}

// ---- Export: the meshes (STL, OBJ), a flat drawing from a side (DXF, SVG, PDF), a picture (PNG) (main.cpp's ExportDialog) ----
static std::string g_export; static double g_exp[8];
// fmt 0 STL 1 OBJ 2 DXF 3 SVG 4 PDF 5 PNG; what 0 the whole part, 1 the selected body; curve 0..2; view 0..6 (6: as on screen).
// out: 0 triangles, 1 lines, 2 width px, 3 height px, 4 bytes, 5 closed, 6 width mm, 7 height mm.
FC void fc_export (int format, int what, int curve, int view, int binary, int hidden, const char *title, double *out)
{
	Doc d; d.feats = A.doc.feats; d.props = A.doc.props; d.upto = A.doc.upto; d.segs = curve == 0 ? 48 : curve == 1 ? 96 : 192; d.rebuild ();
	int only = what == 1 ? A.selBody : -1;
	static const double VA[7][2] = { { -90, 0 }, { 90, 0 }, { 180, 0 }, { 0, 0 }, { -90, 90 }, { -90, -90 }, { 0, 0 } };
	if (view < 0 || view > 6) view = 0;
	double az = view == 6 ? A.cam.az : VA[view][0], el = view == 6 ? A.cam.el : VA[view][1];
	int tris = 0, nlines = 0, pw = 0, ph = 0; double dw = 0, dh = 0; g_export.clear ();
	if (format == 0 || format == 1) g_export = d.exportMesh (format == 1, binary != 0, only, &tris);
	else if (format == 5) g_export = picture_png (d, only, az, el, 1024, &pw, &ph);
	else
	{
		Drawing g;
		if (make_drawing (d, only, az, el, hidden != 0, g))
		{
			nlines = (int) g.segs.size (); dw = g.w; dh = g.h;
			g_export = format == 2 ? drawing_dxf (g) : format == 3 ? drawing_svg (g) : drawing_pdf (g, title ? title : "part");
		}
	}
	if (format == 0 && !tris) g_export.clear ();
	bool closed = true; for (Body &b : d.bodies) if (b.m.Status () != Manifold::Error::NoError) closed = false;
	double o[8] = { (double) tris, (double) nlines, (double) pw, (double) ph, (double) g_export.size (), closed ? 1.0 : 0.0, dw, dh };
	for (int i = 0; i < 8; i++) out[i] = o[i];
}
FC int fc_export_write (const char *path) { if (g_export.empty ()) return -1; return write_file (path, g_export.data (), g_export.size ()) ? 0 : -1; }
FC int fc_doc_segs () { return A.doc.segs; }

// ---- the G-code: what was checked (lines "state<TAB>text": 0 right, 1 to look at, 2 wrong), the code, its sum ----
static std::string g_code;
FC int fc_gcode (char *checks, int cap, char *sum, int scap)
{
	const CamPaths &p = A.paths; const CamSetup &s = A.job; int lines = 0; char t[200], a[24], b[24], c[24];
	g_code = cam_gcode (A.job, A.paths, A.path[0] ? base_name (A.path) : "part", &lines);
	bool bad = p.hitFast || p.gouge || p.moves.empty ();
	std::string r;
	auto row = [&] (int st, const char *x) { r += std::to_string (st); r += '\t'; r += x; r += '\n'; };
	if (p.moves.empty ()) row (2, TR ("There is no operation to write yet."));
	else
	{
		V3 o = cam_origin (s, p.lo, p.hi);
		row (p.hitFast ? 2 : 0, p.hitFast ? TR ("A fast move goes through matter.") : TR ("No fast move goes through matter."));
		row (p.gouge ? 2 : 0, p.gouge ? TR ("The tool cuts into the body.") : TR ("The body is never cut into."));
		fmt (p.lowestZ - o.z, a, 12); fmt (p.lowestZ - p.lo.z, b, 12);
		if (p.lowestZ < p.lo.z - 1e-6) { fmt (p.lo.z - p.lowestZ, b, 12); snprintf (t, sizeof t, TR ("The lowest point is Z %s: %s mm under the stock (a spoil board)."), a, b); }
		else snprintf (t, sizeof t, TR ("The lowest point is Z %s: %s mm above the stock's underside."), a, b);
		row (p.lowestZ < p.lo.z - 1e-6 ? 1 : 0, t);
		fmt (s.machine.tx, a, 12); fmt (s.machine.ty, b, 12); fmt (s.machine.tz, c, 12);
		snprintf (t, sizeof t, p.outside ? TR ("Larger than the machine's travel (%s \xC3\x97 %s \xC3\x97 %s mm).") : TR ("Within the machine's travel (%s \xC3\x97 %s \xC3\x97 %s mm)."), a, b, c); row (p.outside ? 1 : 0, t);
		fmt (s.tool.flute, a, 12);
		snprintf (t, sizeof t, p.tooDeep ? TR ("Deeper than the tool's cutting length (%s mm).") : TR ("Within the tool's cutting length (%s mm)."), a); row (p.tooDeep ? 1 : 0, t);
	}
	put (r, checks, cap);
	int h = (int) (p.minutes / 60), m = (int) (p.minutes + 0.5) % 60;
	if (h) snprintf (a, sizeof a, "%d h %02d", h, m); else snprintf (a, sizeof a, TR ("%d min"), m);
	snprintf (t, sizeof t, TR ("%d lines \xC2\xB7 %u KB \xC2\xB7 %.1f m cut \xC2\xB7 about %s \xC2\xB7 %s, %d rpm"), lines, (unsigned) (g_code.size () + 1023) / 1024, p.length / 1000, a, s.tool.name, (int) s.tool.rpm);
	put (t, sum, scap);
	return bad ? 0 : 1;
}
FC int fc_gcode_text (char *buf, int cap) { return put (g_code, buf, cap); }
FC int fc_gcode_write (const char *path) { if (g_code.empty ()) return -1; return write_file (path, g_code.data (), g_code.size ()) ? 0 : -1; }

// ---- the print file (a resin printer's): what was checked, its sum, its extension ----
FC int fc_printfile (char *checks, int cap, char *sum, int scap, char *ext, int ecap)
{
	const PrintJob &j = A.pjob; char t[200], a[24];
	bool bad = j.tooLarge || !j.onPlate || j.file.layers.empty ();
	std::string r;
	auto row = [&] (int st, const char *x) { r += std::to_string (st); r += '\t'; r += x; r += '\n'; };
	row (j.tooLarge ? 2 : 0, j.tooLarge ? TR ("It does not fit the printer.") : TR ("It fits the plate and the room."));
	row (j.onPlate ? 0 : 2, j.onPlate ? TR ("The first layers lie on the plate.") : TR ("Nothing lies on the plate: lower the body, or give it supports."));
	if (j.islands) { fmt (j.islandAt, a, 12); snprintf (t, sizeof t, j.islands > 1 ? TR ("%d parts start in mid-air, the first at %s mm: they need a support.") : TR ("%d part starts in mid-air, at %s mm: it needs a support."), j.islands, a); row (1, t); }
	else row (0, TR ("Each layer rests on the one before."));
	put (r, checks, cap);
	int mn = (int) (j.minutes + 0.5); if (mn >= 60) snprintf (a, sizeof a, "%d h %02d", mn / 60, mn % 60); else snprintf (a, sizeof a, TR ("%d min"), mn);
	std::string s;
	snprintf (t, sizeof t, TR ("%d layers of %.3g mm \xC2\xB7 %.1f ml of resin \xC2\xB7 about %s"), (int) j.file.layers.size (), A.print.resin.layer, j.volume, a); s += t; s += "\n";
	snprintf (t, sizeof t, TR ("%s \xC2\xB7 %s \xC2\xB7 %.3g s a layer, %.3g s the first %d"), A.print.machine.name, A.print.resin.name, A.print.resin.exposure, A.print.resin.bottomExposure, (int) (A.print.resin.bottomLayers + 0.5)); s += t;
	put (s, sum, scap); put (A.print.machine.ext, ext, ecap);
	return bad ? 0 : 1;
}
FC int fc_printfile_write (const char *path)
{
	if (A.pjob.file.layers.empty ()) return -1;
	std::string bytes = PRINT_FORMATS[0].write (A.pjob.file);
	return write_file (path, bytes.data (), bytes.size ()) ? 0 : -1;
}
FC void fc_set_hint (const char *s) { set_hint (s); ui (0); }
