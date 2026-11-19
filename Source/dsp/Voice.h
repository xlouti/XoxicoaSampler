#pragma once
#include "SampleData.h"
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace xsmp
{
    enum class PadMode { OneShot = 0, Gate = 1, Loop = 2 };

    struct PadSettings
    {
        PadMode mode = PadMode::OneShot;
        float attackMs = 1.0f, releaseMs = 30.0f, filter = 0.0f, gainDb = 0.0f;
        int choke = 0;
        float rate = 1.0f; // 1 = original, 0.5 = half-time (GENERATE slow chops)
    };

    // One playing pad. Two render paths:
    //  * Direct  : key shift 0 and tempo ratio 1 -> plain (sample-rate converting) Hermite playback, bit-clean.
    //  * Stretch : Signalsmith Stretch, transpose and time-ratio are independent. outputSeek() at trigger
    //              pre-rolls the stretcher so the first output sample is the cue point (no latency).
    class Voice
    {
    public:
        Voice() : stretch (0x5eed) {}

        void prepare (double hostRate, int maxBlock)
        {
            sr = hostRate;
            stretch.presetDefault (2, (float) sr);
            const int maxIn = maxBlock * 8 + stretch.outputSeekLength (8.0f) + 64;
            for (auto& s : scratch) s.assign ((size_t) maxIn, 0.0f);
            for (auto& o : outTmp) o.assign ((size_t) maxBlock + 16, 0.0f);
            // pre-warm: first outputSeek/process may size internal vectors
            float* in[2] = { scratch[0].data(), scratch[1].data() };
            float* out[2] = { outTmp[0].data(), outTmp[1].data() };
            stretch.setTransposeSemitones (3.0f);
            stretch.outputSeek (in, stretch.outputSeekLength (2.0f));
            stretch.process (in, maxBlock * 2, out, maxBlock);
            stretch.reset();
            active = false;
        }

        int latencyFreeSeekLength (double speed) const { return stretch.outputSeekLength ((float) speed); }

        void start (const SampleData& d, int padIndex, juce::int64 startPos, juce::int64 sliceEnd, const PadSettings& ps,
                    float velocity, double semis, double tempoRatio, juce::uint64 order,
                    bool reverse = false, juce::int64 autoLenSamples = -1, int eventSemis = 0, double eventRate = 1.0)
        {
            pad = padIndex; age = order;
            isAutoVoice = autoLenSamples >= 0; autoRemain = autoLenSamples; dir = reverse ? -1 : 1;
            autoSemis = eventSemis; autoRate = eventRate;
            mode = isAutoVoice ? PadMode::Gate : ps.mode;
            if (isAutoVoice && sliceEnd <= startPos) sliceEnd = d.length();
            startSrc = xsmp::clampI (0, std::max<juce::int64> (0, d.length() - 1), startPos);
            endSrc   = xsmp::clampI (startSrc + 1, d.length(), sliceEnd);
            if (endSrc - startSrc < (juce::int64) (0.01 * d.fileRate)) endSrc = juce::jmin (d.length(), startSrc + (juce::int64) (0.01 * d.fileRate));
            sampleLen = d.length();
            velGain = juce::jlimit (0.0f, 1.0f, velocity);
            env = 0.0f; stage = Stage::Attack; held = true;
            filterSm = ps.filter; gainSm = juce::Decibels::decibelsToGain (ps.gainDb);
            ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; coefCounter = 0;
            rateRatio = d.fileRate / sr;
            // Auto pitch accents (octave jumps, +/- semitone hits) are played as classic varispeed chops on the
            // direct path: pitch is exact (key conform included) and the grid sets the length. The stretcher's
            // zero-latency outputSeek pre-roll leaves a small residue when transposing by an octave, which was
            // audible as a tick ~30 ms into very short accents.
            varispeed = isAutoVoice && eventSemis != 0;
            useStretch = ! varispeed && (std::abs (semis) > 1.0e-3 || std::abs (tempoRatio - 1.0) > 1.0e-4);
            audible = (double) startSrc;
            inAcc = 0.0;
            curSemis = semis;
            if (useStretch)
            {
                const double speed = tempoRatio * rateRatio;
                stretch.setTransposeFactor ((float) (std::pow (2.0, semis / 12.0) * rateRatio));
                const int n = juce::jmin ((int) scratch[0].size(), stretch.outputSeekLength ((float) speed));
                feed = startSrc;
                fill (d, n);
                float* in[2] = { scratch[0].data(), scratch[1].data() };
                stretch.outputSeek (in, n);
            }
            active = true;
        }

        void release()        { held = false; if (stage != Stage::Release) beginRelease (curReleaseMs); }
        void choke()          { held = false; beginRelease (6.0f); }
        void kill()           { active = false; }

        // Renders and ADDS n samples into out (2 ch) at offset.
        void render (const SampleData& d, juce::AudioBuffer<float>& out, int offset, int n, const PadSettings& ps,
                     double semis, double tempoRatio)
        {
            if (! active) return;
            curReleaseMs = ps.releaseMs;
            const double speed = varispeed ? rateRatio * std::pow (2.0, semis / 12.0)
                                           : (useStretch ? tempoRatio : 1.0) * rateRatio;
            float* o[2] = { outTmp[0].data(), outTmp[1].data() };

            if (useStretch)
            {
                if (std::abs (semis - curSemis) > 1.0e-6)
                {
                    curSemis = semis;
                    stretch.setTransposeFactor ((float) (std::pow (2.0, semis / 12.0) * rateRatio));
                }
                inAcc += n * speed;
                const int inN = juce::jlimit (0, (int) scratch[0].size(), (int) inAcc);
                inAcc -= inN;
                fill (d, inN);
                float* in[2] = { scratch[0].data(), scratch[1].data() };
                stretch.process (in, inN, o, n);
            }
            else
            {
                double p = audible;
                for (int i = 0; i < n; ++i)
                {
                    for (int c = 0; c < 2; ++c) o[c][i] = interp (d, c, p);
                    p += speed * dir;
                    if (mode == PadMode::Loop && p >= (double) endSrc) p -= (double) (endSrc - startSrc);
                }
            }

            // envelope, filter, gain
            const float attackInc = 1.0f / juce::jmax (1.0f, ps.attackMs * 0.001f * (float) sr);
            const float smCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
            const float gainTarget = juce::Decibels::decibelsToGain (ps.gainDb);
            for (int i = 0; i < n; ++i)
            {
                // planned endings. One-shot: the release starts AT the slice end and keeps playing the audio
                // after it while fading (Release = tail length). Gate: short fade before the end of the file.
                if (stage != Stage::Release && mode != PadMode::Loop)
                {
                    if (mode == PadMode::OneShot)
                    {
                        if (audible >= (double) endSrc) beginRelease (ps.releaseMs);
                    }
                    else
                    {
                        const double remainingOut = (dir > 0 ? ((double) sampleLen - audible) : audible) / speed;
                        if (remainingOut <= 0.004 * sr) beginRelease ((float) juce::jmax (0.5, remainingOut / sr * 1000.0));
                    }
                }
                if (isAutoVoice && stage != Stage::Release && --autoRemain <= 0) beginRelease (ps.releaseMs);
                switch (stage)
                {
                    case Stage::Attack:  env += attackInc; if (env >= 1.0f) { env = 1.0f; stage = Stage::Sustain; } break;
                    case Stage::Sustain: break;
                    case Stage::Release: env -= releaseDec; if (env <= 0.0f) { env = 0.0f; active = false; } break;
                }
                filterSm += smCoef * (ps.filter - filterSm);
                gainSm   += smCoef * (gainTarget - gainSm);
                if ((coefCounter++ & 15) == 0) updateFilter();
                const float click = juce::jmin (1.0f, (float) outSamp / juce::jmax (1.0f, 0.0025f * (float) sr));
                ++outSamp;
                const float g = env * gainSm * velGain * click;
                for (int c = 0; c < 2; ++c)
                {
                    float v = o[c][i];
                    if (! std::isfinite (v)) v = 0.0f;
                    out.addSample (c, offset + i, svf (c, v) * g);
                }
                audible += speed * dir;
                if (mode == PadMode::Loop && audible >= (double) endSrc) audible -= (double) (endSrc - startSrc);
                if (! active) { for (int c = 0; c < 2; ++c) { ic1[c] = ic2[c] = 0.0f; } break; }
            }
            if (mode != PadMode::Loop && (audible >= (double) sampleLen || audible < 0.0)) active = false;
        }

        bool isActive() const { return active; }
        bool isHeld() const { return held && active && stage != Stage::Release; }
        bool isReleasing() const { return stage == Stage::Release; }
        int getPad() const { return pad; }
        juce::uint64 getAge() const { return age; }
        PadMode getMode() const { return mode; }
        double getPosition() const { return audible; }
        bool isStretching() const { return useStretch; }
        bool isAuto() const { return isAutoVoice; }
        int getAutoSemis() const { return autoSemis; }
        double getAutoRate() const { return autoRate; }

    private:
        enum class Stage { Attack, Sustain, Release };

        void beginRelease (float ms)
        {
            const float samples = juce::jmax (1.0f, ms * 0.001f * (float) sr);
            if (stage == Stage::Release) { releaseDec = juce::jmax (releaseDec, env / samples); return; }
            stage = Stage::Release;
            releaseDec = juce::jmax (1.0e-7f, env / samples);
        }

        // source sample with loop handling and a 3 ms crossfade into the loop start
        float src (const SampleData& d, int c, juce::int64 p) const
        {
            if (mode == PadMode::Loop)
            {
                const auto L = endSrc - startSrc;
                if (p >= endSrc) p = startSrc + (p - startSrc) % L;
                const auto xf = std::min<juce::int64> ((juce::int64) (0.003 * d.fileRate), juce::jmin (startSrc, L / 2));
                if (xf > 0 && p >= endSrc - xf)
                {
                    const float t = (float) (p - (endSrc - xf)) / (float) xf; // 0 -> 1
                    const auto q = startSrc - (endSrc - p);
                    return d.audio.getSample (c, (int) p) * (1.0f - t) + d.audio.getSample (c, (int) q) * t;
                }
            }
            if (p < 0 || p >= d.length()) return 0.0f;
            return d.audio.getSample (c, (int) p);
        }

        float interp (const SampleData& d, int c, double p) const
        {
            const auto i = (juce::int64) std::floor (p);
            const float f = (float) (p - (double) i);
            const float y0 = src (d, c, i - 1), y1 = src (d, c, i), y2 = src (d, c, i + 1), y3 = src (d, c, i + 2);
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + y1;
        }

        void fill (const SampleData& d, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                for (int c = 0; c < 2; ++c) scratch[(size_t) c][(size_t) i] = src (d, c, feed);
                feed += dir;
                if (mode == PadMode::Loop && feed >= endSrc) feed = startSrc + (feed - startSrc) % (endSrc - startSrc);
            }
        }

        void updateFilter()
        {
            const float a = std::abs (filterSm);
            filterMix = juce::jmin (1.0f, a / 0.05f);
            isHP = filterSm > 0.0f;
            const float nyq = 0.45f * (float) sr;
            const float fc = isHP ? 20.0f * std::pow (300.0f, a) : juce::jmin (nyq, 20000.0f * std::pow (150.0f / 20000.0f, a));
            g1 = std::tan (juce::MathConstants<float>::pi * juce::jmin (fc, nyq) / (float) sr);
            const float k = 1.0f / 0.75f; // Q 0.75: gentle DJ-style
            kRes = k;
            a1 = 1.0f / (1.0f + g1 * (g1 + k)); a2 = g1 * a1; a3 = g1 * a2;
        }

        float svf (int c, float v)
        {
            if (filterMix <= 0.0f) return v;
            const float v3 = v - ic2[c];
            const float v1 = a1 * ic1[c] + a2 * v3;
            const float v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
            ic1[c] = 2.0f * v1 - ic1[c]; ic2[c] = 2.0f * v2 - ic2[c];
            if (std::abs (ic1[c]) < 1.0e-20f) ic1[c] = 0.0f;
            if (std::abs (ic2[c]) < 1.0e-20f) ic2[c] = 0.0f;
            const float y = isHP ? v - kRes * v1 - v2 : v2;
            return v + filterMix * (y - v);
        }

        signalsmith::stretch::SignalsmithStretch<float> stretch;
        std::array<std::vector<float>, 2> scratch, outTmp;
        double sr = 48000.0, rateRatio = 1.0, audible = 0.0, inAcc = 0.0, curSemis = 0.0;
        juce::int64 startSrc = 0, endSrc = 1, sampleLen = 1, feed = 0;
        int pad = -1; juce::uint64 age = 0;
        PadMode mode = PadMode::OneShot;
        bool active = false, held = false, useStretch = false;
        Stage stage = Stage::Attack;
        float env = 0.0f, releaseDec = 0.001f, velGain = 1.0f, curReleaseMs = 30.0f;
        float filterSm = 0.0f, gainSm = 1.0f, filterMix = 0.0f, g1 = 0, a1 = 0, a2 = 0, a3 = 0, kRes = 1.0f;
        bool isHP = false; int coefCounter = 0;
        int outSamp = 0;
        bool isAutoVoice = false; juce::int64 autoRemain = -1; int dir = 1; bool varispeed = false; int autoSemis = 0; double autoRate = 1.0;
        float ic1[2] {}, ic2[2] {};
    };
}
