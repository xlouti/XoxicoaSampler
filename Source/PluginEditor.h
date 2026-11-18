#pragma once
#include "PluginProcessor.h"
#include "ui/SmpLookAndFeel.h"

namespace xsmp::ui
{
    // Waveform (black on white) with 16 draggable cue markers, playheads, wheel zoom and drag-to-pan.
    class WaveformView : public juce::Component
    {
    public:
        explicit WaveformView (XoxicoaSamplerProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        std::function<void (int)> onCueSelected;
        bool dropHighlight = false;
    private:
        juce::Rectangle<float> waveArea() const;
        double xToPos (float x, juce::int64 len) const;
        float posToX (double pos, juce::int64 len) const;
        int markerAt (juce::Point<float> p, juce::int64 len) const;
        XoxicoaSamplerProcessor& proc;
        double viewStart = 0.0, viewLen = 1.0; // fractions of the sample
        int dragging = -1, hover = -1;
        bool panning = false; float panAnchorX = 0; double panAnchorStart = 0;
    };

    class PadGrid : public juce::Component
    {
    public:
        explicit PadGrid (XoxicoaSamplerProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        std::function<void (int)> onPadSelected;
        static const char* keyLabel (int pad);
    private:
        int padAt (juce::Point<float>) const;
        juce::Rectangle<float> padBounds (int pad) const;
        XoxicoaSamplerProcessor& proc;
        int mouseNote = -1;
    };
}

namespace xsmp::ui
{
    // One premade Auto version (A-F): letter + strategy, mini pattern with playhead, its own length, re-roll and lock.
    class AutoSlotTile : public juce::Component
    {
    public:
        AutoSlotTile (XoxicoaSamplerProcessor& p, int slotIndex);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void refresh();
    private:
        XoxicoaSamplerProcessor& proc;
        const int slot;
        juce::ComboBox len;
        juce::TextButton roll { "ROLL" }, lock { "LOCK" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aLen;
    };

    // Button you can click (writes + reveals the file) or drag straight into the DAW / desktop.
    class DragOutButton : public juce::TextButton
    {
    public:
        explicit DragOutButton (const juce::String& t) : juce::TextButton (t) {}
        std::function<juce::File()> makeFile;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        bool dragging = false;
    };
}

class XoxicoaSamplerEditor : public juce::AudioProcessorEditor,
                             public juce::FileDragAndDropTarget,
                             private juce::Timer
{
public:
    explicit XoxicoaSamplerEditor (XoxicoaSamplerProcessor&);
    ~XoxicoaSamplerEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    void selectPad (int p);
    static constexpr int kW = 1120, kH = 920;

private:
    void timerCallback() override;
    void updateReadouts();
    void releaseAllKeys();

    XoxicoaSamplerProcessor& proc;
    xsmp::ui::LookAndFeel lnf;
    xsmp::ui::WaveformView wave;
    xsmp::ui::PadGrid pads;

    juce::TextButton loadBtn { "LOAD" }, halfBtn { "/2" }, dblBtn { "x2" }, resetCuesBtn { "RESET CUES" };
    std::unique_ptr<juce::FileChooser> chooser;

    // global
    xsmp::ui::XfxKnob keyShift, volume;
    juce::ComboBox keyTarget, padRoot;
    juce::ToggleButton sync { "SYNC" };
    // selected pad
    xsmp::ui::XfxKnob attack, release, filter, gain;
    juce::ComboBox padMode, choke;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> aKeyShift, aVolume, aAttack, aRelease, aFilter, aGain;
    std::unique_ptr<CA> aKeyTarget, aRoot, aMode, aChoke;
    std::unique_ptr<BA> aSync;

    // AUTO
    juce::TextButton autoBtn { "AUTO" }, undoBtn { "UNDO" }, autoPlayBtn { "AUTO PLAY" };
    juce::ComboBox historyBox;
    xsmp::ui::DragOutButton dragMidi { "DRAG MIDI" }, dragWav { "DRAG WAV" };
    xsmp::ui::XfxKnob chaos, density, swing;
    std::array<std::unique_ptr<xsmp::ui::AutoSlotTile>, xsmp::kNumAutoSlots> tiles;
    std::unique_ptr<SA> aChaos, aDensity, aSwing;
    std::unique_ptr<BA> aAutoPlay;
    int shownHistSize = -1, shownHistPos = -1;
    void refreshHistory();
    juce::File writeAutoWav() const;

    juce::String bpmText, keyText, statusText, fileText, tempoText;
    std::array<bool, xsmp::kNumPads> keyDown {};
    juce::uint32 lastChange = 0;
    bool dragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XoxicoaSamplerEditor)
};
