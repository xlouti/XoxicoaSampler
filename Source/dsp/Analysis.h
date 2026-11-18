#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

// Offline analysis run on a background thread when a sample is loaded:
//  - BPM   : spectral-flux onset envelope -> autocorrelation with a comb (harmonic) score,
//            folded into 70..180 BPM, fractional lag refined over several beat multiples.
//  - Key   : chromagram (55 Hz..2 kHz) correlated with Krumhansl-Kessler major/minor profiles.
//  - Cues  : spectral-flux peak picking with an adaptive threshold, the 16 strongest onsets kept
//            with a minimum spacing, refined to the transient start in the time domain, sorted.
//            If fewer than 16 onsets exist, the largest gaps are split on the 1/16 beat grid.
namespace xsmp
{
    inline juce::int64 clampI (juce::int64 lo, juce::int64 hi, juce::int64 v) { return v < lo ? lo : (v > hi ? hi : v); }

    struct AnalysisResult
    {
        double bpm = 0.0;           // 0 = unknown
        int keyRoot = -1;           // 0..11, -1 = unknown
        bool minor = false;
        float keyConfidence = 0.0f; // Pearson r of the winning profile
        std::vector<juce::int64> cues; // 16 positions in source samples (sorted)
        std::vector<juce::int64> onsets; // every detected onset (for display/tests)
    };

    namespace analysis
    {
        inline std::vector<float> mono (const juce::AudioBuffer<float>& b)
        {
            std::vector<float> m ((size_t) b.getNumSamples(), 0.0f);
            const float g = 1.0f / (float) juce::jmax (1, b.getNumChannels());
            for (int c = 0; c < b.getNumChannels(); ++c)
            {
                auto* d = b.getReadPointer (c);
                for (int i = 0; i < b.getNumSamples(); ++i) m[(size_t) i] += d[i] * g;
            }
            return m;
        }

        // Spectral flux on log-compressed magnitudes. frame N = 1024, hop H = 256.
        inline std::vector<float> onsetEnvelope (const std::vector<float>& x, double fs, int& hopOut)
        {
            constexpr int order = 10, N = 1 << order, H = 256;
            hopOut = H;
            juce::dsp::FFT fft (order);
            std::vector<float> win (N), buf (2 * N), prev (N / 2 + 1, 0.0f), cur (N / 2 + 1);
            for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
            const int maxBin = juce::jmin (N / 2, (int) (12000.0 / fs * N));
            const int frames = (int) x.size() / H + 1;
            std::vector<float> env ((size_t) frames, 0.0f);
            for (int f = 0; f < frames; ++f)
            {
                const int start = f * H - N / 2; // frame centred on f*H
                std::fill (buf.begin(), buf.end(), 0.0f);
                for (int i = 0; i < N; ++i)
                {
                    const int idx = start + i;
                    if (idx >= 0 && idx < (int) x.size()) buf[(size_t) i] = x[(size_t) idx] * win[(size_t) i];
                }
                fft.performFrequencyOnlyForwardTransform (buf.data(), true);
                float flux = 0.0f;
                for (int k = 1; k <= maxBin; ++k)
                {
                    cur[(size_t) k] = std::log1p (100.0f * buf[(size_t) k]);
                    flux += juce::jmax (0.0f, cur[(size_t) k] - prev[(size_t) k]);
                }
                std::swap (cur, prev);
                env[(size_t) f] = flux;
            }
            return env;
        }

        inline double detectBpm (const std::vector<float>& env, double frameRate)
        {
            if (env.size() < (size_t) (frameRate * 1.5)) return 0.0;
            // remove slow trend (local mean over ~1 s) and half-wave rectify
            const int w = (int) frameRate;
            std::vector<double> e (env.size());
            std::vector<double> cs (env.size() + 1, 0.0);
            for (size_t i = 0; i < env.size(); ++i) cs[i + 1] = cs[i] + env[i];
            for (size_t i = 0; i < env.size(); ++i)
            {
                const size_t a = (size_t) juce::jmax (0, (int) i - w / 2), b = juce::jmin (env.size(), i + (size_t) w / 2 + 1);
                const double mean = (cs[b] - cs[a]) / (double) (b - a);
                e[i] = juce::jmax (0.0, (double) env[i] - mean);
            }
            const int minLag = (int) std::floor (frameRate * 60.0 / 240.0);
            const int maxLag = (int) std::ceil  (frameRate * 60.0 / 40.0);
            juce::ignoreUnused (minLag, maxLag);
            std::vector<double> ac ((size_t) juce::jmax (8, juce::jmin ((int) e.size() - 2, (int) std::ceil (frameRate * 60.0 / 70.0 * 4.0) + 4)), 0.0);
            for (int L = 1; L < (int) ac.size(); ++L)
            {
                double s = 0.0; const int n = (int) e.size() - L;
                if (n <= 0) break;
                for (int i = 0; i < n; ++i) s += e[(size_t) i] * e[(size_t) (i + L)];
                ac[(size_t) L] = s / (double) n;
            }
            auto acAt = [&] (double L) {
                const int i = (int) L; const double fr = L - i;
                if (i + 1 >= (int) ac.size()) return 0.0;
                return ac[(size_t) i] * (1.0 - fr) + ac[(size_t) (i + 1)] * fr;
            };
            auto peakNear = [&] (double L) { // max over +-1 frame
                double m = 0.0;
                for (double d = -1.0; d <= 1.0; d += 0.25) m = juce::jmax (m, acAt (L + d));
                return m;
            };
            // candidate beat periods 70..180 BPM, comb score over multiples (bars line up) + gentle prior
            double best = -1.0, bestLag = 0.0;
            for (double bpm = 70.0; bpm < 180.0; bpm += 0.25)
            {
                const double L = frameRate * 60.0 / bpm;
                double s = peakNear (L) + 0.6 * peakNear (2 * L) + 0.4 * peakNear (4 * L) + 0.3 * peakNear (0.5 * L);
                const double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 125.0) / 0.9, 2.0));
                s *= (0.85 + 0.15 * prior);
                if (s > best) { best = s; bestLag = L; }
            }
            if (bestLag <= 0.0) return 0.0;
            // refine: locate the true peak near k*L for k = 1..8 and average
            double sumL = 0.0, sumW = 0.0;
            for (int k = 1; k <= 8; ++k)
            {
                const double c = bestLag * k;
                if (c + 3 >= (double) ac.size()) break;
                int bi = (int) std::round (c); double bv = -1.0;
                for (int i = (int) std::floor (c - 2); i <= (int) std::ceil (c + 2); ++i)
                    if (i > 0 && i + 1 < (int) ac.size() && ac[(size_t) i] > bv) { bv = ac[(size_t) i]; bi = i; }
                const double y0 = ac[(size_t) (bi - 1)], y1 = ac[(size_t) bi], y2 = ac[(size_t) (bi + 1)];
                const double den = y0 - 2 * y1 + y2;
                const double off = std::abs (den) > 1e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (y0 - y2) / den) : 0.0;
                sumL += (bi + off); sumW += k; // estimate (bi+off)/k weighted by k: longer lags are more precise
            }
            const double L = sumW > 0 ? sumL / sumW : bestLag;
            double bpm = frameRate * 60.0 / L;
            if (std::abs (bpm - std::round (bpm)) < 0.2) bpm = std::round (bpm);
            else bpm = std::round (bpm * 10.0) / 10.0;
            return bpm;
        }

        inline void detectKey (const std::vector<float>& x, double fs, AnalysisResult& r)
        {
            constexpr int order = 13, N = 1 << order;
            const int H = N / 2;
            if ((int) x.size() < N) return;
            juce::dsp::FFT fft (order);
            std::vector<float> win (N), buf (2 * N);
            for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
            std::vector<int> binPc (N / 2 + 1, -1);
            for (int k = 1; k <= N / 2; ++k)
            {
                const double f = k * fs / N;
                if (f < 55.0 || f > 2000.0) continue;
                const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
                const double nearest = std::round (midi);
                if (std::abs (midi - nearest) > 0.4) continue; // in-between bins are ambiguous
                binPc[(size_t) k] = ((int) nearest % 12 + 12) % 12;
            }
            double chroma[12] = {};
            for (int s = 0; s + N <= (int) x.size(); s += H)
            {
                for (int i = 0; i < N; ++i) buf[(size_t) i] = x[(size_t) (s + i)] * win[(size_t) i];
                std::fill (buf.begin() + N, buf.end(), 0.0f);
                fft.performFrequencyOnlyForwardTransform (buf.data(), true);
                double frame[12] = {}, tot = 0.0;
                for (int k = 1; k <= N / 2; ++k)
                    if (binPc[(size_t) k] >= 0) { const double m = (double) buf[(size_t) k] * buf[(size_t) k]; frame[binPc[(size_t) k]] += m; tot += m; }
                if (tot <= 1e-9) continue;
                for (int p = 0; p < 12; ++p) chroma[p] += std::sqrt (frame[p] / tot); // per-frame normalised
            }
            static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
            static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
            auto pearson = [] (const double* a, const double* b) {
                double ma = 0, mb = 0; for (int i = 0; i < 12; ++i) { ma += a[i]; mb += b[i]; }
                ma /= 12; mb /= 12; double sab = 0, saa = 0, sbb = 0;
                for (int i = 0; i < 12; ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
                return (saa > 0 && sbb > 0) ? sab / std::sqrt (saa * sbb) : 0.0;
            };
            double best = -2.0;
            for (int root = 0; root < 12; ++root)
                for (int m = 0; m < 2; ++m)
                {
                    double prof[12];
                    for (int i = 0; i < 12; ++i) prof[(i + root) % 12] = (m ? mnr : maj)[i];
                    const double c = pearson (chroma, prof);
                    if (c > best) { best = c; r.keyRoot = root; r.minor = (m == 1); }
                }
            r.keyConfidence = (float) best;
        }

        // refine an onset estimate to where the transient actually starts (time domain). Works on the first
        // difference (HF emphasis) so sustained bass / chords before the hit don't trigger an early crossing.
        inline juce::int64 refineOnset (const std::vector<float>& x, juce::int64 approx, double fs)
        {
            const int back = (int) (0.012 * fs), fwd = (int) (0.030 * fs);
            const juce::int64 a = std::max<juce::int64> (1, approx - back), b = std::min<juce::int64> ((juce::int64) x.size(), approx + fwd);
            if (b - a < 8) return approx;
            const float coef = std::exp (-1.0f / (float) (0.0005 * fs));
            auto hf = [&] (juce::int64 i) { return std::abs (x[(size_t) i] - x[(size_t) i - 1]); };
            float e = 0.0f;
            const juce::int64 pre = std::max<juce::int64> (1, a - (juce::int64) (0.004 * fs));
            float floorLevel = 0.0f;
            for (juce::int64 i = pre; i < a; ++i) { e = std::max (hf (i), e * coef); floorLevel = std::max (floorLevel, e); }
            std::vector<float> env ((size_t) (b - a));
            for (juce::int64 i = a; i < b; ++i) { e = std::max (hf (i), e * coef); env[(size_t) (i - a)] = e; }
            const float peak = *std::max_element (env.begin(), env.end());
            if (peak < floorLevel * 1.5f) return approx;
            const float thr = floorLevel + 0.25f * (peak - floorLevel);
            for (size_t i = 0; i < env.size(); ++i)
                if (env[i] >= thr)
                    return std::max<juce::int64> (0, a + (juce::int64) i - (juce::int64) (0.001 * fs)); // 1 ms pre-roll keeps the attack
            return approx;
        }

        inline void detectCues (const std::vector<float>& x, const std::vector<float>& env, int hop, double fs, AnalysisResult& r, int numCues)
        {
            const double frameRate = fs / hop;
            const juce::int64 len = (juce::int64) x.size();
            r.onsets.clear(); r.cues.clear();
            if (len <= 0) return;
            const float gmax = env.empty() ? 0.0f : *std::max_element (env.begin(), env.end());
            struct Cand { juce::int64 pos; float strength; };
            std::vector<Cand> cands;
            const int wMean = juce::jmax (2, (int) (0.15 * frameRate));
            for (int i = 0; i + 1 < (int) env.size(); ++i)
            {
                bool isMax = true;
                for (int d = -3; d <= 3 && isMax; ++d)
                    if (d != 0 && i + d >= 0 && i + d < (int) env.size() && env[(size_t) (i + d)] > env[(size_t) i]) isMax = false;
                if (! isMax) continue;
                double s = 0; int n = 0;
                for (int d = -wMean; d <= wMean; ++d) if (i + d >= 0 && i + d < (int) env.size()) { s += env[(size_t) (i + d)]; ++n; }
                const float thr = (float) (s / juce::jmax (1, n)) * 1.3f + 0.06f * gmax;
                if (env[(size_t) i] > thr)
                    cands.push_back ({ refineOnset (x, (juce::int64) i * hop, fs), env[(size_t) i] - thr });
            }
            {
                std::vector<Cand> merged;
                for (auto& c : cands)
                {
                    if (! merged.empty() && (double) (c.pos - merged.back().pos) < 0.025 * fs)
                    {
                        if (c.strength > merged.back().strength) merged.back().strength = c.strength;
                        continue; // keep the earlier position (the transient start)
                    }
                    merged.push_back (c);
                }
                cands.swap (merged);
            }
            for (auto& c : cands) r.onsets.push_back (c.pos);

            const double beat = r.bpm > 0 ? 60.0 / r.bpm * fs : 0.5 * fs;
            const double sixteenth = beat / 4.0;
            double minGap = juce::jmax (0.07 * fs, 0.75 * sixteenth);
            minGap = juce::jmax (minGap, (double) len / (numCues * 2.5)); // spread across long samples
            std::vector<Cand> sorted = cands;
            std::sort (sorted.begin(), sorted.end(), [] (const Cand& a, const Cand& b) { return a.strength > b.strength; });
            for (auto& c : sorted)
            {
                if ((int) r.cues.size() >= numCues) break;
                bool ok = true;
                for (auto q : r.cues) if (std::abs ((double) (q - c.pos)) < minGap) { ok = false; break; }
                if (ok) r.cues.push_back (c.pos);
            }
            std::sort (r.cues.begin(), r.cues.end());
            // fill: split the biggest gap on the 1/16 grid (relative to the first cue / sample start)
            const juce::int64 gridOrigin = r.cues.empty() ? 0 : r.cues.front();
            while ((int) r.cues.size() < numCues)
            {
                std::vector<juce::int64> pts { 0 };
                for (auto q : r.cues) pts.push_back (q);
                pts.push_back (len);
                juce::int64 bestA = 0, bestB = len, bestGap = -1;
                for (size_t i = 0; i + 1 < pts.size(); ++i)
                    if (pts[i + 1] - pts[i] > bestGap) { bestGap = pts[i + 1] - pts[i]; bestA = pts[i]; bestB = pts[i + 1]; }
                juce::int64 mid = (bestA + bestB) / 2;
                const double g = std::round ((double) (mid - gridOrigin) / sixteenth) * sixteenth + (double) gridOrigin;
                if (g > (double) bestA + 0.25 * sixteenth && g < (double) bestB - 0.25 * sixteenth) mid = (juce::int64) g;
                if (bestGap < 2 || std::find (r.cues.begin(), r.cues.end(), mid) != r.cues.end()) break;
                r.cues.push_back (xsmp::clampI (0, len - 1, mid));
                std::sort (r.cues.begin(), r.cues.end());
            }
            while ((int) r.cues.size() < numCues) r.cues.push_back (0); // degenerate (tiny) samples
        }
    }

    inline AnalysisResult analyse (const juce::AudioBuffer<float>& audio, double fs, int numCues = 16)
    {
        AnalysisResult r;
        auto x = analysis::mono (audio);
        if (x.empty()) return r;
        int hop = 256;
        auto env = analysis::onsetEnvelope (x, fs, hop);
        r.bpm = analysis::detectBpm (env, fs / hop);
        analysis::detectKey (x, fs, r);
        analysis::detectCues (x, env, hop, fs, r, numCues);
        return r;
    }

    // Key-target snap: semitone offset (-6..+5) that moves the detected key onto the target.
    // Mode differences are handled through the relative major (A min == C maj).
    inline int keySnapOffset (int detRoot, bool detMinor, int targetChoice)
    {
        if (targetChoice <= 0 || detRoot < 0) return 0;
        const int tIdx = targetChoice - 1;
        const bool tMinor = tIdx >= 12;
        const int tRoot = tIdx % 12;
        const int detRel = detMinor ? (detRoot + 3) % 12 : detRoot;
        const int tRel   = tMinor   ? (tRoot + 3) % 12   : tRoot;
        int d = ((tRel - detRel) % 12 + 12) % 12;
        if (d > 5) d -= 12;
        return d;
    }
}
