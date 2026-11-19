#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::uint32 freshSeed (juce::uint32 counter);

using namespace xsmp;

XoxicoaSamplerProcessor::XoxicoaSamplerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "XoxicoaSampler", createLayout())
{
    pMaster    = apvts.getRawParameterValue ("master");
    pKeyShift  = apvts.getRawParameterValue ("key_shift");
    pKeyTarget = apvts.getRawParameterValue ("key_target");
    pSampleKey = apvts.getRawParameterValue ("sample_key");
    pSync      = apvts.getRawParameterValue ("sync");
    pRoot      = apvts.getRawParameterValue ("pad_root");
    pChopCount = apvts.getRawParameterValue ("chop_count");
    pChopThresh= apvts.getRawParameterValue ("chop_thresh");
    pRevSize   = apvts.getRawParameterValue ("vfx_rev_size");
    pRevMix    = apvts.getRawParameterValue ("vfx_rev_mix");
    pDlyDiv    = apvts.getRawParameterValue ("vfx_dly_div");
    pDlyFb     = apvts.getRawParameterValue ("vfx_dly_fb");
    pDlyMix    = apvts.getRawParameterValue ("vfx_dly_mix");
    pDlyPP     = apvts.getRawParameterValue ("vfx_dly_pp");
    pVfxPitch  = apvts.getRawParameterValue ("vfx_pitch");
    pVfxFormant= apvts.getRawParameterValue ("vfx_formant");
    pDrive     = apvts.getRawParameterValue ("vfx_drive");
    pVfxFilter = apvts.getRawParameterValue ("vfx_filter");
    pStutter   = apvts.getRawParameterValue ("vfx_stutter");
    pChorus    = apvts.getRawParameterValue ("vfx_chorus");
    pWidth     = apvts.getRawParameterValue ("vfx_width");
    pGhostAmt  = apvts.getRawParameterValue ("ghost_amount");
    pGhostSwell= apvts.getRawParameterValue ("ghost_swell");
    pGhostSlide= apvts.getRawParameterValue ("ghost_slide");
    pGhostTone = apvts.getRawParameterValue ("ghost_tone");
    pGhostMix  = apvts.getRawParameterValue ("ghost_mix");
    pAutoSlot  = apvts.getRawParameterValue ("auto_slot");
    pAutoPlay  = apvts.getRawParameterValue ("auto_play");
    pChaos     = apvts.getRawParameterValue ("auto_chaos");
    pDensity   = apvts.getRawParameterValue ("auto_density");
    pSwing     = apvts.getRawParameterValue ("auto_swing");
    for (int v = 0; v < kNumAutoSlots; ++v) pAutoLen[(size_t) v] = apvts.getRawParameterValue (autoLenId (v));
    for (auto& c : cycleLog) c = 0;
    for (int v = 0; v < kNumAutoSlots; ++v) autoStrats[(size_t) v] = v;
    autoTrigs.reserve (16384);
    for (int p = 0; p < kNumPads; ++p)
    {
        auto& pp = padParams[(size_t) p];
        pp.mode    = apvts.getRawParameterValue (padId (p, "mode"));
        pp.attack  = apvts.getRawParameterValue (padId (p, "attack"));
        pp.release = apvts.getRawParameterValue (padId (p, "release"));
        pp.filter  = apvts.getRawParameterValue (padId (p, "filter"));
        pp.gain    = apvts.getRawParameterValue (padId (p, "gain"));
        pp.choke   = apvts.getRawParameterValue (padId (p, "choke"));
        cues[(size_t) p] = 0;
        sliceEnds[(size_t) p] = 0;
        padRate[(size_t) p] = 1.0f;
        padPlaying[(size_t) p] = false;
        padPos[(size_t) p] = 0.0;
    }
}

XoxicoaSamplerProcessor::~XoxicoaSamplerProcessor()
{
    loaderPool.removeAllJobs (true, 10000);
    cancelPendingUpdate();
}

bool XoxicoaSamplerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void XoxicoaSamplerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (16, samplesPerBlock);
    monoScratch.setSize (2, juce::jmax (32768, maxBlock));
    for (auto& v : voices) v.prepare (sr, maxBlock);
    masterGain.reset (sr, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pMaster->load(), -36.0f));
    vocalFx.prepare (sr, maxBlock);
    keyboardState.reset();
    autoRunning = autoArmed = autoMidiHeld = false;
    autoInternalPpq = 0.0;
}

int XoxicoaSamplerProcessor::getPadForNote (int note) const
{
    const int p = note - (int) pRoot->load();
    return (p >= 0 && p < kNumPads) ? p : -1;
}

PadSettings XoxicoaSamplerProcessor::readPad (int p) const
{
    const auto& pp = padParams[(size_t) p];
    PadSettings s;
    s.mode = (PadMode) juce::jlimit (0, 2, (int) pp.mode->load());
    s.attackMs = pp.attack->load(); s.releaseMs = pp.release->load();
    s.filter = pp.filter->load(); s.gainDb = pp.gain->load();
    s.choke = (int) pp.choke->load();
    return s;
}

static Voice* allocVoice (std::array<Voice, XoxicoaSamplerProcessor::kVoices>& voices)
{
    Voice* target = nullptr;
    for (auto& v : voices) if (! v.isActive()) { target = &v; break; }
    if (target == nullptr) // steal: oldest releasing voice, else oldest
    {
        for (auto& v : voices) if (v.isReleasing() && (target == nullptr || v.getAge() < target->getAge())) target = &v;
        if (target == nullptr) for (auto& v : voices) if (target == nullptr || v.getAge() < target->getAge()) target = &v;
    }
    return target;
}

void XoxicoaSamplerProcessor::noteOn (int pad, float vel, const SampleData& d, double semis, double tempoRatio)
{
    const auto ps = readPad (pad);
    for (auto& v : voices)
        if (v.isActive() && ! v.isReleasing() && ! v.isAuto())
            if (v.getPad() == pad || (ps.choke > 0 && readPad (v.getPad()).choke == ps.choke))
                v.choke();

    const juce::int64 start = cues[(size_t) pad].load();
    juce::int64 end = sliceEnds[(size_t) pad].load();
    if (end <= start)
    {
        end = d.length();
        for (auto& c : cues) { const auto q = c.load(); if (q > start && q < end) end = q; }
    }
    if (end - start < (juce::int64) (0.01 * d.fileRate)) return; // empty / silent pad
    const double rate = (double) padRate[(size_t) pad].load();
    allocVoice (voices)->start (d, pad, start, end, ps, vel, semis, tempoRatio * (rate > 0.05 ? rate : 1.0), ++voiceCounter);
}

void XoxicoaSamplerProcessor::noteOff (int pad)
{
    for (auto& v : voices)
        if (v.isActive() && ! v.isAuto() && v.getPad() == pad && v.getMode() != PadMode::OneShot && ! v.isReleasing())
            v.release();
}

// ---------------------------------------------------------------- AUTO playback
void XoxicoaSamplerProcessor::scheduleAuto (int n, double ppq0, double bpm, bool, double, int a, int b,
                                            const AutoFlip& f, std::vector<AutoTrig>& out)
{
    juce::ignoreUnused (n);
    if (f.num <= 0 || f.lenTicks <= 0 || b <= a) return;
    const double L = (double) f.lenTicks;
    const double ticksPerSample = bpm / 60.0 / sr * kTicksPerBeat;
    const double pa = (ppq0 + a * bpm / 60.0 / sr - autoAnchor) * kTicksPerBeat;
    const double pb = pa + (b - a) * ticksPerSample;
    const float swing = pSwing->load();
    auto add = [&] (double absTick, int ev)
    {
        if (absTick < pa - 1.0e-9 || absTick >= pb - 1.0e-9) return;
        const int smp = a + (int) std::ceil ((absTick - pa) / ticksPerSample - 1.0e-6);
        if (smp >= a && smp < b && out.size() < out.capacity()) out.push_back ({ smp, ev });
    };
    for (double k = std::floor (pa / L); k * L < pb; k += 1.0)
    {
        const double base = k * L;
        add (base, -2); // cycle start marker
        for (int i = 0; i < f.num; ++i)
        {
            const auto& e = f.ev[(size_t) i];
            const int m = e.tick % 4;
            const double sw = m == 2 ? swing * 1.0 : (m == 3 ? swing * 0.5 : 0.0); // odd 1/16s pushed late (max 1/32)
            add (base + e.tick + sw, i);
        }
    }
    std::sort (out.begin(), out.end(), [] (const AutoTrig& x, const AutoTrig& y) { return x.sample < y.sample || (x.sample == y.sample && x.ev < y.ev); });
}

void XoxicoaSamplerProcessor::triggerAutoEvent (const AutoEvent& e, const SampleData& d, double bpm, double baseSemis, double autoRatio,
                                                float chaos, float density, const std::array<PadSettings, kNumPads>& settings)
{
    if (e.densRank > density + 1.0e-6f) return;               // Density: rest
    const bool fx = chaos > e.fxRank;
    if (fx && (e.flags & kGap)) return;                        // Chaos: gap
    const bool alt = chaos > e.altRank;
    const int pad = juce::jlimit (0, kNumPads - 1, (int) (alt ? e.altPad : e.pad));
    const int off = alt ? e.altOff : e.off;
    const bool rev = fx && (e.flags & kRev);
    const int es = fx ? e.semis : 0;
    const double rate = (fx && (e.flags & kHalf)) ? 0.5 : 1.0;
    const double sBpm = getSampleBpm() > 0 ? getSampleBpm() : 120.0;
    const double srcTick = 60.0 / sBpm / kTicksPerBeat * d.fileRate;
    const double lenSamples = e.len * 60.0 / bpm / kTicksPerBeat * sr;
    const juce::int64 cue0 = cues[(size_t) pad].load();
    const juce::int64 cue1 = sliceEnds[(size_t) pad].load() > cue0 ? sliceEnds[(size_t) pad].load() : d.length();
    juce::int64 start = cue0 + (juce::int64) (off * srcTick);
    if (start >= cue1) start = cue0; // don't start in the trimmed-off silence
    start = juce::jlimit (cue0, juce::jmax (cue0, cue1 - 1), start);
    // source distance covered by the event: stretched voices move at autoRatio*rate, pitch accents are varispeed chops
    const double travel = es != 0 ? std::pow (2.0, (baseSemis + es) / 12.0) : autoRatio * rate;
    if (rev) start = juce::jmin (cue1, start + (juce::int64) (lenSamples / sr * d.fileRate * travel));
    for (auto& v : voices) if (v.isActive() && v.isAuto() && ! v.isReleasing()) v.choke();   // the Auto line is monophonic
    allocVoice (voices)->start (d, pad, start, cue1, settings[(size_t) pad], e.gain, baseSemis + es, autoRatio * rate,
                                ++voiceCounter, rev, (juce::int64) std::round (lenSamples), es, rate);
}

std::vector<juce::int64> XoxicoaSamplerProcessor::getAutoCycleStarts() const
{
    std::vector<juce::int64> r;
    const int c = cycleLogCount.load();
    for (int i = juce::jmax (0, c - 64); i < c; ++i) r.push_back (cycleLog[(size_t) (i % 64)].load());
    return r;
}

void XoxicoaSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    // Project BPM is read from the host playhead on EVERY block (never cached in prepareToPlay,
    // never defaulted to 120 here). 0 means the host did not report a tempo this block.
    double bpm = 0.0, ppq = 0.0, barLenQ = 4.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            if (auto ts = pos->getTimeSignature()) barLenQ = juce::jmax (0.25, ts->numerator * 4.0 / juce::jmax (1, ts->denominator));
            hostPlaying = pos->getIsPlaying() && pos->getPpqPosition().hasValue();
        }
    hostBpm = bpm;
    struct BlockEnd { juce::int64& g; int n; ~BlockEnd() { g += n; } } blockEnd { globalSample, n };

    const juce::SpinLock::ScopedTryLockType lock (audioSwapLock);
    const SampleData* d = lock.isLocked() ? audioSample.get() : nullptr;
    const auto gen = sampleGeneration.load();
    if (gen != audioGeneration) { for (auto& v : voices) v.kill(); audioGeneration = gen; }
    if (d == nullptr || d->length() <= 0)
    {
        for (auto& p : padPlaying) p = false;
        autoRunning = false; autoRunningNow = false; autoPosNow = -1.0;
        return;
    }

    const int snap = keySnapOffset (d->analysis.keyRoot, d->analysis.minor, (int) pKeyTarget->load(),
                                    pSampleKey != nullptr ? (int) pSampleKey->load() : 0);
    const double semis = juce::jlimit (-24.0, 24.0, (double) pKeyShift->load() + snap);
    const double sBpm = getSampleBpm();
    const double ratio = (pSync->load() > 0.5f && bpm > 0.0 && sBpm > 0.0) ? juce::jlimit (0.5, 2.0, bpm / sBpm) : 1.0;
    snapNow = snap; semisNow = semis; tempoRatioNow = ratio;
    // AUTO always conforms: host tempo (120 if the host gives none) and the Key Target as project key
    const double autoBpm = bpm > 0.0 ? bpm : (sBpm > 0.0 ? sBpm : 120.0);
    const double autoRatio = sBpm > 0.0 ? juce::jlimit (0.25, 4.0, autoBpm / sBpm) : 1.0;

    const bool monoOut = buffer.getNumChannels() < 2;
    juce::AudioBuffer<float> stereo (monoScratch.getArrayOfWritePointers(), 2, juce::jmin (n, monoScratch.getNumSamples()));
    if (monoOut) stereo.clear();
    auto& target = monoOut ? stereo : buffer;

    std::array<PadSettings, kNumPads> settings;
    for (int p = 0; p < kNumPads; ++p) settings[(size_t) p] = readPad (p);

    // ---- AUTO transport: MIDI C0 (hold), C#0..F#0 (version select), AUTO PLAY latch (bar-synced)
    const bool autoNotesFree = (int) pRoot->load() > kAutoSlotNote + kNumAutoSlots - 1;
    auto isAutoNote = [&] (int nn) { return autoNotesFree && nn >= kAutoTriggerNote && nn < kAutoSlotNote + kNumAutoSlots; };
    int slot = juce::jlimit (0, kNumAutoSlots - 1, (int) pAutoSlot->load());
    int midiStart = -1, midiStop = -1;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! (m.isNoteOnOrOff() && isAutoNote (m.getNoteNumber()))) continue;
        const int t = juce::jlimit (0, n - 1, meta.samplePosition);
        if (m.getNoteNumber() == kAutoTriggerNote)
        {
            if (m.isNoteOn()) { autoMidiHeld = true; if (midiStart < 0) midiStart = t; midiStop = -1; }
            else { autoMidiHeld = false; midiStop = t; }
        }
        else if (m.isNoteOn())
        {
            slot = m.getNoteNumber() - kAutoSlotNote;
            midiSlotRequest = slot;
            triggerAsyncUpdate();
        }
    }
    const bool latch = pAutoPlay->load() > 0.5f;
    const double qPerSample = autoBpm / 60.0 / sr;
    int runFrom = -1, runTo = n;
    if (autoRunning && autoOnHost != hostPlaying) // transport started/stopped under us: keep the bar grid
    {
        if (hostPlaying) autoAnchor = std::floor (ppq / barLenQ) * barLenQ;
        else { autoInternalPpq = 0.0; autoAnchor = 0.0; }
        autoOnHost = hostPlaying;
    }
    if (autoRunning) runFrom = 0;
    else if (midiStart >= 0)
    {
        runFrom = midiStart; autoOnHost = hostPlaying;
        if (! hostPlaying) autoInternalPpq = -midiStart * qPerSample; // internal clock = 0 at the note
        autoAnchor = (hostPlaying ? ppq : autoInternalPpq) + midiStart * qPerSample;
    }
    else if (latch)
    {
        if (hostPlaying)
        {
            const double nextBar = std::ceil (ppq / barLenQ - 1.0e-9) * barLenQ;
            const double at = (nextBar - ppq) / qPerSample;
            if (at < n) { runFrom = juce::jmax (0, (int) std::ceil (at - 1.0e-6)); autoAnchor = nextBar; autoOnHost = true; }
            else autoArmed = true;
        }
        else { runFrom = 0; autoInternalPpq = 0.0; autoAnchor = 0.0; autoOnHost = false; }
    }
    if (runFrom >= 0) { autoArmed = false; if (! latch && ! autoMidiHeld) runTo = midiStop >= 0 ? midiStop : runFrom; }
    if (! latch && ! autoMidiHeld && midiStop < 0 && autoRunning && midiStart < 0) runTo = 0;
    if (! latch) autoArmed = false;

    autoTrigs.clear();
    bool stopping = false;
    if (runFrom >= 0)
    {
        const juce::SpinLock::ScopedTryLockType fl (autoFlipLock);
        if (fl.isLocked())
        {
            const int lenIdx = juce::jlimit (0, kNumAutoLens - 1, (int) pAutoLen[(size_t) slot]->load());
            scheduleAuto (n, hostPlaying ? ppq : autoInternalPpq, autoBpm, hostPlaying, barLenQ, runFrom, juce::jmax (runFrom, runTo),
                          (*audioFlips)[(size_t) slot][(size_t) lenIdx], autoTrigs);
        }
        stopping = runTo < n;
        autoRunning = ! stopping;
    }
    if (stopping && autoTrigs.size() < autoTrigs.capacity()) autoTrigs.push_back ({ juce::jmax (0, runTo), -1 });

    const float chaos = pChaos->load(), density = pDensity->load();
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        end = juce::jmin (end, target.getNumSamples());
        while (pos < end) // chunked: voices are sized for maxBlock
        {
            const int len = juce::jmin (maxBlock, end - pos);
            for (auto& v : voices)
                if (v.isActive())
                    v.render (*d, target, pos, len, settings[(size_t) v.getPad()],
                              v.isAuto() ? semis + v.getAutoSemis() : semis,
                              v.isAuto() ? autoRatio * v.getAutoRate() : ratio);
            pos += len;
        }
    };
    size_t ti = 0;
    auto flushAuto = [&] (int upTo)
    {
        while (ti < autoTrigs.size() && autoTrigs[ti].sample < upTo)
        {
            const auto tr = autoTrigs[ti++];
            renderTo (tr.sample);
            if (tr.ev == -1) { for (auto& v : voices) if (v.isActive() && v.isAuto()) v.choke(); }
            else if (tr.ev == -2) { const int c = cycleLogCount.load(); cycleLog[(size_t) (c % 64)] = globalSample + tr.sample; cycleLogCount = c + 1; }
            else
            {
                const juce::SpinLock::ScopedTryLockType fl (autoFlipLock);
                if (fl.isLocked())
                {
                    const int lenIdx = juce::jlimit (0, kNumAutoLens - 1, (int) pAutoLen[(size_t) slot]->load());
                    const auto& f = (*audioFlips)[(size_t) slot][(size_t) lenIdx];
                    if (tr.ev < f.num) triggerAutoEvent (f.ev[(size_t) tr.ev], *d, autoBpm, semis, autoRatio, chaos, density, settings);
                }
            }
        }
    };
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int t = juce::jlimit (0, n, meta.samplePosition);
        flushAuto (t);
        renderTo (t);
        if (m.isNoteOnOrOff() && isAutoNote (m.getNoteNumber())) continue;
        if (m.isNoteOn())
        {
            const int pad = getPadForNote (m.getNoteNumber());
            if (pad >= 0) noteOn (pad, m.getFloatVelocity(), *d, semis, ratio);
        }
        else if (m.isNoteOff())
        {
            const int pad = getPadForNote (m.getNoteNumber());
            if (pad >= 0) noteOff (pad);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& v : voices) if (v.isActive()) v.choke();
        }
    }
    flushAuto (n + 1);
    renderTo (n);
    if (! hostPlaying) autoInternalPpq += n * qPerSample;
    if (autoRunning)
    {
        const int lenIdx = juce::jlimit (0, kNumAutoLens - 1, (int) pAutoLen[(size_t) slot]->load());
        const double L = autoLenTicks (lenIdx);
        const double p = ((hostPlaying ? ppq + n * qPerSample : autoInternalPpq) - autoAnchor) * kTicksPerBeat;
        autoPosNow = p - std::floor (p / L) * L;
    }
    else autoPosNow = -1.0;
    autoRunningNow = autoRunning; autoArmedNow = autoArmed;

    // Vocal FX + Ghost Voice run on the summed stereo mix, before volume and the safety clip.
    {
        float* L = target.getWritePointer (0);
        float* R = target.getNumChannels() > 1 ? target.getWritePointer (1) : L;
        vocalFx.process (L, R, n, readVocalFx (autoBpm, hostPlaying ? ppq : autoInternalPpq, hostPlaying || autoRunning));
    }
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (pMaster->load(), -36.0f));
    if (monoOut)
    {
        for (int i = 0; i < n; ++i)
            buffer.setSample (0, i, 0.5f * (stereo.getSample (0, i) + stereo.getSample (1, i)));
    }
    masterGain.applyGain (buffer, n);
    // safety: NaN guard + soft clip that is bit-transparent below -0.9 dBFS (0.9 linear) and never exceeds 1.0
    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        auto* x = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            float v = std::isfinite (x[i]) ? x[i] : 0.0f;
            const float a = std::abs (v);
            if (a > 0.9f) v = std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f), v);
            x[i] = v;
        }
    }

    std::array<bool, kNumPads> playing {};
    for (auto& v : voices)
        if (v.isActive()) { playing[(size_t) v.getPad()] = true; padPos[(size_t) v.getPad()] = v.getPosition(); }
    for (int p = 0; p < kNumPads; ++p) padPlaying[(size_t) p] = playing[(size_t) p];
}

// ---------------------------------------------------------------- loading
bool XoxicoaSamplerProcessor::isSupportedFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;mp3;flac;ogg;m4a;caf");
}

std::shared_ptr<SampleData> XoxicoaSamplerProcessor::decodeAndAnalyse (const juce::File& f, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr) { error = "Can't read " + f.getFileName() + " (unsupported or damaged file)"; return {}; }
    const double fs = reader->sampleRate;
    const juce::int64 maxLen = (juce::int64) (fs * 60.0 * 10.0); // 10 minutes
    const auto len = juce::jmin (reader->lengthInSamples, maxLen);
    if (len <= 0 || fs <= 0) { error = f.getFileName() + " is empty"; return {}; }
    auto d = std::make_shared<SampleData>();
    d->audio.setSize (2, (int) len);
    reader->read (&d->audio, 0, (int) len, 0, true, true);
    if (reader->numChannels == 1) d->audio.copyFrom (1, 0, d->audio, 0, 0, (int) len);
    for (int c = 0; c < 2; ++c) // sanitise
    {
        auto* x = d->audio.getWritePointer (c);
        for (int i = 0; i < (int) len; ++i) if (! std::isfinite (x[i])) x[i] = 0.0f;
    }
    d->fileRate = fs;
    d->path = f.getFullPathName();
    d->name = f.getFileNameWithoutExtension();
    d->analysis = analyse (d->audio, fs, kNumPads);
    d->buildOverview();
    if (reader->lengthInSamples > maxLen) error = "Trimmed to the first 10 minutes";
    return d;
}

void XoxicoaSamplerProcessor::loadFileAsync (const juce::File& f, std::vector<juce::int64> restoreCues)
{
    loading = true;
    loaderPool.addJob ([this, f, restoreCues]
    {
        juce::String err;
        auto d = decodeAndAnalyse (f, err);
        {
            const juce::ScopedLock sl (pendingLock);
            pending.push_back ({ d, restoreCues, err });
        }
        triggerAsyncUpdate();
    });
}

bool XoxicoaSamplerProcessor::loadFileSync (const juce::File& f, std::vector<juce::int64> restoreCues)
{
    juce::String err;
    auto d = decodeAndAnalyse (f, err);
    { const juce::ScopedLock sl (dataLock); lastError = err; }
    if (d == nullptr) return false;
    install (d, restoreCues);
    return true;
}

void XoxicoaSamplerProcessor::handleAsyncUpdate()
{
    std::vector<Pending> todo;
    { const juce::ScopedLock sl (pendingLock); todo.swap (pending); }
    for (auto& p : todo)
    {
        { const juce::ScopedLock sl (dataLock); lastError = p.error; }
        if (p.data != nullptr) install (p.data, p.cues);
    }
    loading = loaderPool.getNumJobs() > 0;
    const int req = midiSlotRequest.exchange (-1);
    if (req >= 0)
        if (auto* p = apvts.getParameter ("auto_slot")) p->setValueNotifyingHost (p->convertTo0to1 ((float) req));
    ++changeCounter;
}

void XoxicoaSamplerProcessor::install (std::shared_ptr<SampleData> d, std::vector<juce::int64> restoreCues,
                                       std::vector<juce::int64> restoreEnds, std::vector<float> restoreRates)
{
    if (restoreEnds.empty()) restoreEnds.swap (pendingRestoreEnds);
    if (restoreRates.empty()) restoreRates.swap (pendingRestoreRates);
    pendingRestoreEnds.clear(); pendingRestoreRates.clear();
    std::shared_ptr<SampleData> old;
    {
        const juce::SpinLock::ScopedLockType l (audioSwapLock);
        old = audioSample;
        audioSample = d;
        ++sampleGeneration;
        detectedCues = d->analysis.cues;
        detectedEnds = d->analysis.cueEnds;
        detectedRates.assign (kNumPads, 1.0f);
        const bool restore = (int) restoreCues.size() == kNumPads;
        for (int i = 0; i < kNumPads; ++i)
        {
            const juce::int64 st = xsmp::clampI (0, std::max<juce::int64> (0, d->length() - 1),
                                                 restore ? restoreCues[(size_t) i]
                                                         : (i < (int) detectedCues.size() ? detectedCues[(size_t) i] : 0));
            cues[(size_t) i] = st;
            juce::int64 en = d->length();
            if (restore && (int) restoreEnds.size() == kNumPads) en = restoreEnds[(size_t) i];
            else if (i < (int) detectedEnds.size()) en = detectedEnds[(size_t) i];
            sliceEnds[(size_t) i] = xsmp::clampI (st, d->length(), en);
            padRate[(size_t) i] = (restore && (int) restoreRates.size() == kNumPads) ? restoreRates[(size_t) i] : 1.0f;
        }
        detectedBpm = d->analysis.bpm;
        if (! restore) { bpmOverride = 0.0; lastChopWasGenerate = false; lastChopSeed = 0; }
    }
    { const juce::ScopedLock sl (dataLock); sample = d; }
    pendingRestorePath = {};
    ++changeCounter;
    old.reset(); // freed here, on the message thread
    bool any = false;
    for (auto sd : autoSeeds) any |= sd != 0;
    if (any) regenerateAuto(); else autoRollAll();   // premade versions A-F for every new sample
}

void XoxicoaSamplerProcessor::setCue (int i, juce::int64 pos)
{
    auto s = getSample();
    if (s == nullptr) return;
    cues[(size_t) i] = xsmp::clampI (0, std::max<juce::int64> (0, s->length() - 1), pos);
    refreshSliceEnds (false);
    regenerateAuto();
    ++changeCounter;
}

void XoxicoaSamplerProcessor::resetCuesToDetected()
{
    for (int i = 0; i < kNumPads && i < (int) detectedCues.size(); ++i)
    {
        cues[(size_t) i] = detectedCues[(size_t) i];
        sliceEnds[(size_t) i] = i < (int) detectedEnds.size() ? detectedEnds[(size_t) i] : (i + 1 < (int) detectedCues.size() ? detectedCues[(size_t) (i + 1)] : 0);
        padRate[(size_t) i] = 1.0f;
    }
    lastChopWasGenerate = false;
    refreshSliceEnds (true);
    regenerateAuto();
    ++changeCounter;
}

int XoxicoaSamplerProcessor::getChopCount() const
{
    return juce::jlimit (1, kNumPads, pChopCount != nullptr ? (int) pChopCount->load() : 16);
}

void XoxicoaSamplerProcessor::applyChopSet (const std::vector<ChopSlice>& set, bool asDetected)
{
    auto s = getSample();
    if (s == nullptr) return;
    const auto n = s->length();
    for (int i = 0; i < kNumPads; ++i)
    {
        if (i < (int) set.size())
        {
            cues[(size_t) i] = xsmp::clampI (0, std::max<juce::int64> (0, n - 1), set[(size_t) i].start);
            sliceEnds[(size_t) i] = xsmp::clampI (cues[(size_t) i].load(), n, set[(size_t) i].end);
            padRate[(size_t) i] = set[(size_t) i].rate > 0.05f ? set[(size_t) i].rate : 1.0f;
        }
        else
        {
            cues[(size_t) i] = 0;
            sliceEnds[(size_t) i] = 0;
            padRate[(size_t) i] = 1.0f;
        }
    }
    if (asDetected)
    {
        detectedCues.clear(); detectedEnds.clear(); detectedRates.clear();
        for (int i = 0; i < kNumPads; ++i)
        {
            detectedCues.push_back (cues[(size_t) i].load());
            detectedEnds.push_back (sliceEnds[(size_t) i].load());
            detectedRates.push_back (padRate[(size_t) i].load());
        }
    }
    regenerateAuto();
    ++changeCounter;
}

void XoxicoaSamplerProcessor::generateChops()
{
    auto s = getSample();
    if (s == nullptr) return;
    lastChopSeed = freshSeed (++autoRollCounter);
    lastChopWasGenerate = true;
    ChopBuildOpts o;
    o.count = getChopCount();
    o.threshold = pChopThresh != nullptr ? pChopThresh->load() : 0.10f;
    o.seed = lastChopSeed;
    o.gridBpm = getHostBpm() > 0.0 ? getHostBpm() : getSampleBpm();
    o.varyLengths = true;
    o.tightGrid = true;
    applyChopSet (buildChops (s->audio, s->fileRate, s->analysis, o), false);
}

void XoxicoaSamplerProcessor::applyChopThreshold()
{
    auto s = getSample();
    if (s == nullptr) return;
    ChopBuildOpts o;
    o.count = lastChopWasGenerate ? getChopCount() : kNumPads;
    o.threshold = pChopThresh != nullptr ? pChopThresh->load() : 0.10f;
    o.seed = lastChopWasGenerate ? lastChopSeed : 0;
    o.gridBpm = lastChopWasGenerate && getHostBpm() > 0.0 ? getHostBpm() : getSampleBpm();
    o.varyLengths = lastChopWasGenerate;
    o.tightGrid = lastChopWasGenerate;
    applyChopSet (buildChops (s->audio, s->fileRate, s->analysis, o), ! lastChopWasGenerate);
}

void XoxicoaSamplerProcessor::refreshSliceEnds (bool trim)
{
    auto s = getSample();
    if (s == nullptr) return;
    const auto n = s->length();
    const float gate = chops::threshAbs (s->analysis.peakRms, pChopThresh != nullptr ? pChopThresh->load() : 0.10f);
    std::array<juce::int64, kNumPads> starts {};
    for (int i = 0; i < kNumPads; ++i) starts[(size_t) i] = cues[(size_t) i].load();
    for (int i = 0; i < kNumPads; ++i)
    {
        auto a = starts[(size_t) i];
        auto b = n;
        for (int j = 0; j < kNumPads; ++j)
            if (starts[(size_t) j] > a && starts[(size_t) j] < b) b = starts[(size_t) j];
        if (trim)
        {
            chops::trimSilence (s->audio, a, b, gate, s->fileRate, 40.0f);
            a = chops::snapZero (s->audio, a, s->fileRate, 3.0);
            cues[(size_t) i] = a;
        }
        sliceEnds[(size_t) i] = juce::jmax (a, b);
    }
}

VocalFxParams XoxicoaSamplerProcessor::readVocalFx (double bpm, double ppq, bool playing) const
{
    VocalFxParams p;
    p.revSize = pRevSize->load(); p.revMix = pRevMix->load();
    p.dlyDiv = (int) pDlyDiv->load(); p.dlyFb = pDlyFb->load(); p.dlyMix = pDlyMix->load(); p.dlyPP = pDlyPP->load() > 0.5f;
    p.pitch = pVfxPitch->load(); p.formant = pVfxFormant->load();
    p.drive = pDrive->load(); p.filter = pVfxFilter->load();
    p.stutter = pStutter->load(); p.chorus = pChorus->load(); p.width = pWidth->load();
    p.ghostAmt = pGhostAmt->load(); p.ghostSwell = pGhostSwell->load();
    p.ghostSlide = pGhostSlide->load(); p.ghostTone = pGhostTone->load(); p.ghostMix = pGhostMix->load();
    p.bpm = bpm; p.ppq = ppq; p.playing = playing;
    return p;
}

// ---------------------------------------------------------------- state
void XoxicoaSamplerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    auto s = getSample();
    state.setProperty ("samplePath", s != nullptr ? s->path : pendingRestorePath, nullptr);
    juce::StringArray c, e, rt;
    for (auto& q : cues) c.add (juce::String (q.load()));
    for (auto& q : sliceEnds) e.add (juce::String (q.load()));
    for (auto& q : padRate) rt.add (juce::String (q.load(), 4));
    state.setProperty ("cues", c.joinIntoString (","), nullptr);
    state.setProperty ("cueEnds", e.joinIntoString (","), nullptr);
    state.setProperty ("padRates", rt.joinIntoString (","), nullptr);
    state.setProperty ("bpmOverride", bpmOverride.load(), nullptr);
    state.setProperty ("editorScale", editorScale.load(), nullptr);
    juce::StringArray sd, st, lk, hist;
    for (int v = 0; v < kNumAutoSlots; ++v) { sd.add (juce::String ((juce::int64) autoSeeds[(size_t) v])); st.add (juce::String (autoStrats[(size_t) v])); lk.add (autoLocked[(size_t) v] ? "1" : "0"); }
    state.setProperty ("autoSeeds", sd.joinIntoString (","), nullptr);
    state.setProperty ("autoStrats", st.joinIntoString (","), nullptr);
    state.setProperty ("autoLocks", lk.joinIntoString (","), nullptr);
    for (size_t h = (size_t) juce::jmax (0, (int) autoHistory.size() - 24); h < autoHistory.size(); ++h)
    {
        juce::StringArray e;
        for (int v = 0; v < kNumAutoSlots; ++v) e.add (juce::String ((juce::int64) autoHistory[h].seeds[(size_t) v]) + ":" + juce::String (autoHistory[h].strats[(size_t) v]));
        hist.add (e.joinIntoString (","));
    }
    state.setProperty ("autoHistory", hist.joinIntoString (";"), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void XoxicoaSamplerProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    const juce::String path = tree.getProperty ("samplePath").toString();
    juce::StringArray c; c.addTokens (tree.getProperty ("cues").toString(), ",", "");
    juce::StringArray ce; ce.addTokens (tree.getProperty ("cueEnds").toString(), ",", "");
    juce::StringArray rt; rt.addTokens (tree.getProperty ("padRates").toString(), ",", "");
    const double bo = (double) tree.getProperty ("bpmOverride", 0.0);
    editorScale = (float) juce::jlimit (0.70, 1.50, (double) tree.getProperty ("editorScale", 1.0));
    apvts.replaceState (tree);
    {
        juce::StringArray sd, st, lk, hist;
        sd.addTokens (tree.getProperty ("autoSeeds").toString(), ",", ""); st.addTokens (tree.getProperty ("autoStrats").toString(), ",", "");
        lk.addTokens (tree.getProperty ("autoLocks").toString(), ",", ""); hist.addTokens (tree.getProperty ("autoHistory").toString(), ";", "");
        if (sd.size() == kNumAutoSlots && st.size() == kNumAutoSlots)
            for (int v = 0; v < kNumAutoSlots; ++v)
            {
                autoSeeds[(size_t) v] = (juce::uint32) sd[v].getLargeIntValue();
                autoStrats[(size_t) v] = juce::jlimit (0, kNumStrategies - 1, st[v].getIntValue());
                autoLocked[(size_t) v] = lk.size() == kNumAutoSlots && lk[v] == "1";
            }
        autoHistory.clear();
        for (auto& h : hist)
        {
            juce::StringArray e; e.addTokens (h, ",", "");
            if (e.size() != kNumAutoSlots) continue;
            AutoHistoryEntry he;
            for (int v = 0; v < kNumAutoSlots; ++v)
            {
                he.seeds[(size_t) v] = (juce::uint32) e[v].upToFirstOccurrenceOf (":", false, false).getLargeIntValue();
                he.strats[(size_t) v] = e[v].fromFirstOccurrenceOf (":", false, false).getIntValue();
            }
            he.label = "Roll " + juce::String ((int) autoHistory.size() + 1);
            autoHistory.push_back (he);
        }
        autoHistPos = (int) autoHistory.size() - 1;
        regenerateAuto();
    }
        std::vector<juce::int64> restore, restoreEnds;
    std::vector<float> restoreRates;
    if (c.size() == kNumPads) for (auto& tok : c) restore.push_back (tok.getLargeIntValue());
    if (ce.size() == kNumPads) for (auto& tok : ce) restoreEnds.push_back (tok.getLargeIntValue());
    if (rt.size() == kNumPads) for (auto& tok : rt) restoreRates.push_back (tok.getFloatValue());
    if (path.isNotEmpty())
    {
        const juce::File f (path);
        pendingRestorePath = path;
        if (f.existsAsFile())
        {
            pendingRestoreEnds = restoreEnds;
            pendingRestoreRates = restoreRates;
            if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
                && juce::MessageManager::getInstance()->isThisTheMessageThread())
                loadFileAsync (f, restore);
            else
                loadFileSync (f, restore);
            bpmOverride = bo;
        }
        else
        {
            const juce::ScopedLock sl (dataLock);
            lastError = "Missing sample: " + path + " - drop the file again";
        }
    }
}

// ---------------------------------------------------------------- AUTO (message thread)
int XoxicoaSamplerProcessor::getAutoLenIdx (int slot) const { return juce::jlimit (0, kNumAutoLens - 1, (int) pAutoLen[(size_t) slot]->load()); }

void XoxicoaSamplerProcessor::regenerateAuto()
{
    auto s = getSample();
    AutoContext ctx;
    ctx.data = s.get();
    for (int i = 0; i < kNumPads; ++i)
    {
        ctx.cues[(size_t) i] = cues[(size_t) i].load();
        ctx.cueEnds[(size_t) i] = sliceEnds[(size_t) i].load();
    }
    ctx.bpm = getSampleBpm();
    ctx.peakRms = s != nullptr ? s->analysis.peakRms : 0.0f;
    ctx.threshold = pChopThresh != nullptr ? pChopThresh->load() : 0.10f;
    for (int v = 0; v < kNumAutoSlots; ++v)
        for (int l = 0; l < kNumAutoLens; ++l)
            generateAutoFlip (ctx, autoSeeds[(size_t) v], autoStrats[(size_t) v], autoLenTicks (l), (*autoFlips)[(size_t) v][(size_t) l]);
    {
        const juce::SpinLock::ScopedLockType l (autoFlipLock);
        *audioFlips = *autoFlips;
    }
    ++changeCounter;
}

void XoxicoaSamplerProcessor::autoPushHistory (const juce::String& label)
{
    if (autoHistPos + 1 < (int) autoHistory.size()) autoHistory.resize ((size_t) (autoHistPos + 1)); // drop the redo branch
    AutoHistoryEntry e { autoSeeds, autoStrats, label };
    autoHistory.push_back (e);
    if (autoHistory.size() > 64) autoHistory.erase (autoHistory.begin());
    autoHistPos = (int) autoHistory.size() - 1;
}

void XoxicoaSamplerProcessor::autoApply (const AutoHistoryEntry& e)
{
    autoSeeds = e.seeds; autoStrats = e.strats;
    regenerateAuto();
}

static juce::uint32 freshSeed (juce::uint32 counter)
{
    auto x = (juce::uint64) juce::Time::getHighResolutionTicks() ^ ((juce::uint64) juce::Random::getSystemRandom().nextInt64()) ^ ((juce::uint64) counter * 0x9E3779B97F4A7C15ull);
    x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33;
    return (juce::uint32) (x | 1u);
}

void XoxicoaSamplerProcessor::autoRollAll()
{
    ++autoRollCounter;
    // distinct strategies across the six versions: shuffle, keep locked versions' strategies in place
    std::vector<int> free;
    for (int st = 0; st < kNumStrategies; ++st)
    {
        bool usedByLocked = false;
        for (int v = 0; v < kNumAutoSlots; ++v) usedByLocked |= autoLocked[(size_t) v] && autoStrats[(size_t) v] == st;
        if (! usedByLocked) free.push_back (st);
    }
    juce::Random r ((juce::int64) freshSeed (autoRollCounter));
    for (int i = (int) free.size() - 1; i > 0; --i) std::swap (free[(size_t) i], free[(size_t) r.nextInt (i + 1)]);
    std::array<int, kNumAutoSlots> prevStrat {};
    for (int v = 0; v < kNumAutoSlots; ++v) prevStrat[(size_t) v] = autoStrats[(size_t) v];
    std::vector<int> rolled;
    size_t k = 0;
    for (int v = 0; v < kNumAutoSlots; ++v)
    {
        if (autoLocked[(size_t) v] && autoSeeds[(size_t) v] != 0) continue;
        autoSeeds[(size_t) v] = freshSeed (autoRollCounter * 31u + (juce::uint32) v);
        autoStrats[(size_t) v] = k < free.size() ? free[k++] : r.nextInt (kNumStrategies);
        rolled.push_back (v);
    }
    // no version keeps the strategy it had on the previous roll (every press sounds new in every slot)
    for (int v : rolled)
    {
        if (autoStrats[(size_t) v] != prevStrat[(size_t) v]) continue;
        for (int w : rolled)
            if (w != v && autoStrats[(size_t) w] != prevStrat[(size_t) v] && autoStrats[(size_t) v] != prevStrat[(size_t) w])
            { std::swap (autoStrats[(size_t) v], autoStrats[(size_t) w]); break; }
        if (autoStrats[(size_t) v] == prevStrat[(size_t) v] && rolled.size() == 1)
            autoStrats[(size_t) v] = (prevStrat[(size_t) v] + 1 + r.nextInt (kNumStrategies - 1)) % kNumStrategies;
    }
    autoPushHistory ("Roll all");
    regenerateAuto();
}

void XoxicoaSamplerProcessor::autoRollSlot (int slot)
{
    if (slot < 0 || slot >= kNumAutoSlots || autoLocked[(size_t) slot]) return;
    ++autoRollCounter;
    juce::Random r ((juce::int64) freshSeed (autoRollCounter));
    autoSeeds[(size_t) slot] = freshSeed (autoRollCounter * 131u + (juce::uint32) slot);
    // new strategy, different from the version's previous one
    const int prev = autoStrats[(size_t) slot];
    autoStrats[(size_t) slot] = (prev + 1 + r.nextInt (kNumStrategies - 1)) % kNumStrategies;
    autoPushHistory ("Re-roll " + autoSlotChoices()[slot]);
    regenerateAuto();
}

bool XoxicoaSamplerProcessor::autoUndo()
{
    if (autoHistPos <= 0) return false;
    --autoHistPos;
    autoApply (autoHistory[(size_t) autoHistPos]);
    return true;
}

void XoxicoaSamplerProcessor::autoJumpToHistory (int index)
{
    if (index < 0 || index >= (int) autoHistory.size()) return;
    autoHistPos = index;
    autoApply (autoHistory[(size_t) index]);
}

juce::File XoxicoaSamplerProcessor::exportAutoMidi (int slot, double bpm) const
{
    const auto& f = getAutoFlip (slot, getAutoLenIdx (slot));
    const float chaos = pChaos->load(), density = pDensity->load(), swing = pSwing->load();
    juce::MidiMessageSequence seq;
    const int tpq = 960;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / juce::jmax (1.0, bpm))), 0);
    for (int i = 0; i < f.num; ++i)
    {
        const auto& e = f.ev[(size_t) i];
        if (e.densRank > density) continue;
        const bool fx = chaos > e.fxRank;
        if (fx && (e.flags & kGap)) continue;
        const int pad = chaos > e.altRank ? e.altPad : e.pad;
        const int m = e.tick % 4;
        const double t = (e.tick + (m == 2 ? swing : m == 3 ? swing * 0.5 : 0.0)) * tpq / (double) kTicksPerBeat;
        const double len = juce::jmax (1.0, e.len * tpq / (double) kTicksPerBeat - 4.0);
        seq.addEvent (juce::MidiMessage::noteOn (1, (int) pRoot->load() + pad, (juce::uint8) juce::jlimit (1, 127, (int) (e.gain * 110))), t);
        seq.addEvent (juce::MidiMessage::noteOff (1, (int) pRoot->load() + pad), t + len);
    }
    seq.updateMatchedPairs();
    juce::MidiFile mf; mf.setTicksPerQuarterNote (tpq); mf.addTrack (seq);
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("Xoxicoa Auto " + autoSlotChoices()[slot] + " " + autoLenChoices()[getAutoLenIdx (slot)].replace ("/", "-") + ".mid");
    file.deleteFile();
    juce::FileOutputStream os (file);
    if (os.openedOk()) mf.writeTo (os);
    return file;
}

namespace
{
    struct RenderHead : juce::AudioPlayHead
    {
        double bpm = 120.0, ppq = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (true);
            p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            return p;
        }
    };
}

bool XoxicoaSamplerProcessor::renderAutoVersion (int slot, int lenIdx, double bpm, int cycles, juce::AudioBuffer<float>& out, double fs) const
{
    auto s = getSample();
    if (s == nullptr) return false;
    XoxicoaSamplerProcessor r;
    RenderHead head; head.bpm = bpm;
    r.setPlayHead (&head);
    r.setPlayConfigDetails (0, 2, fs, 512);
    r.prepareToPlay (fs, 512);
    r.apvts.replaceState (const_cast<juce::AudioProcessorValueTreeState&> (apvts).copyState());
    r.autoSeeds = autoSeeds; r.autoStrats = autoStrats; r.autoLocked = autoLocked;
    std::vector<juce::int64> c, e; std::vector<float> rt;
    for (auto& q : cues) c.push_back (q.load());
    for (auto& q : sliceEnds) e.push_back (q.load());
    for (auto& q : padRate) rt.push_back (q.load());
    r.bpmOverride = bpmOverride.load();
    r.install (std::const_pointer_cast<SampleData> (s), c, e, rt);
    auto set = [&] (const juce::String& id, float v) { auto* p = r.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set ("auto_slot", (float) slot); set (autoLenId (slot), (float) lenIdx); set ("auto_play", 1.0f);
    const auto total = (int) std::llround (autoLenTicks (lenIdx) / (double) kTicksPerBeat * 60.0 / bpm * fs * cycles);
    out.setSize (2, total); out.clear();
    juce::AudioBuffer<float> blk (2, 512);
    juce::MidiBuffer mb;
    for (int pos = 0; pos < total; pos += 512)
    {
        const int n = juce::jmin (512, total - pos);
        blk.setSize (2, n, false, false, true);
        head.ppq = pos / fs * bpm / 60.0;
        r.processBlock (blk, mb);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, blk, ch, 0, n);
    }
    return true;
}

juce::AudioProcessorEditor* XoxicoaSamplerProcessor::createEditor() { return new XoxicoaSamplerEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new XoxicoaSamplerProcessor(); }
