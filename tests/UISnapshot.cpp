// Renders the editor to a PNG: XoxicoaSamplerUISnapshot <out.png> <scale> [sample.wav]
#include "PluginProcessor.h"
#include "PluginEditor.h"

struct PH : juce::AudioPlayHead
{
    double ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    { PositionInfo p; p.setBpm (160.0); p.setIsPlaying (true); p.setPpqPosition (ppq); p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0); p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 }); return p; }
};

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) return 1;
    const float scale = argc > 2 ? (float) juce::String (argv[2]).getDoubleValue() : 1.0f;
    XoxicoaSamplerProcessor proc;
    PH ph; proc.setPlayHead (&ph);
    proc.prepareToPlay (48000.0, 512);
    if (argc > 3) proc.loadFileSync (juce::File (argv[3]));
    auto set = [&] (const char* id, float v) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set ("auto_slot", 2.0f); set ("auto_play", 1.0f); set ("auto_chaos", 0.62f); set ("auto_swing", 0.25f);
    proc.setAutoLocked (4, true);
    set ("sync", 1.0f); set ("key_shift", 2.0f); set ("key_target", 1 + 12 + 4); set ("pad6_mode", 2.0f); set ("pad6_filter", -0.35f); set ("pad6_release", 120.0f);
    // play a couple of pads so the UI shows active state + playheads
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer mb;
    mb.addEvent (juce::MidiMessage::noteOn (1, 41, 1.0f), 0);
    mb.addEvent (juce::MidiMessage::noteOn (1, 38, 1.0f), 0);
    for (int i = 0; i < 20; ++i) { proc.processBlock (buf, mb); mb.clear(); ph.ppq += 512.0 / 48000.0 * 160.0 / 60.0; }
    // run the Auto version for ~2.3 bars so the active tile shows a playhead mid-pattern
    for (int i = 0; i < 260; ++i) { proc.processBlock (buf, mb); ph.ppq += 512.0 / 48000.0 * 160.0 / 60.0; }
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    if (auto* e = dynamic_cast<XoxicoaSamplerEditor*> (ed.get())) e->selectPad (5);
    ed->setVisible (true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
    juce::File f (argv[1]);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
    std::printf ("wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return 0;
}
