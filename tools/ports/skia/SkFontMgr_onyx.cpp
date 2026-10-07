// SkFontMgr_onyx.cpp -- Onyx's Skia font manager: the custom directory font manager with CSS family aliases,
// case-insensitive family names and per-character fallback (SkFontMgr_onyx.h says why: no fontconfig).
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is hereby granted, free
// of charge, to any person obtaining a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice
// and this permission notice shall be included in all copies or substantial portions of the Software. THE
// SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
#include "SkFontMgr_onyx.h"

#include "include/core/SkData.h"
#include "include/core/SkFontArguments.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkStream.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_directory.h"

#include <map>
#include <mutex>
#include <string>
#include <strings.h>
#include <vector>

namespace {

// CSS generic families and common web family names -> the card's families, in order of preference
// (the first one installed wins). The card: DejaVu Sans / Serif / Sans Mono, Liberation Sans / Serif
// (metric-compatible with Arial / Times New Roman), Gelasio (Georgia), Selawik (Segoe UI).
struct Alias {
	const char *name;
	const char *families[3];
};
const Alias kAliases[] = {
	{ "sans-serif", { "DejaVu Sans", "Liberation Sans", nullptr } },
	{ "serif", { "DejaVu Serif", "Liberation Serif", nullptr } },
	{ "monospace", { "DejaVu Sans Mono", "Liberation Mono", nullptr } },
	{ "cursive", { "DejaVu Serif", "Gelasio", nullptr } },
	{ "fantasy", { "DejaVu Sans", nullptr, nullptr } },
	{ "math", { "DejaVu Serif", "DejaVu Sans", nullptr } },
	{ "system-ui", { "DejaVu Sans", nullptr, nullptr } },
	{ "ui-sans-serif", { "DejaVu Sans", nullptr, nullptr } },
	{ "ui-serif", { "DejaVu Serif", nullptr, nullptr } },
	{ "ui-monospace", { "DejaVu Sans Mono", nullptr, nullptr } },
	{ "ui-rounded", { "DejaVu Sans", nullptr, nullptr } },
	{ "emoji", { "Noto Color Emoji", "DejaVu Sans", nullptr } },
	{ "Arial", { "Liberation Sans", "DejaVu Sans", nullptr } },
	{ "Helvetica", { "Liberation Sans", "DejaVu Sans", nullptr } },
	{ "Helvetica Neue", { "Liberation Sans", "DejaVu Sans", nullptr } },
	{ "Times New Roman", { "Liberation Serif", "DejaVu Serif", nullptr } },
	{ "Times", { "Liberation Serif", "DejaVu Serif", nullptr } },
	{ "Georgia", { "Gelasio", "DejaVu Serif", nullptr } },
	{ "Segoe UI", { "Selawik", "DejaVu Sans", nullptr } },
	{ "Verdana", { "DejaVu Sans", nullptr, nullptr } },
	{ "Tahoma", { "DejaVu Sans", nullptr, nullptr } },
	{ "Bitstream Vera Sans", { "DejaVu Sans", nullptr, nullptr } },
	{ "Courier New", { "Liberation Mono", "DejaVu Sans Mono", nullptr } },
	{ "Courier", { "Liberation Mono", "DejaVu Sans Mono", nullptr } },
	{ "Consolas", { "DejaVu Sans Mono", nullptr, nullptr } },
	{ "Menlo", { "DejaVu Sans Mono", nullptr, nullptr } },
};

// the order the families are tried in for a character no asked family has
const char *const kFallbackOrder[] = { "DejaVu Sans", "DejaVu Serif", "DejaVu Sans Mono", "Liberation Sans",
				       "Liberation Serif", "Gelasio", "Selawik" };

class SkFontMgr_Onyx final : public SkFontMgr {
public:
	explicit SkFontMgr_Onyx (sk_sp<SkFontMgr> base) : fBase (std::move (base))
	{
		int n = fBase->countFamilies ();
		for (int i = 0; i < n; i++) {
			SkString name;
			fBase->getFamilyName (i, &name);
			fNames.push_back (name.c_str ());
		}
		for (const char *f : kFallbackOrder)
			if (const char *c = canonical (f))
				fOrder.push_back (c);
		for (const std::string &f : fNames) {
			bool listed = false;
			for (const char *o : fOrder)
				listed = listed || f == o;
			if (!listed)
				fOrder.push_back (f.c_str ());
		}
	}

	// the installed family's own spelling, or nullptr
	const char *canonical (const char *family) const
	{
		if (!family)
			return nullptr;
		for (const std::string &f : fNames)
			if (strcasecmp (f.c_str (), family) == 0)
				return f.c_str ();
		return nullptr;
	}

	// an installed family for a name: the family itself, else its alias' first installed target
	const char *resolve (const char *family) const
	{
		if (const char *c = canonical (family))
			return c;
		if (!family)
			return nullptr;
		for (const Alias &a : kAliases)
			if (strcasecmp (a.name, family) == 0)
				for (const char *t : a.families)
					if (const char *c = canonical (t))
						return c;
		return nullptr;
	}

protected:
	int onCountFamilies () const override { return fBase->countFamilies (); }
	void onGetFamilyName (int i, SkString *name) const override { fBase->getFamilyName (i, name); }
	sk_sp<SkFontStyleSet> onCreateStyleSet (int i) const override { return fBase->createStyleSet (i); }

	sk_sp<SkFontStyleSet> onMatchFamily (const char family[]) const override
	{
		const char *r = resolve (family);
		return r ? fBase->matchFamily (r) : nullptr;
	}

	sk_sp<SkTypeface> onMatchFamilyStyle (const char family[], const SkFontStyle &style) const override
	{
		// nullptr: a weak match (WebKit's last resort) -> the default family
		const char *r = family ? resolve (family) : resolve ("sans-serif");
		return r ? fBase->matchFamilyStyle (r, style) : nullptr;
	}

	sk_sp<SkTypeface> onMatchFamilyStyleCharacter (const char family[], const SkFontStyle &style, const char *[], int,
						       SkUnichar ch) const override
	{
		// the asked family first, if it has the character
		if (const char *r = family ? resolve (family) : nullptr) {
			sk_sp<SkTypeface> tf = fBase->matchFamilyStyle (r, style);
			if (tf && tf->unicharToGlyph (ch))
				return tf;
		}
		// then the card's families in order: the first with the character (remembered per character)
		int bucket = (style.weight () >= SkFontStyle::kSemiBold_Weight ? 1 : 0) | (style.slant () != SkFontStyle::kUpright_Slant ? 2 : 0);
		long key = (long) ch * 4 + bucket;
		{
			std::lock_guard<std::mutex> lock (fMutex);
			auto it = fCache.find (key);
			if (it != fCache.end ())
				return it->second < 0 ? nullptr : fBase->matchFamilyStyle (fOrder[it->second], style);
		}
		int found = -1;
		sk_sp<SkTypeface> tf;
		for (size_t i = 0; i < fOrder.size () && found < 0; i++) {
			tf = fBase->matchFamilyStyle (fOrder[i], style);
			if (tf && tf->unicharToGlyph (ch))
				found = (int) i;
		}
		std::lock_guard<std::mutex> lock (fMutex);
		fCache[key] = found;
		return found < 0 ? nullptr : tf;
	}

	sk_sp<SkTypeface> onMakeFromData (sk_sp<SkData> d, int i) const override { return fBase->makeFromData (std::move (d), i); }
	sk_sp<SkTypeface> onMakeFromStreamIndex (std::unique_ptr<SkStreamAsset> s, int i) const override
	{
		return fBase->makeFromStream (std::move (s), i);
	}
	sk_sp<SkTypeface> onMakeFromStreamArgs (std::unique_ptr<SkStreamAsset> s, const SkFontArguments &a) const override
	{
		return fBase->makeFromStream (std::move (s), a);
	}
	sk_sp<SkTypeface> onMakeFromFile (const char path[], int i) const override { return fBase->makeFromFile (path, i); }

	sk_sp<SkTypeface> onLegacyMakeTypeface (const char family[], SkFontStyle style) const override
	{
		const char *r = family ? resolve (family) : nullptr;
		sk_sp<SkTypeface> tf = fBase->legacyMakeTypeface (r ? r : resolve ("sans-serif"), style);
		return tf ? tf : fBase->legacyMakeTypeface (nullptr, style);
	}

private:
	sk_sp<SkFontMgr> fBase;
	std::vector<std::string> fNames;
	std::vector<const char *> fOrder;	// into fNames
	mutable std::mutex fMutex;
	mutable std::map<long, int> fCache;	// character * 4 + style bucket -> index in fOrder, -1: none
};

}  // namespace

const char *SkFontMgr_Onyx_Alias (const char *family)
{
	for (const Alias &a : kAliases)
		if (family && strcasecmp (a.name, family) == 0)
			return a.families[0];
	return nullptr;
}

sk_sp<SkFontMgr> SkFontMgr_New_Onyx (const char *dir)
{
	sk_sp<SkFontMgr> base = SkFontMgr_New_Custom_Directory (dir ? dir : "SD:/res/fonts/");
	if (!base)
		return nullptr;
	return sk_make_sp<SkFontMgr_Onyx> (std::move (base));
}
