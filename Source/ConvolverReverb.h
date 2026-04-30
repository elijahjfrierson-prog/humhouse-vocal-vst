#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include <random>

namespace humvocal
{

// Convolver reverb with reverse-reverb mode (FL Studio-style).
// Generates synthetic impulse responses at prepare() time so no
// external WAV files are needed. CPU-friendly: the convolution
// uses JUCE's uniform-partitioned FFT engine and the IR is only
// rebuilt when the user changes size/damping/reverse.
class ConvolverReverb
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        block = blockSize;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(blockSize), 2 };
        convolution.prepare(spec);
        wetBuffer.setSize(2, blockSize);
        rebuildIR();
    }

    void setActive (bool on)  { active = on; }
    void setMix (float m)     { mix = juce::jlimit(0.0f, 1.0f, m); }
    void setPreDelay (float ms)
    {
        float newPD = juce::jlimit(0.0f, 200.0f, ms);
        if (newPD != preDelayMs) { preDelayMs = newPD; dirty = true; }
    }
    void setSize (float s)
    {
        float newS = juce::jlimit(0.1f, 6.0f, s);
        if (newS != decaySec) { decaySec = newS; dirty = true; }
    }
    void setDamping (float d)
    {
        float newD = juce::jlimit(0.0f, 1.0f, d);
        if (newD != damping) { damping = newD; dirty = true; }
    }
    void setReverse (bool r)
    {
        if (r != reverse) { reverse = r; dirty = true; }
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) return;

        if (dirty)
        {
            rebuildIR();
            dirty = false;
        }

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        wetBuffer.setSize(numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
            wetBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        juce::dsp::AudioBlock<float> wetBlock(wetBuffer);
        juce::dsp::ProcessContextReplacing<float> ctx(wetBlock);
        convolution.process(ctx);

        // Mix wet into dry
        float wet = mix;
        float dry = 1.0f;  // keep full dry, add wet on top
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* out = buffer.getWritePointer(ch);
            const float* wetData = wetBuffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                out[i] = out[i] * dry + wetData[i] * wet;
        }
    }

private:
    double sr = 44100.0;
    int block = 512;
    bool active = false;
    float mix = 0.3f;
    float preDelayMs = 0.0f;
    float decaySec = 1.5f;
    float damping = 0.5f;
    bool reverse = false;
    bool dirty = true;

    juce::dsp::Convolution convolution;
    juce::AudioBuffer<float> wetBuffer;

    void rebuildIR()
    {
        if (sr <= 0) return;

        int irLen = static_cast<int>(decaySec * sr);
        irLen = std::max(irLen, 256);
        // Cap at 4 seconds to keep memory and CPU sane
        irLen = std::min(irLen, static_cast<int>(4.0 * sr));

        int preDelaySamples = static_cast<int>(preDelayMs * 0.001f * static_cast<float>(sr));

        int totalLen = preDelaySamples + irLen;
        juce::AudioBuffer<float> ir(1, totalLen);
        ir.clear();

        float* data = ir.getWritePointer(0);

        // Generate synthetic IR: filtered noise with exponential decay
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        float decay60 = decaySec; // RT60
        float decayRate = -6.9078f / (decay60 * static_cast<float>(sr)); // ln(0.001) / (RT60 * sr)

        // LP filter state for damping (simple one-pole)
        float lpCoeff = 1.0f - damping * 0.9f; // higher damping = more filtering
        float lpState = 0.0f;

        for (int i = 0; i < irLen; ++i)
        {
            float noise = dist(rng);
            lpState = lpState + lpCoeff * (noise - lpState);
            float envelope = std::exp(decayRate * static_cast<float>(i));
            data[preDelaySamples + i] = lpState * envelope;
        }

        // Normalize IR
        float peak = 0.0f;
        for (int i = 0; i < totalLen; ++i)
            peak = std::max(peak, std::abs(data[i]));
        if (peak > 0.0f)
        {
            float norm = 0.5f / peak;
            for (int i = 0; i < totalLen; ++i)
                data[i] *= norm;
        }

        // Reverse if enabled (FL Studio reverse reverb effect)
        if (reverse)
        {
            for (int i = 0; i < totalLen / 2; ++i)
                std::swap(data[i], data[totalLen - 1 - i]);
        }

        // Make stereo (identical L/R for mono compatibility)
        juce::AudioBuffer<float> stereoIR(2, totalLen);
        stereoIR.copyFrom(0, 0, ir, 0, 0, totalLen);
        stereoIR.copyFrom(1, 0, ir, 0, 0, totalLen);

        convolution.loadImpulseResponse(
            std::move(stereoIR), sr,
            juce::dsp::Convolution::Stereo::yes,
            juce::dsp::Convolution::Trim::no,
            juce::dsp::Convolution::Normalise::no);
    }
};

} // namespace humvocal
