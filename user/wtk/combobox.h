//
// wtk/combobox.h -- an editable Textbox with a drop-down list of suggestions (WinForms
// ComboBox, DropDown style): type any text, or click the arrow (or press Down / Up) to
// pick one of the options -> the text becomes that option and onPick fires with `picked`
// = its index. While open the widget grows downward over its siblings (like Dropdown).
//
#ifndef _wtk_combobox_h
#define _wtk_combobox_h

#include "wtk/textbox.h"

namespace wtk {

class Combobox : public Textbox
{
public:
	enum { MAXOPT = 16, ARROW_W = 20 };
	char	 opts[MAXOPT][64]; int nopts, picked, rowH; bool open; Action onPick;
	Combobox (int l, int t, int w, int h, const char *s = "", Action enter = 0, Action pick = 0);
	void clearOptions ();
	void addOption (const char *s);
	void pick (int i);			// set the text to option i, fire onPick
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
private:
	void setOpen (bool o);
};

} // namespace wtk

#endif
