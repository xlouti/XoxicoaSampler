#pragma once
#include "Analysis.h"
#include <memory>

namespace xsmp
{
    // Immutable once published to the audio thread.
    struct SampleData
    {
        juce::AudioBuffer<float> audio;   // always 2 channels, at the file's own rate
        double fileRate = 44100.0;
        juce::String path, name;
        AnalysisResult analysis;
        std::vector<float> peakMin, peakMax; // overview, kOverview buckets (mono)
        static constexpr int kOverview = 4096;

        juce::int64 length() const { return audio.getNumSamples(); }
        double seconds() const { return (double) length() / fileRate; }

        void buildOverview()
        {
            peakMin.assign (kOverview, 0.0f); peakMax.assign (kOverview, 0.0f);
            const auto n = length();
            if (n <= 0) return;
            auto* l = audio.getReadPointer (0); auto* r = audio.getReadPointer (1);
            for (int b = 0; b < kOverview; ++b)
            {
                const auto a = (juce::int64) ((double) b / kOverview * (double) n);
                const auto e = juce::jmax (a + 1, (juce::int64) ((double) (b + 1) / kOverview * (double) n));
                float mn = 1.0f, mx = -1.0f;
                for (auto i = a; i < e && i < n; ++i) { const float v = 0.5f * (l[i] + r[i]); mn = juce::jmin (mn, v); mx = juce::jmax (mx, v); }
                peakMin[(size_t) b] = mn > mx ? 0.0f : mn; peakMax[(size_t) b] = mn > mx ? 0.0f : mx;
            }
        }
    };
}
