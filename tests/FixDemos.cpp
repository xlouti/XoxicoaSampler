// Renders demo WAVs + a knob audit for the v1.2 sampler fixes.
// Usage: XoxicoaSamplerDemos <outDir> <vocal.wav>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

static constexpr double kHost = 48000.0;
static constexpr int kBlock = 512;

struct PlayHead : juce::AudioPlayHead
{
    double bpm = 155.0, ppq = 0.0, ppqStart = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (true);
        p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return p;
    }
};

struct Ev { int sample; int note; float vel; };

struct Rig
{
    XoxicoaSamplerProcessor proc;
    PlayHead head;
    explicit Rig (const juce::File& sample, double hostBpm = 155.0)
    {
        head.bpm = hostBpm;
        proc.setPlayHead (&head);
        proc.setPlayConfigDetails (0, 2, kHost, kBlock);
        proc.prepareToPlay (kHost, kBlock);
        if (sample != juce::File()) proc.loadFileSync (sample);
    }
    void set (const juce::String& id, float v)
    {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
    juce::AudioBuffer<float> render (int total, std::vector<Ev> evs)
    {
        juce::AudioBuffer<float> out (2, total); out.clear();
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
                mb.addEvent (ev.vel > 0 ? juce::MidiMessage::noteOn (1, ev.note, ev.vel)
                                        : juce::MidiMessage::noteOff (1, ev.note),
                             juce::jmax (0, ev.sample - pos));
            }
            proc.processBlock (blk, mb);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
        }
        return out;
    }
};

static double rms (const juce::AudioBuffer<float>& b)
{
    double s = 0; int n = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) { s += (double) b.getSample (c, i) * b.getSample (c, i); ++n; }
    return std::sqrt (s / juce::jmax (1, n));
}
static float peak (const juce::AudioBuffer<float>& b)
{
    float p = 0; for (int c = 0; c < 2; ++c) p = juce::jmax (p, b.getMagnitude (c, 0, b.getNumSamples())); return p;
}
static bool finite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}
static double db (double x) { return 20.0 * std::log10 (x + 1e-20); }
static double diffDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& ref)
{
    double s = 0; const int n = juce::jmin (a.getNumSamples(), ref.getNumSamples());
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) { const double d = a.getSample (c, i) - ref.getSample (c, i); s += d * d; }
    return db (std::sqrt (s / (2.0 * n)) / juce::jmax (1e-9, rms (ref)));
}

static void writeWav (const juce::AudioBuffer<float>& b, const juce::File& f)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), kHost, 2, 24, {}, 0));
    if (w == nullptr) return;
    os.release();
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    std::printf ("  wrote %s  peak %.2f dBFS  finite %s  %.2f s\n",
                 f.getFileName().toRawUTF8(), juce::Decibels::gainToDecibels (peak (b)),
                 finite (b) ? "yes" : "NO", b.getNumSamples() / kHost);
}

static std::vector<Ev> padSeq (int bars, double bpm, int root, int pads)
{
    const double beat = 60.0 / bpm;
    const int totalBeats = bars * 4;
    std::vector<Ev> evs;
    for (int i = 0; i < totalBeats; ++i)
    {
        const int s = (int) std::llround (i * beat * kHost);
        const int pad = i % juce::jmax (1, pads);
        evs.push_back ({ s, root + pad, 0.95f });
        evs.push_back ({ s + (int) (0.45 * beat * kHost), root + pad, 0.0f });
    }
    return evs;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3) { std::fprintf (stderr, "usage: XoxicoaSamplerDemos <outDir> <vocal.wav>\n"); return 1; }
    juce::File outDir (argv[1]); outDir.createDirectory();
    juce::File vocal (argv[2]);
    if (! vocal.existsAsFile()) { std::fprintf (stderr, "missing sample\n"); return 2; }

    const double bpm = 155.0;
    const int bMin = 1 + 12 + 11; // PROJECT KEY = B min
    const int root = 36;
    const int bars = 4;
    const int total = (int) std::llround (bars * 4 * 60.0 / bpm * kHost);

    std::printf ("Loading %s ...\n", vocal.getFullPathName().toRawUTF8());
    Rig rig (vocal, bpm);
    auto s = rig.proc.getSample();
    if (s == nullptr) { std::fprintf (stderr, "load failed: %s\n", rig.proc.getLastError().toRawUTF8()); return 3; }
    std::printf ("  sample BPM %.1f  key %s  peakRms %.4f  eligible %d  cues %d\n",
                 rig.proc.getSampleBpm(),
                 (xsmp::keyNames()[juce::jmax (0, s->analysis.keyRoot)] + (s->analysis.minor ? " min" : " maj")).toRawUTF8(),
                 s->analysis.peakRms, (int) s->analysis.eligible.size(), (int) s->analysis.cues.size());

    // File is labelled 86 BPM D maj; detection can land elsewhere on sparse vocals.
    rig.proc.setBpmOverride (86.0);
    rig.set ("key_target", (float) bMin);
    rig.set ("sync", 1.0f);
    rig.set ("chop_count", 16.0f);
    rig.set ("chop_thresh", 0.12f);
    rig.proc.generateChops();
    int live = 0;
    for (int i = 0; i < 16; ++i) if (rig.proc.getSliceEnd (i) > rig.proc.getCue (i)) ++live;
    std::printf ("  GENERATE 16 chops: %d live pads, host BPM %.1f, snap %d st\n", live, rig.proc.getHostBpm(), rig.proc.getSnapOffset());

    auto seq = padSeq (bars, bpm, root, juce::jmax (1, live));

    // 01: generated vocal chops, keyed to B min, synced to 155
    writeWav (rig.render (total, seq), outDir.getChildFile ("01_generated_chops_155bpm_Bmin.wav"));

    // 02: second generate = a different set
    rig.proc.generateChops();
    writeWav (rig.render (total, seq), outDir.getChildFile ("02_generated_chops_reroll_155bpm_Bmin.wav"));

    // 03: 8 longer chops
    rig.set ("chop_count", 8.0f);
    rig.proc.generateChops();
    writeWav (rig.render (total, padSeq (bars, bpm, root, 8)), outDir.getChildFile ("03_generated_8_long_chops_155bpm_Bmin.wav"));

    // reset to 16 for the rest
    rig.set ("chop_count", 16.0f);
    rig.proc.generateChops();
    auto seq16 = padSeq (bars, bpm, root, 16);

    // 04-07 vocal FX / ghost (one effect at a time, rest off)
    struct Demo { const char* file; const char* id; float value; };
    const Demo demos[] = {
        { "04_vocal_reverb_155bpm_Bmin.wav",   "vfx_rev_mix", 0.55f },
        { "05_vocal_delay_1_8D_155bpm_Bmin.wav","vfx_dly_mix", 0.45f },
        { "06_vocal_pitch_+12_155bpm_Bmin.wav", "vfx_pitch",  12.0f },
        { "07_vocal_formant_-7_155bpm_Bmin.wav","vfx_formant",-7.0f },
        { "08_vocal_drive_155bpm_Bmin.wav",     "vfx_drive",   0.65f },
        { "09_vocal_filter_HP_155bpm_Bmin.wav", "vfx_filter",  0.7f },
        { "10_vocal_stutter_155bpm_Bmin.wav",   "vfx_stutter", 0.75f },
        { "11_vocal_chorus_width_155bpm_Bmin.wav","vfx_chorus",0.7f },
        { "12_ghost_voice_155bpm_Bmin.wav",     "ghost_mix",  0.55f },
    };
    rig.set ("vfx_dly_div", 6.0f); // 1/8D
    rig.set ("vfx_dly_pp", 1.0f);
    rig.set ("vfx_width", 0.85f);
    rig.set ("ghost_amount", 0.8f);
    rig.set ("ghost_swell", 0.55f);
    rig.set ("ghost_slide", 0.6f);
    rig.set ("ghost_tone", 0.45f);
    for (auto& d : demos)
    {
        // reset FX
        for (auto id : { "vfx_rev_mix","vfx_dly_mix","vfx_pitch","vfx_formant","vfx_drive","vfx_filter","vfx_stutter","vfx_chorus","ghost_mix" })
            rig.set (id, (juce::String (id) == "vfx_pitch" || juce::String (id) == "vfx_formant" || juce::String (id) == "vfx_filter") ? 0.0f : 0.0f);
        rig.set ("vfx_rev_size", 0.7f);
        if (juce::String (d.file).contains ("reverb")) rig.set ("vfx_rev_size", 0.75f);
        if (juce::String (d.file).contains ("chorus")) rig.set ("vfx_width", 1.0f);
        rig.set (d.id, d.value);
        writeWav (rig.render (total, seq16), outDir.getChildFile (d.file));
        rig.set (d.id, (juce::String (d.id) == "vfx_pitch" || juce::String (d.id) == "vfx_formant" || juce::String (d.id) == "vfx_filter") ? 0.0f : 0.0f);
    }

    // 13: AUTO flip
    for (auto id : { "vfx_rev_mix","vfx_dly_mix","vfx_drive","vfx_stutter","vfx_chorus","ghost_mix" }) rig.set (id, 0.0f);
    rig.set ("vfx_width", 0.5f);
    rig.set ("auto_play", 1.0f);
    rig.set ("auto_density", 0.9f);
    rig.set ("auto_chaos", 0.55f);
    writeWav (rig.render (total, {}), outDir.getChildFile ("13_auto_flip_155bpm_Bmin.wav"));
    rig.set ("auto_play", 0.0f);

    // ---- knob audit for new params
    juce::File audit = outDir.getChildFile ("KNOB_AUDIT.txt");
    audit.deleteFile();
    juce::FileOutputStream os (audit);
    os << "Xoxicoa Sampler v1.2 knob audit (new parameters)\n";
    os << "Sequence: 4 bars of generated vocal chops at 155 BPM, PROJECT KEY B min, HOST SYNC on.\n";
    os << "levelD = RMS change vs min; diff = RMS(render - min render) re min render.\n";
    os << "FLAG when |level| < 1 dB AND diff < -20 dB.\n\n";
    os << juce::String::formatted ("  %-28s %8s %8s %8s %8s  %s\n", "param", "lvl mid", "lvl max", "diff mid", "diff max", "verdict");

    struct Row { const char* name; const char* id; float mn, mid, mx; };
    const Row rows[] = {
        { "Silence Threshold", "chop_thresh", 0.02f, 0.15f, 0.45f },
        { "Vocal Reverb Size", "vfx_rev_size", 0.0f, 0.5f, 1.0f },
        { "Vocal Reverb Mix",  "vfx_rev_mix",  0.0f, 0.5f, 1.0f },
        { "Vocal Delay Mix",   "vfx_dly_mix",  0.0f, 0.4f, 0.8f },
        { "Vocal Delay Feedback","vfx_dly_fb", 0.0f, 0.4f, 0.85f },
        { "Vocal Pitch",       "vfx_pitch",   -12.0f, 0.0f, 12.0f },
        { "Vocal Formant",     "vfx_formant", -12.0f, 0.0f, 12.0f },
        { "Vocal Drive",       "vfx_drive",    0.0f, 0.5f, 1.0f },
        { "Vocal Filter",      "vfx_filter",  -1.0f, 0.0f, 1.0f },
        { "Vocal Stutter",     "vfx_stutter",  0.0f, 0.5f, 1.0f },
        { "Vocal Chorus",      "vfx_chorus",   0.0f, 0.5f, 1.0f },
        { "Vocal Width",       "vfx_width",    0.0f, 0.5f, 1.0f },
        { "Ghost Amount",      "ghost_amount", 0.0f, 0.5f, 1.0f },
        { "Ghost Swell",       "ghost_swell",  0.0f, 0.5f, 1.0f },
        { "Ghost Slide",       "ghost_slide",  0.0f, 0.5f, 1.0f },
        { "Ghost Tone",        "ghost_tone",   0.0f, 0.5f, 1.0f },
        { "Ghost Mix",         "ghost_mix",    0.0f, 0.5f, 1.0f },
    };

    int flagged = 0, nRows = 0;
    auto resetFx = [&] {
        for (auto id : { "vfx_rev_mix","vfx_dly_mix","vfx_drive","vfx_stutter","vfx_chorus","ghost_mix" }) rig.set (id, 0.0f);
        rig.set ("vfx_pitch", 0.0f); rig.set ("vfx_formant", 0.0f); rig.set ("vfx_filter", 0.0f);
        rig.set ("vfx_width", 0.5f); rig.set ("vfx_rev_size", 0.55f); rig.set ("vfx_dly_fb", 0.35f);
        rig.set ("ghost_amount", 0.55f); rig.set ("ghost_swell", 0.45f); rig.set ("ghost_slide", 0.4f); rig.set ("ghost_tone", 0.55f);
        rig.set ("chop_thresh", 0.12f);
    };
    for (auto& r : rows)
    {
        resetFx();
        // For mix-style knobs, leave a related partner on so Size/Feedback/Swell etc. are audible
        if (juce::String (r.id) == "vfx_rev_size") rig.set ("vfx_rev_mix", 0.7f);
        if (juce::String (r.id) == "vfx_dly_fb")   rig.set ("vfx_dly_mix", 0.5f);
        if (juce::String (r.id).startsWith ("ghost_") && juce::String (r.id) != "ghost_mix")
            rig.set ("ghost_mix", 0.7f);
        if (juce::String (r.id) == "chop_thresh")
        {
            // rebuild chops at each value, then play
        }
        juce::AudioBuffer<float> a, b, c;
        auto go = [&] (float v) {
            rig.set (r.id, v);
            if (juce::String (r.id) == "chop_thresh") rig.proc.applyChopThreshold();
            return rig.render (total, seq16);
        };
        a = go (r.mn); b = go (r.mid); c = go (r.mx);
        const double lMid = db (rms (b) / juce::jmax (1e-12, rms (a)));
        const double lMax = db (rms (c) / juce::jmax (1e-12, rms (a)));
        const double dMid = diffDb (b, a), dMax = diffDb (c, a);
        const bool flag = std::abs (lMid) < 1.0 && std::abs (lMax) < 1.0 && dMid < -20.0 && dMax < -20.0;
        const char* verd = flag ? "FLAG" : "ok";
        if (flag) ++flagged;
        ++nRows;
        os << juce::String::formatted ("  %-28s %+8.1f %+8.1f %+8.1f %+8.1f  %s\n",
                                       r.name, lMid, lMax, dMid, dMax, verd);
        os << "                               (values: min " << r.mn << ", mid " << r.mid << ", max " << r.mx
           << "; peak " << juce::String (juce::Decibels::gainToDecibels (peak (c)), 2) << " dBFS)\n";
        std::printf ("  audit %s %s\n", r.name, verd);
        resetFx();
    }
    os << "\nflagged parameters: " << flagged << " of " << nRows << "\n";
    if (flagged == 0) os << "ALL NEW KNOBS AUDIBLY CHANGE THE SOUND\n";
    std::printf ("knob audit flagged %d / %d\n", flagged, nRows);
    return flagged == 0 ? 0 : 4;
}
