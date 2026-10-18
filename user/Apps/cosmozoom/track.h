//
// track.h -- a circuit: read from its text file (tracks/<id>.trk), its centre line made a smooth closed curve
// through the control points (Catmull-Rom), cut into segments SEG metres long, each with its frame (forward,
// right, up -- banked in the turns), its width and its features (speed pads, item boxes, ramps, tunnels...).
// Everything that moves on it lives in the track's coordinates: s (metres along, 0 = the start line), l (metres
// to the right of the centre), h (metres above the road).
//
// The file (lines "key = value", # comments):
//   name = Candy Lane            name_fr = Allée des Bonbons          theme = candy
//   sky = 7EC8FF FFE7F3          (top, horizon)  ground = 9BE564 7FD35A   road = 6B5B95 75659F
//   curb = FF4F79 FFFFFF         wall = FFD23F   edge = FFFFFF            width = 16     laps = 3
//   p = x z [y [width [bank]]]   a control point, in order (bank in degrees, added to the automatic one)
//   boost = t [lane]             a speed pad at t (0..1 of the lap), lane -1..1 (0 centre)
//   items = t                    a row of item boxes across the road
//   ramp = t                     a kicker: the craft is thrown up
//   jump = t                     a kicker, then a gap in the track (flown over)
//   loop = t                     a vertical loop (radius 12 m, its exit beside its entry)
//   tube = t0 t1                 the road rolls down into a tube ridden on its outside (steering goes round it)
//   twist = t0 t1 [turns]        the road turns over round its own line (a Moebius-like roll: the ships stay on it)
//   fork = t0 t1 [gap [rise]]    the road splits into two branches, left and right, each the road's whole width, which
//                                part (a gap of `gap` metres between them at most, 24 by default) and meet again; with
//                                `rise`, the right one climbs and the left one dips by up to `rise` metres (< 0: the
//                                other way). Eased over 75 m at each end; no tunnel, tube, loop, twist or jump in it.
//   cross = t0 t1 [gap [depth]]  a fork whose branches change sides: the left one dips `depth` metres (10 by default, 9
//                                to 20; < 0: the right one), passes under the other and climbs back on the far side, then
//                                they meet again. At least 440 m long (part 75, dip 80, change sides 220, climb 80, meet 75,
//                                overlapping).
//   time = day | dusk | night | aurora | space      rails = left right      walls = wall trim (the buildings)
//   neon = c1 c2 c3 (the signs)  city = 0..1 (how many buildings)      groundkind = city | water | sand | snow | rock |
//   jungle | clouds | none
//   tunnel = t0 t1               a tunnel's roof from t0 to t1
//   prop = model side spacing [scale [t0 t1]]   props along the sides (side: left / right / both)
//   scatter = model count [scale]               props scattered around on the ground
//   decor = model x z [y [yaw [scale]]]         one prop at a place
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_TRACK_H
#define CZ_TRACK_H

#include "math3d.h"
#include "mesh.h"

namespace cz {

static const float SEG = 2.0f;			// metres a segment

enum { F_BOOST = 1, F_ITEMS = 2, F_RAMP = 4, F_TUNNEL = 8, F_START = 16, F_LOOP = 32, F_GAP = 64, F_TUBE = 128, F_TWIST = 256, F_FORK = 512 };

struct Seg
{
	V3 c, f, r, u;			// the centre, forward, right, up (banked)
	float w;			// the road's width (metres, wall to wall)
	float curv;			// the turn (radians a metre; > 0: to the right)
	unsigned flags;			// F_*
	float lane;			// the boost pad's place (-1..1)
	float tube;			// 0 a flat road .. 1 a whole tube ridden on its outside (the road rolled down around it)
	float fork;			// a fork: how far its branches reach from the centre line (0: one road)
	float bl[2], bh[2];		// a fork: its branches' centres across (0 the left one at the split, 1 the right one) and heights
};

static const float TUBE_R = 7.0f;		// a tube's radius (its round: 44 m)
static const float FORK_EASE = 75.0f;		// a fork's branches part (and meet) over this many metres

struct PropLine { char model[24]; int side; float spacing, scale, t0, t1; };
struct Scatter { char model[24]; int count; float scale; };
struct Decor { char model[24]; float x, z, y, yaw, scale; bool hasY; };

struct Track
{
	enum { MAXP = 160, MAXSEG = 2400, MAXF = 64, MAXPROP = 16, MAXDECOR = 48, MAXFORK = 8 };
	char id[32], name[48], nameFr[48], theme[24];
	unsigned sky[2], ground[2], road[2], curb[2], wall, edge;
	// the look (docs/cosmozoom/DESIGN.md 7.4): the time of day, the rails, the buildings, the neon signs
	enum { T_DAY, T_DUSK, T_NIGHT, T_AURORA, T_SPACE };
	int timeOfDay;
	unsigned rails[2], walls[2], neon[3];
	char groundKind[16];			// city, water, sand, snow, rock, jungle, clouds, none
	float city;				// how many buildings (0 none .. 1 dense)
	float twistT0[8], twistT1[8], twistTurns[8]; int ntwist;
	float width; int laps;
	// the control points
	V3 cp[MAXP]; float cpw[MAXP], cpbank[MAXP]; int ncp;
	// the features (t of the lap)
	struct Feat { int kind; float t0, t1, lane, gap, rise; bool cross; } feat[MAXF]; int nfeat;
	float forkS0[MAXFORK], forkS1[MAXFORK]; int nfork;	// the forks made (metres along: where they begin, end)
	PropLine props[MAXPROP]; int nprops;
	Scatter scat[MAXPROP]; int nscat;
	Decor decor[MAXDECOR]; int ndecor;
	// made
	Seg seg[MAXSEG]; int n;		// segments
	float length;			// n * SEG
	V3 lo, hi;			// the bounding box of the centre line
	float groundY;

	Track () { memset (this, 0, sizeof *this); }

	// ---- reading ----------------------------------------------------------------------------------------
	static unsigned hex (const char *&p)
	{
		while (*p == ' ' || *p == '\t' || *p == ',') p++;
		unsigned v = (unsigned) strtoul (p, (char **) &p, 16);
		return v;
	}
	static int sideOf (const char *s) { return !strncmp (s, "left", 4) ? -1 : !strncmp (s, "right", 5) ? 1 : 0; }

	bool parse (char *text)
	{
		ncp = nfeat = nprops = nscat = ndecor = 0; width = 16; laps = 3;
		sky[0] = 0x7EC8FF; sky[1] = 0xFFF0F8; ground[0] = 0x8BD65C; ground[1] = 0x7CC850;
		road[0] = 0x5E5A80; road[1] = 0x67638A; curb[0] = 0xFF4F79; curb[1] = 0xFFFFFF; wall = 0xFFD23F; edge = 0xFFFFFF;
		strcpy (theme, "meadow");
		timeOfDay = T_NIGHT; rails[0] = 0xFF2BD6; rails[1] = 0x22E6FF; walls[0] = 0x16245E; walls[1] = 0x22E6FF;
		neon[0] = 0xFF2BD6; neon[1] = 0x22E6FF; neon[2] = 0xB8FF2E; strcpy (groundKind, "city"); city = 0; ntwist = 0;
		for (char *line = text; line && *line; )
		{
			char *e = strchr (line, '\n'); if (e) *e = 0;
			char *h = strchr (line, '#'); if (h) *h = 0;
			char *eq = strchr (line, '=');
			if (eq)
			{
				char key[24]; int k = 0;
				for (char *p = line; p < eq && k < 23; p++) if (*p != ' ' && *p != '\t') key[k++] = *p;
				key[k] = 0;
				char *v = eq + 1; while (*v == ' ' || *v == '\t') v++;
				int L = (int) strlen (v); while (L > 0 && (v[L - 1] == '\r' || v[L - 1] == ' ')) v[--L] = 0;
				const char *q = v;
				if (!strcmp (key, "name")) snprintf (name, sizeof name, "%s", v);
				else if (!strcmp (key, "name_fr")) snprintf (nameFr, sizeof nameFr, "%s", v);
				else if (!strcmp (key, "theme")) snprintf (theme, sizeof theme, "%s", v);
				else if (!strcmp (key, "sky")) { sky[0] = hex (q); sky[1] = hex (q); }
				else if (!strcmp (key, "ground")) { ground[0] = hex (q); ground[1] = hex (q); }
				else if (!strcmp (key, "road")) { road[0] = hex (q); road[1] = hex (q); }
				else if (!strcmp (key, "curb")) { curb[0] = hex (q); curb[1] = hex (q); }
				else if (!strcmp (key, "wall")) wall = hex (q);
				else if (!strcmp (key, "rails")) { rails[0] = hex (q); rails[1] = hex (q); curb[0] = rails[0]; curb[1] = rails[1]; }
				else if (!strcmp (key, "walls")) { walls[0] = hex (q); walls[1] = hex (q); }
				else if (!strcmp (key, "neon")) { neon[0] = hex (q); neon[1] = hex (q); neon[2] = hex (q); if (!neon[2]) neon[2] = neon[0]; }
				else if (!strcmp (key, "city")) city = (float) atof (v);
				else if (!strcmp (key, "groundkind")) snprintf (groundKind, sizeof groundKind, "%s", v);
				else if (!strcmp (key, "time"))
					timeOfDay = !strcmp (v, "day") ? T_DAY : !strcmp (v, "dusk") ? T_DUSK : !strcmp (v, "aurora") ? T_AURORA : !strcmp (v, "space") ? T_SPACE : T_NIGHT;
				else if (!strcmp (key, "twist") && ntwist < 8)
				{
					float a = 0, b = 0, t = 1;
					if (sscanf (v, "%f %f %f", &a, &b, &t) >= 2) { twistT0[ntwist] = a; twistT1[ntwist] = b; twistTurns[ntwist] = t; ntwist++; }
				}
				else if (!strcmp (key, "edge")) edge = hex (q);
				else if (!strcmp (key, "width")) width = (float) atof (v);
				else if (!strcmp (key, "laps")) laps = atoi (v);
				else if (!strcmp (key, "p") && ncp < MAXP)
				{
					float x = 0, z = 0, y = 0, w = 0, b = 0;
					int got = sscanf (v, "%f %f %f %f %f", &x, &z, &y, &w, &b);
					if (got >= 2) { cp[ncp] = V3 (x, got >= 3 ? y : 0, z); cpw[ncp] = got >= 4 && w > 0 ? w : 0; cpbank[ncp] = got >= 5 ? b : 0; ncp++; }
				}
				else if ((!strcmp (key, "boost") || !strcmp (key, "items") || !strcmp (key, "ramp") || !strcmp (key, "tunnel") || !strcmp (key, "jump") || !strcmp (key, "loop") || !strcmp (key, "tube")) && nfeat < MAXF)
				{
					Feat &F = feat[nfeat];
					F.kind = key[0] == 'b' ? F_BOOST : key[0] == 'i' ? F_ITEMS : key[0] == 'r' ? F_RAMP : key[0] == 'j' ? F_GAP : key[0] == 'l' ? F_LOOP : !strcmp (key, "tube") ? F_TUBE : F_TUNNEL;
					F.t0 = F.t1 = 0; F.lane = 0;
					float a = 0, b = 0; int got = sscanf (v, "%f %f", &a, &b);
					if (got >= 1)
					{
						F.t0 = a;
						if (F.kind == F_TUNNEL || F.kind == F_TUBE) F.t1 = got >= 2 ? b : a + 0.05f;
						else if (F.kind == F_BOOST && got >= 2) F.lane = b;
						nfeat++;
					}
				}
				else if ((!strcmp (key, "fork") || !strcmp (key, "cross")) && nfeat < MAXF)
				{
					Feat &F = feat[nfeat];
					bool cr = key[0] == 'c';
					float a = 0, b = 0, g = 24, r = cr ? 10 : 0;
					if (sscanf (v, "%f %f %f %f", &a, &b, &g, &r) >= 2)
					{
						F.kind = F_FORK; F.cross = cr; F.t0 = a; F.t1 = b; F.lane = 0; F.gap = clampf (g, 2, 60);
						F.rise = cr ? (r < 0 ? -1.0f : 1.0f) * clampf (fabsf (r), 9, 20) : clampf (r, -12, 12);
						nfeat++;
					}
				}
				else if (!strcmp (key, "prop") && nprops < MAXPROP)
				{
					PropLine &P = props[nprops];
					char side[16] = "both"; P.spacing = 20; P.scale = 1; P.t0 = 0; P.t1 = 1;
					if (sscanf (v, "%23s %15s %f %f %f %f", P.model, side, &P.spacing, &P.scale, &P.t0, &P.t1) >= 1)
					{ P.side = sideOf (side); if (P.spacing < 2) P.spacing = 2; nprops++; }
				}
				else if (!strcmp (key, "scatter") && nscat < MAXPROP)
				{
					Scatter &S = scat[nscat]; S.count = 20; S.scale = 1;
					if (sscanf (v, "%23s %d %f", S.model, &S.count, &S.scale) >= 1) nscat++;
				}
				else if (!strcmp (key, "decor") && ndecor < MAXDECOR)
				{
					Decor &D = decor[ndecor]; D.y = 0; D.yaw = 0; D.scale = 1;
					int got = sscanf (v, "%23s %f %f %f %f %f", D.model, &D.x, &D.z, &D.y, &D.yaw, &D.scale);
					if (got >= 3) { D.hasY = got >= 4; D.yaw *= PI / 180; ndecor++; }
				}
			}
			line = e ? e + 1 : 0;
		}
		if (!nameFr[0]) snprintf (nameFr, sizeof nameFr, "%s", name);
		return ncp >= 4;
	}

	// mirror: x turned over (the Mirror class); reverse: driven the other way (the Remix circuits)
	bool load (const char *path, bool mirror = false, bool reverse = false)
	{
		char *b = readFile (path);
		if (!b) return false;
		bool ok = parse (b);
		free (b);
		if (!ok) return false;
		if (mirror) for (int i = 0; i < ncp; i++) { cp[i].x = -cp[i].x; cpbank[i] = -cpbank[i]; }
		for (int k = 0; mirror && k < nfeat; k++) { feat[k].lane = -feat[k].lane; feat[k].rise = -feat[k].rise; }
		for (int k = 0; mirror && k < ndecor; k++) { decor[k].x = -decor[k].x; decor[k].yaw = -decor[k].yaw; }
		if (reverse)
		{
			// the points the other way round, the start kept at the first point
			for (int i = 1, j = ncp - 1; i < j; i++, j--)
			{
				V3 t = cp[i]; cp[i] = cp[j]; cp[j] = t;
				float w = cpw[i]; cpw[i] = cpw[j]; cpw[j] = w;
				w = cpbank[i]; cpbank[i] = cpbank[j]; cpbank[j] = w;
			}
			for (int i = 0; i < ncp; i++) cpbank[i] = -cpbank[i];
			for (int k = 0; k < ntwist; k++) { float a = 1 - twistT1[k], b = 1 - twistT0[k]; twistT0[k] = a; twistT1[k] = b; twistTurns[k] = -twistTurns[k]; }
			for (int k = 0; k < nfeat; k++)
			{
				Feat &F = feat[k];
				if (F.kind == F_TUNNEL || F.kind == F_TUBE || F.kind == F_FORK) { float a = 1 - F.t1, c = 1 - F.t0; F.t0 = a; F.t1 = c; }
				else F.t0 = 1 - F.t0;
				F.lane = -F.lane;
				if (!F.cross) F.rise = -F.rise;		// (a crossing: its lower branch is the left one again, driven back)
				if (F.kind == F_GAP || F.kind == F_RAMP || F.kind == F_LOOP) F.t0 -= 0.02f;	// (the kicker before the gap)
			}
		}
		build ();
		return true;
	}

	// ---- the curve -----------------------------------------------------------------------------------------
	static V3 catmull (const V3 &p0, const V3 &p1, const V3 &p2, const V3 &p3, float t)
	{
		float t2 = t * t, t3 = t2 * t;
		return (p1 * 2 + (p2 - p0) * t + (p0 * 2 - p1 * 5 + p2 * 4 - p3) * t2 + (p1 * 3 - p0 - p2 * 3 + p3) * t3) * 0.5f;
	}
	static float catmullf (float p0, float p1, float p2, float p3, float t)
	{
		float t2 = t * t, t3 = t2 * t;
		return 0.5f * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (3 * p1 - p0 - 3 * p2 + p3) * t3);
	}

	void build ()
	{
		// the curve sampled finely, its length measured
		enum { SUB = 48 };
		int ns = ncp * SUB;
		V3 *fine = (V3 *) malloc (sizeof (V3) * (size_t) (ns + 1));
		float *fw = (float *) malloc (sizeof (float) * (size_t) (ns + 1)), *fb = (float *) malloc (sizeof (float) * (size_t) (ns + 1));
		float *acc = (float *) malloc (sizeof (float) * (size_t) (ns + 1));
		if (!fine || !fw || !fb || !acc) { free (fine); free (fw); free (fb); free (acc); n = 0; return; }
		for (int i = 0; i < ncp; i++)
			for (int k = 0; k < SUB; k++)
			{
				int a = (i + ncp - 1) % ncp, b = i, c = (i + 1) % ncp, d = (i + 2) % ncp;
				float t = (float) k / SUB;
				fine[i * SUB + k] = catmull (cp[a], cp[b], cp[c], cp[d], t);
				float wa = cpw[a] > 0 ? cpw[a] : width, wb = cpw[b] > 0 ? cpw[b] : width, wc = cpw[c] > 0 ? cpw[c] : width, wd = cpw[d] > 0 ? cpw[d] : width;
				fw[i * SUB + k] = catmullf (wa, wb, wc, wd, t);
				fb[i * SUB + k] = catmullf (cpbank[a], cpbank[b], cpbank[c], cpbank[d], t);
			}
		fine[ns] = fine[0]; fw[ns] = fw[0]; fb[ns] = fb[0];
		acc[0] = 0;
		for (int i = 1; i <= ns; i++) acc[i] = acc[i - 1] + len (fine[i] - fine[i - 1]);
		float total = acc[ns];
		n = (int) (total / SEG + 0.5f);
		if (n > MAXSEG) n = MAXSEG;
		if (n < 8) n = 8;
		float step = total / (float) n;
		length = SEG * (float) n;	// (the segments are made SEG long: the curve's own length scaled a hair)
		int j = 0;
		for (int i = 0; i < n; i++)
		{
			float want = step * (float) i;
			while (j < ns - 1 && acc[j + 1] < want) j++;
			float f = acc[j + 1] > acc[j] ? (want - acc[j]) / (acc[j + 1] - acc[j]) : 0;
			seg[i].c = lerp (fine[j], fine[j + 1], f);
			seg[i].w = lerpf (fw[j], fw[j + 1], f);
			seg[i].lane = lerpf (fb[j], fb[j + 1], f);	// (the extra bank, for now)
			seg[i].flags = 0; seg[i].tube = 0; seg[i].fork = 0; seg[i].bl[0] = seg[i].bl[1] = seg[i].bh[0] = seg[i].bh[1] = 0;
		}
		free (fine); free (fw); free (fb); free (acc);
		spliceLoops ();
		// the frames: forward from the neighbours; the turn (signed, radians a metre); the bank
		for (int i = 0; i < n; i++)
		{
			V3 a = seg[(i + n - 1) % n].c, b = seg[(i + 1) % n].c;
			seg[i].f = norm (b - a);
		}
		float *cv = (float *) malloc (sizeof (float) * (size_t) n);
		for (int i = 0; i < n; i++)
		{
			V3 fa = seg[(i + n - 2) % n].f, fb2 = seg[(i + 2) % n].f;
			V3 fa2 = norm (V3 (fa.x, 0, fa.z)), fb3 = norm (V3 (fb2.x, 0, fb2.z));
			float ang = atan2f (cross (fa2, fb3).y, dot (fa2, fb3));	// > 0: to the left (y up, right-handed)
			cv[i] = -ang / (4 * SEG);
		}
		// smoothed curvature
		for (int i = 0; i < n; i++)
		{
			float s = 0; for (int k = -4; k <= 4; k++) s += cv[(i + k + n) % n];
			seg[i].curv = s / 9;
		}
		free (cv);
		for (int i = 0; i < n; i++)
		{
			Seg &S = seg[i];
			if (S.flags & F_LOOP) { S.r = norm (cross (S.f, S.u)); S.u = norm (cross (S.r, S.f)); S.lane = 0; S.curv = 0; continue; }
			V3 right = norm (cross (S.f, V3 (0, 1, 0)));
			V3 up = cross (right, S.f);
			float bank = clampf (S.curv * 14.0f, -0.42f, 0.42f) + S.lane * PI / 180;
			float cb = cosf (bank), sb = sinf (bank);
			S.r = right * cb - up * sb;	// turning right (bank > 0): the right edge goes down
			S.u = up * cb + right * sb;
			S.lane = 0;
		}
		// the features
		for (int k = 0; k < nfeat; k++)
		{
			const Feat &F = feat[k];
			int i0 = ((int) (F.t0 * (float) n) % n + n) % n;
			if (F.kind == F_TUNNEL)
			{
				int i1 = ((int) (F.t1 * (float) n) % n + n) % n;
				for (int i = i0; i != i1; i = (i + 1) % n) seg[i].flags |= F_TUNNEL;
			}
			else if (F.kind == F_TUBE)
			{
				// rolled down over 40 m, a tube, unrolled over 40 m
				int i1 = ((int) (F.t1 * (float) n) % n + n) % n;
				int len = (i1 - i0 + n) % n, ramp = (int) (40 / SEG);
				if (len < 2 * ramp + 2) len = 2 * ramp + 2;
				for (int d = 0; d < len; d++)
				{
					int i = (i0 + d) % n;
					float k = d < ramp ? smooth01 ((float) d / (float) ramp) : d > len - ramp ? smooth01 ((float) (len - d) / (float) ramp) : 1;
					if (k > seg[i].tube) seg[i].tube = k;
					seg[i].flags |= F_TUBE;
				}
			}
			else if (F.kind == F_BOOST) { for (int d = 0; d < 3; d++) { seg[(i0 + d) % n].flags |= F_BOOST; seg[(i0 + d) % n].lane = F.lane; } }
			else if (F.kind == F_GAP) { seg[i0].flags |= F_RAMP; for (int d = 3; d < 7; d++) seg[(i0 + d) % n].flags |= F_GAP; }
			else if (F.kind == F_LOOP || F.kind == F_FORK) {}
			else seg[i0].flags |= (unsigned) F.kind;
		}
		// the twists: r and u turned round f, smoothly, by turns x 2 pi over the span
		for (int k = 0; k < ntwist; k++)
		{
			int i0 = (int) (twistT0[k] * (float) n), i1 = (int) (twistT1[k] * (float) n);
			int len = i1 - i0; if (len < 4) continue;
			for (int d = 0; d <= len; d++)
			{
				int i = ((i0 + d) % n + n) % n;
				float a = 2 * PI * twistTurns[k] * smooth01 ((float) d / (float) len);
				float ca = cosf (a), sa = sinf (a);
				Seg &S = seg[i];
				V3 r = S.r * ca + S.u * sa, u = S.u * ca - S.r * sa;
				S.r = r; S.u = u; S.flags |= F_TWIST;
			}
		}
		for (int i = 0; i < n; i++)
			if (seg[i].tube > 0)
			{
				Seg &S = seg[i];
				// the bank fades out, the width grows to the tube's round
				V3 right = norm (cross (S.f, V3 (0, 1, 0))), up = cross (right, S.f);
				S.r = norm (lerp (S.r, right, S.tube)); S.u = norm (lerp (S.u, up, S.tube));
				S.w = lerpf (S.w, 2 * PI * TUBE_R, S.tube);
			}
		forks ();
		seg[0].flags |= F_START;
		lo = V3 (1e9f, 1e9f, 1e9f); hi = V3 (-1e9f, -1e9f, -1e9f);
		for (int i = 0; i < n; i++)
		{
			const V3 &p = seg[i].c;
			if (p.x < lo.x) lo.x = p.x; if (p.y < lo.y) lo.y = p.y; if (p.z < lo.z) lo.z = p.z;
			float dip = fminf (seg[i].bh[0], seg[i].bh[1]); if (p.y + dip < lo.y) lo.y = p.y + dip;	// (a fork's lower branch)
			if (p.x > hi.x) hi.x = p.x; if (p.y > hi.y) hi.y = p.y; if (p.z > hi.z) hi.z = p.z;
		}
		groundY = lo.y - ((!strcmp (groundKind, "clouds") || !strcmp (groundKind, "islands")) ? 55.0f : 3.0f);	// (a sea of clouds: far below the floating track)
	}

	// The forks: from t0 to t1 the road's two branches part -- their centres at -c and +c, eased from 0 to
	// (gap + width) / 2 over FORK_EASE metres at each end -- and meet again; overlapping (2 c < width) they make one
	// wider road. A plain fork's rise follows the gap's opening (no step where the branches still touch). A crossing:
	// parted, the lower branch dips, the two change sides (the lower under the other), it climbs back, they meet. Not
	// where the road is a tunnel, a tube, a loop, a twist or a gap: the fork is left out there.
	static void forkShape (const Feat &F, float x, float len, float w, float *bl, float *bh)
	{
		const float E = FORK_EASE, LD = 80, LX = 220;
		float C = (F.gap + w) * 0.5f;
		float m = x < E ? smooth01 (x / E) : x > len - E ? smooth01 ((len - x) / E) : 1;
		m *= C;
		bh[0] = bh[1] = 0;
		if (!F.cross)
		{
			bl[0] = -m; bl[1] = m;
			float open = 2 * m - w;
			if (open > 0) { bh[1] = F.rise * smooth01 (open / F.gap); bh[0] = -bh[1]; }
			return;
		}
		// (part; dip; change sides -- under while they pass, the dip whole by then; climb; meet: 390 m and the rest in holds)
		float hold = fmaxf (0, len - (2 * E + 1.5f * LD + 0.75f * LX)) * 0.25f;
		float b0 = E + hold, c0 = b0 + LD * 0.5f + hold, d0 = c0 + LX * 0.75f + hold;
		float q = smooth01 ((x - c0) / LX), dip = smooth01 ((x - b0) / LD) - smooth01 ((x - d0) / LD);
		bl[0] = -m * cosf (PI * q); bl[1] = m * cosf (PI * q);
		bh[F.rise > 0 ? 0 : 1] = -fabsf (F.rise) * dip;
	}
	void forks ()
	{
		nfork = 0;
		for (int k = 0; k < nfeat && nfork < MAXFORK; k++)
		{
			const Feat &F = feat[k];
			if (F.kind != F_FORK) continue;
			int i0 = ((int) (F.t0 * (float) n) % n + n) % n, i1 = ((int) (F.t1 * (float) n) % n + n) % n;
			int len = (i1 - i0 + n) % n, ramp = (int) (FORK_EASE / SEG);
			if (len < 2 * ramp + 4 || len > n / 2 || (F.cross && (float) len * SEG < 440)) continue;
			bool clash = false;
			for (int d = 0; d <= len && !clash; d++)
			{
				const Seg &S = seg[(i0 + d) % n];
				if ((S.flags & (F_TUNNEL | F_TUBE | F_LOOP | F_TWIST | F_GAP)) || S.tube > 0 || S.fork > 0 || (i0 + d) % n == 0) clash = true;
			}
			if (clash) continue;
			for (int d = 0; d <= len; d++)
			{
				Seg &S = seg[(i0 + d) % n];
				forkShape (F, (float) d * SEG, (float) len * SEG, S.w, S.bl, S.bh);
				S.fork = fmaxf (fabsf (S.bl[0]), fabsf (S.bl[1]));
				if (S.fork > 0) S.flags |= F_FORK;
				// a speed pad in it: on a branch, never astride the gap
				if ((S.flags & F_BOOST) && S.fork > 0 && fabsf (S.lane) < 0.2f) S.lane = S.lane < 0 ? -0.5f : 0.5f;
			}
			forkS0[nfork] = (float) i0 * SEG; forkS1[nfork] = (float) ((i0 + len) % n) * SEG; nfork++;
		}
	}

	// The loops: at each one's place, the centre line goes round a vertical circle (radius R), drifting sideways by
	// a road's width so that it comes out beside where it went in, then eases back onto its line.
	void spliceLoops ()
	{
		for (int k = 0; k < nfeat; k++)
		{
			if (feat[k].kind != F_LOOP) continue;
			const float R = 12;
			int N = (int) (2 * PI * R / SEG + 0.5f);
			if (n + N >= MAXSEG) continue;
			int i0 = ((int) (feat[k].t0 * (float) n) % n + n) % n;
			if (i0 < 2) i0 = 2;
			V3 B = seg[i0].c;
			V3 F = seg[(i0 + 1) % n].c - seg[(i0 + n - 1) % n].c; F.y = 0; F = norm (F);
			V3 Up (0, 1, 0), Rt = norm (cross (F, Up));
			float w = seg[i0].w, off = w + 6;
			// make room: segments i0+1.. move N places on
			memmove (&seg[i0 + 1 + N], &seg[i0 + 1], sizeof (Seg) * (size_t) (n - i0 - 1));
			for (int j = 1; j <= N; j++)
			{
				float th = 2 * PI * (float) j / (float) (N + 1);
				float side = off * smooth01 ((float) j / (float) (N + 1));
				Seg &S = seg[i0 + j];
				S.c = B + F * (R * sinf (th)) + Up * (R * (1 - cosf (th))) + Rt * side;
				S.w = w; S.flags = F_LOOP; S.lane = 0; S.tube = 0; S.fork = 0; S.bl[0] = S.bl[1] = S.bh[0] = S.bh[1] = 0;
				V3 centre = B + Up * R + Rt * side;
				S.u = norm (centre - S.c);
			}
			n += N;
			// the rest of the lap: eased back from the side over 60 m
			int ease = (int) (60 / SEG);
			for (int j = 0; j < n - i0 - 1 - N; j++)
			{
				float f = j < ease ? 1 - smooth01 ((float) j / (float) ease) : 0;
				if (f <= 0) break;
				seg[i0 + 1 + N + j].c += Rt * (off * f);
			}
			// the features after it, moved by the loop's segments (their t over the new count)
			float tl = (float) (i0 + 1) / (float) (n - N);
			for (int q = 0; q < nfeat; q++)
			{
				Feat &G = feat[q];
				float scale = (float) (n - N) / (float) n;
				auto fix = [&] (float t) { float base = t * scale; return t > tl ? base + (float) N / (float) n : base; };
				G.t0 = fix (G.t0); G.t1 = fix (G.t1);
				(void) 0;
				if (q != k && G.kind == F_LOOP && G.t0 < feat[k].t0) {}
			}
			for (int q = 0; q < ntwist; q++)
			{
				float scale = (float) (n - N) / (float) n;
				auto fix = [&] (float t) { float base = t * scale; return t > tl ? base + (float) N / (float) n : base; };
				twistT0[q] = fix (twistT0[q]); twistT1[q] = fix (twistT1[q]);
			}
			feat[k].t0 = 2;	// (done)
		}
		length = SEG * (float) n;
	}

	// ---- where things are --------------------------------------------------------------------------------
	float wrapS (float s) const { while (s < 0) s += length; while (s >= length) s -= length; return s; }
	// a's distance ahead of b along the lap (-length/2 .. length/2)
	float ahead (float a, float b) const { float d = a - b; while (d > length * 0.5f) d -= length; while (d < -length * 0.5f) d += length; return d; }
	int segAt (float s) const { int i = (int) (wrapS (s) / SEG); return i >= n ? n - 1 : i; }
	// The frame at s, interpolated between two segments.
	void frameAt (float s, V3 *c, V3 *f, V3 *r, V3 *u, float *w = 0) const
	{
		s = wrapS (s);
		int i = (int) (s / SEG); if (i >= n) i = n - 1;
		int j = (i + 1) % n;
		float k = (s - (float) i * SEG) / SEG;
		const Seg &A = seg[i], &B = seg[j];
		if (c) *c = lerp (A.c, B.c, k);
		if (f) *f = norm (lerp (A.f, B.f, k));
		if (r) *r = norm (lerp (A.r, B.r, k));
		if (u) *u = norm (lerp (A.u, B.u, k));
		if (w) *w = lerpf (A.w, B.w, k);
	}
	float tubeAt (float s) const
	{
		s = wrapS (s);
		int i = (int) (s / SEG); if (i >= n) i = n - 1;
		float k = (s - (float) i * SEG) / SEG;
		return lerpf (seg[i].tube, seg[(i + 1) % n].tube, k);
	}
	// Is the road a whole tube here (no edges: across wraps round)?
	bool closedAt (float s) const { return tubeAt (s) > 0.985f; }
	// The road's surface at (s, l), h above it: the point, its normal (the ship's up), the way across (to the
	// right along the surface). A tube: the road rolled down with the curvature tube / TUBE_R.
	static void surfOf (const V3 &c, const V3 &r, const V3 &u, float tube, float l, float h, V3 *p, V3 *nrm, V3 *lat)
	{
		float kap = tube / TUBE_R;
		if (kap < 1e-4f)
		{
			if (p) *p = c + r * l + u * h;
			if (nrm) *nrm = u;
			if (lat) *lat = r;
			return;
		}
		float ph = kap * l, sp = sinf (ph), cp2 = cosf (ph);
		V3 n = r * sp + u * cp2;
		if (p) *p = c - u * (1 / kap) + n * (1 / kap + h);
		if (nrm) *nrm = n;
		if (lat) *lat = r * cp2 - u * sp;
	}
	void surf (float s, float l, float h, V3 *p, V3 *nrm = 0, V3 *lat = 0, V3 *fwd = 0) const { surfB (s, l, h, -1, p, nrm, lat, fwd); }
	// the same on a fork's branch br (-1: the one under l)
	void surfB (float s, float l, float h, int br, V3 *p, V3 *nrm = 0, V3 *lat = 0, V3 *fwd = 0) const
	{
		V3 c, f, r, u; frameAt (s, &c, &f, &r, &u);
		if (forkAt (s) > 0)
		{
			// up or down its branch: lifted, its normal tilted with the slope
			int k = nearBranch (s, l, br);
			float h0, h1, hb; branchAt (s, k, 0, &hb); branchAt (s - 1, k, 0, &h0); branchAt (s + 1, k, 0, &h1);
			c += u * hb;
			float slope = (h1 - h0) * 0.5f;
			surfOf (c, r, u, tubeAt (s), l, h, p, nrm, lat);
			if (nrm) *nrm = norm (*nrm - f * slope);
			if (fwd) *fwd = norm (f + u * slope);
			return;
		}
		surfOf (c, r, u, tubeAt (s), l, h, p, nrm, lat);
		if (fwd) *fwd = f;
	}
	V3 pointAtB (float s, float l, float h, int br) const { V3 p; surfB (s, l, h, br, &p); return p; }
	V3 pointAt (float s, float l, float h) const { V3 p; surf (s, l, h, &p); return p; }
	// l brought back into the tube's round (closed) -- or as it is
	float wrapL (float s, float l) const
	{
		if (!closedAt (s)) return l;
		float C = widthAt (s);
		while (l > C * 0.5f) l -= C;
		while (l < -C * 0.5f) l += C;
		return l;
	}
	// b's distance across from a (the short way round a tube)
	float across (float s, float a, float b) const { return wrapL (s, b - a); }
	float widthAt (float s) const { float w; frameAt (s, 0, 0, 0, 0, &w); return w; }
	// ---- the forks ----
	// how far its branches reach from the centre line (0: one road)
	float forkAt (float s) const
	{
		s = wrapS (s);
		int i = (int) (s / SEG); if (i >= n) i = n - 1;
		const Seg &A = seg[i], &B = seg[(i + 1) % n];
		if (A.fork <= 0 && B.fork <= 0) return 0;
		return lerpf (A.fork, B.fork, (s - (float) i * SEG) / SEG);
	}
	// branch k at s: its centre across, its height
	void branchAt (float s, int k, float *l, float *h) const
	{
		s = wrapS (s);
		int i = (int) (s / SEG); if (i >= n) i = n - 1;
		const Seg &A = seg[i], &B = seg[(i + 1) % n];
		float f = (s - (float) i * SEG) / SEG;
		if (l) *l = lerpf (A.bl[k], B.bl[k], f);
		if (h) *h = lerpf (A.bh[k], B.bh[k], f);
	}
	// Are the branches apart here (a gap between them, or one under the other)?
	bool splitAt (float s) const
	{
		if (forkAt (s) <= 0) return false;
		float l0, l1, h0, h1; branchAt (s, 0, &l0, &h0); branchAt (s, 1, &l1, &h1);
		return fabsf (l1 - l0) > widthAt (s) || fabsf (h1 - h0) > 2.5f;
	}
	// The branch a thing at l is on: -1 one road (no fork, or its branches still touching); `was` kept once apart
	int branchOf (float s, float l, int was) const
	{
		if (!splitAt (s)) return -1;
		if (was == 0 || was == 1) return was;
		float l0, l1; branchAt (s, 0, &l0, 0); branchAt (s, 1, &l1, 0);
		return fabsf (l - l1) < fabsf (l - l0) ? 1 : 0;
	}
	// the branch under l (one road: the nearer one's centre side)
	int nearBranch (float s, float l, int br) const
	{
		if (br == 0 || br == 1) return br;
		float l0, l1; branchAt (s, 0, &l0, 0); branchAt (s, 1, &l1, 0);
		return fabsf (l - l1) < fabsf (l - l0) || (l0 == l1 && l >= 0) ? 1 : 0;
	}
	// the road's height at (s, l) above the centre line's frame: a fork's branch's (0 elsewhere)
	float heightOf (float s, float l, int br) const
	{
		if (forkAt (s) <= 0) return 0;
		float h; branchAt (s, nearBranch (s, l, br), 0, &h); return h;
	}
	// The road under l at s, wall to wall: [lo, hi] -- one road, the wider one where a fork's branches still touch, or
	// branch br (-1: the nearer one)
	void limits (float s, float l, int br, float *lo, float *hi) const
	{
		float w = widthAt (s);
		if (forkAt (s) <= 0) { *lo = -(w * 0.5f); *hi = w * 0.5f; return; }
		float l0, l1; branchAt (s, 0, &l0, 0); branchAt (s, 1, &l1, 0);
		if (!splitAt (s)) { *lo = fminf (l0, l1) - w * 0.5f; *hi = fmaxf (l0, l1) + w * 0.5f; return; }
		float bc = nearBranch (s, l, br) ? l1 : l0;
		*lo = bc - w * 0.5f; *hi = bc + w * 0.5f;
	}
	// l brought onto the road (pad metres inside its walls): out of a fork's gap onto the nearer branch (or br)
	float onRoad (float s, float l, float pad, int br = -1) const
	{
		if (forkAt (s) <= 0) return l;
		float lo, hi; limits (s, l, br, &lo, &hi);
		return clampf (l, lo + pad, hi - pad);
	}
	// A lane (-1..1 across the road, 0 its middle) as l: in a fork, -1..0 across the branch on the left and 0..1 across
	// the one on the right (0.5: a branch's middle) once they part
	float laneL (float s, float lane) const
	{
		float w = widthAt (s);
		if (forkAt (s) <= 0) return lane * w * 0.5f;
		float l0, l1; branchAt (s, 0, &l0, 0); branchAt (s, 1, &l1, 0);
		float a = fminf (l0, l1), b = fmaxf (l0, l1);
		if (!splitAt (s)) return (a + b) * 0.5f + lane * ((b - a) * 0.5f + w * 0.5f);
		return lane < 0 ? a + (lane + 0.5f) * w : b + (lane - 0.5f) * w;
	}
	// the way a branch goes, against the centre line's (radians, > 0: to the right)
	float branchYa (float s, float l, int br = -1) const
	{
		if (forkAt (s) <= 0) return 0;
		int k = nearBranch (s, l, br);
		float a, b; branchAt (s - 1, k, &a, 0); branchAt (s + 1, k, &b, 0);
		return atanf ((b - a) * 0.5f);
	}
	// the fork at s or within `before` metres ahead (-1: none)
	int forkNear (float s, float before) const
	{
		for (int k = 0; k < nfork; k++)
		{
			float d0 = ahead (forkS0[k], s), d1 = ahead (forkS1[k], s);
			if (d0 <= before && d1 >= 0) return k;
		}
		return -1;
	}
	const char *title (bool fr) const { return fr && nameFr[0] ? nameFr : name; }
};

} // namespace cz

#endif
