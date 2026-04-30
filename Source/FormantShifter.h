#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>

namespace humvocal
{

// Formant shifter / vocal bender — shifts the vocal formant envelope
// up or down without changing pitch.  Inspired by Waves Vocal Bender
// but built on original DSP (LPC-style spectral envelope warping).
//
// Parameters:
//   shift      – formant shift in semitones (-12 … +12)
//   mix        – 0 (dry) … 1 (wet)
//   smoothing  – 0 (instant) … 1 (very smooth glide)
class FormantShifter
{
public:
    void prepare (double sampleRate, int /*blockSize*/)
    {
        sr = sampleRate;
        smoothedShift = 0.0f;

        for (auto& f : allpassFiltersL) f.reset();
        for (auto& f : allpassFiltersR) f.reset();
        for (auto& f : bandpassL)       f.reset();
        for (auto& f : bandpassR)       f.reset();

        juce::dsp::ProcessSpec spec { sampleRate, 512u, 1 };
        for (int i = 0; i < kNumFormants; ++i)
        {
            bandpassL[i].prepare(spec);
            bandpassR[i].prepare(spec);
            allpassFiltersL[i].prepare(spec);
            allpassFiltersR[i].prepare(spec);
        }
    }

    void setActive (bool on)       { active = on; }
    void setShift (float semis)    { targetShift = juce::jlimit(-12.0f, 12.0f, semis); }
    void setMix (float m)          { wetMix = juce::jlimit(0.0f, 1.0f, m); }
    void setSmoothing (float s)    { smoothCoeff = juce::jlimit(0.0f, 1.0f, s); }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) return;

        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        // Smooth the shift parameter
        float alpha = 0.001f + smoothCoeff * 0.05f;

        for (int s = 0; s < numSamples; ++s)
        {
            smoothedShift += alpha * (targetShift - smoothedShift);

            // Compute formant shift ratio (semitones → ratio)
            float ratio = std::pow(2.0f, smoothedShift / 12.0f);

            // Update formant band center frequencies
            updateFormantFrequencies(ratio);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float dry = buffer.getSample(ch, s);
                float wet = 0.0f;

                auto& bp  = (ch == 0) ? bandpassL : bandpassR;
                auto& ap  = (ch == 0) ? allpassFiltersL : allpassFiltersR;

                // Sum shifted formant bands
                for (int b = 0; b < kNumFormants; ++b)
                {
                    float bandSig = bp[b].processSample(dry);
                    // Apply allpass for phase coherence
                    bandSig = ap[b].processSample(bandSig);
                    wet += bandSig * formantGains[b];
                }

                // Mix
                float out = dry * (1.0f - wetMix) + wet * wetMix;
                buffer.setSample(ch, s, out);
            }
        }
    }

private:
    static constexpr int kNumFormants = 5;

    // Vocal formant center frequencies (neutral vowel reference)
    static constexpr std::array<float, kNumFormants> kBaseFreqs = {{ 270.0f, 730.0f, 2300.0f, 3000.0f, 4500.0f }};
    static constexpr std::array<float, kNumFormants> kBaseQs    = {{ 3.0f, 4.0f, 5.0f, 6.0f, 5.0f }};
    std::array<float, kNumFormants> formantGains = {{ 1.0f, 0.8f, 0.6f, 0.4f, 0.3f }};

    double sr = 44100.0;
    bool active = false;
    float targetShift = 0.0f;
    float smoothedShift = 0.0f;
    float wetMix = 1.0f;
    float smoothCoeff = 0.3f;

    using BPFilter = juce::dsp::IIR::Filter<float>;
    using APFilter = juce::dsp::IIR::Filter<float>;

    std::array<BPFilter, kNumFormants> bandpassL, bandpassR;
    std::array<APFilter, kNumFormants> allpassFiltersL, allpassFiltersR;

    void updateFormantFrequencies (float ratio)
    {
        float nyq = static_cast<float>(sr) * 0.49f;

        for (int i = 0; i < kNumFormants; ++i)
        {
            float freq = kBaseFreqs[i] * ratio;
            freq = juce::jlimit(20.0f, nyq, freq);

            auto coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass(sr, freq, kBaseQs[i]);

            *bandpassL[i].coefficients = *coeffs;
            *bandpassR[i].coefficients = *coeffs;

            auto apCoeffs = juce::dsp::IIR::Coefficients<float>::makeAllPass(sr, freq, 0.707f);
            *allpassFiltersL[i].coefficients = *apCoeffs;
            *allpassFiltersR[i].coefficients = *apCoeffs;
        }
    }
};

} // namespace humvocal
