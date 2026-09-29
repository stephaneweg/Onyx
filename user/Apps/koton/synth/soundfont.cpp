//
// soundfont.cpp -- Koton's MeltySynth port: the SF2 loader (SoundFont.cs, SoundFontInfo.cs,
// SoundFontSampleData.cs, SoundFontParameters.cs, PresetInfo/InstrumentInfo/ZoneInfo/Zone/
// Generator/SampleHeader.cs, Instrument/Preset.cs, InstrumentRegion/PresetRegion.cs and the
// modulator de-duplication of the vendored copy).
//
// Parses from a memory buffer; every read is bounds-checked, so a truncated or corrupt file
// yields nullptr + a message, never a crash. The parsed SoundFont owns copies of everything
// (samples, headers, regions, modulators): nothing points into the caller's buffer afterwards.
// Deliberately more tolerant than MeltySynth in one way: unknown sub-chunks are skipped instead
// of rejected (sub-chunks are also skipped by their declared size, not by what was consumed).
//
// ---------------------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2021 Nobuaki Tanaka
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ---------------------------------------------------------------------------------------------
//
#include <stdlib.h>
#include <string.h>
#include "ms_internal.h"

namespace ms {

namespace {

// ---- Little helpers --------------------------------------------------------------------------

inline uint16_t rd16 (const uint8_t *p) { return (uint16_t) (p[0] | (p[1] << 8)); }
inline uint32_t rd32 (const uint8_t *p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }

// BinaryReaderEx.ReadFixedLengthString: up to the first NUL, non-ASCII bytes become '?'.
void readName (char *dst, size_t cap, const uint8_t *src, size_t n)
{
	size_t i = 0;
	for (; i < n && i + 1 < cap && src[i]; i++) dst[i] = src[i] < 128 ? (char) src[i] : '?';
	dst[i] = '\0';
}

// err = a + b + c (truncated to errcap).
void setError (char *err, size_t cap, const char *a, const char *b = nullptr, const char *c = nullptr)
{
	if (!err || !cap) return;
	size_t n = 0;
	const char *parts[3] = { a, b, c };
	for (int k = 0; k < 3; k++)
		for (const char *s = parts[k]; s && *s && n + 1 < cap; s++) err[n++] = *s;
	err[n] = '\0';
}

// Generator.cs / ZoneInfo.cs / PresetInfo.cs / InstrumentInfo.cs (only live while loading).
struct Generator { uint16_t type; uint16_t value; };
struct ZoneInfo { int generatorIndex, modulatorIndex, generatorCount, modulatorCount; };
struct PresetInfo { char name[21]; int patchNumber, bankNumber, zoneStartIndex, zoneEndIndex; };
struct InstrumentInfo { char name[21]; int zoneStartIndex, zoneEndIndex; };
struct Zone { const Generator *gens; int genCount; const Modulator *mods; int modCount; };

// Everything the loader allocates; the temporaries are freed at the end, the rest moves into the
// SoundFont (or is freed on failure).
struct Loader
{
	char *err;
	size_t errcap;

	PresetInfo *presetInfos; int presetInfoCount;
	ZoneInfo *presetBag; int presetBagCount;
	Generator *presetGens; int presetGenCount;
	Modulator *presetMods; int presetModCount;
	InstrumentInfo *instInfos; int instInfoCount;
	ZoneInfo *instBag; int instBagCount;
	Generator *instGens; int instGenCount;
	Modulator *instMods; int instModCount;
	Zone *instZones; int instZoneCount;
	Zone *presetZones; int presetZoneCount;

	SoundFont *sf;

	bool fail (const char *a, const char *b = nullptr, const char *c = nullptr) { setError (err, errcap, a, b, c); return false; }
	bool oom () { return fail ("Out of memory."); }
	void freeTemps ();
};

void Loader::freeTemps ()
{
	free (presetInfos); free (presetBag); free (presetGens); free (presetMods);
	free (instInfos); free (instBag); free (instGens); free (instMods);
	free (instZones); free (presetZones);
	presetInfos = nullptr; presetBag = nullptr; presetGens = nullptr; presetMods = nullptr;
	instInfos = nullptr; instBag = nullptr; instGens = nullptr; instMods = nullptr;
	instZones = nullptr; presetZones = nullptr;
}

// ---- Chunk readers ---------------------------------------------------------------------------

// A "LIST" chunk of the given type starting at *pos: on success [*subStart, *subEnd) is its
// content (after the list type) and *pos is moved past it.
bool readList (Loader &L, const uint8_t *d, size_t len, size_t *pos, const char *type, size_t *subStart, size_t *subEnd)
{
	if (len - *pos < 12 || memcmp (d + *pos, "LIST", 4) != 0) return L.fail ("The LIST chunk was not found.");
	uint32_t size = rd32 (d + *pos + 4);
	size_t start = *pos + 8;
	if (size < 4 || size > len - start) return L.fail ("The LIST chunk is truncated.");
	if (memcmp (d + start, type, 4) != 0) return L.fail ("The type of the LIST chunk must be '", type, "'.");
	*subStart = start + 4;
	*subEnd = start + size;
	*pos = start + size;
	return true;
}

// Iterates the sub-chunks of [pos, end): on success *id / *data / *size describe the next one.
bool nextSubChunk (Loader &L, const uint8_t *d, size_t *pos, size_t end, const uint8_t **id, const uint8_t **data, uint32_t *size)
{
	if (end - *pos < 8) return L.fail ("A sub-chunk header is truncated.");
	*id = d + *pos;
	*size = rd32 (d + *pos + 4);
	if (*size > end - (*pos + 8)) return L.fail ("A sub-chunk is truncated.");
	*data = d + *pos + 8;
	*pos += 8 + (size_t) *size;
	return true;
}

bool readInfo (Loader &L, const uint8_t *d, size_t start, size_t end)
{
	size_t pos = start;
	while (pos < end)
	{
		const uint8_t *id, *data; uint32_t size;
		if (!nextSubChunk (L, d, &pos, end, &id, &data, &size)) return false;
		if (memcmp (id, "INAM", 4) == 0) readName (L.sf->bankName, sizeof L.sf->bankName, data, size);
		// ifil, isng, irom, iver, ICRD, IENG, IPRD, ICOP, ICMT, ISFT: not needed by the synth.
	}
	return true;
}

bool readSampleData (Loader &L, const uint8_t *d, size_t start, size_t end)
{
	size_t pos = start;
	bool found = false;
	while (pos < end)
	{
		const uint8_t *id, *data; uint32_t size;
		if (!nextSubChunk (L, d, &pos, end, &id, &data, &size)) return false;
		if (memcmp (id, "smpl", 4) == 0)
		{
			size_t n = size / 2;
			if (n > 0x7FFFFFF0u) return L.fail ("The sample data is too large.");
			free (L.sf->wave);
			L.sf->wave = (int16_t *) malloc ((n ? n : 1) * sizeof (int16_t));
			if (!L.sf->wave) return L.oom ();
			memcpy (L.sf->wave, data, n * 2);		// little-endian on both the PC and the Pi
			L.sf->waveLength = (int) n;
			found = true;
		}
		// sm24: 24-bit audio is not supported (ignored, as MeltySynth).
	}
	if (!found) return L.fail ("No valid sample data was found.");
	if (L.sf->waveLength >= 2 && memcmp (L.sf->wave, "OggS", 4) == 0) return L.fail ("SoundFont3 is not yet supported.");
	return true;
}

bool readZoneInfos (Loader &L, const uint8_t *p, uint32_t size, ZoneInfo **out, int *count)
{
	if (size == 0 || size % 4 != 0) return L.fail ("The zone list is invalid.");
	int n = (int) (size / 4);
	free (*out);
	*out = (ZoneInfo *) malloc (n * sizeof (ZoneInfo));
	if (!*out) return L.oom ();
	ZoneInfo *z = *out;
	for (int i = 0; i < n; i++)
	{
		z[i].generatorIndex = rd16 (p + 4 * i);
		z[i].modulatorIndex = rd16 (p + 4 * i + 2);
		z[i].generatorCount = z[i].modulatorCount = 0;
	}
	for (int i = 0; i < n - 1; i++)
	{
		z[i].generatorCount = z[i + 1].generatorIndex - z[i].generatorIndex;
		z[i].modulatorCount = z[i + 1].modulatorIndex - z[i].modulatorIndex;
	}
	*count = n;
	return true;
}

bool readGenerators (Loader &L, const uint8_t *p, uint32_t size, Generator **out, int *count)
{
	if (size == 0 || size % 4 != 0) return L.fail ("The generator list is invalid.");
	int n = (int) (size / 4) - 1;			// the last one is the terminator
	free (*out);
	*out = (Generator *) malloc ((n ? n : 1) * sizeof (Generator));
	if (!*out) return L.oom ();
	for (int i = 0; i < n; i++) { (*out)[i].type = rd16 (p + 4 * i); (*out)[i].value = rd16 (p + 4 * i + 2); }
	*count = n;
	return true;
}

bool readModulators (Loader &L, const uint8_t *p, uint32_t size, Modulator **out, int *count)
{
	if (size % 10 != 0) return L.fail ("The modulator list is invalid.");
	int n = (int) (size / 10) - 1;			// the last one is the terminator
	if (n < 0) n = 0;
	free (*out);
	*out = (Modulator *) malloc ((n ? n : 1) * sizeof (Modulator));
	if (!*out) return L.oom ();
	for (int i = 0; i < n; i++)
	{
		const uint8_t *q = p + 10 * i;
		Modulator &m = (*out)[i];
		m.sourceOper = rd16 (q);
		m.destinationOper = rd16 (q + 2);
		m.amount = (int16_t) rd16 (q + 4);
		m.amountSourceOper = rd16 (q + 6);
		m.transformOper = rd16 (q + 8);
	}
	*count = n;
	return true;
}

bool readPresetInfos (Loader &L, const uint8_t *p, uint32_t size)
{
	if (size == 0 || size % 38 != 0) return L.fail ("The preset list is invalid.");
	int n = (int) (size / 38);
	free (L.presetInfos);
	L.presetInfos = (PresetInfo *) malloc (n * sizeof (PresetInfo));
	if (!L.presetInfos) return L.oom ();
	for (int i = 0; i < n; i++)
	{
		const uint8_t *q = p + 38 * i;
		PresetInfo &pi = L.presetInfos[i];
		readName (pi.name, sizeof pi.name, q, 20);
		pi.patchNumber = rd16 (q + 20);
		pi.bankNumber = rd16 (q + 22);
		pi.zoneStartIndex = rd16 (q + 24);
		pi.zoneEndIndex = 0;			// (library, genre, morphology: unused)
	}
	for (int i = 0; i < n - 1; i++) L.presetInfos[i].zoneEndIndex = L.presetInfos[i + 1].zoneStartIndex - 1;
	L.presetInfoCount = n;
	return true;
}

bool readInstrumentInfos (Loader &L, const uint8_t *p, uint32_t size)
{
	if (size == 0 || size % 22 != 0) return L.fail ("The instrument list is invalid.");
	int n = (int) (size / 22);
	free (L.instInfos);
	L.instInfos = (InstrumentInfo *) malloc (n * sizeof (InstrumentInfo));
	if (!L.instInfos) return L.oom ();
	for (int i = 0; i < n; i++)
	{
		const uint8_t *q = p + 22 * i;
		readName (L.instInfos[i].name, sizeof L.instInfos[i].name, q, 20);
		L.instInfos[i].zoneStartIndex = rd16 (q + 20);
		L.instInfos[i].zoneEndIndex = 0;
	}
	for (int i = 0; i < n - 1; i++) L.instInfos[i].zoneEndIndex = L.instInfos[i + 1].zoneStartIndex - 1;
	L.instInfoCount = n;
	return true;
}

bool readSampleHeaders (Loader &L, const uint8_t *p, uint32_t size)
{
	if (size == 0 || size % 46 != 0) return L.fail ("The sample header list is invalid.");
	int n = (int) (size / 46) - 1;			// the last one is the terminator
	SoundFont *sf = L.sf;
	free (sf->samples);
	sf->samples = (SampleHeader *) calloc (n ? n : 1, sizeof (SampleHeader));
	if (!sf->samples) return L.oom ();
	for (int i = 0; i < n; i++)
	{
		const uint8_t *q = p + 46 * i;
		SampleHeader &s = sf->samples[i];
		readName (s.name, sizeof s.name, q, 20);
		s.start = (int32_t) rd32 (q + 20);
		s.end = (int32_t) rd32 (q + 24);
		s.startLoop = (int32_t) rd32 (q + 28);
		s.endLoop = (int32_t) rd32 (q + 32);
		s.sampleRate = (int32_t) rd32 (q + 36);
		s.originalPitch = q[40];
		s.pitchCorrection = (int8_t) q[41];
		// (link, type: unused)
	}
	sf->sampleCount = n;
	return true;
}

// Zone.Create: one zone per bag entry but the terminator.
bool createZones (Loader &L, const ZoneInfo *infos, int infoCount, const Generator *gens, int genCount,
	const Modulator *mods, int modCount, Zone **out, int *outCount)
{
	if (infoCount <= 1) return L.fail ("No valid zone was found.");
	int n = infoCount - 1;
	*out = (Zone *) malloc (n * sizeof (Zone));
	if (!*out) return L.oom ();
	for (int i = 0; i < n; i++)
	{
		const ZoneInfo &z = infos[i];
		if (z.generatorCount < 0 || z.generatorIndex + z.generatorCount > genCount) return L.fail ("The zone list is invalid (generator range).");
		if (z.modulatorCount < 0) return L.fail ("The zone list is invalid (modulator range).");
		int mi = z.modulatorIndex < modCount ? z.modulatorIndex : modCount;
		int avail = modCount - z.modulatorIndex; if (avail < 0) avail = 0;
		int mc = z.modulatorCount < avail ? z.modulatorCount : avail;
		(*out)[i].gens = gens + z.generatorIndex;
		(*out)[i].genCount = z.generatorCount;
		(*out)[i].mods = mods + mi;
		(*out)[i].modCount = mc;
	}
	*outCount = n;
	return true;
}

// SF2 2.04 9.5.1 modulator override (the vendored copy's fix): a local modulator with the same
// identity (source, destination, amount source, transform) as a global one REPLACES it.
bool sameIdentity (const Modulator &a, const Modulator &b)
{
	return a.sourceOper == b.sourceOper && a.destinationOper == b.destinationOper &&
		a.amountSourceOper == b.amountSourceOper && a.transformOper == b.transformOper;
}

int combineModulators (const Zone &global, const Zone &local, Modulator *dst)
{
	int n = 0;
	for (int i = 0; i < global.modCount; i++) dst[n++] = global.mods[i];
	for (int i = 0; i < local.modCount; i++)
	{
		const Modulator &m = local.mods[i];
		int idx = -1;
		for (int k = 0; k < n; k++) if (sameIdentity (dst[k], m)) { idx = k; break; }
		if (idx >= 0) dst[idx] = m;			// local overwrites the identical global modulator
		else dst[n++] = m;
	}
	return n;
}

void setParameters (int16_t *gs, const Zone &z)
{
	for (int i = 0; i < z.genCount; i++)
		if (z.gens[i].type < G_Count) gs[z.gens[i].type] = (int16_t) z.gens[i].value;	// unknown ones are ignored
}

void instrumentRegionDefaults (InstrumentRegion &r)
{
	memset (&r, 0, sizeof r);
	r.gs[G_InitialFilterCutoffFrequency] = 13500;
	r.gs[G_DelayModulationLfo] = -12000;
	r.gs[G_DelayVibratoLfo] = -12000;
	r.gs[G_DelayModulationEnvelope] = -12000;
	r.gs[G_AttackModulationEnvelope] = -12000;
	r.gs[G_HoldModulationEnvelope] = -12000;
	r.gs[G_DecayModulationEnvelope] = -12000;
	r.gs[G_ReleaseModulationEnvelope] = -12000;
	r.gs[G_DelayVolumeEnvelope] = -12000;
	r.gs[G_AttackVolumeEnvelope] = -12000;
	r.gs[G_HoldVolumeEnvelope] = -12000;
	r.gs[G_DecayVolumeEnvelope] = -12000;
	r.gs[G_ReleaseVolumeEnvelope] = -12000;
	r.gs[G_KeyRange] = 0x7F00;
	r.gs[G_VelocityRange] = 0x7F00;
	r.gs[G_KeyNumber] = -1;
	r.gs[G_Velocity] = -1;
	r.gs[G_ScaleTuning] = 100;
	r.gs[G_OverridingRootKey] = -1;
}

void presetRegionDefaults (PresetRegion &r)
{
	memset (&r, 0, sizeof r);
	r.gs[G_KeyRange] = 0x7F00;
	r.gs[G_VelocityRange] = 0x7F00;
}

// Is the first zone of this span a global one? (no generator, or the last one is not the
// "terminal" generator: SampleID for instruments, Instrument for presets)
bool hasGlobalZone (const Zone *zones, int terminal)
{
	return zones[0].genCount == 0 || zones[0].gens[zones[0].genCount - 1].type != terminal;
}

// Instrument.Create + InstrumentRegion.Create
bool createInstruments (Loader &L)
{
	SoundFont *sf = L.sf;
	if (L.instInfoCount <= 1) return L.fail ("No valid instrument was found.");
	int n = L.instInfoCount - 1;			// the last one is the terminator
	sf->instruments = (Instrument *) calloc (n, sizeof (Instrument));
	if (!sf->instruments) return L.oom ();
	sf->instrumentCount = n;

	// Pass 1: validate the zone spans, count the regions and the modulator slots.
	long regionTotal = 0, modTotal = 0;
	for (int i = 0; i < n; i++)
	{
		const InstrumentInfo &info = L.instInfos[i];
		int zoneCount = info.zoneEndIndex - info.zoneStartIndex + 1;
		if (zoneCount <= 0) return L.fail ("The instrument '", info.name, "' has no zone.");
		if (info.zoneStartIndex + zoneCount > L.instZoneCount) return L.fail ("The instrument '", info.name, "' has an invalid zone range.");
		const Zone *zones = L.instZones + info.zoneStartIndex;
		bool global = hasGlobalZone (zones, G_SampleID);
		int first = global ? 1 : 0;
		for (int k = first; k < zoneCount; k++) { regionTotal++; modTotal += (global ? zones[0].modCount : 0) + zones[k].modCount; }
	}
	sf->instrumentRegions = (InstrumentRegion *) calloc (regionTotal ? regionTotal : 1, sizeof (InstrumentRegion));
	Modulator *mods = (Modulator *) malloc ((modTotal ? modTotal : 1) * sizeof (Modulator));
	sf->instrumentModulators = mods;
	if (!sf->instrumentRegions || !mods) return L.oom ();
	InstrumentRegion *reg = sf->instrumentRegions;
	Modulator *mp = mods;

	// Pass 2: build.
	for (int i = 0; i < n; i++)
	{
		const InstrumentInfo &info = L.instInfos[i];
		Instrument &inst = sf->instruments[i];
		memcpy (inst.name, info.name, sizeof inst.name);
		int zoneCount = info.zoneEndIndex - info.zoneStartIndex + 1;
		const Zone *zones = L.instZones + info.zoneStartIndex;
		bool global = hasGlobalZone (zones, G_SampleID);
		Zone empty = { nullptr, 0, nullptr, 0 };
		const Zone &g = global ? zones[0] : empty;
		inst.regions = reg;
		for (int k = global ? 1 : 0; k < zoneCount; k++)
		{
			InstrumentRegion &r = *reg++;
			instrumentRegionDefaults (r);
			setParameters (r.gs, g);
			setParameters (r.gs, zones[k]);
			r.mods = mp;
			r.modCount = combineModulators (g, zones[k], mp);
			mp += r.modCount;
			int id = r.gs[G_SampleID];
			if (!(0 <= id && id < sf->sampleCount)) return L.fail ("The instrument '", info.name, "' contains an invalid sample ID.");
			r.sample = &sf->samples[id];
			inst.regionCount++;
		}
	}
	return true;
}

// Preset.Create + PresetRegion.Create
bool createPresets (Loader &L)
{
	SoundFont *sf = L.sf;
	if (L.presetInfoCount <= 1) return L.fail ("No valid preset was found.");
	int n = L.presetInfoCount - 1;
	sf->presets = (Preset *) calloc (n, sizeof (Preset));
	if (!sf->presets) return L.oom ();
	sf->presetCount = n;

	long regionTotal = 0, modTotal = 0;
	for (int i = 0; i < n; i++)
	{
		const PresetInfo &info = L.presetInfos[i];
		int zoneCount = info.zoneEndIndex - info.zoneStartIndex + 1;
		if (zoneCount <= 0) return L.fail ("The preset '", info.name, "' has no zone.");
		if (info.zoneStartIndex + zoneCount > L.presetZoneCount) return L.fail ("The preset '", info.name, "' has an invalid zone range.");
		const Zone *zones = L.presetZones + info.zoneStartIndex;
		bool global = hasGlobalZone (zones, G_Instrument);
		for (int k = global ? 1 : 0; k < zoneCount; k++) { regionTotal++; modTotal += (global ? zones[0].modCount : 0) + zones[k].modCount; }
	}
	sf->presetRegions = (PresetRegion *) calloc (regionTotal ? regionTotal : 1, sizeof (PresetRegion));
	Modulator *mods = (Modulator *) malloc ((modTotal ? modTotal : 1) * sizeof (Modulator));
	sf->presetModulators = mods;
	if (!sf->presetRegions || !mods) return L.oom ();
	PresetRegion *reg = sf->presetRegions;
	Modulator *mp = mods;

	for (int i = 0; i < n; i++)
	{
		const PresetInfo &info = L.presetInfos[i];
		Preset &p = sf->presets[i];
		memcpy (p.name, info.name, sizeof p.name);
		p.patchNumber = info.patchNumber;
		p.bankNumber = info.bankNumber;
		int zoneCount = info.zoneEndIndex - info.zoneStartIndex + 1;
		const Zone *zones = L.presetZones + info.zoneStartIndex;
		bool global = hasGlobalZone (zones, G_Instrument);
		Zone empty = { nullptr, 0, nullptr, 0 };
		const Zone &g = global ? zones[0] : empty;
		p.regions = reg;
		for (int k = global ? 1 : 0; k < zoneCount; k++)
		{
			PresetRegion &r = *reg++;
			presetRegionDefaults (r);
			setParameters (r.gs, g);
			setParameters (r.gs, zones[k]);
			r.mods = mp;
			r.modCount = combineModulators (g, zones[k], mp);
			mp += r.modCount;
			int id = r.gs[G_Instrument];
			if (!(0 <= id && id < sf->instrumentCount)) return L.fail ("The preset '", info.name, "' contains an invalid instrument ID.");
			r.instrument = &sf->instruments[id];
			p.regionCount++;
		}
	}
	return true;
}

// SoundFont.CheckSamples / CheckRegions: every position the oscillator may read is in range
// (the 4-sample margin makes the interpolation's index + 1 safe).
bool checkSamplesAndRegions (Loader &L)
{
	const SoundFont *sf = L.sf;
	int sampleCount = sf->waveLength - 4;
	for (int i = 0; i < sf->sampleCount; i++)
	{
		const SampleHeader &s = sf->samples[i];
		if (!(0 <= s.start && s.start < sampleCount)) return L.fail ("The start position of the sample '", s.name, "' is out of range.");
		if (!(0 <= s.startLoop && s.startLoop < sampleCount)) return L.fail ("The loop start position of the sample '", s.name, "' is out of range.");
		if (!(0 < s.end && s.end <= sampleCount)) return L.fail ("The end position of the sample '", s.name, "' is out of range.");
		if (!(0 <= s.endLoop && s.endLoop <= sampleCount)) return L.fail ("The loop end position of the sample '", s.name, "' is out of range.");
	}
	for (int i = 0; i < sf->instrumentCount; i++)
	{
		const Instrument &inst = sf->instruments[i];
		for (int k = 0; k < inst.regionCount; k++)
		{
			const InstrumentRegion &r = inst.regions[k];
			// (computed in 64 bits: the coarse offsets can push an int32 over)
			long long s0 = (long long) r.sample->start + 32768LL * r.gs[G_StartAddressCoarseOffset] + r.gs[G_StartAddressOffset];
			long long s1 = (long long) r.sample->end + 32768LL * r.gs[G_EndAddressCoarseOffset] + r.gs[G_EndAddressOffset];
			long long l0 = (long long) r.sample->startLoop + 32768LL * r.gs[G_StartLoopAddressCoarseOffset] + r.gs[G_StartLoopAddressOffset];
			long long l1 = (long long) r.sample->endLoop + 32768LL * r.gs[G_EndLoopAddressCoarseOffset] + r.gs[G_EndLoopAddressOffset];
			if (!(0 <= s0 && s0 < sampleCount)) return L.fail ("The start position of the sample '", r.sample->name, "' in an instrument is out of range.");
			if (!(0 <= l0 && l0 < sampleCount)) return L.fail ("The loop start position of the sample '", r.sample->name, "' in an instrument is out of range.");
			if (!(0 < s1 && s1 <= sampleCount)) return L.fail ("The end position of the sample '", r.sample->name, "' in an instrument is out of range.");
			if (!(0 <= l1 && l1 <= sampleCount)) return L.fail ("The loop end position of the sample '", r.sample->name, "' in an instrument is out of range.");
			int mode = r.sampleModes ();
			if (mode != LOOP_NONE && mode != LOOP_CONTINUOUS && mode != LOOP_UNTIL_NOTE_OFF)
				return L.fail ("The sample '", r.sample->name, "' in an instrument has an invalid loop mode.");
		}
	}
	return true;
}

bool readParameters (Loader &L, const uint8_t *d, size_t start, size_t end)
{
	size_t pos = start;
	bool gotPhdr = false, gotPbag = false, gotPgen = false, gotInst = false, gotIbag = false, gotIgen = false, gotShdr = false;
	while (pos < end)
	{
		const uint8_t *id, *data; uint32_t size;
		if (!nextSubChunk (L, d, &pos, end, &id, &data, &size)) return false;
		bool ok = true;
		if (memcmp (id, "phdr", 4) == 0) { ok = readPresetInfos (L, data, size); gotPhdr = true; }
		else if (memcmp (id, "pbag", 4) == 0) { ok = readZoneInfos (L, data, size, &L.presetBag, &L.presetBagCount); gotPbag = true; }
		else if (memcmp (id, "pmod", 4) == 0) ok = readModulators (L, data, size, &L.presetMods, &L.presetModCount);
		else if (memcmp (id, "pgen", 4) == 0) { ok = readGenerators (L, data, size, &L.presetGens, &L.presetGenCount); gotPgen = true; }
		else if (memcmp (id, "inst", 4) == 0) { ok = readInstrumentInfos (L, data, size); gotInst = true; }
		else if (memcmp (id, "ibag", 4) == 0) { ok = readZoneInfos (L, data, size, &L.instBag, &L.instBagCount); gotIbag = true; }
		else if (memcmp (id, "imod", 4) == 0) ok = readModulators (L, data, size, &L.instMods, &L.instModCount);
		else if (memcmp (id, "igen", 4) == 0) { ok = readGenerators (L, data, size, &L.instGens, &L.instGenCount); gotIgen = true; }
		else if (memcmp (id, "shdr", 4) == 0) { ok = readSampleHeaders (L, data, size); gotShdr = true; }
		if (!ok) return false;
	}
	if (!gotPhdr) return L.fail ("The PHDR sub-chunk was not found.");
	if (!gotPbag) return L.fail ("The PBAG sub-chunk was not found.");
	if (!gotPgen) return L.fail ("The PGEN sub-chunk was not found.");
	if (!gotInst) return L.fail ("The INST sub-chunk was not found.");
	if (!gotIbag) return L.fail ("The IBAG sub-chunk was not found.");
	if (!gotIgen) return L.fail ("The IGEN sub-chunk was not found.");
	if (!gotShdr) return L.fail ("The SHDR sub-chunk was not found.");

	if (!createZones (L, L.instBag, L.instBagCount, L.instGens, L.instGenCount, L.instMods, L.instModCount, &L.instZones, &L.instZoneCount)) return false;
	if (!createInstruments (L)) return false;
	if (!createZones (L, L.presetBag, L.presetBagCount, L.presetGens, L.presetGenCount, L.presetMods, L.presetModCount, &L.presetZones, &L.presetZoneCount)) return false;
	return createPresets (L);
}

} // namespace

SoundFont *soundfont_load (const void *data, size_t len, char *err, size_t errcap)
{
	if (err && errcap) err[0] = '\0';
	const uint8_t *d = (const uint8_t *) data;
	if (!d || len < 12 || memcmp (d, "RIFF", 4) != 0) { setError (err, errcap, "The RIFF chunk was not found."); return nullptr; }
	if (memcmp (d + 8, "sfbk", 4) != 0) { setError (err, errcap, "The type of the RIFF chunk must be 'sfbk'."); return nullptr; }

	SoundFont *sf = (SoundFont *) calloc (1, sizeof (SoundFont));
	if (!sf) { setError (err, errcap, "Out of memory."); return nullptr; }
	Loader L;
	memset (&L, 0, sizeof L);
	L.err = err; L.errcap = errcap; L.sf = sf;

	size_t pos = 12, s, e;
	bool ok = readList (L, d, len, &pos, "INFO", &s, &e) && readInfo (L, d, s, e)
		&& readList (L, d, len, &pos, "sdta", &s, &e) && readSampleData (L, d, s, e)
		&& readList (L, d, len, &pos, "pdta", &s, &e) && readParameters (L, d, s, e)
		&& checkSamplesAndRegions (L);
	L.freeTemps ();
	if (!ok)
	{
		soundfont_free (sf);
		return nullptr;
	}
	return sf;
}

void soundfont_free (SoundFont *sf)
{
	if (!sf) return;
	free (sf->wave);
	free (sf->samples);
	free (sf->instruments);
	free (sf->instrumentRegions);
	free (sf->presets);
	free (sf->presetRegions);
	free (sf->instrumentModulators);
	free (sf->presetModulators);
	free (sf);
}

const char *soundfont_name (const SoundFont *sf) { return sf ? sf->bankName : ""; }
int soundfont_preset_count (const SoundFont *sf) { return sf ? sf->presetCount : 0; }

const char *soundfont_preset_name (const SoundFont *sf, int i)
{
	return sf && i >= 0 && i < sf->presetCount ? sf->presets[i].name : "";
}

int soundfont_preset_bank (const SoundFont *sf, int i)
{
	return sf && i >= 0 && i < sf->presetCount ? sf->presets[i].bankNumber : -1;
}

int soundfont_preset_patch (const SoundFont *sf, int i)
{
	return sf && i >= 0 && i < sf->presetCount ? sf->presets[i].patchNumber : -1;
}

} // namespace ms
