// Json.cs -- a small JSON reader and writer for the MCP server and the window's pipe (Mcp.cs, Control.cs): objects are
// Dictionary<string, object>, arrays List<object>, numbers double, then string, bool, null.
// MIT licence (Onyx).
using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Forge
{
	class JsonCodec
	{
		public T Deserialize<T> (string s) where T : class { int i = 0; var v = Value (s, ref i); return v as T ?? throw new FormatException ("JSON: not the kind expected"); }
		public string Serialize (object o) { var b = new StringBuilder (); Write (b, o); return b.ToString (); }

		static void Ws (string s, ref int i) { while (i < s.Length && char.IsWhiteSpace (s[i])) i++; }
		static object Value (string s, ref int i)
		{
			Ws (s, ref i);
			if (i >= s.Length) throw new FormatException ("JSON: the end too soon");
			char c = s[i];
			if (c == '{')
			{
				var d = new Dictionary<string, object> (); i++; Ws (s, ref i);
				if (s[i] == '}') { i++; return d; }
				while (true)
				{
					Ws (s, ref i); string k = Str (s, ref i); Ws (s, ref i);
					if (s[i++] != ':') throw new FormatException ("JSON: ':' expected");
					d[k] = Value (s, ref i); Ws (s, ref i);
					if (s[i] == ',') { i++; continue; }
					if (s[i] == '}') { i++; return d; }
					throw new FormatException ("JSON: ',' or '}' expected");
				}
			}
			if (c == '[')
			{
				var l = new List<object> (); i++; Ws (s, ref i);
				if (s[i] == ']') { i++; return l; }
				while (true)
				{
					l.Add (Value (s, ref i)); Ws (s, ref i);
					if (s[i] == ',') { i++; continue; }
					if (s[i] == ']') { i++; return l; }
					throw new FormatException ("JSON: ',' or ']' expected");
				}
			}
			if (c == '"') return Str (s, ref i);
			if (s.Length - i >= 4 && string.CompareOrdinal (s, i, "true", 0, 4) == 0) { i += 4; return true; }
			if (s.Length - i >= 5 && string.CompareOrdinal (s, i, "false", 0, 5) == 0) { i += 5; return false; }
			if (s.Length - i >= 4 && string.CompareOrdinal (s, i, "null", 0, 4) == 0) { i += 4; return null; }
			int st = i;
			while (i < s.Length && "+-0123456789.eE".IndexOf (s[i]) >= 0) i++;
			if (i == st) throw new FormatException ("JSON: a value expected at " + st);
			return double.Parse (s.Substring (st, i - st), NumberStyles.Float, CultureInfo.InvariantCulture);
		}
		static string Str (string s, ref int i)
		{
			if (s[i] != '"') throw new FormatException ("JSON: a string expected");
			var b = new StringBuilder (); i++;
			while (s[i] != '"')
			{
				char c = s[i++];
				if (c != '\\') { b.Append (c); continue; }
				c = s[i++];
				switch (c)
				{
				case 'n': b.Append ('\n'); break; case 't': b.Append ('\t'); break; case 'r': b.Append ('\r'); break;
				case 'b': b.Append ('\b'); break; case 'f': b.Append ('\f'); break;
				case 'u': b.Append ((char) Convert.ToInt32 (s.Substring (i, 4), 16)); i += 4; break;
				default: b.Append (c); break;
				}
			}
			i++; return b.ToString ();
		}
		static void Write (StringBuilder b, object o)
		{
			switch (o)
			{
			case null: b.Append ("null"); return;
			case string s:
				b.Append ('"');
				foreach (char c in s)
					if (c == '"' || c == '\\') b.Append ('\\').Append (c);
					else if (c == '\n') b.Append ("\\n"); else if (c == '\r') b.Append ("\\r"); else if (c == '\t') b.Append ("\\t");
					else if (c < 32) b.Append ("\\u").Append (((int) c).ToString ("x4"));
					else b.Append (c);
				b.Append ('"'); return;
			case bool v: b.Append (v ? "true" : "false"); return;
			case double d: b.Append (double.IsNaN (d) || double.IsInfinity (d) ? "null" : d.ToString ("R", CultureInfo.InvariantCulture)); return;
			case float f: Write (b, (double) f); return;
			case int n: b.Append (n.ToString (CultureInfo.InvariantCulture)); return;
			case long n: b.Append (n.ToString (CultureInfo.InvariantCulture)); return;
			case IDictionary dict:
			{
				b.Append ('{'); bool first = true;
				foreach (DictionaryEntry e in dict) { if (!first) b.Append (','); first = false; Write (b, Convert.ToString (e.Key, CultureInfo.InvariantCulture)); b.Append (':'); Write (b, e.Value); }
				b.Append ('}'); return;
			}
			case IEnumerable seq:
			{
				b.Append ('['); bool first = true;
				foreach (var e in seq) { if (!first) b.Append (','); first = false; Write (b, e); }
				b.Append (']'); return;
			}
			default:
				if (o is IConvertible) { Write (b, Convert.ToDouble (o, CultureInfo.InvariantCulture)); return; }
				Write (b, o.ToString ()); return;
			}
		}
	}
}
