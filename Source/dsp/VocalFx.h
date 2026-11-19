#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <array>
#include <cmath>
#include <vector>

// Master-bus vocal FX + Ghost Voice. All processing is preallocated in prepare().
// Pitch/formant use a 4-grain shifter (low latency, independent of the pad stretcher).
namespace xsmp
{
    inline const juce::StringArray vfxDelayDivs { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2" };
    inline constexpr double vfxDelayBeats[] { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0 };

    struct VocalFxParams
    {
        float revSize = 0.5f, revMix = 0.0f;
        int   dlyDiv = 6; // 1/8D
        float dlyFb = 0.35f, dlyMix = 0.0f;
        bool  dlyPP = true;
        float pitch = 0.0f, formant = 0.0f;
        float drive = 0.0f, filter = 0.0f, stutter = 0.0f, chorus = 0.0f, width = 0.5f;
        float ghostAmt = 0.0f, ghostSwell = 0.5f, ghostSlide = 0.4f, ghostTone = 0.5f, ghostMix = 0.0f;
        double bpm = 120.0, ppq = 0.0;
        bool playing = false;
    };

    class GrainPitch
    {
    public:
        void prepare (double sampleRate, int /*maxBlock*/)
        {
            sr = sampleRate;
            const int n = (int) std::ceil (sr * 2.0) + 8;
            bufL.assign ((size_t) n, 0.0f); bufR.assign ((size_t) n, 0.0f);
            len = n; w = 0;
            grain = juce::jmax (64, (int) (0.040 * sr));
            hop  = juce::jmax (32, grain / 2);
            for (auto& g : gpos) g = 0.0;
            for (auto& ph : phase) ph = 0;
            phase[1] = hop; phase[3] = hop;
            writeAge = 0;
        }
        void reset()
        {
            std::fill (bufL.begin(), bufL.end(), 0.0f);
            std::fill (bufR.begin(), bufR.end(), 0.0f);
            w = 0; writeAge = 0;
        }
        void process (const float* L, const float* R, float* oL, float* oR, int n, float semis)
        {
            const double rate = std::pow (2.0, (double) semis / 12.0);
            for (int i = 0; i < n; ++i)
            {
                bufL[(size_t) w] = L[i]; bufR[(size_t) w] = R[i];
                float sl = 0, srgt = 0, tw = 0;
                for (int g = 0; g < 4; ++g)
                {
                    // Hann window over the grain; read pointer walks at `rate`
                    const int ph = phase[g];
                    const float win = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) ph / (float) grain);
                    double rp = (double) w - gpos[g];
                    while (rp < 0.0) rp += (double) len;
                    const int i0 = (int) rp % len;
                    const int i1 = (i0 + 1) % len;
                    const float t = (float) (rp - std::floor (rp));
                    sl   += win * (bufL[(size_t) i0] + t * (bufL[(size_t) i1] - bufL[(size_t) i0]));
                    srgt += win * (bufR[(size_t) i0] + t * (bufR[(size_t) i1] - bufR[(size_t) i0]));
                    tw += win;
                    phase[g] = (ph + 1) % grain;
                    gpos[g] += rate;
                    if (phase[g] == 0)
                        gpos[g] = 0.0; // reset grain to "now"
                }
                const float nrm = tw > 1.0e-6f ? 1.0f / tw * 2.0f : 0.0f; // 2 overlapping grains typically
                oL[i] = sl * nrm; oR[i] = srgt * nrm;
                w = (w + 1) % len;
                ++writeAge;
            }
        }
    private:
        double sr = 48000.0;
        std::vector<float> bufL, bufR;
        int len = 1, w = 0, grain = 1920, hop = 960;
        juce::int64 writeAge = 0;
        double gpos[4] {};
        int phase[4] {};
    };

    class VocalFx
    {
    public:
        void prepare (double sampleRate, int maxBlock)
        {
            sr = sampleRate;
            maxN = juce::jmax (16, maxBlock);
            pitch.prepare (sr, maxN);
            oct.prepare (sr, maxN);
            fifth.prepare (sr, maxN);
            slidePitch.prepare (sr, maxN);
            const int dly = (int) std::ceil (sr * 4.0) + 8;
            dlyL.assign ((size_t) dly, 0.0f); dlyR.assign ((size_t) dly, 0.0f);
            dlyLen = dly; dlyW = 0;
            stutL.assign ((size_t) juce::jmax (dly, (int) (sr * 2)), 0.0f);
            stutR = stutL; stutLen = (int) stutL.size(); stutW = 0;
            wetL.assign ((size_t) maxN + 16, 0.0f);
            wetR = wetL; tmpL = wetL; tmpR = wetL;
            chorus.prepare ({ sr, (juce::uint32) maxN, 2 });
            chorus.setCentreDelay (7.5f);
            chorus.setFeedback (0.15f);
            chorus.setMix (1.0f);
            reverb.setSampleRate (sr);
            reverb.reset();
            ghostRev.setSampleRate (sr);
            ghostRev.reset();
            juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxN, 2 };
            formantLow.prepare (spec); formantHigh.prepare (spec);
            formantLowR.prepare (spec); formantHighR.prepare (spec);
            formantLow.reset(); formantHigh.reset();
            formantLowR.reset(); formantHighR.reset();
            filtL.prepare (spec); filtR.prepare (spec);
            filtL.reset(); filtR.reset();
            sRev = sDly = sDrv = sStu = sCho = sWid = sGho = sPit = 0;
            env = 0; slide = 0; dlyF = {}; dlyH = {}; hpZ = lpZ = 0;
            prepared = true;
        }

        void process (float* L, float* R, int n, const VocalFxParams& p)
        {
            if (! prepared || n <= 0) return;
            if ((int) wetL.size() < n)
            {
                wetL.resize ((size_t) n + 16); wetR.resize ((size_t) n + 16);
                tmpL.resize ((size_t) n + 16); tmpR.resize ((size_t) n + 16);
            }
            const bool idle = p.revMix < 1.0e-4f && p.dlyMix < 1.0e-4f && std::abs (p.pitch) < 0.05f
                && std::abs (p.formant) < 0.05f && p.drive < 1.0e-4f && std::abs (p.filter) < 0.02f
                && p.stutter < 1.0e-4f && p.chorus < 1.0e-4f && std::abs (p.width - 0.5f) < 0.01f
                && p.ghostMix < 1.0e-4f
                && sRev < 1.0e-4f && sDly < 1.0e-4f && sDrv < 1.0e-4f && sStu < 1.0e-4f
                && sCho < 1.0e-4f && sGho < 1.0e-4f && sPit < 1.0e-4f;
            if (idle) return;
            const float sm = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
            auto approach = [sm] (float& s, float t) { s += sm * (t - s); };

            // ----- saturation (soft tanh, pre-FX so delay/reverb get the harmonics)
            approach (sDrv, p.drive);
            if (sDrv > 1.0e-4f)
            {
                const float drive = 1.0f + sDrv * 9.0f;
                const float comp = 1.0f / std::tanh (drive * 0.6f);
                for (int i = 0; i < n; ++i)
                {
                    L[i] = std::tanh (L[i] * drive) * comp;
                    R[i] = std::tanh (R[i] * drive) * comp;
                }
            }

            // ----- pitch + formant wet layer
            approach (sPit, std::abs (p.pitch) + std::abs (p.formant) > 0.05f ? 1.0f : 0.0f);
            if (sPit > 1.0e-4f)
            {
                pitch.process (L, R, tmpL.data(), tmpR.data(), n, p.pitch);
                applyFormant (tmpL.data(), tmpR.data(), n, p.formant);
                const float m = sPit * 0.55f;
                for (int i = 0; i < n; ++i)
                {
                    L[i] += m * (tmpL[(size_t) i] - L[i]);
                    R[i] += m * (tmpR[(size_t) i] - R[i]);
                }
            }

            // ----- bipolar DJ filter (same law as the pad filter): left LP, right HP
            if (std::abs (p.filter) > 0.02f)
            {
                const bool hp = p.filter > 0.0f;
                const float amt = std::abs (p.filter);
                const float fc = hp
                    ? 20.0f * std::pow (6000.0f / 20.0f, amt)
                    : 20000.0f * std::pow (150.0f / 20000.0f, amt);
                juce::dsp::IIR::Coefficients<float>::Ptr coef = hp
                    ? juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, juce::jlimit (20.0f, 18000.0f, fc), 0.75f)
                    : juce::dsp::IIR::Coefficients<float>::makeLowPass  (sr, juce::jlimit (40.0f, 20000.0f, fc), 0.75f);
                *filtL.coefficients = *coef; *filtR.coefficients = *coef;
                for (int i = 0; i < n; ++i)
                {
                    L[i] = filtL.processSample (L[i]);
                    R[i] = filtR.processSample (R[i]);
                }
            }

            // ----- tempo-synced delay (ping-pong optional)
            approach (sDly, p.dlyMix);
            const double bpm = p.bpm > 0.0 ? p.bpm : 120.0;
            const float dTime = (float) (60.0 / bpm * vfxDelayBeats[juce::jlimit (0, 10, p.dlyDiv)]);
            if (sDly > 1.0e-4f || p.dlyMix > 0.0f)
            {
                const float fb = juce::jlimit (0.0f, 0.92f, p.dlyFb);
                const bool pp = p.dlyPP;
                const float dSamp = juce::jlimit (1.0f, (float) dlyLen - 2.0f, dTime * (float) sr);
                for (int i = 0; i < n; ++i)
                {
                    double rp = (double) dlyW - (double) dSamp;
                    if (rp < 0.0) rp += (double) dlyLen;
                    int i0 = (int) rp;
                    const float t = (float) (rp - (double) i0);
                    if (i0 >= dlyLen) i0 -= dlyLen;
                    const int i1 = (i0 + 1) % dlyLen;
                    const float yl = dlyL[(size_t) i0] + t * (dlyL[(size_t) i1] - dlyL[(size_t) i0]);
                    const float yr = dlyR[(size_t) i0] + t * (dlyR[(size_t) i1] - dlyR[(size_t) i0]);
                    if (pp)
                    {
                        const float in = 0.5f * (L[i] + R[i]);
                        dlyL[(size_t) dlyW] = std::tanh (in + yr * fb);
                        dlyR[(size_t) dlyW] = std::tanh (yl * fb);
                    }
                    else
                    {
                        dlyL[(size_t) dlyW] = std::tanh (L[i] + yl * fb);
                        dlyR[(size_t) dlyW] = std::tanh (R[i] + yr * fb);
                    }
                    dlyW = (dlyW + 1) % dlyLen;
                    L[i] += sDly * 1.15f * yl - sDly * 0.25f * L[i];
                    R[i] += sDly * 1.15f * yr - sDly * 0.25f * R[i];
                }
            }

            // ----- reverb
            approach (sRev, p.revMix);
            if (sRev > 1.0e-4f)
            {
                juce::Reverb::Parameters rp;
                rp.roomSize = 0.25f + 0.74f * p.revSize;
                rp.damping = 0.35f + 0.4f * (1.0f - p.revSize);
                rp.width = 1.0f;
                rp.wetLevel = 1.0f / 3.0f;
                rp.dryLevel = 0.0f;
                reverb.setParameters (rp);
                for (int i = 0; i < n; ++i) { wetL[(size_t) i] = L[i]; wetR[(size_t) i] = R[i]; }
                reverb.processStereo (wetL.data(), wetR.data(), n);
                for (int i = 0; i < n; ++i)
                {
                    L[i] += sRev * (wetL[(size_t) i] - 0.15f * L[i]);
                    R[i] += sRev * (wetR[(size_t) i] - 0.15f * R[i]);
                }
            }

            // ----- chorus
            approach (sCho, p.chorus);
            if (sCho > 1.0e-4f)
            {
                chorus.setRate (0.4f + 1.8f * p.chorus);
                chorus.setDepth (0.15f + 0.7f * p.chorus);
                for (int i = 0; i < n; ++i) { wetL[(size_t) i] = L[i]; wetR[(size_t) i] = R[i]; }
                float* ch[2] = { wetL.data(), wetR.data() };
                juce::dsp::AudioBlock<float> blk (ch, 2, (size_t) n);
                juce::dsp::ProcessContextReplacing<float> ctx (blk);
                chorus.process (ctx);
                for (int i = 0; i < n; ++i)
                {
                    L[i] += sCho * (wetL[(size_t) i] - L[i]);
                    R[i] += sCho * (wetR[(size_t) i] - R[i]);
                }
            }

            // ----- width (0 = mono, 0.5 = natural, 1 = wide)
            approach (sWid, p.width);
            {
                const float w = sWid;
                const float midG = 1.0f;
                const float sideG = w <= 0.5f ? w * 2.0f : 1.0f + (w - 0.5f) * 2.0f;
                for (int i = 0; i < n; ++i)
                {
                    const float mid = 0.5f * (L[i] + R[i]);
                    const float side = 0.5f * (L[i] - R[i]);
                    L[i] = midG * mid + sideG * side;
                    R[i] = midG * mid - sideG * side;
                }
            }

            // ----- host-synced stutter / gate
            approach (sStu, p.stutter);
            if (sStu > 1.0e-4f)
            {
                const double beats = p.stutter < 0.34f ? 0.5 : (p.stutter < 0.67f ? 0.25 : 0.125); // 1/8, 1/16, 1/32
                const double period = 60.0 / bpm * beats;
                const float pulse = p.stutter < 0.67f ? 0.55f : 0.40f; // open fraction
                const float fade = (float) (0.003 * sr);
                for (int i = 0; i < n; ++i)
                {
                    const double t = p.ppq + (double) i / sr * bpm / 60.0; // quarter-notes
                    const double phase = std::fmod (t / (beats), 1.0);
                    const double phS = phase < 0 ? phase + 1.0 : phase;
                    float g = phS < pulse ? 1.0f : 0.0f;
                    // 3 ms fade at edges
                    const double edge = 0.003 / juce::jmax (1.0e-4, period);
                    if (phS < edge) g *= (float) (phS / edge);
                    else if (phS > pulse - edge && phS < pulse) g *= (float) ((pulse - phS) / edge);
                    L[i] += sStu * (L[i] * g - L[i]);
                    R[i] += sStu * (R[i] * g - R[i]);
                }
            }

            // ----- GHOST VOICE
            approach (sGho, p.ghostMix * (0.35f + 0.65f * p.ghostAmt));
            if (sGho > 1.0e-4f || p.ghostMix > 0.0f)
                processGhost (L, R, n, p, sGho);
        }

    private:
        void applyFormant (float* L, float* R, int n, float semis)
        {
            // EQ-based formant colour: low shelf vs high shelf, ±9 dB. Instant, no latency.
            const float amt = juce::jlimit (-12.0f, 12.0f, semis) / 12.0f;
            if (std::abs (amt) < 0.02f) return;
            const float shelf = amt * 9.0f;
            auto ls = juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sr, 300.0f,  0.7f, juce::Decibels::decibelsToGain (-shelf));
            auto hs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 2500.0f, 0.7f, juce::Decibels::decibelsToGain ( shelf));
            *formantLow.coefficients  = *ls; *formantHigh.coefficients  = *hs;
            *formantLowR.coefficients = *ls; *formantHighR.coefficients = *hs;
            for (int i = 0; i < n; ++i)
            {
                L[i] = formantHigh.processSample  (formantLow.processSample  (L[i]));
                R[i] = formantHighR.processSample (formantLowR.processSample (R[i]));
            }
        }

        void processGhost (float* L, float* R, int n, const VocalFxParams& p, float mix)
        {
            if ((int) gUniL.size() < n)
            {
                gUniL.resize ((size_t) n + 8); gUniR.resize ((size_t) n + 8);
                gOctL.resize ((size_t) n + 8); gOctR.resize ((size_t) n + 8);
                gFifL.resize ((size_t) n + 8); gFifR.resize ((size_t) n + 8);
                gInL.resize ((size_t) n + 8);  gInR.resize ((size_t) n + 8);
            }
            const double bpm = p.bpm > 0.0 ? p.bpm : 120.0;
            const float swellBeats = 0.5f + p.ghostSwell * 7.5f;
            const float atk = std::exp (-1.0f / juce::jmax (1.0f, swellBeats * 0.25f * 60.0f / (float) bpm * (float) sr));
            const float rel = std::exp (-1.0f / juce::jmax (1.0f, swellBeats * 0.50f * 60.0f / (float) bpm * (float) sr));
            const float slideAmt = p.ghostSlide * 12.0f;
            for (int i = 0; i < n; ++i)
            {
                const float peak = juce::jmax (std::abs (L[i]), std::abs (R[i]));
                const float target = peak > 0.02f ? 1.0f : 0.0f;
                env = target > env ? env + (1.0f - atk) * (target - env)
                                   : env + (1.0f - rel) * (target - env);
                slide += (1.0f - atk) * ((1.0f - env) * slideAmt - slide);
                const float e = env * env;
                gInL[(size_t) i] = L[i] * e;
                gInR[(size_t) i] = R[i] * e;
            }
            slidePitch.process (gInL.data(), gInR.data(), gUniL.data(), gUniR.data(), n, -slide);
            oct.process   (gInL.data(), gInR.data(), gOctL.data(), gOctR.data(), n, 12.0f);
            fifth.process (gInL.data(), gInR.data(), gFifL.data(), gFifR.data(), n,  7.0f);
            const float harm = juce::jlimit (0.0f, 1.0f, p.ghostAmt);
            // Tone: LP 400 Hz .. 12 kHz
            const float toneHz = 400.0f * std::pow (12000.0f / 400.0f, juce::jlimit (0.0f, 1.0f, p.ghostTone));
            const float a = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * toneHz / (float) sr);
            juce::Reverb::Parameters rp;
            rp.roomSize = 0.86f + 0.12f * p.ghostAmt;
            rp.damping = 0.25f + 0.45f * (1.0f - p.ghostTone);
            rp.width = 1.0f;
            rp.wetLevel = 1.0f / 3.0f;
            rp.dryLevel = 0.0f;
            ghostRev.setParameters (rp);
            for (int i = 0; i < n; ++i)
            {
                float l = gUniL[(size_t) i] + harm * 0.55f * gOctL[(size_t) i] + harm * 0.40f * gFifL[(size_t) i];
                float r = gUniR[(size_t) i] + harm * 0.55f * gOctR[(size_t) i] + harm * 0.40f * gFifR[(size_t) i];
                lpZ += a * (l - lpZ);
                hpZ += a * (r - hpZ);
                wetL[(size_t) i] = lpZ;
                wetR[(size_t) i] = hpZ;
            }
            ghostRev.processStereo (wetL.data(), wetR.data(), n);
            const float g = mix * 1.15f;
            for (int i = 0; i < n; ++i)
            {
                L[i] += g * wetL[(size_t) i];
                R[i] += g * wetR[(size_t) i];
            }
        }

        double sr = 48000.0;
        int maxN = 512, dlyLen = 1, dlyW = 0, stutLen = 1, stutW = 0;
        bool prepared = false;
        GrainPitch pitch, oct, fifth, slidePitch;
        juce::Reverb reverb, ghostRev;
        juce::dsp::Chorus<float> chorus;
        juce::dsp::IIR::Filter<float> formantLow, formantHigh, formantLowR, formantHighR, filtL, filtR;
        std::vector<float> dlyL, dlyR, stutL, stutR, wetL, wetR, tmpL, tmpR;
        std::vector<float> gUniL, gUniR, gOctL, gOctR, gFifL, gFifR, gInL, gInR;
        float sRev = 0, sDly = 0, sDrv = 0, sStu = 0, sCho = 0, sWid = 0.5f, sGho = 0, sPit = 0;
        float env = 0, slide = 0, hpZ = 0, lpZ = 0;
        std::array<float, 4> dlyF {}, dlyH {};
    };
}
