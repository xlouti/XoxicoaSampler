// Xoxicoa Sampler offline test + knob audit.
//   XoxicoaSamplerTest <outDir>
// Writes demo WAVs + results into outDir. Exit code = number of failed checks.
#include "PluginProcessor.h"
#include <cstdio>
#include <map>

using namespace xsmp;
static constexpr double kHost = 48000.0, kFileFs = 44100.0;
static constexpr int kBlock = 512;
static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}
static double db (double v) { return 20.0 * std::log10 (v + 1e-12); }

// ------------------------------------------------------------------ synthetic material
struct Beat { juce::AudioBuffer<float> audio; std::vector<double> onsets; double bpm; int root; bool minor; };

static Beat makeBeat (double bpm, int bars, int root, bool minor, double fs, unsigned seed = 1)
{
    Beat b; b.bpm = bpm; b.root = root; b.minor = minor;
    const double beat = 60.0 / bpm;
    const int n = (int) std::ceil (bars * 4 * beat * fs);
    b.audio.setSize (2, n); b.audio.clear();
    juce::Random rng ((juce::int64) seed);
    auto add = [&] (int i, float v) { if (i >= 0 && i < n) { b.audio.addSample (0, i, v); b.audio.addSample (1, i, v); } };
    auto fadeOut = [&] (int i, int len) { const int f = (int) (0.005 * fs); return (float) juce::jmin (1.0, (double) (len - i) / f); };
    const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 }, majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const int* sc = minor ? minorScale : majorScale;
    const int prog[4] = { 0, 5, 3, 4 }; // i-VI-iv-v (minor) / I-vi-IV-V (major), as scale degrees
    for (int bar = 0; bar < bars; ++bar)
    {
        // chord pad (soft attack -> no extra onsets): triad of the degree, 3 harmonics
        const int deg = prog[bar % 4];
        const int t0 = (int) (bar * 4 * beat * fs), t1 = (int) ((bar + 1) * 4 * beat * fs);
        for (int k = 0; k < 3; ++k)
        {
            const int d = deg + 2 * k;
            const int semi = sc[d % 7] + 12 * (d / 7);
            const double f = 220.0 * std::pow (2.0, (root - 9 + semi) / 12.0) * (root > 6 ? 0.5 : 1.0) * 1.0;
            for (int i = t0; i < t1; ++i)
            {
                const double t = (i - t0) / fs, T = (t1 - t0) / fs;
                const float env = (float) (juce::jmin (1.0, t / 0.08) * juce::jmin (1.0, (T - t) / 0.05));
                float v = 0;
                for (int h = 1; h <= 3; ++h) v += (float) (std::sin (juce::MathConstants<double>::twoPi * f * h * t) / (h * h));
                add (i, 0.07f * env * v);
            }
        }
        for (int e8 = 0; e8 < 8; ++e8)
        {
            const double tOn = (bar * 4 + e8 * 0.5) * beat;
            b.onsets.push_back (tOn);
            const int s0 = (int) std::round (tOn * fs);
            // hat
            for (int i = 0; i < (int) (0.04 * fs); ++i)
            {
                const float env = std::exp (-(float) i / (float) (0.008 * fs)) * fadeOut (i, (int) (0.04 * fs));
                add (s0 + i, 0.18f * env * (rng.nextFloat() * 2 - 1) * ((i & 1) ? 1.0f : -1.0f));
            }
            // bass on every beat (root of chord), short
            if (e8 % 2 == 0)
            {
                const int semi = sc[deg % 7];
                const double f = 55.0 * std::pow (2.0, (root - 9 + semi + 12) / 12.0);
                for (int i = 0; i < (int) (beat * 0.45 * fs); ++i)
                {
                    const double t = i / fs;
                    add (s0 + i, fadeOut (i, (int) (beat * 0.45 * fs)) * (float) (0.22 * std::exp (-t / 0.25) * juce::jmin (1.0, t / 0.002) * std::sin (juce::MathConstants<double>::twoPi * f * t)));
                }
            }
            if (e8 == 0 || e8 == 3 || e8 == 4) // kick
            {
                double ph = 0;
                for (int i = 0; i < (int) (0.25 * fs); ++i)
                {
                    const double t = i / fs, f = 45.0 + 90.0 * std::exp (-t / 0.03);
                    ph += juce::MathConstants<double>::twoPi * f / fs;
                    add (s0 + i, fadeOut (i, (int) (0.25 * fs)) * (float) (0.55 * std::exp (-t / 0.09) * std::sin (ph)));
                }
            }
            if (e8 == 2 || e8 == 6) // snare
                for (int i = 0; i < (int) (0.15 * fs); ++i)
                {
                    const double t = i / fs;
                    add (s0 + i, fadeOut (i, (int) (0.15 * fs)) * (float) (std::exp (-t / 0.05) * (0.3 * (rng.nextFloat() * 2 - 1) + 0.25 * std::sin (juce::MathConstants<double>::twoPi * 190 * t))));
                }
        }
    }
    return b;
}

static juce::AudioBuffer<float> makeTone (double freq, double seconds, double fs, float amp = 0.5f)
{
    juce::AudioBuffer<float> b (2, (int) (seconds * fs));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = amp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / fs);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}

static void writeWav (const juce::AudioBuffer<float>& b, const juce::File& f, double fs = kHost)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (auto* w = wav.createWriterFor (os.get(), fs, (unsigned) b.getNumChannels(), 24, {}, 0))
    {
        os.release();
        std::unique_ptr<juce::AudioFormatWriter> writer (w);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

// ------------------------------------------------------------------ host simulation
struct PlayHead : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0, ppqStart = 0.0;
    bool transport = false; // true = host playing with a song position
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm);
        p.setIsPlaying (transport || true);
        if (transport) { p.setPpqPosition (ppq); p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 }); }
        return p;
    }
};

struct Ev { int sample; int note; float vel; }; // vel 0 = note off

struct Rig
{
    XoxicoaSamplerProcessor proc;
    PlayHead head;
    explicit Rig (const juce::File& sample, double hostBpm = 140.0)
    {
        head.bpm = hostBpm;
        proc.setPlayHead (&head);
        proc.setPlayConfigDetails (0, 2, kHost, kBlock);
        proc.prepareToPlay (kHost, kBlock);
        if (sample != juce::File()) proc.loadFileSync (sample);
    }
    void set (const juce::String& id, float plainValue)
    {
        auto* p = proc.apvts.getParameter (id);
        jassert (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    }
    void setNorm (const juce::String& id, float norm) { proc.apvts.getParameter (id)->setValueNotifyingHost (norm); }
    juce::AudioBuffer<float> render (int total, std::vector<Ev> evs)
    {
        juce::AudioBuffer<float> out (2, total);
        out.clear();
        std::sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.sample < b.sample; });
        juce::AudioBuffer<float> blk (2, kBlock);
        size_t e = 0;
        for (int pos = 0; pos < total; pos += kBlock)
        {
            const int n = juce::jmin (kBlock, total - pos);
            blk.setSize (2, n, false, false, true);
            juce::MidiBuffer mb;
            head.ppq = head.ppqStart + pos / kHost * head.bpm / 60.0;
            while (e < evs.size() && evs[e].sample < pos + n)
            {
                const auto& ev = evs[e++];
                mb.addEvent (ev.vel > 0 ? juce::MidiMessage::noteOn (1, ev.note, ev.vel) : juce::MidiMessage::noteOff (1, ev.note), juce::jmax (0, ev.sample - pos));
            }
            proc.processBlock (blk, mb);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
        }
        return out;
    }
};

// ------------------------------------------------------------------ measurements
static double rms (const juce::AudioBuffer<float>& b, int s0 = 0, int s1 = -1)
{
    if (s1 < 0) s1 = b.getNumSamples();
    double s = 0; int n = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = s0; i < s1; ++i) { s += (double) b.getSample (c, i) * b.getSample (c, i); ++n; }
    return std::sqrt (s / juce::jmax (1, n));
}
static float peak (const juce::AudioBuffer<float>& b) { float p = 0; for (int c = 0; c < b.getNumChannels(); ++c) p = juce::jmax (p, b.getMagnitude (c, 0, b.getNumSamples())); return p; }
static bool finite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}
static double maxD2 (const juce::AudioBuffer<float>& b)
{
    double m = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 2; i < b.getNumSamples(); ++i)
            m = juce::jmax (m, (double) std::abs (b.getSample (c, i) - 2 * b.getSample (c, i - 1) + b.getSample (c, i - 2)));
    return m;
}
// dominant frequency via FFT 16384 + parabolic interpolation on a steady section
static double freqOf (const juce::AudioBuffer<float>& b, int start, double fs = kHost)
{
    constexpr int order = 15, N = 1 << order;
    if (start + N > b.getNumSamples()) start = juce::jmax (0, b.getNumSamples() - N);
    juce::dsp::FFT fft (order);
    std::vector<float> buf (2 * N, 0.0f);
    for (int i = 0; i < N && start + i < b.getNumSamples(); ++i)
        buf[(size_t) i] = b.getSample (0, start + i) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    int k = 1; for (int i = 2; i < N / 2 - 1; ++i) if (buf[(size_t) i] > buf[(size_t) k]) k = i;
    const double a = std::log (buf[(size_t) k - 1] + 1e-12), bb = std::log (buf[(size_t) k] + 1e-12), c = std::log (buf[(size_t) k + 1] + 1e-12);
    const double off = 0.5 * (a - c) / (a - 2 * bb + c);
    return (k + off) * fs / N;
}
// time (s) at which a 10 ms RMS falls below -20 dB of the level measured at 50..150 ms
static double soundingLength (const juce::AudioBuffer<float>& b)
{
    const int w = (int) (0.01 * kHost);
    const double ref = rms (b, (int) (0.05 * kHost), (int) (0.15 * kHost));
    for (int s = (int) (0.05 * kHost); s + w < b.getNumSamples(); s += w / 2)
        if (rms (b, s, s + w) < ref * 0.1) return s / kHost;
    return b.getNumSamples() / kHost;
}
static std::array<double, 10> octaveBands (const juce::AudioBuffer<float>& b)
{
    constexpr int order = 12, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf (2 * N);
    std::array<double, 10> e {};
    for (int s = 0; s + N <= b.getNumSamples(); s += N)
    {
        for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (b.getSample (0, s + i) + b.getSample (1, s + i)) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N));
        std::fill (buf.begin() + N, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 1; k < N / 2; ++k)
        {
            const int band = (int) std::floor (std::log2 (k * kHost / N / 22.3));
            if (band >= 0 && band < 10) e[(size_t) band] += (double) buf[(size_t) k] * buf[(size_t) k];
        }
    }
    for (auto& v : e) v = 10.0 * std::log10 (v + 1e-20);
    return e;
}
static double bandDelta (const std::array<double, 10>& a, const std::array<double, 10>& b)
{
    const double top = *std::max_element (a.begin(), a.end());
    double d = 0;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] > top - 60.0) d = std::max (d, std::abs (a[i] - b[i]));
    return d;
}
static double diffDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& ref)
{
    double s = 0;
    const int n = juce::jmin (a.getNumSamples(), ref.getNumSamples());
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) { const double d = a.getSample (c, i) - ref.getSample (c, i); s += d * d; }
    return db (std::sqrt (s / (2.0 * n)) / juce::jmax (1e-9, rms (ref)));
}

// ================================================================== tests
static juce::File gDir;

static void analysisTests()
{
    std::printf ("\n=== AUTO ANALYSIS (BPM / KEY / 16 CUES) on synthetic beats, files at 44.1 kHz ===\n");
    struct Case { double bpm; int bars; int root; bool minor; };
    const Case cases[] = { { 140, 2, 9, true }, { 160, 4, 6, true }, { 92, 4, 2, false }, { 75, 4, 0, true }, { 128, 4, 7, false }, { 172, 4, 11, true }, { 150, 4, 4, true }, { 100, 4, 5, false } };
    int exactBpm = 0, octaveBpm = 0;
    int idx = 0;
    for (auto& c : cases)
    {
        auto b = makeBeat (c.bpm, c.bars, c.root, c.minor, kFileFs, (unsigned) (idx + 3));
        auto r = analyse (b.audio, kFileFs, kNumPads);
        const juce::String keyTrue = keyNames()[c.root] + (c.minor ? " min" : " maj");
        const juce::String keyGot  = r.keyRoot >= 0 ? keyNames()[r.keyRoot] + (r.minor ? " min" : " maj") : "--";
        // cue accuracy vs the true 8th-note onsets
        double worst = 0; int matched = 0;
        std::vector<bool> used (b.onsets.size(), false);
        for (auto q : r.cues)
        {
            double best = 1e9; size_t bi = 0;
            for (size_t i = 0; i < b.onsets.size(); ++i) { const double d = std::abs (q / kFileFs - b.onsets[i]); if (d < best) { best = d; bi = i; } }
            worst = juce::jmax (worst, best);
            if (best < 0.010 && ! used[bi]) { used[bi] = true; ++matched; }
        }
        std::printf ("  %5.0f BPM %d bars %-6s -> BPM %6.1f  key %-6s (r=%.2f)  cues on a real onset: %2d/16, worst error %.1f ms\n",
                     c.bpm, c.bars, keyTrue.toRawUTF8(), r.bpm, keyGot.toRawUTF8(), r.keyConfidence, matched, worst * 1000.0);
        const bool exact = std::abs (r.bpm - c.bpm) < 0.6;
        const bool octave = std::abs (r.bpm * 2.0 - c.bpm) < 1.0 || std::abs (r.bpm * 0.5 - c.bpm) < 0.6;
        exactBpm += exact ? 1 : 0; octaveBpm += (! exact && octave) ? 1 : 0;
        check (exact || octave, "BPM " + juce::String (c.bpm) + " detected (" + juce::String (r.bpm, 1) + (exact ? ", exact)" : octave ? ", octave: fix with x2 / /2)" : ")"));
        check (keyGot == keyTrue, "key " + keyTrue + " detected (" + keyGot + ")");
        check (matched == 16 && worst < 0.010, "16 distinct cues, each within 10 ms of a real transient");
        if (std::getenv ("XSMP_DEBUG"))
        {
            std::printf ("    onsets:"); for (auto o : r.onsets) std::printf (" %.3f", o / kFileFs);
            std::printf ("\n    cues  :"); for (auto q : r.cues) std::printf (" %.3f", q / kFileFs);
            std::printf ("\n    truth :"); for (auto t : b.onsets) std::printf (" %.3f", t); std::printf ("\n");
        }
        if (idx == 0) writeWav (b.audio, gDir.getChildFile ("00_source_loop_140bpm_Amin.wav"), kFileFs);
        ++idx;
    }
    std::printf ("  BPM: %d exact, %d half/double-time (one click on x2 or /2), %d wrong\n", exactBpm, octaveBpm, (int) std::size (cases) - exactBpm - octaveBpm);
    // short / silent edge cases
    juce::AudioBuffer<float> silent (2, 22050); silent.clear();
    auto rs = analyse (silent, kFileFs, kNumPads);
    check (rs.cues.size() == 16 && rs.bpm == 0.0, "silent file: 16 (grid) cues, BPM unknown, no crash");
    juce::AudioBuffer<float> tiny (2, 100); tiny.clear();
    check (analyse (tiny, kFileFs, kNumPads).cues.size() == 16, "100-sample file: no crash");
}

static void pitchTempoTests (const juce::File& toneFile, const juce::File& beatFile)
{
    std::printf ("\n=== KEY SHIFT / TEMPO SYNC (Signalsmith Stretch), 440 Hz tone file @44.1k, host 48k ===\n");
    auto setCuesTone = [] (Rig& rig) { for (int i = 0; i < kNumPads; ++i) rig.proc.setCue (i, (juce::int64) (i * 1.0 * kFileFs)); };
    // tone file has no BPM: give it one manually so sync can be measured
    auto run = [&] (int semis, bool sync, double hostBpm, double sampleBpm, int mode, double holdSec) {
        Rig rig (toneFile, hostBpm);
        setCuesTone (rig);
        rig.proc.setBpmOverride (sampleBpm);
        rig.set ("key_shift", (float) semis);
        rig.set ("sync", sync ? 1.0f : 0.0f);
        rig.set ("pad1_mode", (float) mode);
        rig.set ("pad1_release", 5.0f);
        return rig.render ((int) (3.0 * kHost), { { 0, 36, 1.0f }, { (int) (holdSec * kHost), 36, 0.0f } });
    };
    struct P { int semis; };
    for (int s : { -12, -7, -1, 0, 1, 3, 5, 7, 12 })
    {
        auto out = run (s, false, 120, 120, 1, 2.5);
        const double f = freqOf (out, (int) (0.3 * kHost)), want = 440.0 * std::pow (2.0, s / 12.0);
        const double cents = 1200.0 * std::log2 (f / want);
        std::printf ("  key %+3d st : %8.2f Hz (want %8.2f)  error %+5.1f cents  len %.2fs\n", s, f, want, cents, soundingLength (out));
        check (std::abs (cents) < 8.0, "key shift " + juce::String (s) + " st within 8 cents");
        check (finite (out), "finite output");
    }
    // tempo: one-shot slice of 1.0 s (file seconds). Ratio host/sample.
    for (auto pr : { std::pair<double, double> { 160, 140 }, { 140, 160 }, { 120, 60 }, { 90, 120 } })
        for (int s : { 0, 5 })
        {
            auto out = run (s, true, pr.first, pr.second, 0, 0.01);
            const double ratio = pr.first / pr.second;
            const double len = soundingLength (out), wantLen = 1.0 / ratio;
            const double f = freqOf (out, (int) (0.1 * kHost)), want = 440.0 * std::pow (2.0, s / 12.0);
            // frequency measured on a shorter window for short slices
            std::printf ("  sync %3.0f->%3.0f BPM, key %+d : slice %.3fs (want %.3f)  pitch %7.2f Hz (want %7.2f)\n",
                         pr.second, pr.first, s, len, wantLen, f, want);
            check (std::abs (len - wantLen) < 0.03, "tempo ratio " + juce::String (ratio, 3) + " sets duration (+-30 ms)");
            check (std::abs (1200.0 * std::log2 (f / want)) < 15.0, "pitch independent of tempo (+-15 cents)");
        }
    // sync off = original duration at any key
    {
        auto out = run (7, false, 160, 120, 0, 0.01);
        check (std::abs (soundingLength (out) - 1.0) < 0.03, "sync off: duration unchanged with key +7");
    }
    // direct path (no shift, no sync) at a different file rate: pitch preserved by resampling
    {
        auto out = run (0, false, 120, 120, 1, 2.5);
        check (std::abs (1200.0 * std::log2 (freqOf (out, (int) (0.3 * kHost)) / 440.0)) < 2.0, "44.1k file in 48k host: pitch exact (direct path)");
    }
    // zero-latency pad start (stretch path): first audible sample within 3 ms of the note
    {
        Rig rig (beatFile, 160);
        rig.set ("key_shift", 3.0f);
        auto out = rig.render ((int) (0.5 * kHost), { { 1000, 36, 1.0f } });
        int first = -1;
        for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > 0.01f) { first = i; break; }
        std::printf ("  stretch path: note at 1000, first sound at %d (%.2f ms)\n", first, (first - 1000) / kHost * 1000.0);
        check (first >= 1000 && first - 1000 < (int) (0.003 * kHost), "stretch path starts on the note (outputSeek pre-roll, no latency)");
    }
    // key target snap: detected A min -> target C maj = 0, D min = +5, E min = -5 (wrap), F# maj = -3 (rel. D# min... ) 
    {
        Rig rig (beatFile, 140);
        auto& a = rig.proc.getSample()->analysis;
        struct T { const char* name; int choice; int want; };
        const T ts[] = { { "C maj", 1, 0 }, { "A min", 1 + 12 + 9, 0 }, { "D min", 1 + 12 + 2, 5 }, { "E min", 1 + 12 + 4, -5 }, { "B min", 1 + 12 + 11, 2 }, { "F maj", 1 + 5, 5 } };
        for (auto& t : ts)
        {
            const int got = keySnapOffset (a.keyRoot, a.minor, t.choice);
            check (got == t.want, juce::String ("key target ") + t.name + " from detected A min = " + juce::String (got) + " st");
        }
    }
}

static void playbackTests (const juce::File& toneFile, const juce::File& beatFile)
{
    std::printf ("\n=== PAD MODES / ENVELOPES / CHOKE / CLICKS / NaN ===\n");
    // one-shot plays the slice and ignores note-off
    {
        Rig rig (toneFile);
        for (int i = 0; i < kNumPads; ++i) rig.proc.setCue (i, (juce::int64) (i * 0.5 * kFileFs));
        rig.set ("pad1_release", 5.0f);
        auto out = rig.render ((int) (2 * kHost), { { 0, 36, 1.0f }, { 480, 36, 0.0f } });
        check (std::abs (soundingLength (out) - 0.5) < 0.02, "one-shot: plays the whole 0.5 s slice despite a 10 ms note");
        rig.set ("pad1_mode", 1.0f);
        out = rig.render ((int) (2 * kHost), { { 0, 36, 1.0f }, { (int) (0.25 * kHost), 36, 0.0f } });
        check (std::abs (soundingLength (out) - 0.25) < 0.02, "gate: stops at note-off (0.25 s)");
        out = rig.render ((int) (2 * kHost), { { 0, 36, 1.0f }, { (int) (1.2 * kHost), 36, 0.0f } });
        check (std::abs (soundingLength (out) - 1.2) < 0.02, "gate: plays past later cues while held (1.2 s)");
        rig.set ("pad1_mode", 2.0f);
        out = rig.render ((int) (2.5 * kHost), { { 0, 36, 1.0f }, { (int) (1.7 * kHost), 36, 0.0f } });
        check (std::abs (soundingLength (out) - 1.7) < 0.02, "loop: loops the 0.5 s slice while held (1.7 s)");
        check (maxD2 (out) < 0.02, "loop seam is click-free (max |d2| " + juce::String (maxD2 (out), 4) + ")");
        // release length
        rig.set ("pad1_mode", 1.0f); rig.set ("pad1_release", 400.0f);
        out = rig.render ((int) (2 * kHost), { { 0, 36, 1.0f }, { (int) (0.3 * kHost), 36, 0.0f } });
        const double l = soundingLength (out);
        check (l > 0.55 && l < 0.75, "gate release 400 ms audibly extends the tail (" + juce::String (l, 2) + " s)");
        // attack
        rig.set ("pad1_release", 5.0f); rig.set ("pad1_attack", 200.0f);
        out = rig.render ((int) (1 * kHost), { { 0, 36, 1.0f }, { (int) (0.8 * kHost), 36, 0.0f } });
        const double early = rms (out, 0, (int) (0.02 * kHost)), later = rms (out, (int) (0.3 * kHost), (int) (0.4 * kHost));
        check (db (early) - db (later) < -15.0, "attack 200 ms fades in (first 20 ms " + juce::String (db (early) - db (later), 1) + " dB)");
    }
    // choke
    {
        Rig rig (toneFile);
        for (int i = 0; i < kNumPads; ++i) rig.proc.setCue (i, (juce::int64) (i * 0.2 * kFileFs));
        rig.set ("pad1_mode", 1.0f); rig.set ("pad2_mode", 1.0f);
        auto both = rig.render ((int) (1 * kHost), { { 0, 36, 1.0f }, { (int) (0.3 * kHost), 37, 1.0f } });
        rig.set ("pad1_choke", 1.0f); rig.set ("pad2_choke", 1.0f);
        auto choked = rig.render ((int) (1 * kHost), { { 0, 36, 1.0f }, { (int) (0.3 * kHost), 37, 1.0f } });
        const double lb = rms (both, (int) (0.4 * kHost), (int) (0.6 * kHost)), lc = rms (choked, (int) (0.4 * kHost), (int) (0.6 * kHost));
        std::printf ("  overlap level %.1f dB, with choke %.1f dB\n", db (lb), db (lc));
        check (db (lb) - db (lc) > 4.0, "choke group: pad 2 cuts pad 1");
        check (maxD2 (choked) < 0.03, "choke is click-free (max |d2| " + juce::String (maxD2 (choked), 4) + ")");
    }
    // stress: random triggers on the tone (clicks are easy to see on a sine) with all modes, shifts and filter moves
    {
        Rig rig (toneFile, 150);
        rig.proc.setBpmOverride (120.0);
        rig.set ("master", -12.0f); // keep the polyphonic sum below the output safety clipper: this test is about the voices
        juce::Random rng (7);
        for (int i = 0; i < kNumPads; ++i) rig.proc.setCue (i, (juce::int64) (rng.nextDouble() * 3.5 * kFileFs));
        for (int p = 0; p < kNumPads; ++p)
        {
            rig.set (padId (p, "mode"), (float) (p % 3));
            rig.set (padId (p, "choke"), (float) (p % 5));
            rig.set (padId (p, "filter"), (float) (rng.nextDouble() * 2 - 1));
            rig.set (padId (p, "attack"), 0.5f);
            rig.set (padId (p, "release"), 3.0f);
        }
        double worst = 0; bool fin = true; float pk = 0;
        for (int pass = 0; pass < 4; ++pass)
        {
            rig.set ("key_shift", (float) (pass * 4 - 6));
            rig.set ("sync", pass >= 2 ? 1.0f : 0.0f);
            std::vector<Ev> evs;
            for (int k = 0; k < 120; ++k)
            {
                const int t = rng.nextInt ((int) (5.5 * kHost)), note = 36 + rng.nextInt (16);
                evs.push_back ({ t, note, 0.4f + 0.6f * rng.nextFloat() });
                evs.push_back ({ t + rng.nextInt ((int) (0.5 * kHost)) + 64, note, 0.0f });
            }
            auto out = rig.render ((int) (6 * kHost), evs);
            fin &= finite (out); pk = juce::jmax (pk, peak (out));
            // d2 of a 440 Hz sine at amp 0.5 (x up to 2 for +12 st ... and polyphony) ~ 0.0004..0.01
            worst = juce::jmax (worst, maxD2 (out) / juce::jmax (0.05f, peak (out)));
            if (pass == 3) writeWav (out, gDir.getChildFile ("90_stress_random_triggers_tone.wav"));
        }
        std::printf ("  stress: 480 random notes over all modes/chokes/filters/shifts: worst |d2|/peak %.4f, peak %.2f\n", worst, pk);
        check (fin, "stress: no NaN/Inf");
        check (worst < 0.05, "stress: no clicks (normalised |d2| < 0.05; a hard step would be >= 0.2)");
    }
    // extremes: every param at min and at max, rapid retrigger
    {
        Rig rig (beatFile, 175);
        bool fin = true;
        for (float norm : { 0.0f, 1.0f })
        {
            for (auto* p : rig.proc.getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) if (rp->getParameterID() != "pad_root") rp->setValueNotifyingHost (norm);
            rig.set ("pad_root", 36.0f);
            std::vector<Ev> evs;
            for (int k = 0; k < 200; ++k) evs.push_back ({ k * 240, 36 + (k % 16), k % 3 ? 1.0f : 0.0f });
            fin &= finite (rig.render ((int) (2 * kHost), evs));
        }
        check (fin, "all params at min and at max + 200 rapid retriggers: finite");
    }
    // state round trip (path + edited cues + params)
    {
        Rig rig (beatFile);
        rig.proc.setCue (5, 12345); rig.set ("key_shift", 4.0f); rig.set ("pad6_filter", -0.5f);
        juce::MemoryBlock mb; rig.proc.getStateInformation (mb);
        Rig rig2 (juce::File(), 140);
        rig2.proc.setStateInformation (mb.getData(), (int) mb.getSize()); // async load on the message thread
        for (int k = 0; k < 200 && rig2.proc.getSample() == nullptr; ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
        auto s2 = rig2.proc.getSample();
        check (s2 != nullptr && rig2.proc.getCue (5) == 12345 && (int) rig2.proc.apvts.getRawParameterValue ("key_shift")->load() == 4
               && std::abs (rig2.proc.apvts.getRawParameterValue ("pad6_filter")->load() + 0.5f) < 1e-3,
               "state restore: sample path, edited cue, params");
        rig2.proc.resetCuesToDetected();
        check (rig2.proc.getCue (5) != 12345, "RESET CUES restores the detected cue points");
    }
}

static void performanceTests (const juce::File& beatFile)
{
    std::printf ("\n=== PERFORMANCE (this Linux box, single core, Release) ===\n");
    {
        auto b = makeBeat (150, 2, 4, true, kFileFs, 9);
        juce::AudioBuffer<float> song (2, (int) (180.0 * kFileFs));
        for (int pos = 0; pos < song.getNumSamples(); pos += b.audio.getNumSamples())
            for (int c = 0; c < 2; ++c) song.copyFrom (c, pos, b.audio, c, 0, juce::jmin (b.audio.getNumSamples(), song.getNumSamples() - pos));
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto r = analyse (song, kFileFs, kNumPads);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("  analysis of a 3-minute track: %.0f ms (background thread), BPM %.1f\n", ms, r.bpm);
        check (ms < 5000.0, "3-minute analysis under 5 s");
        check (r.cues.size() == 16 && r.cues.back() > (juce::int64) (90.0 * kFileFs), "long file: cues spread across the track (last cue at " + juce::String (r.cues.back() / kFileFs, 1) + " s)");
    }
    for (int voices : { 1, 8, 12 })
    {
        Rig rig (beatFile, 160);
        rig.set ("key_shift", 3.0f); rig.set ("sync", 1.0f);
        std::vector<Ev> evs;
        for (int p = 0; p < voices; ++p) { rig.set (padId (p, "mode"), 2.0f); evs.push_back ({ p * 64, 36 + p, 0.8f }); }
        const int total = (int) (10.0 * kHost);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto out = rig.render (total, evs);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("  %2d stretched voices (key +3, synced, looping): %.1f%% of one core in real time\n", voices, ms / 10000.0 * 100.0);
        if (voices == 8) check (ms / 10000.0 < 0.6, "8 stretched voices under 60% of one core");
    }
    {
        Rig rig (beatFile, 160);
        rig.set ("key_shift", 3.0f);
        std::vector<Ev> evs;
        double worst = 0;
        juce::AudioBuffer<float> blk (2, kBlock);
        for (int k = 0; k < 64; ++k)
        {
            juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::noteOn (1, 36 + (k % 16), 1.0f), 0);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            rig.proc.processBlock (blk, mb);
            worst = juce::jmax (worst, juce::Time::getMillisecondCounterHiRes() - t0);
        }
        std::printf ("  worst block with a stretched pad trigger (outputSeek pre-roll): %.2f ms of a %.1f ms block\n", worst, kBlock / kHost * 1000.0);
        check (worst < kBlock / kHost * 1000.0 * 0.5, "trigger spike stays under half the block time");
    }
}

static void formatTests (const juce::File& beatFile)
{
    std::printf ("\n=== FILE FORMATS ===\n");
    for (auto ext : { "aiff", "mp3", "flac" })
    {
        auto f = gDir.getChildFile (juce::String ("fmt_test.") + ext);
        f.deleteFile();
        juce::ChildProcess cp;
        juce::StringArray args { "ffmpeg", "-loglevel", "error", "-y", "-i", beatFile.getFullPathName() };
        if (juce::String (ext) == "mp3") args.addArray ({ "-b:a", "256k" });
        args.add (f.getFullPathName());
        if (! cp.start (args) || ! cp.waitForProcessToFinish (60000) || ! f.existsAsFile()) { std::printf ("  (ffmpeg unavailable for %s - skipped)\n", ext); continue; }
        Rig rig (f, 140);
        auto s = rig.proc.getSample();
        const bool ok = s != nullptr && std::abs (s->analysis.bpm - 140.0) < 0.6 && s->analysis.keyRoot == 9 && s->analysis.minor;
        check (ok, juce::String (ext).toUpperCase() + " loads; BPM " + (s ? juce::String (s->analysis.bpm, 1) : "-") + ", key " + (s && s->analysis.keyRoot >= 0 ? keyNames()[s->analysis.keyRoot] + (s->analysis.minor ? " min" : " maj") : "-"));
        f.deleteFile();
    }
    Rig bad (juce::File(), 140);
    auto junk = gDir.getChildFile ("junk.wav"); junk.replaceWithText ("not audio");
    check (! bad.proc.loadFileSync (junk) && bad.proc.getLastError().isNotEmpty(), "damaged file is rejected with a message, no crash");
    junk.deleteFile();
}

// ------------------------------------------------------------------ demos
static void demos (const juce::File& beatFile)
{
    std::printf ("\n=== DEMO WAVs ===\n");
    const double beat160 = 60.0 / 160.0 * kHost, beat140 = 60.0 / 140.0 * kHost;
    auto pattern = [] (double beat) {
        std::vector<Ev> e;
        const int seq[16] = { 0, 2, 4, 2, 8, 9, 4, 6, 0, 0, 12, 2, 14, 15, 4, 7 };
        for (int bar = 0; bar < 2; ++bar)
            for (int k = 0; k < 16; ++k)
            {
                const int t = (int) ((bar * 16 + k) * beat / 2);
                e.push_back ({ t, 36 + seq[k], 0.9f });
                e.push_back ({ t + (int) (beat / 2) - 64, 36 + seq[k], 0.0f });
            }
        return e;
    };
    struct D { const char* file; double host; int key; bool sync; int target; int mode; };
    const D ds[] = {
        { "01_pads_flip_original_140.wav", 140, 0, false, 0, 0 },
        { "02_pads_flip_key_+3.wav", 140, 3, false, 0, 0 },
        { "03_pads_flip_key_-5.wav", 140, -5, false, 0, 0 },
        { "04_pads_flip_synced_140_to_160.wav", 160, 0, true, 0, 0 },
        { "05_pads_flip_synced_160_key_+2.wav", 160, 2, true, 0, 0 },
        { "06_pads_flip_keytarget_E_min_snap.wav", 140, 0, false, 1 + 12 + 4, 0 },
        { "07_pads_gate_mode_synced_160.wav", 160, 0, true, 0, 1 },
    };
    for (auto& d : ds)
    {
        Rig rig (beatFile, d.host);
        rig.set ("master", -3.0f);
        rig.set ("key_shift", (float) d.key); rig.set ("sync", d.sync ? 1.0f : 0.0f); rig.set ("key_target", (float) d.target);
        for (int p = 0; p < kNumPads; ++p) rig.set (padId (p, "mode"), (float) d.mode);
        auto out = rig.render ((int) (32 * (d.host == 160 ? beat160 : beat140) / 2 + kHost), pattern (d.host == 160 ? beat160 : beat140));
        writeWav (out, gDir.getChildFile (d.file));
        std::printf ("  %-44s peak %.2f dBFS  finite %s\n", d.file, db (peak (out)), finite (out) ? "yes" : "NO");
        if (! finite (out)) ++failures;
    }
    { // loop mode held + filter sweep via automation
        Rig rig (beatFile, 140);
        rig.set ("pad1_mode", 2.0f);
        juce::AudioBuffer<float> all (2, (int) (6 * kHost)); all.clear();
        std::vector<Ev> evs { { 0, 36, 1.0f } };
        for (int seg = 0; seg < 12; ++seg)
        {
            rig.set ("pad1_filter", -1.0f + 2.0f * seg / 11.0f);
            auto part = rig.render ((int) (0.5 * kHost), seg == 0 ? evs : std::vector<Ev> {});
            for (int c = 0; c < 2; ++c) all.copyFrom (c, (int) (seg * 0.5 * kHost), part, c, 0, part.getNumSamples());
        }
        writeWav (all, gDir.getChildFile ("08_loop_mode_pad1_filter_sweep_LP_to_HP.wav"));
        std::printf ("  08_loop_mode_pad1_filter_sweep_LP_to_HP.wav     finite %s\n", finite (all) ? "yes" : "NO");
    }
}

// ------------------------------------------------------------------ AUTO
static XoxicoaSamplerProcessor::AutoHistoryEntry fixedAuto (juce::uint32 base)
{
    XoxicoaSamplerProcessor::AutoHistoryEntry e;
    for (int v = 0; v < kNumAutoSlots; ++v) { e.seeds[(size_t) v] = base * 2654435761u + (juce::uint32) v * 97u + 1u; e.strats[(size_t) v] = v; }
    return e;
}
static double eventDiff (const AutoFlip& a, const AutoFlip& b)
{
    // share of onset cells (union of both flips' hits on the 1/32 grid) whose (onset, chop, flags, pitch, length) differ
    int diff = 0, cells = 0; const int L = juce::jmax (a.lenTicks, b.lenTicks);
    auto at = [] (const AutoFlip& f, int t) -> const AutoEvent* { for (int i = 0; i < f.num; ++i) if (f.ev[(size_t) i].tick == t) return &f.ev[(size_t) i]; return nullptr; };
    for (int t = 0; t < L; ++t)
    {
        auto* x = at (a, t); auto* y = at (b, t);
        if (x == nullptr && y == nullptr) continue;
        ++cells;
        if ((x == nullptr) != (y == nullptr)) { ++diff; continue; }
        if (x && (x->altPad != y->altPad || x->pad != y->pad || x->flags != y->flags || x->semis != y->semis || x->len != y->len)) ++diff;
    }
    return cells > 0 ? (double) diff / cells : 0.0;
}

static void autoTests (const juce::File& toneFile, const juce::File& beatFile)
{
    std::printf ("\n=== AUTO (flip generator, versions A-F, history/undo/lock, sync) ===\n");
    auto autoDir = gDir.getChildFile ("auto"); autoDir.createDirectory();
    // 10 consecutive rolls
    {
        Rig rig (beatFile, 140);
        std::vector<juce::AudioBuffer<float>> renders;
        std::vector<AutoFlip> flips;
        std::vector<int> strats;
        for (int k = 0; k < 10; ++k)
        {
            rig.proc.autoRollAll();
            juce::AudioBuffer<float> b;
            rig.proc.renderAutoVersion (0, 3, 140.0, 1, b, kHost);
            renders.push_back (b);
            flips.push_back (rig.proc.getAutoFlip (0, 3));
            strats.push_back (rig.proc.getAutoStrategy (0));
        }
        std::printf ("  10 rolls of version A (4 bars @140): strategies");
        for (auto st : strats) std::printf (" %s", strategyName (st));
        std::printf ("\n  pairwise audio difference (dB re. RMS; 0 dB = as loud as the music itself, -inf = identical):\n       ");
        for (int j = 0; j < 10; ++j) std::printf ("  #%-4d", j + 1);
        std::printf ("\n");
        double minDb = 1e9, sumDb = 0, minEv = 1e9, sumEv = 0; int pairs = 0;
        for (int i = 0; i < 10; ++i)
        {
            std::printf ("   #%-2d", i + 1);
            for (int j = 0; j < 10; ++j)
            {
                if (j <= i) { std::printf ("  %5s", j == i ? "--" : ""); continue; }
                const double dd = diffDb (renders[(size_t) j], renders[(size_t) i]);
                minDb = juce::jmin (minDb, dd); sumDb += dd; ++pairs;
                { const double ed = eventDiff (flips[(size_t) i], flips[(size_t) j]); minEv = juce::jmin (minEv, ed); sumEv += ed; }
                std::printf ("  %+5.1f", dd);
            }
            std::printf ("\n");
        }
        std::printf ("  audio: min %+.1f dB, mean %+.1f dB over %d pairs; pattern: min %.0f%% / mean %.0f%% of hit cells differ\n", minDb, sumDb / pairs, pairs, minEv * 100.0, sumEv / pairs * 100.0);
        check (minDb > -6.0, "10 consecutive rolls are all clearly different (every pair > -6 dB)");
        check (minEv > 0.5, "every pair of rolls differs in > 50% of hit cells (onset, chop, FX or pitch)");
        // the six versions of one roll are distinct too
        double minSlot = 1e9;
        std::vector<juce::AudioBuffer<float>> sl;
        for (int v = 0; v < kNumAutoSlots; ++v) { juce::AudioBuffer<float> b; rig.proc.renderAutoVersion (v, 3, 140.0, 1, b, kHost); sl.push_back (b); }
        for (int i = 0; i < 6; ++i) for (int j = i + 1; j < 6; ++j) minSlot = juce::jmin (minSlot, diffDb (sl[(size_t) j], sl[(size_t) i]));
        std::printf ("  versions A-F of one roll: min pairwise %+.1f dB\n", minSlot);
        check (minSlot > -6.0, "versions A-F are distinct flips");
    }
    // exact lengths: cycle restarts land on exact sample multiples
    for (double bpm : { 140.0, 160.0 })
    {
        Rig rig (beatFile, bpm);
        rig.head.transport = true;
        juce::String line;
        bool allOk = true;
        for (int li = 0; li < kNumAutoLens; ++li)
        {
            rig.set ("auto_play", 0.0f);
            rig.render (kBlock * 2, {});
            rig.set (autoLenId (0), (float) li); rig.set ("auto_slot", 0.0f); rig.set ("auto_play", 1.0f);
            const double exact = autoLenTicks (li) / (double) kTicksPerBeat * 60.0 / bpm * kHost;
            const int cyclesWanted = li <= 2 ? 4 : (li == 3 ? 3 : 2);
            const auto before = rig.proc.getAutoCycleStarts().size();
            rig.head.ppqStart = 0.0;
            rig.render ((int) std::ceil (exact * cyclesWanted) + 16, {});
            auto cs = rig.proc.getAutoCycleStarts();
            std::vector<juce::int64> st (cs.begin() + (long) juce::jmin (before, cs.size()), cs.end());
            double worst = 0;
            for (size_t k = 1; k < st.size(); ++k) worst = juce::jmax (worst, std::abs ((double) (st[k] - st[0]) - k * exact));
            const bool ok = (int) st.size() >= cyclesWanted && worst <= 1.0;
            allOk &= ok;
            line << autoLenChoices()[li] << " " << juce::String (exact / kHost, 4) << "s (" << (int) st.size() << " cycles, max err " << juce::String (worst, 2) << " smp)  ";
        }
        std::printf ("  %3.0f BPM: %s\n", bpm, line.toRawUTF8());
        check (allOk, "all 6 Auto lengths exact at " + juce::String (bpm, 0) + " BPM (cycle starts within 1 sample of n x length)");
    }
    // MIDI C0 trigger (host stopped): starts on the note, stops on note-off; C#0.. selects versions
    {
        Rig rig (beatFile, 150);
        auto out = rig.render ((int) (3 * kHost), { { 4800, kAutoTriggerNote, 1.0f }, { (int) (2 * kHost), kAutoTriggerNote, 0.0f } });
        const auto cs = rig.proc.getAutoCycleStarts();
        int first = -1; for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > 1e-4f) { first = i; break; }
        const double tail = rms (out, (int) (2.2 * kHost), (int) (3 * kHost));
        std::printf ("  C0 trigger at 4800: first cycle start %lld, first sound %d, after note-off %.1f dBFS\n", cs.empty() ? -1LL : (long long) cs.front(), first, db (tail));
        check (! cs.empty() && cs.front() == 4800 && first >= 4800 && first < 4800 + 48, "MIDI C0 starts the Auto version on the note");
        check (db (tail) < -80.0, "note-off stops it");
        rig.render (kBlock, { { 0, kAutoSlotNote + 3, 1.0f }, { 100, kAutoSlotNote + 3, 0.0f } });
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        check ((int) rig.proc.apvts.getRawParameterValue ("auto_slot")->load() == 3, "MIDI D#0 selects version D");
    }
    // AUTO PLAY latch waits for the next host bar line
    {
        Rig rig (beatFile, 160);
        rig.head.transport = true; rig.head.ppqStart = 1.5;  // host is mid-bar
        rig.set ("auto_play", 1.0f);
        rig.render ((int) (2 * kHost), {});
        const auto cs = rig.proc.getAutoCycleStarts();
        const double barAt = (4.0 - 1.5) * 60.0 / 160.0 * kHost;
        std::printf ("  latch pressed at beat 2.5: first cycle at sample %lld (next bar line = %.1f)\n", cs.empty() ? -1LL : (long long) cs.front(), barAt);
        check (! cs.empty() && std::abs ((double) cs.front() - barAt) <= 1.0, "AUTO PLAY starts exactly on the next host bar");
    }
    // history / undo / lock / per-slot re-roll
    {
        Rig rig (beatFile, 140);
        auto seeds = [&] { std::array<juce::uint32, 6> a {}; for (int v = 0; v < 6; ++v) a[(size_t) v] = rig.proc.getAutoSeed (v); return a; };
        const auto s0 = seeds();
        rig.proc.autoRollAll(); const auto s1 = seeds();
        rig.proc.autoRollAll();
        check (rig.proc.autoUndo() && seeds() == s1, "UNDO returns to the previous roll");
        check (rig.proc.autoUndo() && seeds() == s0, "UNDO twice returns to the first roll");
        rig.proc.autoJumpToHistory ((int) rig.proc.getAutoHistory().size() - 1);
        check (seeds() != s0, "HISTORY jumps forward again");
        rig.proc.setAutoLocked (1, true);
        const auto keepB = rig.proc.getAutoSeed (1);
        rig.proc.autoRollAll();
        check (rig.proc.getAutoSeed (1) == keepB, "LOCK keeps version B through AUTO");
        const auto before = seeds();
        rig.proc.autoRollSlot (2);
        auto after = seeds();
        bool onlyC = after[2] != before[2];
        for (int v = 0; v < 6; ++v) if (v != 2) onlyC &= after[(size_t) v] == before[(size_t) v];
        check (onlyC, "per-version re-roll changes only that version");
        rig.proc.autoRollSlot (1);
        check (rig.proc.getAutoSeed (1) == keepB, "re-roll is ignored on a locked version");
    }
    // chops stay editable: moving a cue changes the Auto version that uses it
    {
        Rig rig (beatFile, 140);
        rig.proc.autoRestore (fixedAuto (5));
        juce::AudioBuffer<float> a, b;
        rig.proc.renderAutoVersion (0, 3, 140.0, 1, a, kHost);
        for (int i = 0; i < kNumPads; ++i) rig.proc.setCue (i, rig.proc.getCue (i) + 3000);
        rig.proc.renderAutoVersion (0, 3, 140.0, 1, b, kHost);
        check (diffDb (b, a) > -10.0, "moving cue markers re-chops the Auto version (" + juce::String (diffDb (b, a), 1) + " dB)");
    }
    // project key: Key Target conforms the Auto version (tone file, Chaos 0 = no pitch accents)
    {
        Rig rig (toneFile, 120);
        rig.proc.setBpmOverride (120.0);
        rig.set ("auto_chaos", 0.0f); rig.set ("auto_density", 1.0f);
        const auto& an = rig.proc.getSample()->analysis;
        const int target = 1 + 2; // D maj
        const int snap = keySnapOffset (an.keyRoot, an.minor, target);
        rig.set ("key_target", (float) target);
        juce::AudioBuffer<float> b;
        rig.proc.renderAutoVersion (0, 3, 120.0, 1, b, kHost);
        const double f = freqOf (b, (int) (0.5 * kHost)), want = 440.0 * std::pow (2.0, snap / 12.0);
        std::printf ("  key target D maj on a tone detected as %s: snap %+d st, Auto output %.1f Hz (want %.1f)\n",
                     (keyNames()[juce::jmax (0, an.keyRoot)] + (an.minor ? " min" : " maj")).toRawUTF8(), snap, f, want);
        check (std::abs (1200.0 * std::log2 (f / want)) < 15.0, "Auto version follows the project key (Key Target)");
    }
    // clicks / NaN: every version, every length up to 4 bars, Chaos/Density max, swing on, on the tone file
    {
        Rig rig (toneFile, 160);
        rig.proc.setBpmOverride (120.0);
        juce::Random rng (11);
        for (int i = 0; i < kNumPads; ++i) rig.proc.setCue (i, (juce::int64) (rng.nextDouble() * 3.0 * kFileFs));
        rig.set ("master", -6.0f); rig.set ("auto_chaos", 1.0f); rig.set ("auto_density", 1.0f); rig.set ("auto_swing", 0.6f);
        double worst = 0; bool fin = true;
        for (int roll = 0; roll < 3; ++roll)
        {
            rig.proc.autoRollAll();
            for (int v = 0; v < kNumAutoSlots; ++v)
            {
                juce::AudioBuffer<float> b;
                rig.proc.renderAutoVersion (v, 3, 160.0, 1, b, kHost);
                fin &= finite (b);
                const double w = maxD2 (b) / juce::jmax (0.05f, peak (b));
                if (std::getenv ("XSMP_DEBUG") && w > 0.05)
                {
                    int wi = 0; double wm = 0;
                    for (int i = 2; i < b.getNumSamples(); ++i) { const double d2 = std::abs (b.getSample (0, i) - 2 * b.getSample (0, i - 1) + b.getSample (0, i - 2)); if (d2 > wm) { wm = d2; wi = i; } }
                    const double tick = wi / (60.0 / 160.0 / kTicksPerBeat * kHost);
                    std::printf ("    click roll %d slot %d strat %s at sample %d (tick %.3f) d2 %.4f; nearby:", roll, v, strategyName (rig.proc.getAutoStrategy (v)), wi, tick, wm);
                    for (int k = -3; k <= 3; ++k) std::printf (" %.4f", b.getSample (0, wi + k));
                    const auto& f = rig.proc.getAutoFlip (v, 3);
                    for (int i = 0; i < f.num; ++i) if (std::abs (f.ev[(size_t) i].tick - tick) < 1.5 || std::abs (f.ev[(size_t) i].tick + f.ev[(size_t) i].len - tick) < 1.5)
                        std::printf ("\n      ev t%d len%d pad%d/%d off%d/%d semis%d flags%d ranks a%.2f f%.2f d%.2f", f.ev[(size_t) i].tick, f.ev[(size_t) i].len, f.ev[(size_t) i].pad, f.ev[(size_t) i].altPad, f.ev[(size_t) i].off, f.ev[(size_t) i].altOff, f.ev[(size_t) i].semis, f.ev[(size_t) i].flags, f.ev[(size_t) i].altRank, f.ev[(size_t) i].fxRank, f.ev[(size_t) i].densRank);
                    std::printf ("\n");
                    writeWav (b, gDir.getChildFile ("debug_click.wav"));
                }
                worst = juce::jmax (worst, w);
            }
        }
        std::printf ("  18 Auto renders on a sine (reverse, octaves, stutters, half-time, swing): worst |d2|/peak %.4f\n", worst);
        check (fin, "Auto: no NaN/Inf");
        check (worst < 0.05, "Auto: no clicks (same threshold as the pad stress test)");
        Rig beat (beatFile, 172);
        beat.set ("auto_chaos", 1.0f); beat.set ("auto_density", 1.0f);
        juce::AudioBuffer<float> b; beat.proc.renderAutoVersion (2, 5, 172.0, 1, b, kHost);
        check (finite (b) && peak (b) <= 1.0f, "Auto 16 bars @172 on the beat: finite, never over 0 dBFS");
    }
    // demo WAVs: three different rolls at 4 bars + one 16-bar version (sample 140 BPM A min -> host 160, project key C min)
    {
        Rig rig (beatFile, 160);
        rig.set ("key_target", 1 + 12 + 0); // C min
        rig.set ("master", -2.0f);
        for (int k = 0; k < 3; ++k)
        {
            rig.proc.autoRollAll();
            juce::AudioBuffer<float> b;
            rig.proc.renderAutoVersion (0, 3, 160.0, 2, b, kHost);
            const juce::String name = "auto_roll" + juce::String (k + 1) + "_" + juce::String (strategyName (rig.proc.getAutoStrategy (0))).toLowerCase() + "_4bars_160bpm_Cmin_x2.wav";
            writeWav (b, autoDir.getChildFile (name));
            std::printf ("  %s  peak %.2f dBFS\n", name.toRawUTF8(), db (peak (b)));
        }
        juce::AudioBuffer<float> b;
        rig.proc.renderAutoVersion (4, 5, 160.0, 1, b, kHost);
        const juce::String name = "auto_" + juce::String (strategyName (rig.proc.getAutoStrategy (4))).toLowerCase() + "_16bars_160bpm_Cmin.wav";
        writeWav (b, autoDir.getChildFile (name));
        std::printf ("  %s  peak %.2f dBFS, %.3f s (exact 16 bars = %.3f s)\n", name.toRawUTF8(), db (peak (b)), b.getNumSamples() / kHost, 64 * 60.0 / 160.0);
        auto mid = rig.proc.exportAutoMidi (0, 160.0);
        juce::FileInputStream fis (mid); juce::MidiFile mf;
        check (fis.openedOk() && mf.readFrom (fis) && mf.getNumTracks() == 1 && mf.getTrack (0)->getNumEvents() > 8, "drag-out MIDI file is written and readable (" + mid.getFileName() + ")");
        mid.copyFileTo (autoDir.getChildFile ("auto_roll3_versionA.mid"));
    }
}

// ------------------------------------------------------------------ knob audit
static void knobAudit (const juce::File& beatFile)
{
    std::printf ("\n=== KNOB AUDIT (min / mid / max, every parameter; sample = 140 BPM A-min beat, host 160 BPM) ===\n");
    std::printf ("Sequence: pads 1-8 over 4 bars at 160 BPM, pad 1 held for 2 beats (longer than its slice), pad 2 overlaps pad 1.\n"
                 "Fixtures: pad 2 choke = group 1 (so pad 1's choke has a partner). Defaults otherwise (sync off, key 0).\n"
                 "levelD = RMS change vs min; diff = RMS(render - min render) re min render; band = max octave-band change.\n"
                 "FLAG (too subtle) when |level| < 1 dB AND diff < -20 dB AND band < 1.5 dB (same rule as Xoxicoa FX).\n\n");
    const double beat = 60.0 / 160.0 * kHost;
    std::vector<Ev> seq;
    auto hit = [&] (double atBeat, int pad, double lenBeats, float vel = 0.9f)
    { seq.push_back ({ (int) (atBeat * beat), 36 + pad, vel }); seq.push_back ({ (int) ((atBeat + lenBeats) * beat), 36 + pad, 0.0f }); };
    for (int bar = 0; bar < 4; ++bar)
    {
        const double b0 = bar * 4.0;
        hit (b0 + 0.0, 0, 2.0); hit (b0 + 0.5, 1, 0.5); hit (b0 + 1.0, 2, 0.25); hit (b0 + 1.5, 3, 0.5);
        hit (b0 + 2.0, 4, 0.5); hit (b0 + 2.5, 5, 0.25); hit (b0 + 3.0, 6, 0.5); hit (b0 + 3.5, 7, 0.25);
    }
    const int total = (int) (16 * beat + kHost);
    // pad-focused sequence for per-pad params: pad 1 alone (long + short holds), pad 2 lands inside pad 1's long holds
    std::vector<Ev> padSeq;
    auto phit = [&] (double atBeat, int pad, double lenBeats)
    { padSeq.push_back ({ (int) (atBeat * beat), 36 + pad, 0.9f }); padSeq.push_back ({ (int) ((atBeat + lenBeats) * beat), 36 + pad, 0.0f }); };
    for (int bar = 0; bar < 4; ++bar)
    {
        const double b0 = bar * 4.0;
        phit (b0 + 0.0, 0, 2.0); phit (b0 + 0.75, 1, 0.25); phit (b0 + 2.5, 0, 0.25); phit (b0 + 3.0, 0, 1.0); phit (b0 + 3.5, 1, 0.25);
    }

    auto renderWith = [&] (const std::map<juce::String, float>& norms, std::function<void (Rig&)> extra = {}, bool focus = false) {
        Rig rig (beatFile, 160);
        rig.set ("pad2_choke", 1.0f);
        if (extra) extra (rig);
        for (auto& kv : norms) rig.setNorm (kv.first, kv.second);
        return rig.render (total, focus ? padSeq : seq);
    };
    auto renderAuto = [&] (const std::map<juce::String, float>& norms, std::function<void (Rig&)> extra) {
        Rig rig (beatFile, 160);
        if (extra) extra (rig);
        for (auto& kv : norms) rig.setNorm (kv.first, kv.second);
        return rig.render ((int) (16 * beat), {}); // 4 bars, no pad notes: only the Auto version plays
    };
    struct Item { juce::String id; juce::String label; std::vector<float> norms; bool focus; std::function<void (Rig&)> extra; juce::String note; bool autoOnly = false; };
    XoxicoaSamplerProcessor info;
    auto normOf = [&] (const juce::String& id, float plain) { auto* p = info.apvts.getParameter (id); return p->convertTo0to1 (plain); };
    std::vector<Item> items;
    auto add = [&] (const juce::String& id, const juce::String& label, bool focus = false, std::function<void (Rig&)> extra = {}, juce::String note = {}) {
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (info.apvts.getParameter (id));
        std::vector<float> n { 0.0f, 0.5f, 1.0f };
        if (dynamic_cast<juce::AudioParameterChoice*> (p) || dynamic_cast<juce::AudioParameterBool*> (p))
        { n.clear(); const int st = p->getNumSteps(); for (int k = 0; k < st; ++k) n.push_back ((float) k / (float) juce::jmax (1, st - 1)); }
        items.push_back ({ id, label, n, focus, extra, note, false });
    };
    add ("master", "Volume"); add ("key_shift", "Key Shift"); add ("key_target", "Key Target (auto snap)"); add ("sync", "Tempo Sync (auto)");
    items.push_back ({ "pad_root", "Pad Root Note", { normOf ("pad_root", 36), normOf ("pad_root", 33), normOf ("pad_root", 40) }, false, {}, {}, false });
    add (padId (0, "mode"), "Pad mode", true);
    add (padId (0, "attack"), "Pad attack", true);
    add (padId (0, "release"), "Pad release (one-shot tail)", true);
    add (padId (0, "release"), "Pad release (gate)", true, [] (Rig& r) { r.set ("pad1_mode", 1.0f); }, "pad 1 in Gate");
    add (padId (0, "filter"), "Pad filter", true);
    add (padId (0, "gain"), "Pad gain", true);
    add (padId (0, "choke"), "Pad choke group", true, [] (Rig& r) { r.set ("pad1_mode", 1.0f); }, "pad 1 in Gate, pad 2 in group 1");
    // AUTO rows: fixed seeds, AUTO PLAY latched, host playing at 160 BPM, version A, 4 bars
    auto autoFix = [] (Rig& r) { r.proc.autoRestore (fixedAuto (42)); r.head.transport = true; r.set ("auto_play", 1.0f); };
    for (auto* id : { "auto_chaos", "auto_density", "auto_swing", "auto_slot", "auto_len_a" })
        { add (id, juce::String ("Auto ") + juce::String (id).fromFirstOccurrenceOf ("_", false, false), false, autoFix, "AUTO PLAY on, fixed roll"); items.back().autoOnly = true; }

    std::printf ("  %-26s %8s %8s %8s %8s %8s %8s  %s\n", "param", "lvl mid", "lvl max", "diff mid", "diff max", "band mid", "band max", "verdict");
    int flagged = 0;
    for (auto& it : items)
    {
        std::vector<juce::AudioBuffer<float>> r;
        for (float nv : it.norms) r.push_back (it.autoOnly ? renderAuto ({ { it.id, nv } }, it.extra) : renderWith ({ { it.id, nv } }, it.extra, it.focus));
        auto metrics = [&] (size_t k, double& lv, double& df, double& bd) {
            lv = db (rms (r[k])) - db (rms (r[0])); df = diffDb (r[k], r[0]); bd = bandDelta (octaveBands (r[0]), octaveBands (r[k])); };
        size_t iMid = it.norms.size() / 2, iMax = it.norms.size() - 1;
        const bool isChoice = dynamic_cast<juce::AudioParameterChoice*> (info.apvts.getParameter (it.id)) != nullptr;
        if (isChoice || it.id.endsWith ("_mode") || it.id.endsWith ("_choke"))
        { double best = -1e9; for (size_t k = 1; k < it.norms.size(); ++k) { double lv, df, bd; metrics (k, lv, df, bd); if (df > best) { best = df; iMax = k; } } }
        double l1, d1, b1, l2, d2, b2;
        metrics (iMid, l1, d1, b1); metrics (iMax, l2, d2, b2);
        bool fin = true; float pk = 0; for (auto& b : r) { fin &= finite (b); pk = juce::jmax (pk, peak (b)); }
        const bool flag = std::max (std::abs (l1), std::abs (l2)) < 1.0 && std::max (d1, d2) < -20.0 && std::max (b1, b2) < 1.5;
        flagged += flag ? 1 : 0;
        if (! fin) ++failures;
        std::printf ("  %-26s %+8.1f %+8.1f %8.1f %8.1f %8.1f %8.1f  %s%s\n", it.label.toRawUTF8(), l1, l2, d1, d2, b1, b2, flag ? "FLAG (too subtle)" : "ok", fin ? "" : " NAN!");
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (info.apvts.getParameter (it.id));
        std::printf ("  %-26s   (values: min '%s', mid '%s', max '%s'; peak %.2f dBFS%s%s)\n", "",
                     p->getText (it.norms[0], 32).toRawUTF8(), p->getText (it.norms[iMid], 32).toRawUTF8(), p->getText (it.norms[iMax], 32).toRawUTF8(), db (pk), it.note.isNotEmpty() ? "; fixture: " : "", it.note.toRawUTF8());
        if (pk > 1.0f) { std::printf ("  CLIP!\n"); ++failures; }
    }
    std::printf ("\n  flagged parameters: %d of %d rows (pad rows use pad 1 with a pad-focused sequence; pads 1-16 share the code, isolation checked below)\n", flagged, (int) items.size());
    if (flagged > 0) ++failures;

    // per-pad isolation: pad N's params only affect pad N, for every pad
    int isoFail = 0;
    for (int p = 0; p < kNumPads; ++p)
    {
        Rig rig (beatFile, 140);
        rig.set (padId (p, "gain"), -24.0f);
        auto own = rig.render ((int) (0.3 * kHost), { { 0, 36 + p, 1.0f } });
        auto other = rig.render ((int) (0.3 * kHost), { { 0, 36 + (p + 1) % 16, 1.0f } });
        Rig ref (beatFile, 140);
        auto ownRef = ref.render ((int) (0.3 * kHost), { { 0, 36 + p, 1.0f } });
        auto otherRef = ref.render ((int) (0.3 * kHost), { { 0, 36 + (p + 1) % 16, 1.0f } });
        const bool ok = db (rms (own)) - db (rms (ownRef)) < -20.0 && std::abs (diffDb (other, otherRef)) > 100.0 - 1e9 && diffDb (other, otherRef) < -100.0;
        if (! ok) ++isoFail;
    }
    check (isoFail == 0, "per-pad isolation: pad N gain -24 dB changes pad N by -24 dB and leaves the next pad bit-identical (all 16 pads)");

    // auto controls that are buttons / analysis rather than knobs
    {
        auto a = renderWith ({ { "sync", 1.0f } });
        auto b = renderWith ({ { "sync", 1.0f } }, [] (Rig& r) { r.proc.setBpmOverride (r.proc.getSampleBpm() * 2.0); });
        std::printf ("  BPM x2 button (sync on): diff %.1f dB vs detected BPM\n", diffDb (b, a));
        check (diffDb (b, a) > -20.0, "BPM x2 / /2 buttons change the synced playback");
        auto c = renderWith ({}, [] (Rig& r) { for (int i = 0; i < kNumPads; ++i) r.proc.setCue (i, r.proc.getCue (i) + 2000); });
        auto d0 = renderWith ({});
        check (diffDb (c, d0) > -20.0, "dragging cue markers changes what the pads play");
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    gDir = juce::File (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("sampler_demo").getFullPathName());
    gDir.createDirectory();
    auto beat = makeBeat (140, 2, 9, true, kFileFs, 3);
    auto beatFile = gDir.getChildFile ("00_source_loop_140bpm_Amin.wav");
    writeWav (beat.audio, beatFile, kFileFs);
    auto toneFile = gDir.getFullPathName() + "/tone440_44k.wav";
    writeWav (makeTone (440.0, 4.0, kFileFs), juce::File (toneFile), kFileFs);

    analysisTests();
    pitchTempoTests (juce::File (toneFile), beatFile);
    playbackTests (juce::File (toneFile), beatFile);
    formatTests (beatFile);
    performanceTests (beatFile);
    demos (beatFile);
    autoTests (juce::File (toneFile), beatFile);
    knobAudit (beatFile);
    juce::File (toneFile).deleteFile();

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures;
}
