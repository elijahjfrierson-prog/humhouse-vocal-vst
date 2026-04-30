#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>

namespace humvocal
{

// 5-band vocal-focused multiband compressor with Linkwitz-Riley crossovers.
// Bands:  Body (0-200), Mud (200-600), Clarity (600-3k), Presence (3k-8k), Air (8k+)
class MultibandCompressor
{
public:
    static constexpr int kNumBands = 5;

    struct BandParams
    {
        float threshold = -18.0f;  // dB
        float ratio     = 4.0f;
        float attack    = 5.0f;   // ms
        float release   = 50.0f;  // ms
        float makeup    = 0.0f;   // dB
        float mix       = 1.0f;   // parallel blend per band
    };

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        bs = blockSize;

        // Set up crossover filters (4th-order Linkwitz-Riley via cascaded Butterworth)
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(blockSize), 2 };

        for (auto& lp : lowpass)  lp.prepare(spec);
        for (auto& hp : highpass) hp.prepare(spec);

        updateCrossoverFrequencies();

        // Envelope followers
        for (int i = 0; i < kNumBands; ++i)
        {
            envFollower[i] = 0.0f;
            gainReduction[i].store(0.0f);
        }

        // Allocate band buffers and scratch buffer
        for (auto& buf : bandBuffers)
            buf.setSize(2, blockSize);
        remainingBuf.setSize(2, blockSize);
    }

    void setActive (bool on) { active = on; }

    void setBandParams (int band, float threshold, float ratio, float attack, float release, float makeup, float mix)
    {
        if (band < 0 || band >= kNumBands) return;
        auto& p = params[band];
        p.threshold = threshold;
        p.ratio     = juce::jlimit(1.0f, 40.0f, ratio);
        p.attack    = attack;
        p.release   = release;
        p.makeup    = makeup;
        p.mix       = juce::jlimit(0.0f, 1.0f, mix);
    }

    void setCrossoverFreq (int index, float freqHz)
    {
        if (index < 0 || index >= kNumCrossovers) return;
        crossoverFreqs[index] = juce::jlimit(20.0f, 20000.0f, freqHz);
        updateCrossoverFrequencies();
    }

    float getGainReduction (int band) const
    {
        if (band < 0 || band >= kNumBands) return 0.0f;
        return gainReduction[band].load();
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) return;

        const int numSamples  = buffer.getNumSamples();
        const int numChannels = juce::jmin(buffer.getNumChannels(), 2);

        // Split into bands
        splitIntoBands(buffer, numSamples, numChannels);

        // Compress each band independently
        for (int b = 0; b < kNumBands; ++b)
            compressBand(b, numSamples, numChannels);

        // Sum bands back
        buffer.clear();
        for (int b = 0; b < kNumBands; ++b)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.addFrom(ch, 0, bandBuffers[b], ch, 0, numSamples);
        }
    }

private:
    static constexpr int kNumCrossovers = 4;

    double sr = 44100.0;
    int bs = 512;
    bool active = false;

    // Default crossover frequencies: vocal-focused
    std::array<float, kNumCrossovers> crossoverFreqs = {{ 200.0f, 600.0f, 3000.0f, 8000.0f }};

    std::array<BandParams, kNumBands> params;
    std::array<float, kNumBands> envFollower {};
    std::array<std::atomic<float>, kNumBands> gainReduction;
    std::array<juce::AudioBuffer<float>, kNumBands> bandBuffers;
    juce::AudioBuffer<float> remainingBuf;

    // Crossover filters (2nd order cascaded = 4th order L-R)
    using BiquadFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                         juce::dsp::IIR::Coefficients<float>>;

    std::array<BiquadFilter, kNumCrossovers> lowpass;
    std::array<BiquadFilter, kNumCrossovers> highpass;

    void updateCrossoverFrequencies()
    {
        for (int i = 0; i < kNumCrossovers; ++i)
        {
            float freq = juce::jlimit(20.0f, static_cast<float>(sr) * 0.49f, crossoverFreqs[i]);
            *lowpass[i].state  = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, freq, 0.707f);
            *highpass[i].state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, freq, 0.707f);
        }
    }

    void splitIntoBands (const juce::AudioBuffer<float>& input, int numSamples, int numChannels)
    {
        // Use pre-allocated buffer
        remainingBuf.setSize(numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
            remainingBuf.copyFrom(ch, 0, input, ch, 0, numSamples);

        for (int b = 0; b < kNumBands; ++b)
        {
            bandBuffers[b].setSize(numChannels, numSamples, false, false, true);

            if (b < kNumCrossovers)
            {
                // Extract the low part as this band
                for (int ch = 0; ch < numChannels; ++ch)
                    bandBuffers[b].copyFrom(ch, 0, remainingBuf, ch, 0, numSamples);

                juce::dsp::AudioBlock<float> bandBlock (bandBuffers[b]);
                juce::dsp::ProcessContextReplacing<float> ctx (bandBlock);
                lowpass[b].process(ctx);

                // remaining = highpass part
                juce::dsp::AudioBlock<float> remBlock (remainingBuf);
                juce::dsp::ProcessContextReplacing<float> remCtx (remBlock);
                highpass[b].process(remCtx);
            }
            else
            {
                // Last band = whatever is left
                for (int ch = 0; ch < numChannels; ++ch)
                    bandBuffers[b].copyFrom(ch, 0, remainingBuf, ch, 0, numSamples);
            }
        }
    }

    void compressBand (int band, int numSamples, int numChannels)
    {
        auto& p = params[band];

        float threshLin = juce::Decibels::decibelsToGain(p.threshold);
        float attackCoeff  = std::exp(-1.0f / (static_cast<float>(sr) * p.attack  * 0.001f));
        float releaseCoeff = std::exp(-1.0f / (static_cast<float>(sr) * p.release * 0.001f));
        float makeupLin = juce::Decibels::decibelsToGain(p.makeup);

        float maxGR = 0.0f;

        for (int s = 0; s < numSamples; ++s)
        {
            // Compute level (max across channels)
            float level = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                level = juce::jmax(level, std::abs(bandBuffers[band].getSample(ch, s)));

            // Envelope follower
            float coeff = (level > envFollower[band]) ? attackCoeff : releaseCoeff;
            envFollower[band] = coeff * envFollower[band] + (1.0f - coeff) * level;

            // Gain computation
            float env = envFollower[band];
            float gainLin = 1.0f;
            if (env > threshLin && p.ratio > 1.0f)
            {
                float envDb = juce::Decibels::gainToDecibels(env);
                float overDb = envDb - p.threshold;
                float compressedDb = p.threshold + overDb / p.ratio;
                gainLin = juce::Decibels::decibelsToGain(compressedDb - envDb);
            }

            float gr = juce::Decibels::gainToDecibels(gainLin);
            maxGR = juce::jmin(maxGR, gr);

            // Apply gain + makeup + parallel mix
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float dry = bandBuffers[band].getSample(ch, s);
                float wet = dry * gainLin * makeupLin;
                float out = dry * (1.0f - p.mix) + wet * p.mix;
                bandBuffers[band].setSample(ch, s, out);
            }
        }

        gainReduction[band].store(maxGR);
    }
};

} // namespace humvocal
