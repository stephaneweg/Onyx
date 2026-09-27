// Connection.cs -- the link to rdpd: the protocol (see user/bin/rdpd.c), a reading thread that
// keeps the model of the Onyx windows (their place, frame, pixels) up to date, and the
// messages sent back (pointer, keys, focus, close).
using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading;

namespace OnyxRemote
{
	// One Onyx window as the client knows it (written by the reading thread under the lock).
	class WinModel
	{
		public uint Id;
		public int X, Y, W, H;				// the client area on the Pi's screen
		public int OW, OH, IL, IT;			// the whole window with its frame (0 0: none)
		public uint Flags; public int Alpha; public int State;
		public string Title = "";
		public int[] Content = new int[0];		// W x H, 0x00RRGGBB
		public int[] ChromeA = new int[0], ChromeI = new int[0];	// OW x OH
		public bool Dirty = true;			// pixels changed since shown
		public const uint BORDERLESS = 1, BACKMOST = 2, TOPMOST = 4, TRANSPARENT = 8, SYSTEM = 16;
		public const int KEYS = 1, FULLSCREEN = 2;
		public const uint DESKTOP_ID = 0xFFFFFFFF;		// (the desktop: the wallpaper + the backmost windows)
		public bool HasFrame { get { return OW > 0 && OH > 0; } }
	}

	class Connection
	{
		public int ScreenW, ScreenH, KernelAbi;
		public readonly object Lock = new object ();
		public readonly Dictionary<uint, WinModel> Windows = new Dictionary<uint, WinModel> ();
		public List<uint> ZOrder = new List<uint> ();
		public event Action RoundDone;			// (on the reading thread)
		public event Action<string> Closed;

		TcpClient tcp; NetworkStream net; BinaryReader rd;
		readonly object sendLock = new object ();
		bool bpp16;
		volatile bool stop;

		public void Open (string host, int port, bool bits16, bool desktopOn, bool onyxFrames)
		{
			bpp16 = bits16;
			tcp = new TcpClient ();
			tcp.NoDelay = true;
			tcp.Connect (host, port);
			net = tcp.GetStream ();
			rd = new BinaryReader (net);
			byte[] hello = rd.ReadBytes (14);
			if (hello.Length < 14 || Encoding.ASCII.GetString (hello, 0, 8) != "ONYXRDP1") throw new IOException ("not an Onyx rdpd server (or an older rdpd: update the SD card)");
			ScreenW = hello[8] | hello[9] << 8; ScreenH = hello[10] | hello[11] << 8; KernelAbi = hello[12] | hello[13] << 8;
			byte[] ans = new byte[9];
			Encoding.ASCII.GetBytes ("ONYXRDP1").CopyTo (ans, 0);
			ans[8] = (byte) ((bits16 ? 1 : 0) | (onyxFrames ? 0 : 2));	// (2: no frames sent)
			Send (ans);
			Desktop (desktopOn);
			new Thread (ReadLoop) { IsBackground = true, Name = "rdpd reader" }.Start ();
			Ready ();
		}

		public void Close () { stop = true; try { tcp?.Close (); } catch { } }

		// ---- sending ----
		void Send (byte[] b) { lock (sendLock) { try { net.Write (b, 0, b.Length); } catch { } } }
		public void Ready () { Send (new byte[] { 1 }); }
		public void Pointer (uint id, int x, int y, int buttons, int wheel)
		{
			byte[] b = new byte[11];
			b[0] = 2; BitConverter.GetBytes (id).CopyTo (b, 1);
			b[5] = (byte) x; b[6] = (byte) (x >> 8); b[7] = (byte) y; b[8] = (byte) (y >> 8);
			b[9] = (byte) buttons; b[10] = (byte) (sbyte) wheel;
			Send (b);
		}
		public void Key (bool down, bool heldOnly, uint keysym)
		{
			byte[] b = new byte[6];
			b[0] = 3; b[1] = (byte) ((down ? 1 : 0) | (heldOnly ? 2 : 0)); BitConverter.GetBytes (keysym).CopyTo (b, 2);
			Send (b);
		}
		public void Desktop (bool on) { Send (new byte[] { 7, (byte) (on ? 1 : 0) }); }
		public void Char (uint c) { byte[] b = new byte[5]; b[0] = 6; BitConverter.GetBytes (c).CopyTo (b, 1); Send (b); }
		public void Raise (uint id) { byte[] b = new byte[5]; b[0] = 4; BitConverter.GetBytes (id).CopyTo (b, 1); Send (b); }
		public void CloseWindow (uint id) { byte[] b = new byte[5]; b[0] = 5; BitConverter.GetBytes (id).CopyTo (b, 1); Send (b); }

		// ---- receiving ----
		byte[] unpack = new byte[0];

		void ReadLoop ()
		{
			string why = "the connection was closed";
			try
			{
				while (!stop)
				{
					int type = rd.ReadByte ();
					int len = rd.ReadInt32 ();
					byte[] p = len > 0 ? rd.ReadBytes (len) : new byte[0];
					if (p.Length < len) throw new EndOfStreamException ();
					lock (Lock) Handle (type, p);
					if (type == 5) RoundDone?.Invoke ();
				}
			}
			catch (Exception e) { if (!stop) why = e.Message; }
			if (!stop) Closed?.Invoke (why);
		}

		static int U16 (byte[] p, int o) { return p[o] | p[o + 1] << 8; }
		static int S16 (byte[] p, int o) { return (short) U16 (p, o); }
		static uint U32 (byte[] p, int o) { return BitConverter.ToUInt32 (p, o); }

		void Handle (int type, byte[] p)
		{
			switch (type)
			{
			case 1:		// WIN
			{
				uint id = U32 (p, 0);
				WinModel w;
				if (!Windows.TryGetValue (id, out w)) { w = new WinModel { Id = id }; Windows[id] = w; }
				w.X = S16 (p, 4); w.Y = S16 (p, 6);
				int nw = U16 (p, 8), nh = U16 (p, 10), now = U16 (p, 12), noh = U16 (p, 14);
				w.IL = U16 (p, 16); w.IT = U16 (p, 18);
				w.Flags = U32 (p, 20); w.Alpha = p[24]; w.State = p[25];
				int tn = p[26];
				w.Title = Encoding.GetEncoding (28591).GetString (p, 27, tn);
				if (nw != w.W || nh != w.H) { w.W = nw; w.H = nh; w.Content = new int[nw * nh]; }
				if (now != w.OW || noh != w.OH) { w.OW = now; w.OH = noh; w.ChromeA = new int[now * noh]; w.ChromeI = new int[now * noh]; }
				w.Dirty = true;
				break;
			}
			case 2: Windows.Remove (U32 (p, 0)); break;	// GONE
			case 3:		// ZORDER
			{
				int n = U16 (p, 0);
				var z = new List<uint> ();
				for (int i = 0; i < n; i++) z.Add (U32 (p, 2 + 4 * i));
				ZOrder = z;
				break;
			}
			case 4:		// PIXELS
			{
				uint id = U32 (p, 0);
				int part = p[4], bpp = p[5], lz = p[6];
				int x = U16 (p, 7), y = U16 (p, 9), w = U16 (p, 11), h = U16 (p, 13);
				WinModel m;
				if (!Windows.TryGetValue (id, out m)) break;
				int[] dst; int dw, dh;
				if (part == 0) { dst = m.Content; dw = m.W; dh = m.H; }
				else { dst = part == 1 ? m.ChromeA : m.ChromeI; dw = m.OW; dh = m.OH; }
				int bytes = w * h * (bpp == 16 ? 2 : 4);
				byte[] src; int so;
				if (lz != 0)
				{
					if (unpack.Length < bytes) unpack = new byte[bytes];
					if (!Lz4.Decode (p, 15, p.Length - 15, unpack, bytes)) break;
					src = unpack; so = 0;
				}
				else { src = p; so = 15; }
				for (int j = 0; j < h; j++)
				{
					if (y + j >= dh) break;
					int row = (y + j) * dw;
					for (int i = 0; i < w && x + i < dw; i++)
					{
						int c;
						if (bpp == 16)
						{
							int o = so + (j * w + i) * 2, v = src[o] | src[o + 1] << 8;
							int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
							c = (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
						}
						else c = BitConverter.ToInt32 (src, so + (j * w + i) * 4) & 0xFFFFFF;
						dst[row + x + i] = c;
					}
				}
				m.Dirty = true;
				break;
			}
			}
		}
	}
}
