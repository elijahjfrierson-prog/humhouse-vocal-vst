#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

namespace humvocal
{

// Filter type per band — user-selectable
enum class EQBandType
{
    Bell = 0,
    LowShelf,
    HighShelf,
    LowPass,
    HighPass,
    Notch,
    NumTypes
};

// 12-band parametric EQ with selectable filter type per band.
// Designed for vocal-focused surgical + musical processing.
class VisualEQ
{
public:
    static constexpr int kNumBands = 12;

    struct BandState
    {
        float    frequency = 1000.0f;     // Hz
        float    gain      = 0.0f;        // dB
        float    q         = 1.0f;
        EQBandType type    = EQBandType::Bell;
        bool     active    = true;
        bool     dynamic   = false;       // dynamic EQ: gain responds to signal level
    };

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(blockSize), 2 };
        for (auto& f : filters) f.prepare(spec);
        for (auto& f : dynFilters) f.prepare(spec);
        for (auto& f : scFilters) f.prepare(spec);

        setDefaultFrequencies();
    }

    void setActive (bool on) { active = on; }

    void setBand (int index, float freqHz, float gainDb, float q, EQBandType type, bool bandActive)
    {
        if (index < 0 || index >= kNumBands) return;
        auto& b = bands[static_cast<size_t>(index)];
        // Only update coefficients if something actually changed
        if (b.frequency != freqHz || b.gain != gainDb || b.q != q || b.type != type)
        {
            b.frequency = freqHz;
            b.gain      = gainDb;
            b.q         = q;
            b.type      = type;
            b.active    = bandActive;
            updateCoefficients(index);
        }
        else
        {
            b.active = bandActive;
        }
    }

    void setBandFreq (int i, float f)   { if (i >= 0 && i < kNumBands && bands[i].frequency != f) { bands[i].frequency = f; updateCoefficients(i); updateScCoefficients(i); } }
    void setBandGain (int i, float g)   { if (i >= 0 && i < kNumBands && bands[i].gain != g) { bands[i].gain = g; updateCoefficients(i); } }
    void setBandQ    (int i, float q)   { if (i >= 0 && i < kNumBands && bands[i].q != q) { bands[i].q = q; updateCoefficients(i); updateScCoefficients(i); } }
    void setBandType (int i, int t)     { if (i >= 0 && i < kNumBands && bands[i].type != static_cast<EQBandType>(t)) { bands[i].type = static_cast<EQBandType>(t); updateCoefficients(i); updateScCoefficients(i); } }
    void setBandActive(int i, bool a)   { if (i >= 0 && i < kNumBands) { bands[i].active = a; } }
    void setBandDynamic(int i, bool d)  { if (i >= 0 && i < kNumBands) { bands[i].dynamic = d; } }

    const BandState& getBandState (int i) const { return bands[static_cast<size_t>(juce::jlimit(0, kNumBands - 1, i))]; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);

        for (int i = 0; i < kNumBands; ++i)
        {
            bool hasGain = std::abs(bands[i].gain) > 0.01f;
            bool isFilter = bands[i].type == EQBandType::LowPass
                         || bands[i].type == EQBandType::HighPass
                         || bands[i].type == EQBandType::Notch;

            if (!bands[i].active || (!hasGain && !isFilter))
                continue;

            if (!bands[i].dynamic || isFilter)
            {
                // Static path (also used for HP/LP/Notch where gain isn't used)
                filters[i].process(ctx);
            }
            else
            {
                // Dynamic EQ: detect sidechain level, scale gain proportionally
                // 1. Run sidechain bandpass to measure energy at this frequency
                //    (sc coefficients are cached — only updated on freq/Q/type change)
                scBuffer.setSize(numChannels, numSamples, false, false, true);
                for (int ch = 0; ch < numChannels; ++ch)
                    scBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);
                juce::dsp::AudioBlock<float> scBlock(scBuffer);
                juce::dsp::ProcessContextReplacing<float> scCtx(scBlock);
                scFilters[i].process(scCtx);

                // 2. Measure RMS of the filtered sidechain (sc coefficients cached — updated on freq/Q change)
                float rms = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    rms = std::max(rms, scBuffer.getRMSLevel(ch, 0, numSamples));
                float rmsDb = juce::Decibels::gainToDecibels(rms, -80.0f);

                // 3. Scale: gain is fully applied above -20dB, fades below
                constexpr float dynThresh = -30.0f;
                constexpr float dynRange  = 20.0f;
                float dynScale = juce::jlimit(0.0f, 1.0f, (rmsDb - dynThresh) / dynRange);

                // 4. Apply scaled gain via separate dynFilters (keeps filters[] stable for UI)
                float scaledGain = bands[i].gain * dynScale;
                if (std::abs(scaledGain) > 0.01f)
                {
                    updateDynCoefficients(i, scaledGain);
                    dynFilters[i].process(ctx);
                }
            }
        }
    }

    // FFT magnitude response for the UI visualizer
    float getMagnitudeAtFrequency (double freq) const
    {
        float totalMag = 1.0f;
        for (int i = 0; i < kNumBands; ++i)
        {
            if (!bands[i].active) continue;
            if (std::abs(bands[i].gain) < 0.01f
                && bands[i].type != EQBandType::LowPass
                && bands[i].type != EQBandType::HighPass
                && bands[i].type != EQBandType::Notch)
                continue;

            auto coeffs = filters[i].state;
            if (coeffs != nullptr)
            {
                auto mag = coeffs->getMagnitudeForFrequency(freq, sr);
                totalMag *= static_cast<float>(mag);
            }
        }
        return totalMag;
    }

private:
    double sr = 44100.0;
    bool active = true;

    std::array<BandState, kNumBands> bands;

    using IIRFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                      juce::dsp::IIR::Coefficients<float>>;
    std::array<IIRFilter, kNumBands> filters;
    std::array<IIRFilter, kNumBands> dynFilters; // separate filters for dynamic EQ (avoids mutating filters[] visible to UI)
    std::array<IIRFilter, kNumBands> scFilters;  // sidechain bandpass for dynamic EQ
    juce::AudioBuffer<float> scBuffer;

    void setDefaultFrequencies()
    {
        // Vocal-focused defaults
        const float defaultFreqs[kNumBands] = {
            30.0f, 80.0f, 160.0f, 300.0f, 500.0f, 800.0f,
            1200.0f, 2500.0f, 4000.0f, 6000.0f, 10000.0f, 16000.0f
        };
        const EQBandType defaultTypes[kNumBands] = {
            EQBandType::HighPass, EQBandType::LowShelf, EQBandType::Bell, EQBandType::Bell,
            EQBandType::Bell, EQBandType::Bell, EQBandType::Bell, EQBandType::Bell,
            EQBandType::Bell, EQBandType::Bell, EQBandType::HighShelf, EQBandType::LowPass
        };

        for (int i = 0; i < kNumBands; ++i)
        {
            bands[i].frequency = defaultFreqs[i];
            bands[i].gain = 0.0f;
            bands[i].q = 1.0f;
            bands[i].type = defaultTypes[i];
            bands[i].active = true;
            updateCoefficients(i);
            updateScCoefficients(i);
        }
    }

    void updateCoefficients (int index)
    {
        if (index < 0 || index >= kNumBands) return;
        auto& b = bands[static_cast<size_t>(index)];

        float freq = juce::jlimit(20.0f, static_cast<float>(sr) * 0.49f, b.frequency);
        float q    = juce::jlimit(0.1f, 30.0f, b.q);
        float gain = juce::Decibels::decibelsToGain(b.gain);

        juce::dsp::IIR::Coefficients<float>::Ptr coeffs;

        switch (b.type)
        {
            case EQBandType::Bell:
                coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, freq, q, gain);
                break;
            case EQBandType::LowShelf:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr, freq, q, gain);
                break;
            case EQBandType::HighShelf:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr, freq, q, gain);
                break;
            case EQBandType::LowPass:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, freq, q);
                break;
            case EQBandType::HighPass:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, freq, q);
                break;
            case EQBandType::Notch:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeNotch(sr, freq, q);
                break;
            default:
                coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr, freq, q, gain);
                break;
        }

        *filters[static_cast<size_t>(index)].state = *coeffs;
    }

    // Compute biquad coefficients directly into dynFilters — zero heap allocations
    void updateDynCoefficients (int index, float gainDb)
    {
        if (index < 0 || index >= kNumBands) return;
        auto& b = bands[static_cast<size_t>(index)];
        float freq = juce::jlimit(20.0f, static_cast<float>(sr) * 0.49f, b.frequency);
        float q    = juce::jlimit(0.1f, 30.0f, b.q);

        float A     = std::pow(10.0f, gainDb / 40.0f);
        float w0    = juce::MathConstants<float>::twoPi * freq / static_cast<float>(sr);
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / (2.0f * q);

        float b0, b1, b2, a0, a1, a2;

        switch (b.type)
        {
            case EQBandType::LowShelf:
            {
                float sqrtA = std::sqrt(A);
                b0 =     A * ((A + 1) - (A - 1) * cosw0 + 2 * sqrtA * alpha);
                b1 = 2 * A * ((A - 1) - (A + 1) * cosw0);
                b2 =     A * ((A + 1) - (A - 1) * cosw0 - 2 * sqrtA * alpha);
                a0 =          (A + 1) + (A - 1) * cosw0 + 2 * sqrtA * alpha;
                a1 =    -2 * ((A - 1) + (A + 1) * cosw0);
                a2 =          (A + 1) + (A - 1) * cosw0 - 2 * sqrtA * alpha;
                break;
            }
            case EQBandType::HighShelf:
            {
                float sqrtA = std::sqrt(A);
                b0 =      A * ((A + 1) + (A - 1) * cosw0 + 2 * sqrtA * alpha);
                b1 = -2 * A * ((A - 1) + (A + 1) * cosw0);
                b2 =      A * ((A + 1) + (A - 1) * cosw0 - 2 * sqrtA * alpha);
                a0 =           (A + 1) - (A - 1) * cosw0 + 2 * sqrtA * alpha;
                a1 =      2 * ((A - 1) - (A + 1) * cosw0);
                a2 =           (A + 1) - (A - 1) * cosw0 - 2 * sqrtA * alpha;
                break;
            }
            default: // Bell/Peak
            {
                b0 = 1 + alpha * A;
                b1 = -2 * cosw0;
                b2 = 1 - alpha * A;
                a0 = 1 + alpha / A;
                a1 = -2 * cosw0;
                a2 = 1 - alpha / A;
                break;
            }
        }

        float invA0 = 1.0f / a0;
        auto& c = dynFilters[static_cast<size_t>(index)].state->coefficients;
        c.clearQuick();
        c.add(b0 * invA0, b1 * invA0, b2 * invA0, a1 * invA0, a2 * invA0);
    }

    void updateScCoefficients (int index)
    {
        if (index < 0 || index >= kNumBands) return;
        auto& b = bands[static_cast<size_t>(index)];
        float freq = juce::jlimit(20.0f, static_cast<float>(sr) * 0.49f, b.frequency);
        float q    = juce::jlimit(0.1f, 30.0f, b.q);
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass(sr, freq, q);
        *scFilters[static_cast<size_t>(index)].state = *coeffs;
    }
};

} // namespace humvocal
