//
// pocketqi/listen.h -- PocketQI on Onyx: Pocket's ear (`pocketqi --tray`, tray.h). The microphone (AudioKit's ak_in_*:
// the one the Sound applet chose, 16 kHz) goes through PocketQI's own listening, unchanged (core/pocket_ecoute.c):
//   asleep    only "dis Pocket" is listened for, on the device -- PocketSphinx's keyphrase spotting
//             (core/pocket_kws.c, its French model in SD:/apps/pocketqi.app/ps/): NOTHING leaves the device;
//   heard     the sentence ("dis Pocket, quel temps fait-il ?" said at once, or the next one, within 6 s);
//   sentence  until 0.7 s of silence after speech: then sent, whole, from the start of "dis Pocket";
//   again     after Pocket's answer, 8 s to speak again without "dis Pocket"; nothing: asleep again.
// The sentence goes to Mistral's Voxtral (POST /v1/audio/transcriptions, multipart: the model, the language, the sound
// in Ogg Opus 16 kHz mono -- core/pocket_son.c over libopus --, a WAV when the encoder fails), as PocketQI's kernel
// does; what is left of "dis Pocket" is taken off the text (pocket_ecoute_strip).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef POCKETQI_LISTEN_H
#define POCKETQI_LISTEN_H

#include "Apps/pocketqi/net.h"
#include "Apps/pocketqi/core/pocket_ecoute.h"
#include "Apps/pocketqi/core/pocket_kws.h"
#include "Apps/pocketqi/core/pocket_son.h"
#include "audiokit/audiokit.h"

namespace pq {

#define VOXTRAL_URL		"https://api.mistral.ai/v1/audio/transcriptions"
#define VOXTRAL_MODEL		"voxtral-mini-latest"
#define VOXTRAL_BOUNDARY	"pocket7d1c0a9b3e5f"

// ---- Voxtral: a sentence's sound -> its text ---------------------------------------------------------------------
// the request's multipart body: the model, the language, the sound (Ogg Opus; "": no memory)
static std::string voxtral_body (const short *pcm, size_t n, const char *lang)
{
	std::string head = "--" VOXTRAL_BOUNDARY "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n" VOXTRAL_MODEL "\r\n"
			   "--" VOXTRAL_BOUNDARY "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\n" + std::string (lang) + "\r\n"
			   "--" VOXTRAL_BOUNDARY "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"";
	static const char tail[] = "\r\n--" VOXTRAL_BOUNDARY "--\r\n";
	pocket_son son;
	if (!pocket_son_prepare (pcm, n, 16000, 0, 0, &son)) return "";
	std::string body = head + pocket_son_nom (&son) + "\"\r\nContent-Type: " + pocket_son_mime (&son) + "\r\n\r\n";
	body.append ((const char *) son.data, son.len);
	body += tail;
	pocket_son_libere (&son);
	return body;
}

// pcm: 16 kHz mono; lang: "fr", "en" (the system's) -> the HTTP status (200: text), < 0 the network's error
static int voxtral_transcribe (const std::string &key, const short *pcm, size_t n, const char *lang, std::string &text,
			       std::string &err)
{
	text.clear ();
	if (key.empty ()) { err = "no Mistral key"; return 0; }
	std::string body = voxtral_body (pcm, n, lang);
	if (body.empty ()) { err = "no memory"; return 0; }
	enum { CAP = 64 * 1024 };
	static char *buf;
	if (!buf) buf = new char[CAP];
	HttpClient c;
	c.user_agent ("Onyx-PocketQI/1.0").timeout_ms (20000).progress (http_complete, buf);
	c.header ("Authorization", ("Bearer " + key).c_str ());
	c.accept ("application/json");
	HttpResponse r = c.post (VOXTRAL_URL, "multipart/form-data; boundary=" VOXTRAL_BOUNDARY, body.data (), (int) body.size (), buf, CAP);
	if (r.status <= 0) { err = "no answer (" + num (r.status) + ")"; return r.status; }
	std::string b (r.body, (size_t) r.body_len);
	if (r.status != 200) { err = "HTTP " + num (r.status) + ": " + b.substr (0, 200); return r.status; }
	// {"model": ..., "text": "...", "language": ...}: the text, its escapes undone
	size_t k = b.find ("\"text\"");
	if (k == std::string::npos || (k = b.find ('"', b.find (':', k) + 1)) == std::string::npos) { err = "no text in the answer"; return 0; }
	for (k++; k < b.size () && b[k] != '"'; k++)
	{
		char ch = b[k];
		if (ch != '\\' || k + 1 >= b.size ()) { text += ch; continue; }
		ch = b[++k];
		if (ch == 'n' || ch == 't' || ch == 'r') text += ' ';
		else if (ch == 'u' && k + 4 < b.size ())
		{
			unsigned u = (unsigned) strtoul (b.substr (k + 1, 4).c_str (), 0, 16);
			k += 4;
			if (u < 0x80) text += (char) u;
			else if (u < 0x800) { text += (char) (0xC0 | (u >> 6)); text += (char) (0x80 | (u & 63)); }
			else { text += (char) (0xE0 | (u >> 12)); text += (char) (0x80 | ((u >> 6) & 63)); text += (char) (0x80 | (u & 63)); }
		}
		else text += ch;
	}
	text = trim (text);
	return 200;
}

// ---- the ear --------------------------------------------------------------------------------------------------
struct Ear
{
	pocket_kws kws;
	pocket_ecoute e;
	std::vector<short> buf;				// a sentence: 25 s at 16 kHz
	bool ready = false;
	std::string err;

	// the model and the dictionary: <home>/ps/model, <home>/ps/eveil.dict
	bool start (const std::string &home)
	{
		buf.assign (25 * 16000, 0);
		pocket_ecoute_init (&e, &buf[0], buf.size (), 0.5f);
		std::string hmm = home + "/ps/model", dict = home + "/ps/eveil.dict";
		if (pocket_kws_init (&kws, hmm.c_str (), dict.c_str (), POCKET_KWS_PHRASE, POCKET_KWS_SEUIL) < 0) { err = kws.err; return false; }
		e.det = pocket_kws_detect; e.det_reset = pocket_kws_reset_cb; e.det_ctx = &kws;
		pocket_ecoute_veille (&e);
		ready = true;
		return true;
	}
};

} // namespace pq

#endif
