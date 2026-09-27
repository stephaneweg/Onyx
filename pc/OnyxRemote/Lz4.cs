// Lz4.cs -- the LZ4 block format, decoding (rdpd compresses the pixels with it).
namespace OnyxRemote
{
	static class Lz4
	{
		// src[s .. s + n) -> dst (its size known: dlen); false if the data is damaged
		public static bool Decode (byte[] src, int s, int n, byte[] dst, int dlen)
		{
			int ip = s, end = s + n, op = 0;
			while (ip < end)
			{
				int tok = src[ip++], lit = tok >> 4;
				if (lit == 15) { int b; do { if (ip >= end) return false; b = src[ip++]; lit += b; } while (b == 255); }
				if (ip + lit > end || op + lit > dlen) return false;
				System.Buffer.BlockCopy (src, ip, dst, op, lit); ip += lit; op += lit;
				if (ip >= end) break;						// (the last sequence: literals only)
				if (ip + 2 > end) return false;
				int off = src[ip] | src[ip + 1] << 8; ip += 2;
				int ml = tok & 15;
				if (ml == 15) { int b; do { if (ip >= end) return false; b = src[ip++]; ml += b; } while (b == 255); }
				ml += 4;
				int r = op - off;
				if (off == 0 || r < 0 || op + ml > dlen) return false;
				for (int i = 0; i < ml; i++) dst[op++] = dst[r++];		// (may overlap: byte by byte)
			}
			return op == dlen;
		}
	}
}
