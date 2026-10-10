// conntest -- Onyx Remote's Connection (the client's protocol, its sending queue, the
// reconnection) against rdpd built for the PC (tools/tests/rdpd/build_host.sh):
//   dotnet run --project tools/tests/rdpd/conntest -- NEW_RDPD_HOST [OLD_RDPD_HOST]
// Checks: pipelined rounds with the new rdpd, lock-step with an older one (no PONG sent to it),
// whole rounds in the model, the pointer's moves coalesced (buttons and keys all sent, in
// order), the reconnection after rdpd restarts, the windows kept meanwhile; COPY (a window scrolled: RDPD_ANIM=2)
// applied right.
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using OnyxRemote;

static class Program
{
	static int fails;
	static void Check (bool ok, string what) { Console.WriteLine ((ok ? "ok   " : "FAIL ") + what); if (!ok) fails++; }

	class Server
	{
		Process p; readonly string exe; readonly int port; readonly int anim;
		public readonly System.Collections.Concurrent.ConcurrentQueue<string> Log = new System.Collections.Concurrent.ConcurrentQueue<string> ();
		public Server (string exe, int port, bool anim) : this (exe, port, anim ? 1 : 0) { }
		public Server (string exe, int port, int anim) { this.exe = exe; this.port = port; this.anim = anim; Start (); }
		public void Start ()
		{
			var si = new ProcessStartInfo (exe, port.ToString ()) { RedirectStandardError = true, RedirectStandardOutput = true, UseShellExecute = false };
			si.Environment["RDPD_ANIM"] = anim.ToString ();
			p = Process.Start (si);
			p.ErrorDataReceived += (s, e) => { if (e.Data != null) Log.Enqueue (e.Data); };
			p.BeginErrorReadLine ();
			Thread.Sleep (300);
		}
		public void Kill () { try { p.Kill (); p.WaitForExit (); } catch { } }
		public int Count (string s) { return Log.Count (l => l.Contains (s)); }
	}

	static int Main (string[] args)
	{
		string fresh = args[0], old = args.Length > 1 ? args[1] : null;
		int port = 34100;

		// 1. the new rdpd: pipelined
		var sv = new Server (fresh, port, true);
		var c = new Connection ();
		int rounds = 0, badRounds = 0; string link = "";
		c.RoundDone += s =>
		{
			Interlocked.Increment (ref rounds);
			lock (c.Lock)				// a whole round: both windows, with their pixels
				if (c.Windows.Count != 2 || c.ZOrder.Count != 2 || c.Windows.Values.Any (w => w.Content.Length != w.W * w.H)) badRounds++;
			c.Ready (s);
		};
		c.LinkChanged += w => { link = w; Console.WriteLine ("     link: " + (w ?? "connected again")); };
		c.Open ("127.0.0.1", port, false, false, true);
		Thread.Sleep (2000);
		Check (c.Pipelined && c.InFlight == 3, "new rdpd: pipelined, 3 rounds in flight");
		Check (rounds >= 35, "new rdpd: " + rounds + " rounds in 2 s (an animation at 25 / s)");
		Check (badRounds == 0, "whole rounds only in the model (" + badRounds + " partial)");
		lock (c.Lock) Check (c.Windows.TryGetValue (2, out var w2) && w2.Content.Any (px => px != 0), "the pixels arrived");

		// the pointer: 300 moves in ~300 ms -> coalesced; the buttons, the wheel, the keys all sent
		int before = sv.Count ("host: ptr ");
		var sw = Stopwatch.StartNew ();
		int rb = rounds;
		for (int i = 0; i < 300; i++) { c.Pointer (2, i % 300, 50, 0, 0); Thread.Sleep (1); }
		double ms = sw.Elapsed.TotalMilliseconds;
		int during = rounds - rb;			// (a READY takes the latest move along: no extra segment)
		c.Pointer (2, 10, 10, 1, 0); c.Pointer (2, 11, 10, 1, 0); c.Pointer (2, 12, 10, 0, 0); c.Pointer (2, 12, 10, 0, -1);
		for (int i = 0; i < 40; i++) c.Char ((uint) ('a' + i % 26));
		Thread.Sleep (500);
		int moves = sv.Count ("host: ptr ") - before;
		Check (moves >= 5 && moves <= (int) (ms / Connection.MoveMs) + during + 8, "pointer moves coalesced: " + moves + " sent for 300 in " + (int) ms + " ms, "
		       + during + " READYs meanwhile (+ 4 button / wheel)");
		Check (sv.Count ("host: ptr 210 170 1 0") == 1 && sv.Count ("host: ptr 212 170 0 -1") == 1, "button down and the wheel sent, at once");
		Check (sv.Log.Last (l => l.StartsWith ("host: ptr ")).Contains ("0 -1"), "the last pointer message is the wheel (in order)");
		var keys = sv.Log.Where (l => l.StartsWith ("host: key ")).Select (l => l[11]).ToArray ();
		Check (new string (keys) == string.Concat (Enumerable.Range (0, 40).Select (i => (char) ('a' + i % 26))), "40 characters sent, in order (" + keys.Length + ")");

		// 2. rdpd restarts: the client reconnects, the model kept meanwhile
		sv.Kill ();
		Thread.Sleep (300);
		lock (c.Lock) Check (c.Windows.Count == 2, "connection lost: the windows kept");
		Check (link != null && link.StartsWith ("Connection lost"), "the status says so: " + link);
		sv.Start ();
		for (int i = 0; i < 40 && c.Reconnects == 0; i++) Thread.Sleep (100);
		Check (c.Reconnects == 1 && link == null, "reconnected by itself (" + c.Reconnects + ")");
		int r0 = rounds; Thread.Sleep (1000);
		Check (rounds - r0 >= 15 && c.Pipelined, "rounds flowing again: " + (rounds - r0) + " in 1 s");
		c.Close (); sv.Kill ();

		// 3. a slow PC (each round shown 150 ms later): the server keeps 3 rounds in flight, no more
		sv = new Server (fresh, port + 1, true);
		c = new Connection ();
		int slowRounds = 0;
		c.RoundDone += s => { Interlocked.Increment (ref slowRounds); new Thread (() => { Thread.Sleep (150); c.Ready (s); }).Start (); };
		c.Open ("127.0.0.1", port + 1, true, false, false);
		Thread.Sleep (2000);
		Check (slowRounds >= 30 && slowRounds <= 45, "slow PC: " + slowRounds + " rounds in 2 s (3 in flight / 150 ms: ~40)");
		c.Close (); Thread.Sleep (200); sv.Kill ();
		Check (sv.Log.Any (l => l.Contains ("in flight max 3")), "kmsg: in flight max 3");

		// 3b. COPY (option bit 3): window 2 scrolled 7 rows a frame -- after every round the model is what the
		// server shows (its gen in the title), whether the rows came by COPY or as pixels
		sv = new Server (fresh, port + 5, 2);
		c = new Connection ();
		int copyRounds = 0, copyBad = 0;
		c.RoundDone += s =>
		{
			lock (c.Lock)
				if (c.Windows.TryGetValue (2, out var m) && m.Title.StartsWith ("Window 2 g"))
				{
					uint g = uint.Parse (m.Title.Substring (10));
					for (int y = 0, bad = 0; y < m.H && bad == 0; y++)
						for (int x = 0; x < m.W; x++)
						{
							uint want = x / 16 == (int) (g % 18) && y < 64 ? 0xFF2020u : (uint) (x * 3 + (y + 7 * (int) g) * 5) * 0x010203u & 0xFFFFFFu;
							if (((uint) m.Content[y * m.W + x] & 0xFFFFFFu) != want) { bad = 1; copyBad++; break; }
						}
					copyRounds++;
				}
			c.Ready (s);
		};
		c.Open ("127.0.0.1", port + 5, false, false, false);
		Thread.Sleep (1500);
		c.Close (); Thread.Sleep (300); sv.Kill ();
		Check (copyRounds >= 20 && copyBad == 0, "COPY: a scrolled window right after every round (" + copyRounds + " rounds, " + copyBad + " wrong)");
		var cl = sv.Log.FirstOrDefault (l => l.Contains ("copy:"));
		Check (cl != null && !cl.Contains (" 0 copies"), "COPY: the server moved the rows (" + cl + ")");

		// 4. an older rdpd: lock-step, no PONG (it would end the session)
		if (old != null)
		{
			sv = new Server (old, port + 2, true);
			c = new Connection ();
			rounds = 0; link = "";
			c.RoundDone += s => { Interlocked.Increment (ref rounds); c.Ready (s); };
			c.LinkChanged += w => link = w;
			c.Open ("127.0.0.1", port + 2, false, false, true);
			Thread.Sleep (3000);
			Check (!c.Pipelined && c.InFlight == 1, "older rdpd: lock-step");
			Check (rounds >= 40 && link == "", "older rdpd: " + rounds + " rounds in 3 s, never lost");
			c.Pointer (1, 5, 5, 1, 0); c.Pointer (1, 5, 5, 0, 0);
			Thread.Sleep (300);
			Check (sv.Count ("host: ptr 105 85 1 0") == 1, "older rdpd: the input arrives");
			c.Close (); sv.Kill ();
		}
		// 5. damaged messages (a fake server): skipped, the session goes on; a damaged stream: a new connection
		{
			var ls = new System.Net.Sockets.TcpListener (System.Net.IPAddress.Loopback, port + 3);
			ls.Start ();
			int accepted = 0;
			var srv = new Thread (() =>
			{
				for (int k = 0; k < 2; k++)
				{
					using var t = ls.AcceptTcpClient ();
					accepted++;
					var s = t.GetStream ();
					var o = new MemoryStream ();
					void Msg (int type, byte[] pay) { o.WriteByte ((byte) type); o.Write (BitConverter.GetBytes (pay.Length)); o.Write (pay); }
					o.Write (System.Text.Encoding.ASCII.GetBytes ("ONYXRDP1")); o.Write (new byte[] { 0, 4, 0, 3, 70, 0 });
					var win = new byte[27 + 3];			// a window 4 x 2, no frame, "abc"
					BitConverter.GetBytes (5u).CopyTo (win, 0); win[8] = 4; win[10] = 2; win[26] = 3; win[27] = (byte) 'a'; win[28] = (byte) 'b'; win[29] = (byte) 'c';
					Msg (1, win);
					Msg (1, new byte[10]);					// WIN cut short
					Msg (2, new byte[0]);					// GONE without its id
					var px = new byte[15 + 4 * 4];				// PIXELS 4 x 1 at x 2: clipped to 2 columns
					BitConverter.GetBytes (5u).CopyTo (px, 0); px[5] = 32; px[7] = 2; px[11] = 4; px[13] = 1;
					for (int i = 15; i < px.Length; i++) px[i] = 0x11;
					Msg (4, px);
					var bad = (byte[]) px.Clone (); bad[13] = 50;		// PIXELS: 4 x 50, the data for 4 x 1
					Msg (4, bad);
					Msg (20, new byte[3]);					// a newer type: skipped
					Msg (3, new byte[] { 1, 0, 5, 0, 0, 0 });		// ZORDER
					Msg (5, new byte[0]);
					s.Write (o.ToArray ());
					Thread.Sleep (500);
					if (k == 0) { s.Write (new byte[] { 0, 1, 2, 3, 4, 5, 6, 7 }); Thread.Sleep (1000); }	// garbage: type 0
					else Thread.Sleep (3000);
				}
			}) { IsBackground = true };
			srv.Start ();
			c = new Connection ();
			rounds = 0;
			c.RoundDone += s => Interlocked.Increment (ref rounds);
			c.Open ("127.0.0.1", port + 3, false, false, true);
			Thread.Sleep (300);
			lock (c.Lock)
			{
				Check (c.Damaged == 3 && rounds == 1, "damaged messages skipped (" + c.Damaged + " of 3), the round applied");
				Check (c.Windows.TryGetValue (5, out var w) && w.Title == "abc" && w.Content[2] == 0x11111111 && w.Content[3] == 0x11111111 && w.Content[1] == 0,
				       "the good ones applied, the pixels clipped");
			}
			for (int i = 0; i < 40 && accepted < 2; i++) Thread.Sleep (100);
			Thread.Sleep (1500);
			Check (accepted == 2 && c.Reconnects == 1 && rounds == 2, "a damaged stream: connected again (" + c.Reconnects + ", " + rounds + " rounds)");
			c.Close (); ls.Stop ();
		}

		Console.WriteLine (fails == 0 ? "all passed" : "FAILED: " + fails);
		return fails == 0 ? 0 : 1;
	}
}
