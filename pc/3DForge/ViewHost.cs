// ViewHost.cs -- the 3D view: the core draws it (the bodies by its z-buffer, the tools' marks, the bodies' panel, the
// orientation cube...) into a WriteableBitmap the size of the control, and gets its mouse: a drag turns the view, the
// right button (or Shift) pans, the wheel zooms, a click goes to the tool at work.
// MIT licence (Onyx).
using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Forge
{
	class ViewHost : Border
	{
		readonly Image image = new Image { Stretch = Stretch.Fill };
		WriteableBitmap bmp;
		int pw, ph;			// the picture: the view's size in pixels of the screen ... or of the page, below
		bool captured;
		public event Action Changed;	// (after a mouse event: the window asks the core what changed)

		public ViewHost ()
		{
			Child = image; Background = Brushes.White; ClipToBounds = true; Focusable = false;
			RenderOptions.SetBitmapScalingMode (image, BitmapScalingMode.Linear);
			SizeChanged += (s, e) => Resize ();
			MouseDown += OnMouse; MouseUp += OnMouse; MouseMove += OnMouse;
			MouseWheel += (s, e) => { Send (e.GetPosition (this), e.Delta > 0 ? 1 : -1); e.Handled = true; };
			MouseLeave += (s, e) => { if (!captured) { Core.fc_view_mouse (-1000, -1000, 0, 0, Mods ()); Changed?.Invoke (); } };
			LostMouseCapture += (s, e) => { if (captured) { captured = false; Core.fc_view_mouse (-1000, -1000, 0, 0, Mods ()); Changed?.Invoke (); } };
			AllowDrop = true;
		}
		// (the core works in the view's own units -- the pixels of a 96 dpi screen --, the picture is drawn at that size)
		void Resize ()
		{
			int w = Math.Max (8, (int) ActualWidth), h = Math.Max (8, (int) ActualHeight);
			if (bmp != null && w == pw && h == ph) return;
			pw = w; ph = h;
			bmp = new WriteableBitmap (w, h, 96, 96, PixelFormats.Bgr32, null);
			image.Source = bmp;
			Draw ();
		}
		public void Draw ()
		{
			if (bmp == null) return;
			bmp.Lock ();
			try { Core.fc_view_draw (bmp.BackBuffer, pw, ph, bmp.BackBufferStride / 4); bmp.AddDirtyRect (new Int32Rect (0, 0, pw, ph)); }
			finally { bmp.Unlock (); }
		}
		static int Mods ()
		{
			var m = Keyboard.Modifiers; int r = 0;
			if ((m & ModifierKeys.Control) != 0) r |= 1; if ((m & ModifierKeys.Shift) != 0) r |= 2; if ((m & ModifierKeys.Alt) != 0) r |= 4;
			return r;
		}
		static int Buttons ()
		{
			return (Mouse.LeftButton == MouseButtonState.Pressed ? 1 : 0) | (Mouse.RightButton == MouseButtonState.Pressed ? 2 : 0) | (Mouse.MiddleButton == MouseButtonState.Pressed ? 4 : 0);
		}
		void OnMouse (object s, MouseEventArgs e)
		{
			int b = Buttons ();
			if (b != 0 && !captured) { captured = CaptureMouse (); }
			Send (e.GetPosition (this), 0);
			if (b == 0 && captured) { captured = false; ReleaseMouseCapture (); }
			if (e is MouseButtonEventArgs) e.Handled = true;
		}
		void Send (Point p, int wheel)
		{
			Core.fc_view_mouse ((int) Math.Floor (p.X), (int) Math.Floor (p.Y), Buttons (), wheel, Mods ());
			Changed?.Invoke ();
		}
	}
}
