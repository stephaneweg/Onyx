// Compositor.cs -- the Pi's whole screen as one picture. rdpd sends windows, not a screen (each Onyx
// window's own pixels, its frame, where it is: Connection.cs keeps that model); the simulator shows one
// screen, so it puts them together as the Pi's compositor does and as Onyx Remote's screenshot does
// (pc/OnyxRemote/Screenshot.cs): the desktop's picture, then the windows bottom to top with their frames
// and their transparency.
// MIT licence (Onyx).
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using OnyxRemote;

namespace PocketTermSim
{
	static class Compositor
	{
		// src (0xAARRGGBB) over dst (opaque)
		static int Over (int dst, int src)
		{
			int a = (src >> 24) & 0xFF;
			if (a == 255) return src;
			if (a == 0) return dst;
			int r = (((src >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * (255 - a)) / 255;
			int g = (((src >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * (255 - a)) / 255;
			int b = ((src & 0xFF) * a + (dst & 0xFF) * (255 - a)) / 255;
			return unchecked ((int) 0xFF000000) | r << 16 | g << 8 | b;
		}

		// px (w x h, Onyx pixels 0xTTRRGGBB) at x, y over the screen. alpha: T is a transparency (an ALPHA
		// window, a frame's corners); keyed: magenta is clear
		static void Blit (int[] scr, int sw, int sh, int[] px, int x, int y, int w, int h, bool alpha, bool keyed, int winAlpha)
		{
			if (px == null || w <= 0 || h <= 0 || px.Length < w * h || winAlpha <= 0) return;
			int x0 = Math.Max (0, x), y0 = Math.Max (0, y), x1 = Math.Min (sw, x + w), y1 = Math.Min (sh, y + h);
			bool plain = !alpha && !keyed && winAlpha >= 255;
			for (int yy = y0; yy < y1; yy++)
			{
				int s = (yy - y) * w - x, d = yy * sw;
				if (plain) { for (int xx = x0; xx < x1; xx++) scr[d + xx] = px[s + xx] | unchecked ((int) 0xFF000000); continue; }
				for (int xx = x0; xx < x1; xx++)
				{
					int c = px[s + xx];
					int a = alpha ? 255 - ((c >> 24) & 0xFF) : keyed && (c & 0xFFFFFF) == 0xFF00FF ? 0 : 255;
					if (winAlpha < 255) a = a * winAlpha / 255;
					scr[d + xx] = Over (scr[d + xx], a << 24 | (c & 0xFFFFFF));
				}
			}
		}

		// The screen from the model (the caller holds c.Lock) into bmp (made again when the size changed).
		public static void Screen (Connection c, ref Bitmap bmp, ref int[] scr)
		{
			int sw = c.ScreenW, sh = c.ScreenH;
			if (sw <= 0 || sh <= 0) return;
			if (scr == null || scr.Length != sw * sh) scr = new int[sw * sh];
			int back = unchecked ((int) 0xFF0B0C0E);
			for (int i = 0; i < scr.Length; i++) scr[i] = back;
			WinModel d;
			bool desk = c.Windows.TryGetValue (WinModel.DESKTOP_ID, out d) && d.W > 0 && d.H > 0 && d.Content.Length >= d.W * d.H;
			if (desk) Blit (scr, sw, sh, d.Content, 0, 0, d.W, d.H, false, false, 255);
			foreach (uint id in c.ZOrder)				// bottom to top
			{
				WinModel m;
				if (id == WinModel.DESKTOP_ID || !c.Windows.TryGetValue (id, out m)) continue;
				if (m.W <= 0 || m.H <= 0 || m.Hidden) continue;
				if ((m.Flags & WinModel.BACKMOST) != 0 && desk) continue;	// (in the desktop's picture)
				bool full = (m.State & WinModel.FULLSCREEN) != 0;
				if (m.HasFrame && !full)
					Blit (scr, sw, sh, (m.State & WinModel.KEYS) != 0 ? m.ChromeA : m.ChromeI, m.X - m.IL, m.Y - m.IT, m.OW, m.OH, true, false, m.Alpha);
				Blit (scr, sw, sh, m.Content, m.X, m.Y, m.W, m.H, (m.Flags & WinModel.ALPHA) != 0, (m.Flags & WinModel.TRANSPARENT) != 0, m.Alpha);
			}
			if (bmp == null || bmp.Width != sw || bmp.Height != sh) { if (bmp != null) bmp.Dispose (); bmp = new Bitmap (sw, sh, PixelFormat.Format32bppRgb); }
			var bd = bmp.LockBits (new Rectangle (0, 0, sw, sh), ImageLockMode.WriteOnly, PixelFormat.Format32bppRgb);
			for (int y = 0; y < sh; y++) Marshal.Copy (scr, y * sw, bd.Scan0 + y * bd.Stride, sw);
			bmp.UnlockBits (bd);
		}
	}
}
