#ifndef _circle_chargenerator_h
#define _circle_chargenerator_h
class CCharGenerator
{
public:
	unsigned GetCharWidth (void) const { return 8; }
	unsigned GetCharHeight (void) const { return 16; }
	bool GetPixel (char, unsigned, unsigned) const { return false; }
};
#endif
