#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/AutoFlip.h"

namespace xsmp
{
    constexpr int kNumPads = 16;

    inline const juce::StringArray& keyNames()
    {
        static const juce::StringArray n { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return n;
    }

    // "Off" + 12 major + 12 minor
    inline juce::StringArray keyTargetChoices()
    {
        juce::StringArray c { "Off" };
        for (auto& k : keyNames()) c.add (k + " maj");
        for (auto& k : keyNames()) c.add (k + " min");
        return c;
    }

    inline juce::StringArray padModeChoices()  { return { "One-Shot", "Gate", "Loop" }; }
    inline juce::StringArray chokeChoices()    { return { "Off", "1", "2", "3", "4" }; }

    inline juce::String autoLenId (int slot) { return "auto_len_" + juce::String::charToString ((juce::juce_wchar) ('a' + slot)); }
    constexpr int kAutoTriggerNote = 24;   // C0 (middle C = C3): hold to play the active Auto version
    constexpr int kAutoSlotNote = 25;      // C#0..F#0 select versions A..F

    inline juce::String padId (int pad, const char* what) { return "pad" + juce::String (pad + 1) + "_" + what; }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout l;
        auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
        auto ms  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 100.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; });
        auto db  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; });
        auto filt = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            if (std::abs (v) < 0.02f) return String ("Open");
            return (v < 0 ? String ("LP ") : String ("HP ")) + String (roundToInt (std::abs (v) * 100.0f)) + "%";
        });

        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Volume", NormalisableRange<float> (-36.0f, 6.0f, 0.01f), 0.0f, db));
        l.add (std::make_unique<AudioParameterInt>   (ParameterID { "key_shift", 1 }, "Key Shift", -12, 12, 0,
                    AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; })));
        l.add (std::make_unique<AudioParameterChoice>(ParameterID { "key_target", 1 }, "Key Target", keyTargetChoices(), 0));
        l.add (std::make_unique<AudioParameterBool>  (ParameterID { "sync", 1 }, "Tempo Sync", false));
        l.add (std::make_unique<AudioParameterInt>   (ParameterID { "pad_root", 1 }, "Pad Root Note", 0, 112, 36));

        // AUTO
        l.add (std::make_unique<AudioParameterChoice>(ParameterID { "auto_slot", 1 }, "Auto Version", autoSlotChoices(), 0));
        l.add (std::make_unique<AudioParameterBool>  (ParameterID { "auto_play", 1 }, "Auto Play", false));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "auto_chaos", 1 }, "Auto Chaos", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, pct));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "auto_density", 1 }, "Auto Density", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f, pct));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "auto_swing", 1 }, "Auto Swing", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, pct));
        {
            const int defLen[kNumAutoSlots] = { 3, 2, 3, 1, 4, 3 };
            for (int v = 0; v < kNumAutoSlots; ++v)
                l.add (std::make_unique<AudioParameterChoice>(ParameterID { autoLenId (v), 1 }, "Auto " + autoSlotChoices()[v] + " Length", autoLenChoices(), defLen[v]));
        }

        for (int p = 0; p < kNumPads; ++p)
        {
            const String n = "Pad " + String (p + 1) + " ";
            l.add (std::make_unique<AudioParameterChoice>(ParameterID { padId (p, "mode"), 1 },    n + "Mode", padModeChoices(), 0));
            l.add (std::make_unique<AudioParameterFloat> (ParameterID { padId (p, "attack"), 1 },  n + "Attack",
                        NormalisableRange<float> (0.5f, 500.0f, 0.01f, 0.3f), 1.0f, ms));
            l.add (std::make_unique<AudioParameterFloat> (ParameterID { padId (p, "release"), 1 }, n + "Release",
                        NormalisableRange<float> (3.0f, 2000.0f, 0.01f, 0.3f), 30.0f, ms));
            l.add (std::make_unique<AudioParameterFloat> (ParameterID { padId (p, "filter"), 1 },  n + "Filter",
                        NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f, filt));
            l.add (std::make_unique<AudioParameterFloat> (ParameterID { padId (p, "gain"), 1 },    n + "Gain",
                        NormalisableRange<float> (-24.0f, 6.0f, 0.01f), 0.0f, db));
            l.add (std::make_unique<AudioParameterChoice>(ParameterID { padId (p, "choke"), 1 },   n + "Choke Group", chokeChoices(), 0));
        }
        return l;
    }
}
