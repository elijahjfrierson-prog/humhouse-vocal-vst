#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <atomic>

namespace humvocal
{

// FabFilter-style sibilance detector and reducer.
// Uses a configurable band-pass filter to isolate sibilant energy,
// then dynamically attenuates that band when it exceeds the threshold.
// Supports split-band and wideband modes, adjustable Q, and
// gain reduction metering.
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
        grDb.store(0.0f);

        sidechainBuffer.setSize(2, blockSize);
    }

    void setFrequency (float hz)  { centreFreq = hz; updateFilters(); }
    void setThreshold (float db)  { thresholdDb = db; }
    void setReduction (float db)  { reductionDb = db; }
    void setActive (bool on)      { active = on; }
    void setBandwidth (float q)   { bandwidth = q; updateFilters(); }
    void setMode (int m)          { mode = m; }  // 0=split-band, 1=wideband
    void setListen (bool on)      { listenMode = on; }

    // UI readback: current gain reduction in dB (always <= 0)
    float getGainReductionDb() const { return grDb.load(); }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) { grDb.store(0.0f); return; }

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        // Copy input to sidechain buffer for band-pass filtering
        sidechainBuffer.setSize(numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
            sidechainBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        juce::dsp::AudioBlock<float> scBlock (sidechainBuffer);
        juce::dsp::ProcessContextReplacing<float> scCtx (scBlock);
        detector.process(scCtx);

        // Envelope follower coefficients
        float attackCoeff  = std::exp(-1.0f / (static_cast<float>(sr) * 0.0005f));  // 0.5ms attack
        float releaseCoeff = std::exp(-1.0f / (static_cast<float>(sr) * 0.015f));   // 15ms release
        float threshLin = juce::Decibels::decibelsToGain(thresholdDb);
        float maxReductionLin = juce::Decibels::decibelsToGain(reductionDb); // negative dB → < 1.0

        float peakGR = 0.0f;

        // Raw pointers for fast inner loop
        float* bufPtrs[2] = { nullptr, nullptr };
        const float* scPtrs[2] = { nullptr, nullptr };
        for (int ch = 0; ch < numChannels; ++ch)
        {
            bufPtrs[ch] = buffer.getWritePointer(ch);
            scPtrs[ch] = sidechainBuffer.getReadPointer(ch);
        }

        for (int i = 0; i < numSamples; ++i)
        {
            float level = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float absVal = std::abs(scPtrs[ch][i]);
                if (absVal > level) level = absVal;
            }

            if (level > envelope)
                envelope = attackCoeff * envelope + (1.0f - attackCoeff) * level;
            else
                envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * level;

            // Linear-domain threshold comparison (avoids per-sample dB conversion)
            float gain = 1.0f;
            if (envelope > threshLin)
            {
                // Over-threshold ratio in linear domain
                float overRatio = envelope / threshLin;
                // Soft knee approximation: smooth blend for small overages
                float overDb = 20.0f * std::log10(overRatio);
                float knee = 3.0f;
                float effectiveOver = (overDb < knee) ? (overDb * overDb) / (2.0f * knee) : overDb;
                float reductionApplied = std::min(effectiveOver, -reductionDb);
                gain = std::pow(10.0f, -reductionApplied * 0.05f);
                if (reductionApplied > peakGR) peakGR = reductionApplied;
            }

            if (listenMode)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    bufPtrs[ch][i] = scPtrs[ch][i];
            }
            else if (mode == 1)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    bufPtrs[ch][i] *= gain;
            }
            else
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    float dry = bufPtrs[ch][i];
                    float sib = scPtrs[ch][i];
                    bufPtrs[ch][i] = dry - sib * (1.0f - gain);
                }
            }
        }

        grDb.store(-peakGR);
    }

private:
    double sr = 44100.0;
    bool active = true;
    float centreFreq = 7000.0f;
    float thresholdDb = -20.0f;
    float reductionDb = -12.0f;
    float bandwidth = 2.0f;
    int mode = 0;         // 0=split-band, 1=wideband
    bool listenMode = false;
    float envelope = 0.0f;
    std::atomic<float> grDb { 0.0f };

    juce::AudioBuffer<float> sidechainBuffer;

    using IIRFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                      juce::dsp::IIR::Coefficients<float>>;
    IIRFilter detector;
    IIRFilter attenuator;

    void updateFilters()
    {
        if (sr <= 0.0) return;
        *detector.state = *juce::dsp::IIR::Coefficients<float>::makeBandPass(sr, centreFreq, bandwidth);
        *attenuator.state = *juce::dsp::IIR::Coefficients<float>::makeBandPass(sr, centreFreq, bandwidth);
    }
};

} // namespace humvocal
