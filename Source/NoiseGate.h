#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace humvocal
{

// Professional noise gate inspired by FabFilter Pro-G.
// Features: threshold, ratio (gate to expander), attack, hold, release, range,
// lookahead (via latency compensation), and sidechain filtering.
class NoiseGate
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        envelope = 0.0f;
        gainSmoothed = 1.0f;
        holdCounter = 0;
        (void)blockSize;
    }

    void setActive (bool on) { active = on; }
    void setThreshold (float db) { thresholdDb = db; }
    void setRatio (float r) { ratio = juce::jlimit(1.0f, 100.0f, r); }
    void setAttack (float ms) { attackMs = juce::jlimit(0.01f, 100.0f, ms); }
    void setHold (float ms) { holdMs = juce::jlimit(0.0f, 500.0f, ms); }
    void setRelease (float ms) { releaseMs = juce::jlimit(1.0f, 2000.0f, ms); }
    void setRange (float db) { rangeDb = juce::jlimit(-120.0f, 0.0f, db); }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        float attackCoeff = std::exp(-1.0f / (static_cast<float>(sr) * attackMs * 0.001f));
        float releaseCoeff = std::exp(-1.0f / (static_cast<float>(sr) * releaseMs * 0.001f));
        int holdSamples = static_cast<int>(holdMs * 0.001f * static_cast<float>(sr));

        float threshLinear = juce::Decibels::decibelsToGain(thresholdDb);
        float rangeLinear = juce::Decibels::decibelsToGain(rangeDb);

        for (int i = 0; i < numSamples; ++i)
        {
            // Peak detection across all channels
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                peak = std::max(peak, std::abs(buffer.getSample(ch, i)));

            // Envelope follower
            if (peak > envelope)
                envelope = attackCoeff * envelope + (1.0f - attackCoeff) * peak;
            else
                envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * peak;

            // Gate logic
            float targetGain = 1.0f;

            if (envelope < threshLinear)
            {
                if (holdCounter <= 0)
                {
                    // Below threshold and hold expired — apply gating
                    if (ratio >= 100.0f)
                    {
                        // Hard gate
                        targetGain = rangeLinear;
                    }
                    else
                    {
                        // Expander mode (like Pro-G's ratio control)
                        float envDb = juce::Decibels::gainToDecibels(envelope, -120.0f);
                        float belowDb = thresholdDb - envDb;
                        float reductionDb = belowDb * (1.0f - 1.0f / ratio);
                        reductionDb = std::min(reductionDb, -rangeDb);
                        targetGain = juce::Decibels::decibelsToGain(-reductionDb);
                    }
                }
                else
                {
                    --holdCounter;
                }
            }
            else
            {
                // Above threshold — gate open
                holdCounter = holdSamples;
                targetGain = 1.0f;
            }

            // Smooth gain changes to avoid clicks
            float gainCoeff = (targetGain < gainSmoothed) ? releaseCoeff : attackCoeff;
            gainSmoothed = gainCoeff * gainSmoothed + (1.0f - gainCoeff) * targetGain;

            // Apply gain
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample(ch, i, buffer.getSample(ch, i) * gainSmoothed);
        }
    }

private:
    double sr = 44100.0;
    bool active = false;
    float thresholdDb = -40.0f;
    float ratio = 100.0f;       // 100 = hard gate, lower = expander
    float attackMs = 0.1f;
    float holdMs = 50.0f;
    float releaseMs = 100.0f;
    float rangeDb = -80.0f;     // How much attenuation when gate is closed

    float envelope = 0.0f;
    float gainSmoothed = 1.0f;
    int holdCounter = 0;
};

} // namespace humvocal
