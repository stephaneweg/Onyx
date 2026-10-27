//
// pocketqi/voice.h -- PocketQI on Onyx: Pocket's voice, Mistral's Voxtral TTS (as PocketQI's `voix=mistral`: its
// kernel's httpscmd.cpp, pocketqi_win's voix_mistral.c). POST https://api.mistral.ai/v1/audio/speech {"model",
// "voice_id", "response_format": "pcm", "stream": true, "input"}: the answer is an SSE stream of "speech.audio.delta"
// events whose "audio_data" is float32 little-endian PCM, 24 kHz mono, in base64. It is read as it comes (HttpClient's
// progress: the bytes so far, headers and chunk sizes included, decoded here) and played at once through AudioKit's
// output (ak_out_*: 44.1 kHz stereo, the rate converted): Pocket speaks before the whole sentence is made.
// The voice: chosen in Pocket > Settings for French and for English (a "family" -- fr_marie, en_paul, gb_oliver,
// gb_jane -- follows Pocket's mood); another language has Mistral's voice of that language. The key: the LLM's
// Mistral key. Played on the caller's thread (main.cpp's speaker thread).
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
#ifndef POCKETQI_VOICE_H
#define POCKETQI_VOICE_H

#include "Apps/pocketqi/net.h"
#include "Apps/pocketqi/core/pocket_humeur.h"
#include "audiokit/audiokit.h"
#include <vector>

namespace pq {

#define TTS_URL		"https://api.mistral.ai/v1/audio/speech"
#define TTS_MODEL	"voxtral-mini-tts-3-0"
#define TTS_RATE	24000

// ---- the voices (GET /v1/audio/voices, 2026-10-26) ---------------------------------------------------------------
struct VoiceChoice { const char *id, *name; bool moods; };
// a family's id is its prefix: the mood's emotion is added (fr_marie_happy)
static const VoiceChoice VOICES_FR[] = {
	{ "voice_589", "L\xC3\xA9" "a", false }, { "fr_marie", "Marie", true }, { "voice_3137", "Manon", false },
	{ "voice_3141", "Camille", false }, { "voice_592", "Marie (2)", false } };
static const VoiceChoice VOICES_EN[] = {
	{ "en_paul", "Paul", true }, { "gb_oliver", "Oliver (GB)", true }, { "gb_jane", "Jane (GB)", true },
	{ "voice_3150", "James", false }, { "voice_478", "Sheila", false }, { "voice_447", "Margaret", false },
	{ "voice_485", "Rob", false }, { "voice_590", "Harriet (GB)", false }, { "voice_591", "Fred (GB)", false },
	{ "voice_1787", "Mary (GB)", false }, { "voice_1788", "David (GB)", false }, { "voice_1786", "Sky (GB)", false } };
#define VOICE_FR_DEFAULT "voice_589"
#define VOICE_EN_DEFAULT "en_paul"

// the emotions a family has (Mistral's presets), the first one a mood asks for that it has
static std::string voice_emotion (const std::string &family, pocket_humeur h)
{
	const char *have = family == "fr_marie" ? " neutral happy sad excited curious angry "
			 : family == "en_paul" ? " neutral happy sad frustrated excited confident cheerful angry "
			 : family == "gb_oliver" ? " neutral sad excited curious confident cheerful angry "
			 : " neutral sad confused curious confident frustrated ";		// (gb_jane)
	const char *want[3] = { 0, 0, 0 };
	switch (h)
	{
	case HUMEUR_JOYEUX: case HUMEUR_RIEUR: case HUMEUR_AMOUREUX: case HUMEUR_CLIN: want[0] = "happy"; want[1] = "cheerful"; break;
	case HUMEUR_FIER: want[0] = "confident"; want[1] = "happy"; break;
	case HUMEUR_TRISTE: case HUMEUR_FATIGUE: case HUMEUR_ENNUI: case HUMEUR_TIMIDE: want[0] = "sad"; break;
	case HUMEUR_SURPRIS: case HUMEUR_EXCITE: want[0] = "excited"; want[1] = "cheerful"; break;
	case HUMEUR_CURIEUX: want[0] = "curious"; break;
	case HUMEUR_PERPLEXE: case HUMEUR_MEFIANT: want[0] = "confused"; want[1] = "curious"; break;
	case HUMEUR_COLERE: case HUMEUR_DEGOUT: want[0] = "angry"; want[1] = "frustrated"; break;
	default: break;
	}
	for (int i = 0; i < 3 && want[i]; i++) if (strstr (have, (std::string (" ") + want[i] + " ").c_str ())) return want[i];
	return "neutral";
}

// the voice of a sentence: its language's (French and English: the settings'), the mood's emotion for a family;
// "" no voice for that language
static std::string voice_id (const std::string &lang, const std::string &fr, const std::string &en, pocket_humeur mood)
{
	std::string v;
	if (lang.empty () || lang == "fr") v = fr.empty () ? VOICE_FR_DEFAULT : fr;
	else if (lang == "en") v = en.empty () ? VOICE_EN_DEFAULT : en;
	else if (lang == "de") return "voice_481";
	else if (lang == "es") return "voice_483";
	else if (lang == "it") return "voice_482";
	else if (lang == "pt") return "voice_480";
	else if (lang == "nl") return "voice_3145";
	else return "";
	for (auto &c : VOICES_FR) if (v == c.id && c.moods) return v + "_" + voice_emotion (v, mood);
	for (auto &c : VOICES_EN) if (v == c.id && c.moods) return v + "_" + voice_emotion (v, mood);
	return v;
}

// ---- the stream -------------------------------------------------------------------------------------------------
struct TtsStream
{
	char *buf;			// HttpClient's buffer: the raw answer
	long done = 0;			// its bytes looked at
	long body = -1;			// the body's start (-1: the headers not over yet)
	int status = 0;
	bool chunked = false;
	long chunkLeft = -1;		// the bytes left of the chunk (-1: reading a size)
	std::string size, line;
	int skip = 0;			// the CRLF after a chunk
	unsigned acc = 0; int bits = 0; unsigned char f4[4]; int nf4 = 0;
	std::vector<short> mono;	// decoded, 24 kHz, not yet converted
	std::vector<short> pend;	// converted (44.1 kHz stereo frames), not yet taken by the output
	ak_resampler *rs = 0;
	bool opened = false, failed = false;
	unsigned samples = 0;
	void (*first) (void *) = 0; void *firstCtx = 0;
	std::string err;
};

// the decoded samples converted and handed to the output as far as it takes them without waiting (wait: all of them)
static void tts_flush (TtsStream &t, bool wait)
{
	if (!t.mono.empty ())
	{
		std::vector<short> st (t.mono.size () * 2);
		for (size_t i = 0; i < t.mono.size (); i++) st[2 * i] = st[2 * i + 1] = t.mono[i];
		int in = (int) t.mono.size (), at = 0;
		static short out[8192 * 2];
		while (at < in)
		{
			int used = 0;
			int n = ak_resample (t.rs, &st[2 * at], in - at, out, 8192, &used);
			t.pend.insert (t.pend.end (), out, out + 2 * n);
			if (used <= 0) break;
			at += used;
		}
		t.mono.clear ();
	}
	if (t.pend.empty () || !t.opened) return;
	int frames = (int) t.pend.size () / 2, n = wait ? frames : ak_out_free ();
	if (n > frames) n = frames;
	if (n <= 0) return;
	int w = ak_out_write (&t.pend[0], n);
	t.pend.erase (t.pend.begin (), t.pend.begin () + 2 * (w > 0 ? w : 0));
}

static int b64v (int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
	     : c == '+' ? 62 : c == '/' ? 63 : -1;
}

// a whole SSE line: its "audio_data" decoded into samples
static void tts_line (TtsStream &t)
{
	const char *l = t.line.c_str ();
	const char *a = strstr (l, "\"audio_data\"");
	if (!a)
	{
		if (strstr (l, "\"error\"") || strstr (l, "\"message\"")) t.err = t.line.substr (0, 200);
		return;
	}
	if (!(a = strchr (a + 12, ':')) || !(a = strchr (a, '"'))) return;
	t.acc = 0; t.bits = 0; t.nf4 = 0;
	for (a++; *a && *a != '"'; a++)
	{
		int v = b64v ((unsigned char) *a);
		if (v < 0) continue;
		t.acc = (t.acc << 6) | (unsigned) v; t.bits += 6;
		if (t.bits < 8) continue;
		t.bits -= 8;
		t.f4[t.nf4++] = (unsigned char) (t.acc >> t.bits);
		if (t.nf4 < 4) continue;
		t.nf4 = 0;
		float x;
		memcpy (&x, t.f4, 4);
		int s = (int) (x * 32767.0f);
		t.mono.push_back ((short) (s > 32767 ? 32767 : s < -32767 ? -32767 : s));
	}
	if (t.mono.empty ()) return;
	t.samples += (unsigned) t.mono.size ();
	if (!t.opened)
	{
		if (ak_out_open (0, 0) != 1) { t.failed = true; t.err = "the sound output is busy"; t.mono.clear (); return; }
		t.opened = true;
		if (t.first) t.first (t.firstCtx);
	}
	tts_flush (t, false);
}

static void tts_byte (TtsStream &t, char c)
{
	if (c == '\n') { tts_line (t); t.line.clear (); return; }
	if (c != '\r') t.line += c;
}

// HttpClient's progress: the bytes arrived since the last call
static bool tts_progress (void *ctx, long got)
{
	TtsStream &t = *(TtsStream *) ctx;
	for (; t.done < got; t.done++)
	{
		char c = t.buf[t.done];
		if (t.body < 0)
		{
			if (t.done >= 3 && memcmp (t.buf + t.done - 3, "\r\n\r\n", 4) == 0)
			{
				t.body = t.done + 1;
				std::string head (t.buf, (size_t) t.body);
				size_t sp = head.find (' ');
				t.status = sp != std::string::npos ? atoi (head.c_str () + sp + 1) : 0;
				t.chunked = lower (head).find ("transfer-encoding: chunked") != std::string::npos;
				if (t.status != 200) return true;		// (the error's words: read whole)
			}
			continue;
		}
		if (t.status != 200) continue;
		if (!t.chunked) { tts_byte (t, c); continue; }
		if (t.skip > 0) { t.skip--; continue; }
		if (t.chunkLeft < 0)
		{
			if (c == '\n')
			{
				t.chunkLeft = strtol (t.size.c_str (), 0, 16);
				t.size.clear ();
				if (t.chunkLeft == 0) return false;		// (the last chunk: over)
			}
			else if (c != '\r') t.size += c;
			continue;
		}
		tts_byte (t, c);
		if (--t.chunkLeft == 0) { t.chunkLeft = -1; t.skip = 2; }
	}
	if (t.failed) return false;
	tts_flush (t, false);
	return true;
}

// says the text (UTF-8) with that voice; returns once it is heard (true: sound was played; false: err says why).
// first: told at the first sound (the face then speaks).
static bool voice_say (const std::string &key, const std::string &voice, const std::string &text,
		       void (*first) (void *), void *ctx, std::string &err)
{
	enum { CAP = 12 * 1024 * 1024 };		// (~90 s of sound: a sentence is far shorter)
	static char *buf;
	if (key.empty ()) { err = "no Mistral key"; return false; }
	if (!buf && !(buf = new char[CAP])) { err = "no memory"; return false; }
	std::string body = "{\"model\": \"" TTS_MODEL "\", \"voice_id\": " + json_str (voice) +
			   ", \"response_format\": \"pcm\", \"stream\": true, \"input\": " + json_str (text) + "}";
	TtsStream t;
	t.buf = buf; t.first = first; t.firstCtx = ctx;
	t.rs = ak_resampler_new (TTS_RATE, AUDIOKIT_RATE);
	HttpClient c;
	c.user_agent ("Onyx-PocketQI/1.0").timeout_ms (15000).progress (tts_progress, &t);
	c.header ("Authorization", ("Bearer " + key).c_str ());
	c.accept ("text/event-stream");
	HttpResponse r = c.post (TTS_URL, "application/json", body.data (), (int) body.size (), buf, CAP);
	if (!t.line.empty ()) tts_line (t);
	if (t.opened)
	{
		tts_flush (t, true);
		for (int i = 0; i < 1000 && ak_out_queued () > 0; i++) kapi_msleep (10);
		ak_out_close ();
	}
	ak_resampler_free (t.rs);
	if (t.samples > 0 && !t.failed) return true;
	if (!t.err.empty ()) err = t.err;
	else if (r.status < 0) err = "no answer (" + num (r.status) + ")";
	else if (t.status != 200 && t.body >= 0)
	{
		long n = t.done - t.body;
		err = "HTTP " + num (t.status) + ": " + std::string (buf + t.body, (size_t) (n < 0 ? 0 : n > 200 ? 200 : n));
	}
	else err = "no sound received";
	return false;
}

} // namespace pq

#endif
