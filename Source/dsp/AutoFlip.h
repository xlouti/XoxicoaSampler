#pragma once
#include "SampleData.h"
#include <array>

// AUTO flip generator.
// A flip is a pattern of chop events on a 1/32-note grid (8 ticks per beat). It is generated
// deterministically from (seed, strategy, length) and the analysed sample: BPM, the 16 cue points
// (transients) and per-chop phrase energy. Events point at PADS (cue index + offset), so moving a cue
// marker re-chops every Auto version that uses it.
//
// Each event carries three "ranks", which let the Chaos / Density knobs change the flip LIVE
// without re-rolling it:
//   altRank  : Chaos above it -> the event plays the re-ordered chop instead of the straight
//              (conformed, in-order) source position.
//   fxRank   : Chaos above it -> its modifiers apply (reverse, pitch accent / octave jump, half-time, gap, stutter chop).
//   densRank : Density at or above it -> the event plays; otherwise it's a rest.
// Swing is applied at playback (odd 1/16 ticks are delayed).
namespace xsmp
{
    constexpr int kTicksPerBeat = 8, kTicksPerBar = 32, kMaxAutoEvents = 768, kNumAutoSlots = 6, kNumAutoLens = 6;
    inline int autoLenTicks (int idx) { static const int t[kNumAutoLens] = { 16, 32, 64, 128, 256, 512 }; return t[juce::jlimit (0, kNumAutoLens - 1, idx)]; }
    inline juce::StringArray autoLenChoices() { return { "1/2 Bar", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars" }; }
    inline juce::StringArray autoSlotChoices() { return { "A", "B", "C", "D", "E", "F" }; }

    enum AutoStrategy { kReorder = 0, kHalfTime, kStutter, kOctaves, kGaps, kChop, kNumStrategies };
    inline const char* strategyName (int s)
    {
        static const char* n[kNumStrategies] = { "REORDER", "HALF-TIME", "STUTTER", "OCTAVES", "GAPS", "CHOP" };
        return n[juce::jlimit (0, kNumStrategies - 1, s)];
    }

    enum AutoFlags : juce::uint8 { kRev = 1, kHalf = 2, kGap = 4, kStut = 8 };

    struct AutoEvent
    {
        juce::int16 tick = 0, len = 1;      // pattern ticks
        juce::int8 pad = 0, altPad = 0;     // straight / re-ordered chop
        juce::int16 off = 0, altOff = 0;    // offset into the chop, in source ticks
        juce::int8 semis = 0;               // pitch accent (fx)
        juce::uint8 flags = 0;              // AutoFlags (fx)
        float altRank = 1.0f, fxRank = 1.0f, densRank = 0.0f, gain = 1.0f;
    };

    struct AutoFlip
    {
        int lenTicks = 32, num = 0, strategy = 0;
        juce::uint32 seed = 0;
        std::array<AutoEvent, kMaxAutoEvents> ev {};
    };

    struct AutoContext
    {
        const SampleData* data = nullptr;
        std::array<juce::int64, 16> cues {};
        double bpm = 120.0;
    };

    namespace autogen
    {
        struct Tpl { int n; int t[16]; };
        inline const Tpl& tpl (int i)
        {
            static const Tpl T[] = {
                { 4,  { 0, 8, 16, 24 } },                                   // 0 quarters
                { 8,  { 0, 4, 8, 12, 16, 20, 24, 28 } },                    // 1 eighths
                { 16, { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30 } }, // 2 sixteenths
                { 6,  { 0, 6, 12, 16, 22, 28 } },                           // 3 tresillo x2
                { 6,  { 0, 6, 12, 18, 24, 28 } },                           // 4 3-3-3-3-2-2
                { 5,  { 0, 10, 16, 20, 26 } },                              // 5 boom-bap
                { 11, { 0, 4, 6, 8, 12, 16, 20, 22, 24, 28, 30 } },         // 6 rage
                { 8,  { 0, 4, 10, 14, 16, 20, 26, 30 } },                   // 7 syncopated
                { 4,  { 0, 12, 16, 28 } },                                  // 8 half-time
                { 8,  { 0, 6, 8, 14, 16, 22, 24, 30 } },                    // 9 skip
            };
            return T[juce::jlimit (0, 9, i)];
        }
        inline std::vector<int> templatesFor (int strategy)
        {
            switch (strategy)
            {
                case kReorder:  return { 1, 7, 9, 3 };
                case kHalfTime: return { 8, 0, 5 };
                case kStutter:  return { 1, 6, 2, 9 };
                case kOctaves:  return { 3, 4, 1 };
                case kGaps:     return { 5, 8, 7 };
                default:        return { 6, 2, 9, 7 };
            }
        }
        struct Probs { float rev, pitch, oct, gap, stut, half; };
        inline Probs probsFor (int s)
        {
            switch (s)
            {
                case kReorder:  return { 0.10f, 0.08f, 0.04f, 0.05f, 0.10f, 0.00f };
                case kHalfTime: return { 0.12f, 0.06f, 0.06f, 0.06f, 0.08f, 1.00f };
                case kStutter:  return { 0.08f, 0.10f, 0.04f, 0.04f, 0.45f, 0.00f };
                case kOctaves:  return { 0.08f, 0.15f, 0.40f, 0.05f, 0.10f, 0.00f };
                case kGaps:     return { 0.18f, 0.06f, 0.04f, 0.30f, 0.10f, 0.00f };
                default:        return { 0.12f, 0.12f, 0.08f, 0.08f, 0.20f, 0.00f };
            }
        }
    }

    inline void generateAutoFlip (const AutoContext& ctx, juce::uint32 seed, int strategy, int lenTicks, AutoFlip& out)
    {
        using namespace autogen;
        out.num = 0; out.lenTicks = lenTicks; out.strategy = strategy; out.seed = seed;
        if (ctx.data == nullptr || ctx.data->length() <= 0) return;
        const auto& d = *ctx.data;
        juce::Random rng ((juce::int64) seed * 7919 + strategy * 104729 + 17);
        const double bpm = ctx.bpm > 0 ? ctx.bpm : 120.0;
        const double srcTick = 60.0 / bpm / kTicksPerBeat * d.fileRate;
        const auto len = d.length();

        // cues sorted by time + per-chop energy (1 beat after the cue): the "phrase energy" map
        std::array<int, 16> order {};
        for (int i = 0; i < 16; ++i) order[(size_t) i] = i;
        std::sort (order.begin(), order.end(), [&] (int a, int b) { return ctx.cues[(size_t) a] < ctx.cues[(size_t) b]; });
        const juce::int64 origin = ctx.cues[(size_t) order[0]];
        std::array<float, 16> energy {};
        for (int p = 0; p < 16; ++p)
        {
            const auto a = ctx.cues[(size_t) p], b = std::min<juce::int64> (len, a + (juce::int64) (srcTick * kTicksPerBeat));
            double s = 0;
            for (auto i = a; i < b; i += 4) s += (double) d.audio.getSample (0, (int) i) * d.audio.getSample (0, (int) i);
            energy[(size_t) p] = (float) std::sqrt (s / std::max<juce::int64> (1, (b - a) / 4));
        }
        std::array<int, 16> byEnergy = order;
        std::sort (byEnergy.begin(), byEnergy.end(), [&] (int a, int b) { return energy[(size_t) a] > energy[(size_t) b]; });

        const double srcTicksAvail = (double) (len - origin) / srcTick;
        const int srcSpan = srcTicksAvail >= kTicksPerBar ? (int) (std::floor (srcTicksAvail / kTicksPerBar) * kTicksPerBar)
                                                         : std::max (8, (int) std::floor (srcTicksAvail));
        auto padOff = [&] (double srcTickPos, juce::int8& pad, juce::int16& off)
        {
            const double pos = (double) origin + srcTickPos * srcTick;
            int best = order[0];
            for (int k = 0; k < 16; ++k) if ((double) ctx.cues[(size_t) order[(size_t) k]] <= pos + 1.0) best = order[(size_t) k];
            pad = (juce::int8) best;
            off = (juce::int16) juce::jlimit (0, 30000, (int) std::round ((pos - (double) ctx.cues[(size_t) best]) / srcTick));
        };
        auto pickPad = [&] (bool strong)
        {
            const int top = strong ? 4 : 12;
            return byEnergy[(size_t) rng.nextInt (top)];
        };

        const Probs pr = probsFor (strategy);
        const auto tpls = templatesFor (strategy);
        const int bars = std::max (1, (lenTicks + kTicksPerBar - 1) / kTicksPerBar);
        // section templates: A (main), B (alt); motif of chops for the re-order
        const int tA = tpls[(size_t) rng.nextInt ((int) tpls.size())];
        int tB = tpls[(size_t) rng.nextInt ((int) tpls.size())];
        if (tB == tA && tpls.size() > 1) tB = tpls[(size_t) ((std::find (tpls.begin(), tpls.end(), tA) - tpls.begin() + 1) % (int) tpls.size())];
        const int motifLen = rng.nextBool() ? 4 : 8;
        std::vector<int> motif ((size_t) motifLen);
        for (int k = 0; k < motifLen; ++k)
        {
            int p = pickPad (k == 0 || k == motifLen / 2);
            if (k > 0 && p == motif[(size_t) k - 1]) p = pickPad (false);
            motif[(size_t) k] = p;
        }
        const int octSign = rng.nextBool() ? 1 : -1;
        const int pitchSet[] = { 12, -12, 7, 5, -5, 3, -7 };

        auto push = [&] (const AutoEvent& e) { if (out.num < kMaxAutoEvents) out.ev[(size_t) out.num++] = e; };

        int onsetCount = 0;
        int prevAltPad = -1, prevAltEnd = -1, prevAltOffEnd = 0;
        for (int b = 0; b < bars; ++b)
        {
            const int barTick = b * kTicksPerBar;
            if (barTick >= lenTicks) break;
            const int phraseBar = b % 4, section = (b / 4) % 4;      // A A' B A''
            const bool sectionB = (section == 2) || (bars <= 4 && bars >= 2 && b == bars - 1 && strategy == kHalfTime);
            const bool fillBar = (phraseBar == 3) || (bars == 2 && b == 1) || (bars == 1);
            const Tpl& T = tpl (sectionB ? tB : (phraseBar == 1 && rng.nextFloat() < 0.5f ? tB : tA));
            std::vector<int> ons (T.t, T.t + T.n);
            // light mutation per bar so repeats aren't copies
            if (b > 0 && rng.nextFloat() < 0.35f && ons.size() > 3) ons.erase (ons.begin() + 1 + rng.nextInt ((int) ons.size() - 1));
            if (rng.nextFloat() < 0.25f) { const int add = 2 * rng.nextInt (16); if (std::find (ons.begin(), ons.end(), add) == ons.end()) ons.push_back (add); }
            std::sort (ons.begin(), ons.end());
            // section B of the half-time strategy: everything moves at half speed
            const bool halfBar = pr.half > 0.0f && sectionB;
            // mutate the motif for each new section
            if (phraseBar == 0 && b > 0) motif[(size_t) rng.nextInt (motifLen)] = pickPad (false);

            for (size_t i = 0; i < ons.size(); ++i)
            {
                const int o = ons[i];
                const int next = i + 1 < ons.size() ? ons[i + 1] : kTicksPerBar;
                const int tick = barTick + o;
                if (tick >= lenTicks) break;
                AutoEvent e;
                e.tick = (juce::int16) tick;
                e.len = (juce::int16) std::max (1, std::min (next - o, lenTicks - tick));
                // straight: the conformed source in order (looping over whole bars of the sample)
                const double sPos = halfBar ? std::fmod ((double) barTick, (double) srcSpan) + o * 0.5 : std::fmod ((double) tick, (double) srcSpan);
                padOff (sPos, e.pad, e.off);
                // re-ordered chop from the motif (downbeats prefer strong chops)
                int ap = motif[(size_t) (onsetCount % motifLen)];
                if (o == 0 && rng.nextFloat() < 0.6f) ap = byEnergy[(size_t) rng.nextInt (3)];
                e.altPad = (juce::int8) ap;
                e.altOff = (ap == prevAltPad && prevAltEnd == tick) ? (juce::int16) prevAltOffEnd : 0;
                prevAltPad = ap; prevAltEnd = tick + e.len; prevAltOffEnd = e.altOff + e.len;
                e.altRank = rng.nextFloat() * 0.85f + (o == 0 ? 0.1f : 0.0f);
                e.fxRank  = 0.15f + rng.nextFloat() * 0.8f;
                e.densRank = o == 0 ? 0.0f : (o % 8 == 0 ? 0.12f + 0.2f * rng.nextFloat()
                                         : (o % 4 == 0 ? 0.32f + 0.3f * rng.nextFloat() : 0.5f + 0.45f * rng.nextFloat()));
                e.gain = o % 8 == 0 ? 1.0f : 0.88f;
                // modifiers
                const bool beforeDown = o >= 24 && next == kTicksPerBar;
                if (halfBar) { e.flags |= kHalf; e.fxRank = 0.1f + 0.3f * rng.nextFloat(); }
                if (rng.nextFloat() < pr.rev * (beforeDown ? 2.5f : 1.0f)) e.flags |= kRev;
                if (rng.nextFloat() < pr.oct) e.semis = (juce::int8) (12 * (onsetCount % 2 == 0 ? octSign : -octSign));
                else if (rng.nextFloat() < pr.pitch) e.semis = (juce::int8) pitchSet[rng.nextInt (7)];
                if (o != 0 && rng.nextFloat() < pr.gap) e.flags |= kGap;
                ++onsetCount;

                // stutter: fills at phrase ends, plus random ones for the stutter-heavy strategies
                const bool stutterHere = (fillBar && o >= 16 && rng.nextFloat() < 0.35f + pr.stut) || (rng.nextFloat() < pr.stut * 0.35f && e.len >= 4);
                if (stutterHere && e.len >= 2)
                {
                    const int step = (e.len >= 4 && rng.nextBool()) ? 2 : 1; // 1/16 or 1/32 repeats
                    const bool rise = rng.nextFloat() < 0.4f;
                    const float rank = 0.2f + 0.6f * rng.nextFloat();
                    const int sp = rng.nextFloat() < 0.5f ? e.altPad : pickPad (true);
                    int k = 0;
                    for (int t = 0; t < e.len; t += step, ++k)
                    {
                        AutoEvent s = e;
                        s.tick = (juce::int16) (tick + t);
                        s.len = (juce::int16) std::min (step, e.len - t);
                        padOff (halfBar ? sPos + t * 0.5 : std::fmod ((double) (tick + t), (double) srcSpan), s.pad, s.off);
                        s.altPad = (juce::int8) sp; s.altOff = 0;
                        s.altRank = rank; s.fxRank = rank;
                        s.flags = (juce::uint8) (kStut | (e.flags & kHalf));
                        s.semis = rise ? (juce::int8) juce::jmin (12, k * 2) : e.semis;
                        s.densRank = k == 0 ? e.densRank : 0.55f + 0.4f * rng.nextFloat();
                        s.gain = k == 0 ? e.gain : 0.8f + 0.2f * (float) k / (float) std::max (1, e.len / step);
                        push (s);
                    }
                    continue;
                }
                push (e);
            }
        }
    }
}
