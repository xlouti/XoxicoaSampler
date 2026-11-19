#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Params.h"
#include "dsp/Voice.h"
#include "dsp/VocalFx.h"

class XoxicoaSamplerProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    static constexpr int kVoices = 12;

    XoxicoaSamplerProcessor();
    ~XoxicoaSamplerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Xoxicoa Sampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- sample loading (message thread API; decoding + analysis run on a background thread)
    static bool isSupportedFile (const juce::File& f);
    void loadFileAsync (const juce::File& f, std::vector<juce::int64> restoreCues = {});
    bool loadFileSync (const juce::File& f, std::vector<juce::int64> restoreCues = {}); // tests / state restore fallback
    std::shared_ptr<const xsmp::SampleData> getSample() const { const juce::ScopedLock sl (dataLock); return sample; }
    bool isLoading() const { return loading.load(); }
    juce::String getLastError() const { const juce::ScopedLock sl (dataLock); return lastError; }

    // ---- cues (source-sample positions, editable from the UI)
    juce::int64 getCue (int i) const { return cues[(size_t) i].load(); }
    juce::int64 getSliceEnd (int i) const { return sliceEnds[(size_t) i].load(); }
    float getPadRate (int i) const { return padRate[(size_t) i].load(); }
    void setCue (int i, juce::int64 pos);
    void resetCuesToDetected();
    void generateChops();                 // new random set, count from chop_count
    void applyChopThreshold();            // rebuild / re-trim using the threshold knob
    int  getChopCount() const;

    // ---- BPM override (x2 / /2 / manual)
    double getSampleBpm() const { return bpmOverride.load() > 0 ? bpmOverride.load() : detectedBpm.load(); }
    void setBpmOverride (double b) { bpmOverride = b; regenerateAuto(); }

    // ---- live info for the UI
    int getPadForNote (int note) const;
    // Prefer a live playhead read so GENERATE / AUTO work before the first processBlock.
    double getHostBpm() const
    {
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm())
                    if (*b > 0.0) return *b;
        return hostBpm.load();
    }
    double getTempoRatio() const { return tempoRatioNow.load(); }
    double getTotalSemis() const { return semisNow.load(); }
    bool isPadPlaying (int p) const { return padPlaying[(size_t) p].load(); }
    double getPadPosition (int p) const { return padPos[(size_t) p].load(); }
    int getSnapOffset() const { return snapNow.load(); }
    static constexpr float kMinEditorScale = 0.70f, kMaxEditorScale = 1.50f;
    float getEditorScale() const { return editorScale.load(); }
    void setEditorScale (float s) { editorScale = juce::jlimit (kMinEditorScale, kMaxEditorScale, s); }

    // ---- AUTO (message thread API)
    struct AutoHistoryEntry { std::array<juce::uint32, xsmp::kNumAutoSlots> seeds {}; std::array<int, xsmp::kNumAutoSlots> strats {}; juce::String label; };
    void autoRollAll();                       // AUTO button: re-roll every unlocked version
    void autoRollSlot (int slot);             // per-version re-roll (ignored when locked)
    bool autoUndo();                          // back to the previous roll
    void autoJumpToHistory (int index);
    void autoRestore (const AutoHistoryEntry& e) { autoApply (e); }
    void setAutoLocked (int slot, bool l) { autoLocked[(size_t) slot] = l; ++changeCounter; }
    bool isAutoLocked (int slot) const { return autoLocked[(size_t) slot]; }
    const std::vector<AutoHistoryEntry>& getAutoHistory() const { return autoHistory; }
    int getAutoHistoryPos() const { return autoHistPos; }
    juce::uint32 getAutoSeed (int slot) const { return autoSeeds[(size_t) slot]; }
    int getAutoStrategy (int slot) const { return autoStrats[(size_t) slot]; }
    const xsmp::AutoFlip& getAutoFlip (int slot, int lenIdx) const { return (*autoFlips)[(size_t) slot][(size_t) lenIdx]; } // message thread
    int getAutoLenIdx (int slot) const;
    void regenerateAuto();
    double getAutoPosTicks() const { return autoPosNow.load(); }   // -1 when stopped
    bool isAutoRunning() const { return autoRunningNow.load(); }
    bool isAutoArmed() const { return autoArmedNow.load(); }
    juce::File exportAutoMidi (int slot, double bpm) const;
    bool renderAutoVersion (int slot, int lenIdx, double bpm, int cycles, juce::AudioBuffer<float>& out, double sampleRate = 48000.0) const;
    // test hook: global sample positions where the Auto pattern (re)started a cycle
    std::vector<juce::int64> getAutoCycleStarts() const;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState; // computer keyboard + on-screen pads feed this
    std::atomic<int> uiSelectedPad { 0 };
    std::atomic<juce::uint32> changeCounter { 0 }; // bumped when sample/cues change (UI repaint hint)

private:
    void handleAsyncUpdate() override;
    void autoPushHistory (const juce::String& label);
    void autoApply (const AutoHistoryEntry& e);
    struct AutoTrig { int sample; int ev; };
    void scheduleAuto (int n, double ppq0, double bpm, bool hostPlaying, double barLenQ, int startAt, int stopAt,
                       const xsmp::AutoFlip& f, std::vector<AutoTrig>& out);
    void triggerAutoEvent (const xsmp::AutoEvent& e, const xsmp::SampleData& d, double bpm, double baseSemis, double autoRatio,
                           float chaos, float density, const std::array<xsmp::PadSettings, xsmp::kNumPads>& settings);
    void install (std::shared_ptr<xsmp::SampleData> d, std::vector<juce::int64> restoreCues, std::vector<juce::int64> restoreEnds = {}, std::vector<float> restoreRates = {});
    void applyChopSet (const std::vector<xsmp::ChopSlice>& set, bool asDetected);
    void refreshSliceEnds (bool trim);
    xsmp::VocalFxParams readVocalFx (double bpm, double ppq, bool playing) const;
    static std::shared_ptr<xsmp::SampleData> decodeAndAnalyse (const juce::File& f, juce::String& error);
    xsmp::PadSettings readPad (int p) const;
    void noteOn (int pad, float vel, const xsmp::SampleData& d, double semis, double tempoRatio);
    void noteOff (int pad);

    struct PadParams { std::atomic<float>* mode; std::atomic<float>* attack; std::atomic<float>* release;
                       std::atomic<float>* filter; std::atomic<float>* gain; std::atomic<float>* choke; };
    std::array<PadParams, xsmp::kNumPads> padParams;
    std::atomic<float>* pMaster = nullptr; std::atomic<float>* pKeyShift = nullptr; std::atomic<float>* pKeyTarget = nullptr;
    std::atomic<float>* pSampleKey = nullptr;
    std::atomic<float>* pSync = nullptr;   std::atomic<float>* pRoot = nullptr;
    std::atomic<float>* pChopCount = nullptr; std::atomic<float>* pChopThresh = nullptr;
    std::atomic<float>* pRevSize = nullptr; std::atomic<float>* pRevMix = nullptr;
    std::atomic<float>* pDlyDiv = nullptr; std::atomic<float>* pDlyFb = nullptr;
    std::atomic<float>* pDlyMix = nullptr; std::atomic<float>* pDlyPP = nullptr;
    std::atomic<float>* pVfxPitch = nullptr; std::atomic<float>* pVfxFormant = nullptr;
    std::atomic<float>* pDrive = nullptr; std::atomic<float>* pVfxFilter = nullptr;
    std::atomic<float>* pStutter = nullptr; std::atomic<float>* pChorus = nullptr; std::atomic<float>* pWidth = nullptr;
    std::atomic<float>* pGhostAmt = nullptr; std::atomic<float>* pGhostSwell = nullptr;
    std::atomic<float>* pGhostSlide = nullptr; std::atomic<float>* pGhostTone = nullptr; std::atomic<float>* pGhostMix = nullptr;

    // sample: the audio thread uses `audioSample` under a try-lock; swaps happen on the message thread.
    mutable juce::CriticalSection dataLock;
    std::shared_ptr<xsmp::SampleData> sample;
    juce::SpinLock audioSwapLock;
    std::shared_ptr<xsmp::SampleData> audioSample;
    std::atomic<juce::uint32> sampleGeneration { 0 };
    juce::uint32 audioGeneration = 0;
    juce::String lastError;
    std::atomic<bool> loading { false };
    juce::ThreadPool loaderPool { 1 };
    struct Pending { std::shared_ptr<xsmp::SampleData> data; std::vector<juce::int64> cues; juce::String error; };
    juce::CriticalSection pendingLock;
    std::vector<Pending> pending;

    std::array<std::atomic<juce::int64>, xsmp::kNumPads> cues;
    std::array<std::atomic<juce::int64>, xsmp::kNumPads> sliceEnds;
    std::array<std::atomic<float>, xsmp::kNumPads> padRate;
    std::vector<juce::int64> detectedCues, detectedEnds;
    std::vector<float> detectedRates;
    std::atomic<double> detectedBpm { 0.0 }, bpmOverride { 0.0 };
    juce::uint32 lastChopSeed = 0;
    bool lastChopWasGenerate = false;
    xsmp::VocalFx vocalFx;

    std::array<xsmp::Voice, kVoices> voices;
    juce::uint64 voiceCounter = 0;
    double sr = 48000.0;
    int maxBlock = 512;
    juce::SmoothedValue<float> masterGain;
    juce::AudioBuffer<float> monoScratch;

    std::atomic<double> hostBpm { 0.0 }, tempoRatioNow { 1.0 }, semisNow { 0.0 };
    std::atomic<float> editorScale { 1.0f };
    std::atomic<int> snapNow { 0 };
    std::array<std::atomic<bool>, xsmp::kNumPads> padPlaying;
    std::array<std::atomic<double>, xsmp::kNumPads> padPos;
    juce::String pendingRestorePath;
    std::vector<juce::int64> pendingRestoreEnds;
    std::vector<float> pendingRestoreRates;

    // AUTO state
    std::array<juce::uint32, xsmp::kNumAutoSlots> autoSeeds {};
    std::array<int, xsmp::kNumAutoSlots> autoStrats {};
    std::array<bool, xsmp::kNumAutoSlots> autoLocked {};
    std::vector<AutoHistoryEntry> autoHistory;
    int autoHistPos = -1;
    juce::uint32 autoRollCounter = 0;
    using FlipTable = std::array<std::array<xsmp::AutoFlip, xsmp::kNumAutoLens>, xsmp::kNumAutoSlots>;
    std::unique_ptr<FlipTable> autoFlips { std::make_unique<FlipTable>() };   // written on the message thread
    std::unique_ptr<FlipTable> audioFlips { std::make_unique<FlipTable>() };  // copy read by the audio thread
    juce::SpinLock autoFlipLock;
    std::atomic<float>* pAutoSlot = nullptr; std::atomic<float>* pAutoPlay = nullptr; std::atomic<float>* pChaos = nullptr;
    std::atomic<float>* pDensity = nullptr; std::atomic<float>* pSwing = nullptr;
    std::array<std::atomic<float>*, xsmp::kNumAutoSlots> pAutoLen {};
    // audio-thread transport for AUTO
    bool autoRunning = false, autoArmed = false, autoOnHost = false, autoMidiHeld = false;
    double autoAnchor = 0.0, autoInternalPpq = 0.0;
    juce::int64 globalSample = 0;
    std::vector<AutoTrig> autoTrigs;
    std::atomic<double> autoPosNow { -1.0 };
    std::atomic<bool> autoRunningNow { false }, autoArmedNow { false };
    std::atomic<int> midiSlotRequest { -1 };
    std::array<std::atomic<juce::int64>, 64> cycleLog;
    std::atomic<int> cycleLogCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XoxicoaSamplerProcessor)
};
