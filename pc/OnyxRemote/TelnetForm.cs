// TelnetForm.cs -- Onyx Remote's console: a telnet session with the Pi's telnetd (the Onyx
// shell, /bin/cmd), in a window of its own. The output is a text box one can scroll, select and
// copy from; the command is typed in the line below (Enter sends it, Up / Down recall the
// previous ones, Ctrl+C with nothing selected interrupts, Esc clears the line).
// telnetd does the line editing and the echo (it puts the client in character mode): a
// command is sent as its characters then CR LF, and comes back as its echo. Telnet
// negotiation (IAC ...) is dropped, "ESC [2J" (the `clear` command) clears the text, other
// ANSI sequences are ignored.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace OnyxRemote
{
	class TelnetForm : Form
	{
		readonly string host; readonly int port;
		readonly RichTextBox output = new RichTextBox ();
		readonly TextBox input = new TextBox ();
		readonly ToolStripLabel status = new ToolStripLabel ();
		readonly ToolStripButton reconnect = new ToolStripButton ("Reconnect");
		readonly List<string> history = new List<string> ();
		int historyAt;
		TcpClient client; NetworkStream stream; Thread reader;
		volatile bool closing;
		const int MaxChars = 1000000;			// (the oldest text dropped beyond that)

		public TelnetForm (string host, int port)
		{
			this.host = host; this.port = port;
			Text = "Onyx console - " + host;
			if (Program.AppIcon != null) Icon = Program.AppIcon;
			ClientSize = new Size (820, 520);
			Font = new Font ("Segoe UI", 9f);
			var mono = new Font ("Consolas", 10f);
			output.ReadOnly = true; output.Dock = DockStyle.Fill; output.Font = mono;
			output.BackColor = Color.FromArgb (16, 18, 28); output.ForeColor = Color.FromArgb (220, 220, 220);
			output.BorderStyle = BorderStyle.None; output.DetectUrls = false; output.WordWrap = true;
			output.HideSelection = false;
			var menu = new ContextMenuStrip ();
			menu.Items.Add ("Copy", null, (s, e) => output.Copy ());
			menu.Items.Add ("Select All", null, (s, e) => output.SelectAll ());
			menu.Items.Add ("Clear", null, (s, e) => output.Clear ());
			menu.Items.Add ("Save As...", null, (s, e) => SaveAs ());
			output.ContextMenuStrip = menu;
			output.KeyPress += (s, e) => { if (!char.IsControl (e.KeyChar)) { input.Focus (); input.AppendText (e.KeyChar.ToString ()); input.SelectionStart = input.TextLength; e.Handled = true; } };
			input.Dock = DockStyle.Bottom; input.Font = mono;
			input.BackColor = Color.FromArgb (28, 30, 44); input.ForeColor = Color.White; input.BorderStyle = BorderStyle.FixedSingle;
			input.KeyDown += InputKey;
			var ts = new ToolStrip { GripStyle = ToolStripGripStyle.Hidden, Dock = DockStyle.Top };
			var ctrlC = new ToolStripButton ("Ctrl+C") { ToolTipText = "Interrupt the command running (Ctrl+C with nothing selected)" };
			ctrlC.Click += (s, e) => SendRaw (new byte[] { 3 });
			var clear = new ToolStripButton ("Clear");
			clear.Click += (s, e) => output.Clear ();
			reconnect.Click += (s, e) => Open ();
			ts.Items.AddRange (new ToolStripItem[] { ctrlC, clear, new ToolStripSeparator (), reconnect, new ToolStripSeparator (), status });
			Controls.Add (output);
			Controls.Add (input);
			Controls.Add (ts);
			Shown += (s, e) => { input.Focus (); Open (); };
		}

		void Open ()
		{
			Close2 ();
			status.Text = "Connecting to " + host + ":" + port + "...";
			reconnect.Enabled = false;
			var c = new TcpClient ();
			try
			{
				var ar = c.BeginConnect (host, port, null, null);
				if (!ar.AsyncWaitHandle.WaitOne (5000)) throw new IOException ("no answer (is telnetd running on the Pi?)");
				c.EndConnect (ar);
				c.NoDelay = true;
			}
			catch (Exception ex) { c.Close (); status.Text = "Cannot connect: " + ex.Message; reconnect.Enabled = true; return; }
			client = c; stream = c.GetStream ();
			status.Text = "Connected to " + host + ":" + port;
			var s = stream;
			reader = new Thread (() => ReadLoop (s)) { IsBackground = true, Name = "telnet" };
			reader.Start ();
		}

		void Close2 ()
		{
			try { stream?.Close (); client?.Close (); } catch { }
			stream = null; client = null;
		}

		// ---- the Pi -> the text box ---------------------------------------------------------------------------
		enum St { Data, Iac, Opt, Sb, SbIac, Esc, Csi }
		St st = St.Data;
		readonly Decoder utf8 = new UTF8Encoding (false).GetDecoder ();

		void ReadLoop (NetworkStream s)
		{
			var buf = new byte[8192];
			try
			{
				for (;;)
				{
					int n = s.Read (buf, 0, buf.Length);
					if (n <= 0) break;
					var copy = new byte[n]; Array.Copy (buf, copy, n);
					BeginInvoke ((Action) (() => Received (copy)));
				}
			}
			catch { }
			if (!closing)
				try { BeginInvoke ((Action) (() => { if (stream == s) { Append ("\n[connection closed]\n"); status.Text = "Disconnected"; reconnect.Enabled = true; Close2 (); } })); } catch { }
		}

		void Received (byte[] b)
		{
			var text = new List<byte> (b.Length);
			foreach (byte c in b)
			{
				switch (st)
				{
				case St.Data:
					if (c == 255) st = St.Iac;
					else if (c == 27) st = St.Esc;
					else if (c == '\r') { }					// (CR LF -> LF)
					else if (c == 8) { Flush (text); Backspace (); }
					else if (c == 7) { }					// (bell)
					else text.Add (c);
					break;
				case St.Iac:
					if (c == 255) { text.Add (255); st = St.Data; }
					else if (c >= 251 && c <= 254) st = St.Opt;
					else if (c == 250) st = St.Sb;
					else st = St.Data;
					break;
				case St.Opt: st = St.Data; break;
				case St.Sb: if (c == 255) st = St.SbIac; break;
				case St.SbIac: st = c == 240 ? St.Data : St.Sb; break;
				case St.Esc: st = c == '[' ? St.Csi : St.Data; csiText.Clear (); break;
				case St.Csi:
					if (c >= 0x40 && c <= 0x7E)				// the final byte
					{
						if (c == 'J' && csiText.ToString () == "2") { text.Clear (); output.Clear (); }
						st = St.Data;
					}
					else csiText.Append ((char) c);
					break;
				}
			}
			Flush (text);
		}
		readonly StringBuilder csiText = new StringBuilder ();

		void Flush (List<byte> text)
		{
			if (text.Count == 0) return;
			var bytes = text.ToArray ();
			var chars = new char[utf8.GetCharCount (bytes, 0, bytes.Length)];
			utf8.GetChars (bytes, 0, bytes.Length, chars, 0);
			text.Clear ();
			Append (new string (chars));
		}

		// telnetd's "\b \b" (a character rubbed out): the last character goes
		void Backspace ()
		{
			if (output.TextLength == 0) return;
			bool atEnd = output.SelectionStart >= output.TextLength && output.SelectionLength == 0;
			output.ReadOnly = false;
			output.Select (output.TextLength - 1, 1);
			if (output.SelectedText != "\n") output.SelectedText = "";
			output.ReadOnly = true;
			if (atEnd) output.Select (output.TextLength, 0);
		}

		void Append (string s)
		{
			// (a space then a backspace from the "\b \b" pairs: the space went in, the next \b removes it)
			int selStart = output.SelectionStart, selLen = output.SelectionLength;
			bool follow = selLen == 0;					// (the user selecting text: do not jump)
			output.Select (output.TextLength, 0);
			output.SelectedText = s;
			if (output.TextLength > MaxChars)
			{
				output.ReadOnly = false;
				output.Select (0, output.TextLength - MaxChars * 9 / 10);
				output.SelectedText = "";
				output.ReadOnly = true;
				follow = true;
			}
			if (follow) { output.Select (output.TextLength, 0); output.ScrollToCaret (); SendMessage (output.Handle, WM_VSCROLL, (IntPtr) SB_BOTTOM, IntPtr.Zero); }
			else output.Select (selStart, selLen);
		}

		// ---- the keyboard -> the Pi ---------------------------------------------------------------------------
		void InputKey (object sender, KeyEventArgs e)
		{
			if (e.KeyCode == Keys.Enter)
			{
				e.SuppressKeyPress = true;
				string line = input.Text;
				if (line.Length > 0 && (history.Count == 0 || history[history.Count - 1] != line)) history.Add (line);
				historyAt = history.Count;
				input.Clear ();
				var b = new List<byte> ();
				foreach (byte c in Encoding.UTF8.GetBytes (line)) { b.Add (c); if (c == 255) b.Add (255); }
				b.Add ((byte) '\r'); b.Add ((byte) '\n');
				SendRaw (b.ToArray ());
			}
			else if (e.KeyCode == Keys.Up && history.Count > 0)
			{
				e.SuppressKeyPress = true;
				if (historyAt > 0) historyAt--;
				input.Text = history[historyAt]; input.SelectionStart = input.TextLength;
			}
			else if (e.KeyCode == Keys.Down && history.Count > 0)
			{
				e.SuppressKeyPress = true;
				if (historyAt < history.Count) historyAt++;
				input.Text = historyAt < history.Count ? history[historyAt] : "";
				input.SelectionStart = input.TextLength;
			}
			else if (e.KeyCode == Keys.Escape) { e.SuppressKeyPress = true; input.Clear (); }
			else if (e.Control && e.KeyCode == Keys.C && input.SelectionLength == 0)
			{
				e.SuppressKeyPress = true;
				SendRaw (new byte[] { 3 });
			}
			else if (e.KeyCode == Keys.PageUp || e.KeyCode == Keys.PageDown)
			{
				e.SuppressKeyPress = true;
				output.Focus (); SendKeys.Send (e.KeyCode == Keys.PageUp ? "{PGUP}" : "{PGDN}");
				BeginInvoke ((Action) (() => input.Focus ()));
			}
		}

		void SendRaw (byte[] b)
		{
			var s = stream;
			if (s == null) { status.Text = "Not connected"; return; }
			try { s.Write (b, 0, b.Length); }
			catch (Exception ex) { status.Text = "Send failed: " + ex.Message; }
		}

		void SaveAs ()
		{
			using (var d = new SaveFileDialog { Filter = "Text (*.txt)|*.txt|All files|*.*", FileName = "onyx-console.txt" })
				if (d.ShowDialog (this) == DialogResult.OK)
					try { File.WriteAllText (d.FileName, output.Text.Replace ("\n", "\r\n")); } catch (Exception ex) { MessageBox.Show (this, ex.Message, Text); }
		}

		const int WM_VSCROLL = 0x115, SB_BOTTOM = 7;
		[System.Runtime.InteropServices.DllImport ("user32.dll")]
		static extern IntPtr SendMessage (IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

		protected override void OnFormClosed (FormClosedEventArgs e) { closing = true; Close2 (); base.OnFormClosed (e); }
	}
}
