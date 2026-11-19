#pragma once
#include <numeric>
// included from Analysis.h after AnalysisResult and refineOnset

// Onset / syllable-aware chop builder used on load, when the silence threshold
// changes, and by GENERATE. Never invents slice points in silence (except the
// silent-file fallback of 16 evenly spaced grid cues, so a blank file still
// loads 16 pads and does not crash).
    struct ChopSlice
    {
        juce::int64 start = 0, end = 0;
        float rms = 0.0f, vocalRms = 0.0f;
        float rate = 1.0f; // 1 = original, 0.5 = half-time stretch
    };

    struct ChopBuildOpts
    {
        int count = 16;
        float threshold = 0.10f;     // fraction of peak RMS (and a -50 dB floor)
        juce::uint32 seed = 0;       // 0 = deterministic "best" set
        double gridBpm = 0.0;        // 0 = use analysis BPM
        bool varyLengths = false;    // GENERATE: short / long / slow chops
        bool tightGrid = false;      // GENERATE: snap starts to the host grid more firmly
        float minLenMs = 40.0f;
    };

    namespace chops
    {
        inline float bufferRms (const juce::AudioBuffer<float>& a, juce::int64 s, juce::int64 e, int step = 8)
        {
            const juce::int64 n = a.getNumSamples();
            s = xsmp::clampI (0, n, s); e = xsmp::clampI (s, n, e);
            if (e <= s) return 0.0f;
            const int ch = a.getNumChannels();
            auto* L = a.getReadPointer (0);
            auto* R = ch > 1 ? a.getReadPointer (1) : L;
            double sum = 0.0; int c = 0;
            for (auto i = s; i < e; i += step)
            {
                const float l = L[(int) i], r = R[(int) i];
                sum += 0.5 * (double) (l * l + r * r);
                ++c;
            }
            return c > 0 ? (float) std::sqrt (sum / (double) c) : 0.0f;
        }

        inline float vocalRms (const juce::AudioBuffer<float>& a, juce::int64 s, juce::int64 e, double fs)
        {
            // One-pole 250 Hz..4 kHz band on a mono mix, then RMS. Offline only.
            const juce::int64 n = a.getNumSamples();
            s = xsmp::clampI (0, n, s); e = xsmp::clampI (s, n, e);
            if (e <= s) return 0.0f;
            const int ch = a.getNumChannels();
            auto* L = a.getReadPointer (0);
            auto* R = ch > 1 ? a.getReadPointer (1) : L;
            const float ahp = std::exp (-2.0f * juce::MathConstants<float>::pi * 250.0f / (float) fs);
            const float alp = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 4000.0 / fs));
            float x1 = 0, hp = 0, lp = 0; double sum = 0.0; int c = 0;
            const int step = 4;
            for (auto i = s; i < e; i += step)
            {
                const float x = 0.5f * (L[(int) i] + R[(int) i]);
                hp = ahp * (hp + x - x1); x1 = x;
                lp += alp * (hp - lp);
                sum += (double) lp * (double) lp;
                ++c;
            }
            return c > 0 ? (float) std::sqrt (sum / (double) c) : 0.0f;
        }

        inline float threshAbs (float peak, float rel)
        {
            const float floor = juce::Decibels::decibelsToGain (-50.0f);
            return juce::jmax (floor, juce::jlimit (0.0f, 1.0f, rel) * juce::jmax (peak, floor));
        }

        inline juce::int64 snapZero (const juce::AudioBuffer<float>& a, juce::int64 pos, double fs, double winMs = 3.0)
        {
            const juce::int64 n = a.getNumSamples();
            if (n <= 2) return 0;
            pos = xsmp::clampI (1, n - 2, pos);
            const juce::int64 w = juce::jmax ((juce::int64) 1, (juce::int64) (winMs * 0.001 * fs));
            const juce::int64 a0 = std::max<juce::int64> (1, pos - w), b = std::min (n - 1, pos + w);
            auto* L = a.getReadPointer (0);
            juce::int64 best = pos; float bestScore = 1.0e9f;
            for (auto i = a0; i < b; ++i)
            {
                const float x0 = L[(int) (i - 1)], x1 = L[(int) i];
                const bool zc = (x0 <= 0.0f && x1 >= 0.0f) || (x0 >= 0.0f && x1 <= 0.0f);
                const float mag = std::min (std::abs (x0), std::abs (x1));
                if (! zc && mag > 1.0e-4f) continue;
                const float score = mag + (i > pos ? 0.0002f : 0.0f); // prefer at or before the onset
                if (score < bestScore) { bestScore = score; best = (std::abs (x1) < std::abs (x0) ? i : i - 1); }
            }
            if (bestScore > 1.0e8f)
            {
                for (auto i = a0; i < b; ++i)
                    if (std::abs (L[(int) i]) < bestScore) { bestScore = std::abs (L[(int) i]); best = i; }
            }
            return best;
        }

        inline juce::int64 snapGrid (juce::int64 pos, double fs, double bpm, juce::int64 origin, double maxMs)
        {
            if (bpm <= 0.0 || fs <= 0.0) return pos;
            const double step = 60.0 / bpm * fs / 8.0; // 1/32 note
            if (step < 2.0) return pos;
            const double k = std::round (((double) pos - (double) origin) / step);
            const auto snapped = (juce::int64) std::llround ((double) origin + k * step);
            if (std::abs ((double) (snapped - pos)) <= maxMs * 0.001 * fs)
                return xsmp::clampI (0, std::max<juce::int64> (0, (juce::int64) (fs * 60 * 10) ), snapped);
            return pos;
        }

        inline void trimSilence (const juce::AudioBuffer<float>& a, juce::int64& start, juce::int64& end,
                                 float gate, double fs, float minLenMs)
        {
            const juce::int64 n = a.getNumSamples();
            start = xsmp::clampI (0, n, start); end = xsmp::clampI (start, n, end);
            const juce::int64 win = juce::jmax ((juce::int64) 32, (juce::int64) (0.008 * fs));
            const juce::int64 minLen = juce::jmax (win, (juce::int64) (minLenMs * 0.001 * fs));
            while (end - start > minLen && bufferRms (a, start, start + win, 2) < gate)
                start += win / 2;
            while (end - start > minLen && bufferRms (a, end - win, end, 2) < gate)
                end -= win / 2;
            if (end - start < minLen) end = juce::jmin (n, start + minLen);
        }

        // Mid-band (syllable) onset envelope, hop-aligned with the flux envelope.
        inline std::vector<float> syllableEnv (const std::vector<float>& x, double fs, int hop)
        {
            const int frames = (int) x.size() / hop + 1;
            std::vector<float> env ((size_t) frames, 0.0f);
            const float ahp = std::exp (-2.0f * juce::MathConstants<float>::pi * 250.0f / (float) fs);
            const float alp = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 4000.0 / fs));
            float x1 = 0, hp = 0, lp = 0, prev = 0;
            for (int f = 0; f < frames; ++f)
            {
                double e = 0.0; int c = 0;
                const int a = f * hop, b = juce::jmin ((int) x.size(), a + hop);
                for (int i = a; i < b; ++i)
                {
                    const float s = x[(size_t) i];
                    hp = ahp * (hp + s - x1); x1 = s;
                    lp += alp * (hp - lp);
                    e += (double) lp * (double) lp; ++c;
                }
                const float cur = c > 0 ? (float) (e / (double) c) : 0.0f;
                env[(size_t) f] = juce::jmax (0.0f, cur - prev);
                prev = cur;
            }
            return env;
        }

        inline void pickPeaks (const std::vector<float>& env, float gmax, std::vector<int>& frames)
        {
            const int wMean = juce::jmax (2, (int) env.size() / 200 + 2);
            for (int i = 1; i + 1 < (int) env.size(); ++i)
            {
                bool isMax = true;
                for (int d = -3; d <= 3 && isMax; ++d)
                    if (d != 0 && i + d >= 0 && i + d < (int) env.size() && env[(size_t) (i + d)] > env[(size_t) i])
                        isMax = false;
                if (! isMax) continue;
                double s = 0; int n = 0;
                for (int d = -wMean; d <= wMean; ++d)
                    if (i + d >= 0 && i + d < (int) env.size()) { s += env[(size_t) (i + d)]; ++n; }
                const float thr = (float) (s / juce::jmax (1, n)) * 1.25f + 0.05f * gmax;
                if (env[(size_t) i] > thr) frames.push_back (i);
            }
        }
    }

    // Fills r.onsets (every transient) and r.eligible (non-silent, trimmed slices).
    // Then builds the default cue list (numCues long) into r.cues / r.cueEnds / r.cueRms.
    inline void detectCues (const juce::AudioBuffer<float>& audio, const std::vector<float>& x,
                            const std::vector<float>& flux, int hop, double fs, AnalysisResult& r, int numCues)
    {
        using namespace chops;
        const juce::int64 len = (juce::int64) x.size();
        r.onsets.clear(); r.cues.clear(); r.cueEnds.clear(); r.cueRms.clear(); r.eligible.clear();
        r.peakRms = bufferRms (audio, 0, len, 16);
        if (len <= 0) return;

        auto syl = syllableEnv (x, fs, hop);
        const float gFlux = flux.empty() ? 0.0f : *std::max_element (flux.begin(), flux.end());
        const float gSyl  = syl.empty()  ? 0.0f : *std::max_element (syl.begin(), syl.end());
        std::vector<int> fluxFrames, sylFrames;
        pickPeaks (flux, gFlux, fluxFrames);
        pickPeaks (syl,  gSyl,  sylFrames);
        std::vector<int> frames = fluxFrames;
        // Syllable peaks fill in soft vocal onsets the flux missed; ignore ones already next to a transient.
        const int near = juce::jmax (2, (int) (0.04 * fs / hop));
        for (int f : sylFrames)
        {
            bool close = false;
            for (int g : fluxFrames) if (std::abs (f - g) <= near) { close = true; break; }
            if (! close) frames.push_back (f);
        }
        std::sort (frames.begin(), frames.end());
        frames.erase (std::unique (frames.begin(), frames.end()), frames.end());

        auto collect = [&] (const std::vector<int>& fr)
        {
            std::vector<juce::int64> raw;
            const double mergeSec = 0.025;
            for (int f : fr)
            {
                auto pos = analysis::refineOnset (x, (juce::int64) f * hop, fs);
                pos = snapZero (audio, pos, fs, 3.0);
                if (raw.empty() || (double) (pos - raw.back()) >= mergeSec * fs)
                    raw.push_back (pos);
            }
            return raw;
        };
        auto fluxRaw = collect (fluxFrames);
        auto raw = collect (frames);
        // On a clean percussive loop the flux list is the right default (keeps every hit).
        // Mixed flux+syllable is used as the full onset pool for GENERATE / vocals.
        if (fluxRaw.size() >= 8) raw = fluxRaw;
        r.onsets = raw;

        const double bpm = r.bpm > 0.0 ? r.bpm : 0.0;
        const juce::int64 origin = raw.empty() ? 0 : raw.front();
        const float gate = threshAbs (r.peakRms, 0.08f);
        const juce::int64 minLen = (juce::int64) (15.0 * 0.001 * fs);

        for (size_t i = 0; i < raw.size(); ++i)
        {
            ChopSlice sl;
            sl.start = raw[i];
            sl.end = (i + 1 < raw.size() ? raw[i + 1] : len);
            // cap default candidate length at 2 bars so a late next-onset doesn't swallow a whole song
            if (bpm > 0.0)
            {
                const juce::int64 twoBars = (juce::int64) (60.0 / bpm * 8.0 * fs);
                sl.end = juce::jmin (sl.end, sl.start + juce::jmax (minLen, twoBars));
            }
            // Default path: keep the onset start (zero-crossed only, no grid snap) so transients
            // stay within a few milliseconds. GENERATE does the firmer host-grid snap.
            auto keptStart = sl.start;
            trimSilence (audio, sl.start, sl.end, gate, fs, 15.0f);
            sl.start = snapZero (audio, keptStart, fs, 3.0); // do not let trim pull the start off the transient
            if (sl.end <= sl.start) sl.end = juce::jmin (len, sl.start + minLen);
            sl.rms = bufferRms (audio, sl.start, sl.end, 4);
            sl.vocalRms = vocalRms (audio, sl.start, sl.end, fs);
            if (sl.rms < gate && bufferRms (audio, sl.start, juce::jmin (len, sl.start + (juce::int64) (0.04 * fs)), 2) < gate)
                continue; // truly silent after the onset
            r.eligible.push_back ({ sl.start, sl.end, sl.rms, sl.vocalRms });
        }

        auto placeGrid = [&] ()
        {
            const double beat = bpm > 0.0 ? 60.0 / bpm * fs : 0.5 * fs;
            const double sixteenth = beat / 4.0;
            r.cues.clear(); r.cueEnds.clear(); r.cueRms.clear();
            r.eligible.clear();
            for (int i = 0; i < numCues; ++i)
            {
                auto a = xsmp::clampI (0, std::max<juce::int64> (0, len - 1), (juce::int64) (i * sixteenth));
                auto b = xsmp::clampI (a + 1, len, (juce::int64) ((i + 1) * sixteenth));
                const float rms = bufferRms (audio, a, b, 4);
                r.cues.push_back (a);
                r.cueEnds.push_back (b);
                r.cueRms.push_back (rms);
                r.eligible.push_back ({ a, b, rms, vocalRms (audio, a, b, fs) });
            }
        };

        if (r.eligible.empty())
        {
            placeGrid();
            return;
        }

        // Default set: onset-starting slices, time-sorted. Tight min gap so 16th-note
        // hits on a 2-bar loop are all kept. Do not invent mid-slice points (those are
        // often silence, and they miss real transients).
        const double beat = bpm > 0.0 ? 60.0 / bpm * fs : 0.5 * fs;
        const double minGap = juce::jmax (0.012 * fs, 0.15 * (beat / 4.0));
        std::vector<ChopSlice> sorted;
        for (auto& e : r.eligible) sorted.push_back ({ e.start, e.end, e.rms, e.vocalRms, 1.0f });
        std::sort (sorted.begin(), sorted.end(), [] (const ChopSlice& a, const ChopSlice& b)
        {
            const float sa = a.rms + 0.5f * a.vocalRms, sb = b.rms + 0.5f * b.vocalRms;
            return sa > sb;
        });
        std::vector<ChopSlice> picked;
        for (auto& c : sorted)
        {
            if ((int) picked.size() >= numCues) break;
            bool ok = true;
            for (auto& q : picked)
                if (std::abs ((double) (q.start - c.start)) < minGap) { ok = false; break; }
            if (ok) picked.push_back (c);
        }
        // Fill missing 16th-grid slots that still have energy (catches a missed hit on a tight loop).
        if (bpm > 0.0 && (int) picked.size() < numCues)
        {
            const double sixteenth = beat / 4.0;
            const int slots = (int) std::floor ((double) len / sixteenth);
            for (int k = 0; k < slots && (int) picked.size() < numCues; ++k)
            {
                auto a = (juce::int64) std::llround ((double) origin + k * sixteenth);
                if (a < 0 || a >= len) continue;
                a = snapZero (audio, a, fs, 3.0);
                bool used = false;
                for (auto& q : picked)
                    if (std::abs ((double) (q.start - a)) < 0.010 * fs) { used = true; break; }
                if (used) continue;
                auto b = juce::jmin (len, a + (juce::int64) sixteenth);
                const float rms = bufferRms (audio, a, b, 4);
                if (rms < gate) continue;
                picked.push_back ({ a, b, rms, vocalRms (audio, a, b, fs), 1.0f });
            }
        }
        // Prefer leftover onset slices (relaxed gap) over inventing mid-slice points.
        while ((int) picked.size() < numCues)
        {
            bool added = false;
            for (auto& c : sorted)
            {
                bool used = false;
                for (auto& q : picked)
                    if (std::abs ((double) (q.start - c.start)) < 0.008 * fs) { used = true; break; }
                if (! used) { picked.push_back (c); added = true; break; }
            }
            if (! added) break;
        }
        // Last resort only: split the longest LOUD slice (never a silent gap).
        while ((int) picked.size() < numCues)
        {
            int best = -1; juce::int64 bestLen = minLen * 4;
            for (int i = 0; i < (int) picked.size(); ++i)
                if (picked[(size_t) i].end - picked[(size_t) i].start > bestLen)
                { bestLen = picked[(size_t) i].end - picked[(size_t) i].start; best = i; }
            if (best < 0) break;
            auto a = picked[(size_t) best];
            juce::int64 mid = (a.start + a.end) / 2;
            mid = snapZero (audio, mid, fs, 3.0);
            if (mid <= a.start + minLen || mid >= a.end - minLen) break;
            ChopSlice left = a, right = a;
            left.end = mid; right.start = mid;
            left.rms = bufferRms (audio, left.start, left.end, 4);
            right.rms = bufferRms (audio, right.start, right.end, 4);
            if (left.rms < gate || right.rms < gate) break;
            picked[(size_t) best] = left;
            picked.push_back (right);
        }
        std::sort (picked.begin(), picked.end(), [] (const ChopSlice& a, const ChopSlice& b) { return a.start < b.start; });
        if (picked.empty()) { placeGrid(); return; }
        // Snap any stray (off-onset) cue onto an unused onset or 16th-grid slot. This repairs
        // a missed off-beat hat without inventing a silent mid-slice point.
        if (bpm > 0.0 && ! raw.empty())
        {
            const double sixteenth = beat / 4.0;
            auto farFromOnsets = [&] (juce::int64 pos)
            {
                double best = 1.0e9;
                for (auto o : raw) best = std::min (best, std::abs ((double) (pos - o)) / fs);
                return best > 0.012;
            };
            auto usedByOthers = [&] (juce::int64 pos, ChopSlice* self)
            {
                for (auto& q : picked)
                    if (&q != self && std::abs ((double) (q.start - pos)) < 0.010 * fs) return true;
                return false;
            };
            for (auto& sl : picked)
            {
                if (! farFromOnsets (sl.start)) continue;
                juce::int64 dest = -1;
                for (auto o : raw) if (! usedByOthers (o, &sl)) { dest = o; break; }
                if (dest < 0)
                {
                    const int slots = (int) std::floor ((double) len / sixteenth) + 1;
                    for (int k = 0; k < slots; ++k)
                    {
                        auto a = (juce::int64) std::llround (k * sixteenth);
                        if (a < 0 || a >= len) continue;
                        a = snapZero (audio, a, fs, 3.0);
                        if (usedByOthers (a, &sl)) continue;
                        if (bufferRms (audio, a, juce::jmin (len, a + (juce::int64) sixteenth), 4) < gate) continue;
                        dest = a; break;
                    }
                }
                if (dest >= 0)
                {
                    sl.start = snapZero (audio, dest, fs, 3.0);
                    sl.end = juce::jmin (len, sl.start + (juce::int64) sixteenth);
                    sl.rms = bufferRms (audio, sl.start, sl.end, 4);
                }
            }
            std::sort (picked.begin(), picked.end(), [] (const ChopSlice& a, const ChopSlice& b) { return a.start < b.start; });
        }
        std::sort (picked.begin(), picked.end(), [] (const ChopSlice& a, const ChopSlice& b) { return a.start < b.start; });
        for (int i = 0; i < numCues; ++i)
        {
            const auto& sl = picked[(size_t) juce::jmin (i, (int) picked.size() - 1)];
            r.cues.push_back (sl.start);
            r.cueEnds.push_back (sl.end);
            r.cueRms.push_back (sl.rms);
        }
    }

    inline std::vector<ChopSlice> buildChops (const juce::AudioBuffer<float>& audio, double fs,
                                              const AnalysisResult& an, const ChopBuildOpts& opt)
    {
        using namespace chops;
        std::vector<ChopSlice> out;
        const juce::int64 len = audio.getNumSamples();
        if (len <= 0) return out;
        const float peak = an.peakRms > 0.0f ? an.peakRms : bufferRms (audio, 0, len, 16);
        const float gate = threshAbs (peak, opt.threshold);
        const double bpm = opt.gridBpm > 0.0 ? opt.gridBpm : an.bpm;
        const juce::int64 origin = an.onsets.empty() ? 0 : an.onsets.front();
        const juce::int64 minLen = (juce::int64) (opt.minLenMs * 0.001 * fs);
        const double gridMs = opt.tightGrid ? 30.0 : 6.0;

        std::vector<ChopSlice> pool;
        auto consider = [&] (juce::int64 s0, juce::int64 e0, float rate)
        {
            ChopSlice sl; sl.start = s0; sl.end = e0; sl.rate = rate;
            if (bpm > 0.0) sl.start = snapGrid (sl.start, fs, bpm, origin, gridMs);
            sl.start = snapZero (audio, sl.start, fs, 3.0);
            trimSilence (audio, sl.start, sl.end, gate, fs, opt.minLenMs);
            sl.start = snapZero (audio, sl.start, fs, 2.0);
            if (sl.end - sl.start < minLen) return;
            sl.rms = bufferRms (audio, sl.start, sl.end, 4);
            sl.vocalRms = vocalRms (audio, sl.start, sl.end, fs);
            if (sl.rms < gate) return;
            pool.push_back (sl);
        };

        if (! an.eligible.empty())
        {
            for (auto& el : an.eligible)
            {
                // Re-trim at the live threshold (eligible was built at 10%).
                consider (el.start, el.end, 1.0f);
            }
        }
        else
        {
            for (size_t i = 0; i < an.onsets.size(); ++i)
            {
                auto s = an.onsets[i];
                auto e = i + 1 < an.onsets.size() ? an.onsets[i + 1] : len;
                consider (s, e, 1.0f);
            }
        }

        // Unique by start (within 15 ms)
        std::sort (pool.begin(), pool.end(), [] (const ChopSlice& a, const ChopSlice& b) { return a.start < b.start; });
        std::vector<ChopSlice> uniq;
        for (auto& c : pool)
        {
            if (! uniq.empty() && (double) (c.start - uniq.back().start) < 0.015 * fs)
            {
                if (c.rms + 0.5f * c.vocalRms > uniq.back().rms + 0.5f * uniq.back().vocalRms)
                    uniq.back() = c;
                continue;
            }
            uniq.push_back (c);
        }
        pool.swap (uniq);

        if (pool.empty())
        {
            // Degenerate / silent: evenly spaced grid, marked with rms 0 so auto-flip can skip them.
            const double beat = bpm > 0.0 ? 60.0 / bpm * fs : 0.5 * fs;
            const double step = beat / 4.0;
            for (int i = 0; i < opt.count; ++i)
            {
                ChopSlice sl;
                sl.start = xsmp::clampI (0, len - 1, (juce::int64) (i * step));
                sl.end   = xsmp::clampI (sl.start + 1, len, (juce::int64) ((i + 1) * step));
                sl.rms = 0; sl.rate = 1.0f;
                out.push_back (sl);
            }
            return out;
        }

        const int want = juce::jlimit (1, 16, opt.count);
        if (opt.seed == 0 && ! opt.varyLengths)
        {
            // Deterministic: loudest with a musical min gap, time-sorted, first `want`.
            const double minGap = juce::jmax (0.06 * fs, bpm > 0.0 ? 0.5 * (60.0 / bpm * fs / 4.0) : 0.06 * fs);
            std::vector<ChopSlice> sorted = pool;
            std::sort (sorted.begin(), sorted.end(), [] (const ChopSlice& a, const ChopSlice& b)
            { return a.rms + 0.5f * a.vocalRms > b.rms + 0.5f * b.vocalRms; });
            for (auto& c : sorted)
            {
                if ((int) out.size() >= want) break;
                bool ok = true;
                for (auto& q : out)
                    if (std::abs ((double) (q.start - c.start)) < minGap) { ok = false; break; }
                if (ok) out.push_back (c);
            }
            for (auto& c : sorted)
            {
                if ((int) out.size() >= want) break;
                bool used = false;
                for (auto& q : out) if (q.start == c.start) { used = true; break; }
                if (! used) out.push_back (c);
            }
            std::sort (out.begin(), out.end(), [] (const ChopSlice& a, const ChopSlice& b) { return a.start < b.start; });
            return out;
        }

        // GENERATE: a new random set every press. Mix short, long and half-time chops.
        juce::Random rng ((juce::int64) (opt.seed | 1u));
        const double beatSamp = bpm > 0.0 ? 60.0 / bpm * fs : 0.5 * fs;
        std::vector<int> idx ((int) pool.size());
        std::iota (idx.begin(), idx.end(), 0);
        // Weighted shuffle: prefer higher vocal/RMS energy at the front, then pick.
        std::sort (idx.begin(), idx.end(), [&] (int a, int b)
        { return pool[(size_t) a].rms + 0.6f * pool[(size_t) a].vocalRms > pool[(size_t) b].rms + 0.6f * pool[(size_t) b].vocalRms; });
        // Fisher-Yates on a prefix that still includes quieter-but-valid chops
        const int span = (int) idx.size();
        for (int i = span - 1; i > 0; --i)
        {
            // slightly biased: more likely to swap within the louder half, but any slice can move
            const int j = rng.nextInt (i + 1);
            std::swap (idx[(size_t) i], idx[(size_t) j]);
        }
        const double minGap = juce::jmax (0.05 * fs, 0.25 * (beatSamp / 4.0));
        for (int k = 0; k < span && (int) out.size() < want; ++k)
        {
            auto sl = pool[(size_t) idx[(size_t) k]];
            bool ok = true;
            for (auto& q : out)
                if (std::abs ((double) (q.start - sl.start)) < minGap) { ok = false; break; }
            if (! ok) continue;

            if (opt.varyLengths)
            {
                const float roll = rng.nextFloat();
                if (roll < 0.28f)
                {
                    // short chop: 1/16 .. 1/8
                    const juce::int64 dur = (juce::int64) ((rng.nextBool() ? 0.25 : 0.5) * beatSamp);
                    sl.end = juce::jmin (sl.end, sl.start + juce::jmax (minLen, dur));
                    sl.rate = 1.0f;
                }
                else if (roll < 0.55f)
                {
                    // medium: 1/4 .. 1/2
                    const juce::int64 dur = (juce::int64) ((rng.nextBool() ? 1.0 : 2.0) * 0.5 * beatSamp);
                    sl.end = juce::jmin (len, sl.start + juce::jmax (minLen, dur));
                    sl.rate = 1.0f;
                }
                else if (roll < 0.80f)
                {
                    // long: 1 .. 2 bars
                    const juce::int64 dur = (juce::int64) ((rng.nextBool() ? 4.0 : 8.0) * beatSamp);
                    sl.end = juce::jmin (len, sl.start + juce::jmax (minLen, dur));
                    sl.rate = 1.0f;
                }
                else
                {
                    // slow / half-time: take a shorter source and stretch it
                    const juce::int64 dur = (juce::int64) ((rng.nextBool() ? 1.0 : 2.0) * beatSamp);
                    sl.end = juce::jmin (sl.end, sl.start + juce::jmax (minLen, dur));
                    sl.rate = rng.nextBool() ? 0.5f : 0.5f;
                }
                trimSilence (audio, sl.start, sl.end, gate, fs, opt.minLenMs);
                sl.rms = bufferRms (audio, sl.start, sl.end, 4);
                if (sl.rms < gate || sl.end - sl.start < minLen) continue;
            }
            out.push_back (sl);
        }
        // If the gap rule left us short, fill from the remaining pool without the gap test
        for (int k = 0; k < span && (int) out.size() < want; ++k)
        {
            auto sl = pool[(size_t) idx[(size_t) k]];
            bool used = false;
            for (auto& q : out) if (q.start == sl.start) { used = true; break; }
            if (! used) out.push_back (sl);
        }
        std::sort (out.begin(), out.end(), [] (const ChopSlice& a, const ChopSlice& b) { return a.start < b.start; });
        if ((int) out.size() > want) out.resize ((size_t) want);
        return out;
    }

    inline bool sliceIsSilent (float rms, float peak, float relThresh)
    {
        return rms < chops::threshAbs (peak, relThresh);
    }

