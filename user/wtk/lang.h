//
// wtk/lang.h -- an app's words in the user's language. The sources keep their English words, wrapped:
// TR ("Save") is the word in the language chosen, else the English one. TRC ("status", "Open") for a word
// whose translation depends on where it stands (the key "status|Open" in the catalogue).
//
// The catalogues are UTF-8 text files, a line a word: "English<TAB>translation" (\t, \n, \\ escaped; "#"
// begins a comment line):
//   SD:/res/lang/<code>.txt                 wtk's own words (the dialogs' buttons, the months...)
//   SD:/apps/<app>.app/lang/<code>.txt      the app's
// The language an app was told to use is in SD:/apps/<app>.app/lang.txt ("fr"); none: English. An app
// drawing with wtk's bitmap fonts gets the words in Latin-1 (as they draw it), one with a text face
// (FreeType's) in UTF-8: install the face before wk_lang_init.
//
#ifndef WTK_LANG_H
#define WTK_LANG_H

const char *wk_tr (const char *en);			// the translation, else en itself
const char *wk_trc (const char *ctx, const char *en);	// "ctx|en"'s, else en's, else en
#define TR(s)		wk_tr (s)
#define TRC(c, s)	wk_trc (c, s)

void        wk_lang_init ();				// the language chosen (lang.txt) loaded -- before the UI is built
bool        wk_lang_load (const char *code);		// that language's catalogues ("en": none) -> false: none found
const char *wk_lang ();					// the language in use: "en", "fr"...
const char *wk_lang_chosen ();				// the one lang.txt names ("" none)
bool        wk_lang_choose (const char *code);		// written in lang.txt (taken at the next start)

#endif
