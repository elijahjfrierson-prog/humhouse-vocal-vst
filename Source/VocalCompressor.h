#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace humvocal
{

// Studio-grade vocal compressor with soft/hard knee THD modes,
// auto-gain makeup, and an auto-level module for consistency.
class VocalCompressor
{
public:
    void prepare (double sampleRate, int /*blockSize*/)
    {
        sr = sampleRate;
        envelope = 0.0f;
    }

    void setThreshold (float db) { thresholdDb = db; }
    void setRatio (float r) { ratio = std::max(r, 1.0f); }
    void setAttack (float ms) { attackMs = ms; }
    void setRelease (float ms) { releaseMs = ms; }
    void setMakeupGain (float db) { makeupDb = db; }
    void setKnee (float db) { kneeDb = db; }
    void setAutoGain (bool on) { autoGain = on; }
    void setAutoLevel (bool on) { autoLevel = on; }
    void setAutoLevelTarget (float db) { autoLevelTarget = db; }
    void setTHDMode (int mode) { thdMode = mode; } // 0=off, 1=soft, 2=hard
    void setActive (bool on) { active = on; }
    void setOutputGain (float db) { outputGainLin = juce::Decibels::decibelsToGain(db); }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        float attackCoeff  = std::exp(-1.0f / (static_cast<float>(sr) * attackMs * 0.001f));
        float releaseCoeff = std::exp(-1.0f / (static_cast<float>(sr) * releaseMs * 0.001f));
        float threshLin = juce::Decibels::decibelsToGain(thresholdDb);
        float ratioFactor = (ratio > 1.0f) ? (1.0f - 1.0f / ratio) : 0.0f;
        float makeupLinConst = autoGain ? 1.0f : juce::Decibels::decibelsToGain(makeupDb);
        float halfKneeLin = (kneeDb > 0.0f) ? juce::Decibels::decibelsToGain(-kneeDb * 0.5f) : 1.0f;

        float peakLevel = 0.0f;

        // Get raw channel pointers for fast inner loop
        float* chPtrs[2] = { nullptr, nullptr };
        for (int ch = 0; ch < numChannels; ++ch)
            chPtrs[ch] = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            // Detect level (max across channels)
            float inputLevel = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float absVal = std::abs(chPtrs[ch][i]);
                if (absVal > inputLevel) inputLevel = absVal;
            }

            // Envelope follower
            if (inputLevel > envelope)
                envelope = attackCoeff * envelope + (1.0f - attackCoeff) * inputLevel;
            else
                envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * inputLevel;

            // Gain computation — linear domain (avoids per-sample dB conversions)
            float gainLin = 1.0f;
            if (envelope > threshLin && ratioFactor > 0.0f)
            {
                if (kneeDb > 0.0f && envelope < threshLin / halfKneeLin)
                {
                    // Soft knee region — blend
                    float kneeBlend = (envelope - threshLin * halfKneeLin) / (threshLin * (1.0f / halfKneeLin - halfKneeLin));
                    kneeBlend = juce::jlimit(0.0f, 1.0f, kneeBlend);
                    float fullGR = std::pow(threshLin / envelope, ratioFactor);
                    gainLin = 1.0f + kneeBlend * (fullGR - 1.0f);
                }
                else
                {
                    gainLin = std::pow(threshLin / envelope, ratioFactor);
                }
            }

            // Auto-gain makeup: compensate by half the gain reduction
            float makeup = autoGain ? (1.0f / std::sqrt(std::max(gainLin, 0.01f))) : makeupLinConst;

            // THD — add harmonic warmth
            float thdGain = 1.0f;
            if (thdMode > 0)
                thdGain = applyTHD(envelope);

            float combined = gainLin * makeup * thdGain;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                chPtrs[ch][i] *= combined;
                float absOut = std::abs(chPtrs[ch][i]);
                if (absOut > peakLevel) peakLevel = absOut;
            }
        }

        // Auto-level: normalize output to target (once per block)
        if (autoLevel && peakLevel > 0.0f)
        {
            float targetGain = juce::Decibels::decibelsToGain(autoLevelTarget);
            float correction = std::min(targetGain / peakLevel, 6.0f);
            buffer.applyGain(0, numSamples, correction);
        }

        // Output gain
        if (std::abs(outputGainLin - 1.0f) > 0.001f)
            buffer.applyGain(0, numSamples, outputGainLin);
    }

private:
    double sr = 44100.0;
    bool active = true;
    float thresholdDb = -18.0f;
    float ratio = 4.0f;
    float attackMs = 5.0f;
    float releaseMs = 50.0f;
    float makeupDb = 0.0f;
    float kneeDb = 6.0f;
    bool autoGain = true;
    bool autoLevel = false;
    float autoLevelTarget = -14.0f;
    int thdMode = 0;
    float envelope = 0.0f;
    float outputGainLin = 1.0f;

    float computeGainReduction (float inputDb) const
    {
        float overDb = inputDb - thresholdDb;

        // Soft knee
        if (kneeDb > 0.0f && overDb > -kneeDb * 0.5f && overDb < kneeDb * 0.5f)
        {
            float x = overDb + kneeDb * 0.5f;
            overDb = x * x / (2.0f * kneeDb);
        }
        else
        {
            overDb = std::max(overDb, 0.0f);
        }

        return -overDb * (1.0f - 1.0f / ratio);
    }

    float applyTHD (float level) const
    {
        if (thdMode == 1) // Soft — even harmonics (tube warmth)
            return 1.0f + 0.05f * level;
        if (thdMode == 2) // Hard — odd harmonics (transistor grit)
            return 1.0f + 0.1f * level * level;
        return 1.0f;
    }
};

} // namespace humvocal
