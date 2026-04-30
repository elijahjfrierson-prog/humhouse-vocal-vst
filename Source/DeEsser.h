#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace humvocal
{

// Sibilance detector and reducer.  Uses a band-pass filter centred
// around 5–9 kHz to isolate sibilant energy, then dynamically
// attenuates that band when it exceeds the threshold.
class DeEsser
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(blockSize), 2 };
        detector.prepare(spec);
        attenuator.prepare(spec);
        updateFilters();
        envelope = 0.0f;

        // Pre-allocate sidechain buffer
        sidechainBuffer.setSize(2, blockSize);
    }

    void setFrequency (float hz) { centreFreq = hz; updateFilters(); }
    void setThreshold (float db) { thresholdDb = db; }
    void setReduction (float db) { reductionDb = db; }
    void setActive (bool on) { active = on; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        // Use pre-allocated buffer (resize only if needed, no alloc in steady state)
        sidechainBuffer.setSize(numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
            sidechainBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        juce::dsp::AudioBlock<float> scBlock (sidechainBuffer);
        juce::dsp::ProcessContextReplacing<float> scCtx (scBlock);
        detector.process(scCtx);

        float attackCoeff  = std::exp(-1.0f / (static_cast<float>(sr) * 0.001f));
        float releaseCoeff = std::exp(-1.0f / (static_cast<float>(sr) * 0.020f));

        for (int i = 0; i < numSamples; ++i)
        {
            float level = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                level = std::max(level, std::abs(sidechainBuffer.getSample(ch, i)));

            if (level > envelope)
                envelope = attackCoeff * envelope + (1.0f - attackCoeff) * level;
            else
                envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * level;

            float envDb = juce::Decibels::gainToDecibels(envelope, -100.0f);
            float gain = 1.0f;

            if (envDb > thresholdDb)
            {
                float overDb = envDb - thresholdDb;
                float reductionApplied = std::min(overDb, -reductionDb);
                gain = juce::Decibels::decibelsToGain(-reductionApplied);
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float dry = buffer.getSample(ch, i);
                float sib = sidechainBuffer.getSample(ch, i);
                buffer.setSample(ch, i, dry - sib * (1.0f - gain));
            }
        }
    }

private:
    double sr = 44100.0;
    bool active = true;
    float centreFreq = 7000.0f;
    float thresholdDb = -20.0f;
    float reductionDb = -12.0f;
    float envelope = 0.0f;

    juce::AudioBuffer<float> sidechainBuffer;

    using IIRFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                      juce::dsp::IIR::Coefficients<float>>;
    IIRFilter detector;
    IIRFilter attenuator;

    void updateFilters()
    {
        if (sr <= 0.0) return;
        *detector.state = *juce::dsp::IIR::Coefficients<float>::makeBandPass(sr, centreFreq, 2.0f);
        *attenuator.state = *juce::dsp::IIR::Coefficients<float>::makeBandPass(sr, centreFreq, 2.0f);
    }
};

} // namespace humvocal
