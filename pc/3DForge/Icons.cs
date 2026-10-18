// Icons.cs -- 3DForge's icons (ficons.h: drawn from their geometry by the core) as WPF pictures: each drawn twice, over
// black and over white, gives its colours and its transparency. Kept once made.
// MIT licence (Onyx).
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Forge
{
	static class Icons
	{
		static readonly Dictionary<string, BitmapSource> cache = new Dictionary<string, BitmapSource> ();
		public const uint INK = 0x3C4150, ACC = 0x2B6CD4;

		// The icon `kind`, s x s pixels (made at the screen's scale: sharp), ink its lines, acc its accent.
		public static BitmapSource Get (int kind, int s, uint ink = INK, uint acc = ACC)
		{
			double scale = Scale;
			int px = (int) System.Math.Round (s * scale);
			string key = kind + "/" + px + "/" + ink + "/" + acc;
			if (cache.TryGetValue (key, out var b)) return b;
			var black = new uint[px * px]; var white = new uint[px * px];
			Core.fc_icon (kind, px, ink, acc, 0x000000, black);
			Core.fc_icon (kind, px, ink, acc, 0xFFFFFF, white);
			var o = new uint[px * px];
			for (int i = 0; i < o.Length; i++)
			{
				uint k = black[i], w = white[i];
				int a = 255 - ((int) ((w >> 8) & 255) - (int) ((k >> 8) & 255));
				if (a <= 0) { o[i] = 0; continue; }
				if (a > 255) a = 255;
				// premultiplied: what was drawn over black is the colour times its cover
				o[i] = (uint) a << 24 | (k & 0xFFFFFF);
			}
			var bmp = BitmapSource.Create (px, px, 96 * scale, 96 * scale, PixelFormats.Pbgra32, null, o, px * 4);
			bmp.Freeze ();
			cache[key] = bmp;
			return bmp;
		}
		public static Image Image (int kind, int s, uint ink = INK, uint acc = ACC)
		{
			var im = new Image { Source = Get (kind, s, ink, acc), Width = s, Height = s, SnapsToDevicePixels = true };
			RenderOptions.SetBitmapScalingMode (im, BitmapScalingMode.HighQuality);
			return im;
		}
		public static double Scale
		{
			get
			{
				var w = Application.Current?.MainWindow;
				if (w != null) { var src = PresentationSource.FromVisual (w); if (src?.CompositionTarget != null) return src.CompositionTarget.TransformToDevice.M11; }
				return 1;
			}
		}
		public static uint Rgb (string hex, uint dflt)
		{
			if (string.IsNullOrEmpty (hex)) return dflt;
			try { return System.Convert.ToUInt32 (hex, 16); } catch { return dflt; }
		}
		public static Color Colour (uint rgb) => Color.FromRgb ((byte) (rgb >> 16), (byte) (rgb >> 8), (byte) rgb);
		public static Brush Brush (string hex, Brush dflt)
		{
			if (string.IsNullOrEmpty (hex)) return dflt;
			var b = new SolidColorBrush (Colour (Rgb (hex, 0))); b.Freeze (); return b;
		}
	}
}
