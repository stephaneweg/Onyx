// Connection.cs -- the link to rdpd: the protocol (see user/bin/rdpd.c), a reading thread that
// keeps the model of the Onyx windows (their place, frame, pixels) up to date, a sending thread
// for the messages sent back (pointer, keys, focus, close), and the reconnection.
//
// Made for a lossy link (Wi-Fi): every TCP segment lost stalls what follows it until it is
// resent, so the client sends few of them and keeps the server busy:
//  * the pointer's moves are coalesced: at most one every MoveMs, the latest position (a button
//    change or the wheel goes at once and is never dropped; the keys at once, in order; what
//    queues while a write is under way goes out with it, in one segment);
//  * pipelined rounds (option bit 2, an rdpd that answers CAPS): up to 3 rounds in flight, so one
//    lost READY no longer freezes the screen; a round is applied to the model only when its END
//    has come (whole rounds only: the window shows no half-updated state);
//  * the server's PINGs are answered at once (PONG): after a loss, these segments make the
//    other side resend sooner (duplicate ACKs) -- see rdpd.c;
//  * the connection lost (an error, or nothing heard for SilentMs), it reconnects by itself with
//    the same options, waiting longer each time (0.5 s .. 8 s), the windows kept meanwhile;
//  * a message whose fields are out of range is skipped (counted: Damaged), the pixels clipped to
//    their window's buffers; a stream whose framing is wrong (an unknown type, a huge length:
//    bytes lost or doubled) is dropped and the connection made again.
using System;
using System.Collections.Generic;
using System.Diagnostics;
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
		public int[] Content = new int[0];		// W x H, 0xTTRRGGBB (T: a transparency, 0 opaque .. 255
		public int[] ChromeA = new int[0], ChromeI = new int[0];	// OW x OH    clear: an ALPHA window's, the frame's corners)
		public bool Dirty = true;			// pixels changed since shown
		public const uint BORDERLESS = 1, BACKMOST = 2, TOPMOST = 4, TRANSPARENT = 8, SYSTEM = 16, ALPHA = 32;
		public const int KEYS = 1, FULLSCREEN = 2, MINIMISED = 4, OFFDESK = 8;
		public const uint DESKTOP_ID = 0xFFFFFFFF;		// (the desktop: the wallpaper + the backmost windows)
		public bool HasFrame { get { return OW > 0 && OH > 0; } }
		public bool Hidden { get { return (State & (MINIMISED | OFFDESK)) != 0; } }	// (minimised, on another workspace)
	}

	// The server is not rdpd (or one this client cannot talk to): no point reconnecting.
	class RefusedException : IOException { public RefusedException (string m) : base (m) { } }

	class Connection
	{
		public int ScreenW, ScreenH, KernelAbi;
		public readonly object Lock = new object ();
		public readonly Dictionary<uint, WinModel> Windows = new Dictionary<uint, WinModel> ();
		public List<uint> ZOrder = new List<uint> ();
		public event Action<int> RoundDone;		// a round applied (its session's number; on the reading thread)
		public event Action<string> Closed;		// given up: refused (on the reading thread)
		public event Action<string> LinkChanged;	// lost / reconnecting ...: a status; null: connected again

		// the link, for the status line
		public volatile int Session;			// +1 at each connection (a READY of an older one is not sent)
		public volatile bool Online;
		public volatile bool Pipelined;			// the server answered CAPS
		public volatile int InFlight = 1;		// rounds the server may have in flight
		public volatile int Reconnects;
		public volatile int Pings;			// PINGs answered (probes after a loss, liveness)
		public volatile int Damaged;			// messages skipped: a field out of range

		public const int MoveMs = 16;			// a pointer move at most this often
		const int SilentMs = 12000;			// nothing heard that long: the connection is dead
		const int ConnectMs = 8000, HelloMs = 15000;
		const int MaxMessage = 4 << 20, MaxRound = 256 << 20;	// (larger: the stream is damaged)
		const byte OPT_16BIT = 1, OPT_NOFRAMES = 2, OPT_PIPELINED = 4;

		string host; int port;
		bool bpp16, frames;
		volatile bool desktopOn;
		TcpClient tcp; NetworkStream net;
		volatile bool stop;
		bool freshModel;				// (a new session: its first round replaces the model)

		// ---- connecting ----

		// The first connection (throws when it fails), then the reading / sending threads; the
		// reconnections are theirs.
		public void Open (string host, int port, bool bits16, bool desktop, bool onyxFrames)
		{
			this.host = host; this.port = port; bpp16 = bits16; desktopOn = desktop; frames = onyxFrames;
			Handshake ();
			new Thread (Run) { IsBackground = true, Name = "rdpd reader" }.Start ();
			new Thread (SendLoop) { IsBackground = true, Name = "rdpd sender" }.Start ();
		}

		public void Close ()
		{
			stop = true;
			TcpClient pending;
			lock (q) { Online = false; Monitor.PulseAll (q); pending = connecting; }
			try { tcp?.Close (); } catch { }
			try { pending?.Close (); } catch { }	// (a reconnection under way: abandoned, not left open)
		}

		TcpClient connecting;		// the connection a handshake is making (Close aborts it)

		void Handshake ()
		{
			var t = new TcpClient ();
			lock (q)
			{
				if (stop) { t.Close (); throw new IOException ("closed"); }
				connecting = t;
			}
			try
			{
				t.NoDelay = true;
				var ar = t.BeginConnect (host, port, null, null);
				if (!ar.AsyncWaitHandle.WaitOne (ConnectMs)) throw new IOException ("no answer from " + host);
				t.EndConnect (ar);
				t.ReceiveTimeout = HelloMs;
				var s = t.GetStream ();
				byte[] hello = ReadExactly (s, 14);
				if (Encoding.ASCII.GetString (hello, 0, 8) != "ONYXRDP1") throw new RefusedException ("not an Onyx rdpd server (or an older rdpd: update the SD card)");
				t.ReceiveTimeout = SilentMs;
				var ans = new List<byte> (Encoding.ASCII.GetBytes ("ONYXRDP1"));
				ans.Add ((byte) ((bpp16 ? OPT_16BIT : 0) | (frames ? 0 : OPT_NOFRAMES) | OPT_PIPELINED));
				ans.Add (7); ans.Add ((byte) (desktopOn ? 1 : 0));	// DESKTOP
				ans.Add (1);						// READY: the first round
				s.Write (ans.ToArray (), 0, ans.Count);
				lock (Lock)
				{
					ScreenW = hello[8] | hello[9] << 8; ScreenH = hello[10] | hello[11] << 8; KernelAbi = hello[12] | hello[13] << 8;
					freshModel = true;
				}
				lock (q)
				{
					connecting = null;
					if (stop) throw new IOException ("closed");	// (Disconnect during the handshake)
					tcp = t; net = s;
					outBuf.Clear (); pendingMove = null; lastButtons = 0;
					Pipelined = false; InFlight = 1;
					Session++;
					Online = true;
				}
			}
			catch { lock (q) if (connecting == t) connecting = null; try { t.Close (); } catch { } throw; }
		}

		static byte[] ReadExactly (Stream s, int n)
		{
			var b = new byte[n];
			for (int o = 0; o < n; )
			{
				int k = s.Read (b, o, n - o);
				if (k <= 0) throw new EndOfStreamException ("the connection was closed");
				o += k;
			}
			return b;
		}

		// The reading thread: a session, and when it ends a new one (unless closed or refused).
		void Run ()
		{
			for (;;)
			{
				string why = ReadSession ();
				lock (q) Online = false;
				try { tcp?.Close (); } catch { }
				if (stop) return;
				for (int attempt = 1; ; attempt++)
				{
					int delay = Math.Min (500 << Math.Min (attempt - 1, 4), 8000);	// 0.5 1 2 4 8 8 ... s
					LinkChanged?.Invoke (string.Format ("Connection lost ({0}): reconnecting in {1:0.#} s (attempt {2})...", why, delay / 1000.0, attempt));
					for (int w = 0; w < delay && !stop; w += 100) Thread.Sleep (100);
					if (stop) return;
					LinkChanged?.Invoke (string.Format ("Reconnecting to {0} (attempt {1})...", host, attempt));
					try { Handshake (); break; }
					catch (RefusedException e) { if (!stop) Closed?.Invoke (e.Message); return; }
					catch (Exception e) { if (stop) return; why = Short (e); }
				}
				Reconnects++;
				LinkChanged?.Invoke (null);
			}
		}

		static string Short (Exception e)
		{
			if (e is IOException && e.InnerException is SocketException se) e = se;
			if (e is SocketException s)
			{
				if (s.SocketErrorCode == SocketError.TimedOut) return "nothing heard from the Pi for " + SilentMs / 1000 + " s";
				if (s.SocketErrorCode == SocketError.ConnectionRefused) return "refused: is rdpd running?";
				if (s.SocketErrorCode == SocketError.ConnectionReset) return "reset by the Pi";
			}
			string m = e.Message;
			return m.Length > 80 ? m.Substring (0, 80) + "..." : m;
		}

		// One session: the messages, a round applied at its END -> why it ended.
		string ReadSession ()
		{
			var round = new List<KeyValuePair<int, byte[]>> ();
			long roundBytes = 0;
			int session = Session;
			NetworkStream s; lock (q) s = net;
			try
			{
				byte[] head = new byte[5];
				while (!stop)
				{
					ReadInto (s, head, 5);
					int type = head[0], len = BitConverter.ToInt32 (head, 1);
					// the framing itself wrong (bytes lost or doubled in the stream): it cannot be
					// trusted any more -- a new connection (the largest message: a strip of PIXELS,
					// 2560 x 64 x 4 bytes + its header, uncompressed)
					if (type == 0 || type > 32 || len < 0 || len > MaxMessage || (roundBytes += len) > MaxRound)
						throw new IOException (string.Format ("a damaged stream (message {0}, {1} bytes): starting again", type, len));
					byte[] p = len > 0 ? ReadExactly (s, len) : new byte[0];
					if (type == 9)					// CAPS: pipelined rounds
					{
						if (p.Length >= 2) { InFlight = Math.Max (1, (int) p[1]); Pipelined = true; }
						continue;
					}
					if (type == 10)					// PING: answered at once (PONG)
					{
						if (Pipelined && p.Length >= 4) { Queue (new byte[] { 8, p[0], p[1], p[2], p[3] }); Pings++; }
						continue;
					}
					if (type != 5) { round.Add (new KeyValuePair<int, byte[]> (type, p)); continue; }
					lock (Lock)					// END: the whole round at once
					{
						if (freshModel) { Windows.Clear (); ZOrder = new List<uint> (); freshModel = false; }
						foreach (var m in round)
						{
							bool ok;
							try { ok = Handle (m.Key, m.Value); } catch { ok = false; }
							if (!ok) Damaged++;			// (skipped: its fields out of range)
						}
					}
					round.Clear (); roundBytes = 0;
					RoundDone?.Invoke (session);
				}
				return "closed";
			}
			catch (Exception e) { return Short (e); }
		}

		static void ReadInto (Stream s, byte[] b, int n)
		{
			for (int o = 0; o < n; )
			{
				int k = s.Read (b, o, n - o);
				if (k <= 0) throw new EndOfStreamException ("the Pi closed the connection");
				o += k;
			}
		}

		// ---- sending: a queue written by its own thread (the UI never waits on the network) ----
		readonly object q = new object ();
		readonly List<byte> outBuf = new List<byte> ();
		byte[] pendingMove;				// the latest pointer move not sent yet
		int lastButtons;
		readonly Stopwatch clock = Stopwatch.StartNew ();
		long lastMoveAt = -MoveMs;

		void Queue (byte[] b)
		{
			lock (q)
			{
				if (!Online) return;			// (lost: input meanwhile is not replayed later)
				if (pendingMove != null) { outBuf.AddRange (pendingMove); pendingMove = null; lastMoveAt = clock.ElapsedMilliseconds; }	// (in order)
				outBuf.AddRange (b);
				Monitor.Pulse (q);
			}
		}

		void SendLoop ()
		{
			while (!stop)
			{
				byte[] data; NetworkStream s;
				lock (q)
				{
					for (;;)
					{
						if (stop) return;
						long now = clock.ElapsedMilliseconds;
						if (Online && (outBuf.Count > 0 || (pendingMove != null && now - lastMoveAt >= MoveMs))) break;
						int wait = Online && pendingMove != null ? (int) Math.Max (1, lastMoveAt + MoveMs - now) : Timeout.Infinite;
						Monitor.Wait (q, wait);
					}
					if (pendingMove != null) { outBuf.AddRange (pendingMove); pendingMove = null; lastMoveAt = clock.ElapsedMilliseconds; }
					data = outBuf.ToArray (); outBuf.Clear ();
					s = net;
				}
				try { s.Write (data, 0, data.Length); }
				catch { try { s.Close (); } catch { } }		// (the reading thread sees it and reconnects)
			}
		}

		public void Ready (int session) { if (session == Session) Queue (new byte[] { 1 }); }
		public void Pointer (uint id, int x, int y, int buttons, int wheel)
		{
			byte[] b = new byte[11];
			b[0] = 2; BitConverter.GetBytes (id).CopyTo (b, 1);
			b[5] = (byte) x; b[6] = (byte) (x >> 8); b[7] = (byte) y; b[8] = (byte) (y >> 8);
			b[9] = (byte) buttons; b[10] = (byte) (sbyte) wheel;
			lock (q)
			{
				if (!Online) return;
				if (buttons == lastButtons && wheel == 0) { pendingMove = b; Monitor.Pulse (q); return; }	// a move: the latest wins
				pendingMove = null;			// (a button / the wheel: at once, its own position)
				lastButtons = buttons;
				outBuf.AddRange (b);
				Monitor.Pulse (q);
			}
		}
		public void Key (bool down, bool heldOnly, uint keysym)
		{
			byte[] b = new byte[6];
			b[0] = 3; b[1] = (byte) ((down ? 1 : 0) | (heldOnly ? 2 : 0)); BitConverter.GetBytes (keysym).CopyTo (b, 2);
			Queue (b);
		}
		public void Desktop (bool on) { desktopOn = on; Queue (new byte[] { 7, (byte) (on ? 1 : 0) }); }
		public void Char (uint c) { byte[] b = new byte[5]; b[0] = 6; BitConverter.GetBytes (c).CopyTo (b, 1); Queue (b); }
		public void Raise (uint id) { byte[] b = new byte[5]; b[0] = 4; BitConverter.GetBytes (id).CopyTo (b, 1); Queue (b); }
		public void CloseWindow (uint id) { byte[] b = new byte[5]; b[0] = 5; BitConverter.GetBytes (id).CopyTo (b, 1); Queue (b); }

		// ---- the model ----
		byte[] unpack = new byte[0];

		static int U16 (byte[] p, int o) { return p[o] | p[o + 1] << 8; }
		static int S16 (byte[] p, int o) { return (short) U16 (p, o); }
		static uint U32 (byte[] p, int o) { return BitConverter.ToUInt32 (p, o); }

		// One message of a round into the model -> false if it was damaged (a field out of range:
		// it is skipped, nothing written out of a buffer). Each payload is checked against what
		// is read from it; a rectangle of pixels is clipped to its window's buffers as they are.
		bool Handle (int type, byte[] p)
		{
			switch (type)
			{
			case 1:		// WIN
			{
				if (p.Length < 27 || p.Length < 27 + p[26]) return false;
				uint id = U32 (p, 0);
				WinModel w;
				if (!Windows.TryGetValue (id, out w)) { w = new WinModel { Id = id }; Windows[id] = w; }
				w.X = S16 (p, 4); w.Y = S16 (p, 6);
				int nw = U16 (p, 8), nh = U16 (p, 10), now = U16 (p, 12), noh = U16 (p, 14);
				w.IL = U16 (p, 16); w.IT = U16 (p, 18);
				w.Flags = U32 (p, 20); w.Alpha = p[24]; w.State = p[25];
				int tn = p[26];
				w.Title = Encoding.GetEncoding (28591).GetString (p, 27, tn);
				if (nw != w.W || nh != w.H || w.Content.Length != nw * nh) { w.W = nw; w.H = nh; w.Content = new int[nw * nh]; }
				if (now != w.OW || noh != w.OH || w.ChromeA.Length != now * noh)
				{
					w.OW = now; w.OH = noh; w.ChromeA = new int[now * noh]; w.ChromeI = new int[now * noh];
				}
				w.Dirty = true;
				return true;
			}
			case 2:		// GONE
				if (p.Length < 4) return false;
				Windows.Remove (U32 (p, 0));
				return true;
			case 8:		// SCREEN: the Pi's screen's new size
				if (p.Length < 4) return false;
				ScreenW = U16 (p, 0); ScreenH = U16 (p, 2);
				return true;
			case 3:		// ZORDER
			{
				if (p.Length < 2) return false;
				int n = U16 (p, 0);
				if (p.Length < 2 + 4 * n) return false;
				var z = new List<uint> ();
				for (int i = 0; i < n; i++) z.Add (U32 (p, 2 + 4 * i));
				ZOrder = z;
				return true;
			}
			case 4:		// PIXELS
			{
				if (p.Length < 15) return false;
				uint id = U32 (p, 0);
				int part = p[4], bpp = p[5], lz = p[6];
				int x = U16 (p, 7), y = U16 (p, 9), w = U16 (p, 11), h = U16 (p, 13);
				if (part > 2 || (bpp != 16 && bpp != 32)) return false;
				int px = bpp / 8, bytes = w * h * px;
				WinModel m;
				if (!Windows.TryGetValue (id, out m)) return true;	// (gone meanwhile: nothing to do)
				int[] dst; int dw, dh;
				if (part == 0) { dst = m.Content; dw = m.W; dh = m.H; }
				else { dst = part == 1 ? m.ChromeA : m.ChromeI; dw = m.OW; dh = m.OH; }
				if (dst.Length < dw * dh) return false;
				byte[] src; int so;
				if (lz != 0)
				{
					if (unpack.Length < bytes) unpack = new byte[bytes];
					if (!Lz4.Decode (p, 15, p.Length - 15, unpack, bytes)) return false;
					src = unpack; so = 0;
				}
				else
				{
					if (p.Length < 15 + bytes) return false;
					src = p; so = 15;
				}
				int cw = Math.Min (w, dw - x), ch = Math.Min (h, dh - y);	// (clipped to the buffer)
				for (int j = 0; j < ch; j++)
				{
					int row = (y + j) * dw + x, o = so + j * w * px;
					for (int i = 0; i < cw; i++, o += px)
					{
						int c;
						if (bpp == 16)
						{
							int v = src[o] | src[o + 1] << 8;
							int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
							c = (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
						}
						else c = src[o] | src[o + 1] << 8 | src[o + 2] << 16 | src[o + 3] << 24;	// (the top byte kept)
						dst[row + i] = c;
					}
				}
				m.Dirty = true;
				return true;
			}
			default:	// (a newer rdpd's message: skipped)
				return true;
			}
		}
	}
}
