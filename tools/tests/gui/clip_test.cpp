// host test: drawing with clip rectangles == drawing everything, inside the rectangles
#include <kern/gui/gimage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void scene (GImage &s, GImage &win, GImage &cur, int shift)
{
	s.Clear (0x00203040);
	s.FillRectangle (10 + shift, 20, 200, 150, 0x00FF0000);
	s.PutOther (&win, 50 + shift, 60, FALSE);
	s.PutOther (&win, -30, 100 + shift, TRUE);
	s.PutOtherPart (&win, 150, 5, 10, 10, 60, 40, TRUE);
	s.DrawRectangle (5, 5, 300 - shift, 230, 0x0000FF00);
	s.DrawText (20 + shift, 200, "Onyx clip", 0x00FFFFFF);
	s.PutOther (&cur, 280 - shift, 190, TRUE);
}
int main ()
{
	const int W = 320, H = 240;
	u32 *a = (u32 *) calloc (W * H, 4), *b = (u32 *) calloc (W * H, 4);
	GImage win, cur; win.SetSize (120, 90); cur.SetSize (16, 16);
	for (int i = 0; i < 120 * 90; i++) win.Buffer ()[i] = (i % 7 == 0) ? 0xFF00FF : (u32) (i * 2654435761u) & 0xFFFFFF;
	for (int i = 0; i < 256; i++) cur.Buffer ()[i] = (i % 3) ? 0xFFFFFF : 0xFF00FF;
	srand (1);
	int bad = 0;
	for (int t = 0; t < 500; t++)
	{
		int shift = rand () % 40;
		GImage A (a, W, H), B (b, W, H);
		// B: the previous full frame, then only a random rectangle redrawn with the new scene
		scene (B, win, cur, (shift + 7) % 40);
		scene (A, win, cur, shift);
		int x0 = rand () % W - 20, y0 = rand () % H - 20, x1 = x0 + rand () % 200, y1 = y0 + rand () % 150;
		B.SetClip (x0, y0, x1, y1);
		scene (B, win, cur, shift);
		int cx0 = B.ClipX0 (), cy0 = B.ClipY0 (), cx1 = B.ClipX1 (), cy1 = B.ClipY1 ();
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
			{
				bool in = x >= cx0 && x < cx1 && y >= cy0 && y < cy1;
				if (in && a[y * W + x] != b[y * W + x]) bad++;
			}
		// outside the clip nothing may have changed: redraw the old scene fully in A and compare outside
		GImage C (a, W, H); scene (C, win, cur, (shift + 7) % 40);
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
			{
				bool in = x >= cx0 && x < cx1 && y >= cy0 && y < cy1;
				if (!in && a[y * W + x] != b[y * W + x]) bad++;
			}
	}
	printf (bad ? "FAIL %d pixels\n" : "ok: clipped drawing == full drawing inside, untouched outside\n", bad);
	free (a); free (b);
	return bad != 0;
}
