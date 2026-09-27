// KeyMap.cs -- the PC's keys as the X11 keysyms rdpd expects (as a VNC viewer sends them).
// The text typed goes as characters (the PC's layout applied); a letter / digit / space key
// also reports its down / up ("held": the games' keys).
using System.Windows.Forms;

namespace OnyxRemote
{
	static class KeyMap
	{
		public static bool IsModifier (Keys k)
		{
			return k == Keys.ShiftKey || k == Keys.LShiftKey || k == Keys.RShiftKey || k == Keys.ControlKey
			    || k == Keys.LControlKey || k == Keys.RControlKey || k == Keys.Menu || k == Keys.LMenu || k == Keys.RMenu;
		}

		// the keys sent as keys (not as text); 0: none of them
		public static uint Special (Keys k)
		{
			switch (k)
			{
			case Keys.Back: return 0xFF08;
			case Keys.Tab: return 0xFF09;
			case Keys.Enter: return 0xFF0D;
			case Keys.Escape: return 0xFF1B;
			case Keys.Delete: return 0xFFFF;
			case Keys.Home: return 0xFF50;
			case Keys.Left: return 0xFF51;
			case Keys.Up: return 0xFF52;
			case Keys.Right: return 0xFF53;
			case Keys.Down: return 0xFF54;
			case Keys.PageUp: return 0xFF55;
			case Keys.PageDown: return 0xFF56;
			case Keys.End: return 0xFF57;
			case Keys.Insert: return 0xFF63;
			case Keys.ShiftKey: case Keys.LShiftKey: return 0xFFE1;
			case Keys.RShiftKey: return 0xFFE2;
			case Keys.ControlKey: case Keys.LControlKey: return 0xFFE3;
			case Keys.RControlKey: return 0xFFE4;
			case Keys.Menu: case Keys.LMenu: return 0xFFE9;
			case Keys.RMenu: return 0xFFEA;
			}
			if (k >= Keys.F1 && k <= Keys.F12) return 0xFFBE + (uint) (k - Keys.F1);
			return 0;
		}

		// a key whose down / up the games follow (kapi_key_held): letters, digits, space
		public static uint Held (Keys k)
		{
			if (k >= Keys.A && k <= Keys.Z) return 'a' + (uint) (k - Keys.A);
			if (k >= Keys.D0 && k <= Keys.D9) return '0' + (uint) (k - Keys.D0);
			if (k == Keys.Space) return ' ';
			return 0;
		}
	}
}
