// (Elegant's stand-in for Circle's bitmap font) No glyphs yet: the server draws no text (the
// frames and their titles are the programs', UIKit's skin). The cell's size is the kernel font's.
#ifndef _circle_chargenerator_h
#define _circle_chargenerator_h
class CCharGenerator
{
public:
	constexpr CCharGenerator (void) {}
	unsigned GetCharWidth (void) const { return 8; }
	unsigned GetCharHeight (void) const { return 16; }
	bool GetPixel (char, unsigned, unsigned) const { return false; }
};
#endif
