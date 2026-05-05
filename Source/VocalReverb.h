#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>

namespace humvocal
{

// High-quality vocal reverb using an 8-channel Feedback Delay Network (FDN)
// with early reflections, modulated delay lines, and musical damping.
// Inspired by FabFilter Pro-R's architecture — smooth, dense, never metallic.
class VocalReverb
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        duckEnvelope = 0.0f;

        // Initialize delay lines for the FDN (prime-ish lengths for density)
        const int baseLengths[kFDNSize] = { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };
        for (int i = 0; i < kFDNSize; ++i)
        {
            int len = static_cast<int>(baseLengths[i] * sr / 44100.0);
            fdnDelayLen[i] = len;
            fdnBuffers[i].assign(static_cast<size_t>(len + 256), 0.0f);
            fdnWritePos[i] = 0;
            fdnState[i] = 0.0f;
            fdnLPState[i] = 0.0f;
            fdnHPState[i] = 0.0f;
        }

        // Early reflections (short tapped delay line)
        int erLen = static_cast<int>(0.08 * sr); // 80ms max
        earlyBuffer[0].assign(static_cast<size_t>(erLen), 0.0f);
        earlyBuffer[1].assign(static_cast<size_t>(erLen), 0.0f);
        earlyWritePos = 0;
        earlyBufLen = erLen;

        // Pre-delay buffer
        int pdLen = static_cast<int>(0.25 * sr); // 250ms max
        preDelayBuf[0].assign(static_cast<size_t>(pdLen), 0.0f);
        preDelayBuf[1].assign(static_cast<size_t>(pdLen), 0.0f);
        preDelayWritePos = 0;
        preDelayBufLen = pdLen;

        // Modulation LFO
        lfoPhase = 0.0f;

        // Pre-allocate output buffer
        wetBuf.setSize(2, blockSize);

        updateDecayCoeffs();
    }

    // API compatible with existing processor code
    void setShortSize (float s)    { space = juce::jlimit(0.0f, 1.0f, s); updateDecayCoeffs(); }
    void setShortDamping (float d) { brightness = 1.0f - juce::jlimit(0.0f, 1.0f, d); updateDecayCoeffs(); }
    void setShortMix (float m)     { shortMix = juce::jlimit(0.0f, 1.0f, m); }

    void setLongSize (float s)     { decayTime = juce::jlimit(0.1f, 10.0f, s * 10.0f); updateDecayCoeffs(); }
    void setLongDamping (float d)  { (void)d; } // handled by brightness
    void setLongMix (float m)      { longMix = juce::jlimit(0.0f, 1.0f, m); }

    void setDuckAmount (float d)   { duckAmount = juce::jlimit(0.0f, 1.0f, d); }
    void setPostEQFreq (float hz)  { postEQCutoff = juce::jlimit(1000.0f, 20000.0f, hz); updateDecayCoeffs(); }
    void setActive (bool on)       { active = on; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (!active) return;

        float totalMix = shortMix + longMix;
        if (totalMix < 0.001f) return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = juce::jmin(buffer.getNumChannels(), 2);

        wetBuf.setSize(numChannels, numSamples, false, false, true);
        wetBuf.clear();

        float* dryPtrs[2] = { nullptr, nullptr };
        float* wetPtrs[2] = { nullptr, nullptr };
        for (int ch = 0; ch < numChannels; ++ch)
        {
            dryPtrs[ch] = buffer.getWritePointer(ch);
            wetPtrs[ch] = wetBuf.getWritePointer(ch);
        }

        // LFO rate for modulation (~0.7 Hz, subtle chorus on tail)
        float lfoInc = 0.7f / static_cast<float>(sr);

        // Ducking coefficients
        float duckAttack  = std::exp(-1.0f / (static_cast<float>(sr) * 0.005f));
        float duckRelease = std::exp(-1.0f / (static_cast<float>(sr) * 0.150f));

        // Pre-delay in samples
        int preDelaySamples = static_cast<int>(preDelayMs * 0.001f * static_cast<float>(sr));
        preDelaySamples = juce::jmin(preDelaySamples, preDelayBufLen - 1);

        for (int i = 0; i < numSamples; ++i)
        {
            // Input: mono sum
            float inputL = (numChannels > 0) ? dryPtrs[0][i] : 0.0f;
            float inputR = (numChannels > 1) ? dryPtrs[1][i] : inputL;
            float monoIn = (inputL + inputR) * 0.5f;

            // Pre-delay
            preDelayBuf[0][static_cast<size_t>(preDelayWritePos)] = inputL;
            preDelayBuf[1][static_cast<size_t>(preDelayWritePos)] = inputR;
            int pdReadPos = (preDelayWritePos - preDelaySamples + preDelayBufLen) % preDelayBufLen;
            float pdL = preDelayBuf[0][static_cast<size_t>(pdReadPos)];
            float pdR = preDelayBuf[1][static_cast<size_t>(pdReadPos)];
            preDelayWritePos = (preDelayWritePos + 1) % preDelayBufLen;

            float pdMono = (pdL + pdR) * 0.5f;

            // --- Early reflections (tapped delay) ---
            earlyBuffer[0][static_cast<size_t>(earlyWritePos)] = pdL;
            earlyBuffer[1][static_cast<size_t>(earlyWritePos)] = pdR;

            float erL = 0.0f, erR = 0.0f;
            // 6 taps at musically spaced intervals
            const float erTapTimes[6] = { 0.007f, 0.013f, 0.019f, 0.029f, 0.037f, 0.053f };
            const float erTapGains[6] = { 0.75f, 0.55f, 0.45f, 0.35f, 0.25f, 0.18f };
            const float erTapPanL[6]  = { 0.8f, 0.3f, 0.7f, 0.4f, 0.6f, 0.5f };

            for (int t = 0; t < 6; ++t)
            {
                int tapDelay = static_cast<int>(erTapTimes[t] * static_cast<float>(sr) * (0.3f + space * 0.7f));
                tapDelay = juce::jmin(tapDelay, earlyBufLen - 1);
                int readPos = (earlyWritePos - tapDelay + earlyBufLen) % earlyBufLen;
                float tapSample = earlyBuffer[0][static_cast<size_t>(readPos)] * erTapGains[t];
                erL += tapSample * erTapPanL[t];
                erR += tapSample * (1.0f - erTapPanL[t]);
            }
            earlyWritePos = (earlyWritePos + 1) % earlyBufLen;

            // --- FDN Late reverb ---
            // Modulation (subtle pitch variation prevents metallic ringing)
            float lfoVal = std::sin(lfoPhase * 6.2831853f);
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

            // Read from FDN delay lines
            float fdnOut[kFDNSize];
            for (int n = 0; n < kFDNSize; ++n)
            {
                // Modulated read position (±3 samples)
                int modOffset = static_cast<int>(lfoVal * 3.0f * ((n & 1) ? 1.0f : -1.0f));
                int readPos = (fdnWritePos[n] - fdnDelayLen[n] + modOffset + static_cast<int>(fdnBuffers[n].size())) % static_cast<int>(fdnBuffers[n].size());
                fdnOut[n] = fdnBuffers[n][static_cast<size_t>(readPos)];

                // Damping: one-pole LP + HP per delay line
                fdnLPState[n] += lpCoeff * (fdnOut[n] - fdnLPState[n]);
                fdnHPState[n] += hpCoeff * (fdnLPState[n] - fdnHPState[n]);
                fdnOut[n] = fdnLPState[n] - fdnHPState[n] * 0.3f;
            }

            // Hadamard-like mixing matrix (unitary for energy preservation)
            float mixed[kFDNSize];
            for (int n = 0; n < kFDNSize; n += 2)
            {
                mixed[n]     = (fdnOut[n] + fdnOut[n + 1]) * 0.7071f;
                mixed[n + 1] = (fdnOut[n] - fdnOut[n + 1]) * 0.7071f;
            }
            // Second butterfly stage
            float mixed2[kFDNSize];
            for (int n = 0; n < kFDNSize; n += 4)
            {
                mixed2[n]     = (mixed[n] + mixed[n + 2]) * 0.7071f;
                mixed2[n + 1] = (mixed[n + 1] + mixed[n + 3]) * 0.7071f;
                mixed2[n + 2] = (mixed[n] - mixed[n + 2]) * 0.7071f;
                mixed2[n + 3] = (mixed[n + 1] - mixed[n + 3]) * 0.7071f;
            }

            // Write back into delay lines with feedback + input injection
            for (int n = 0; n < kFDNSize; ++n)
            {
                float input = pdMono * inputGains[n];
                float fb = mixed2[n] * decayGain;
                fdnBuffers[n][static_cast<size_t>(fdnWritePos[n])] = input + fb;
                fdnWritePos[n] = (fdnWritePos[n] + 1) % static_cast<int>(fdnBuffers[n].size());
            }

            // Sum FDN outputs to stereo (alternating pan)
            float lateL = 0.0f, lateR = 0.0f;
            for (int n = 0; n < kFDNSize; ++n)
            {
                if (n & 1)
                    lateR += mixed2[n];
                else
                    lateL += mixed2[n];
            }
            lateL *= 0.25f;
            lateR *= 0.25f;

            // Stereo width on late reverb
            float lateMid  = (lateL + lateR) * 0.5f;
            float lateSide = (lateL - lateR) * 0.5f * stereoWidth;
            lateL = lateMid + lateSide;
            lateR = lateMid - lateSide;

            // --- Combine early + late ---
            float wetL = erL * shortMix + lateL * longMix;
            float wetR = erR * shortMix + lateR * longMix;

            // --- Ducking ---
            float dryLevel = std::max(std::abs(inputL), std::abs(inputR));
            if (dryLevel > duckEnvelope)
                duckEnvelope = duckAttack * duckEnvelope + (1.0f - duckAttack) * dryLevel;
            else
                duckEnvelope = duckRelease * duckEnvelope + (1.0f - duckRelease) * dryLevel;
            float duckGain = 1.0f - duckAmount * juce::jlimit(0.0f, 1.0f, duckEnvelope * 3.0f);

            // Write wet output
            if (numChannels > 0) wetPtrs[0][i] = wetL * duckGain;
            if (numChannels > 1) wetPtrs[1][i] = wetR * duckGain;
        }

        // Add wet to dry
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.addFrom(ch, 0, wetBuf, ch, 0, numSamples);
    }

private:
    static constexpr int kFDNSize = 8;

    double sr = 44100.0;
    bool active = true;

    // User parameters
    float shortMix = 0.2f;       // early reflections level
    float longMix  = 0.15f;      // late reverb level
    float space = 0.3f;          // room size (affects ER spacing + FDN character)
    float brightness = 0.5f;     // high-frequency content in tail
    float decayTime = 1.5f;      // RT60 in seconds
    float duckAmount = 0.5f;
    float postEQCutoff = 8000.0f;
    float preDelayMs = 10.0f;
    float stereoWidth = 1.0f;

    // Derived coefficients
    float decayGain = 0.85f;
    float lpCoeff = 0.3f;
    float hpCoeff = 0.01f;

    // FDN state
    std::array<std::vector<float>, kFDNSize> fdnBuffers;
    std::array<int, kFDNSize> fdnDelayLen {};
    std::array<int, kFDNSize> fdnWritePos {};
    std::array<float, kFDNSize> fdnState {};
    std::array<float, kFDNSize> fdnLPState {};
    std::array<float, kFDNSize> fdnHPState {};

    // Input gains for each FDN channel (varied for decorrelation)
    const float inputGains[kFDNSize] = { 0.25f, 0.22f, 0.20f, 0.18f, 0.23f, 0.21f, 0.19f, 0.24f };

    // Early reflections
    std::array<std::vector<float>, 2> earlyBuffer;
    int earlyWritePos = 0;
    int earlyBufLen = 0;

    // Pre-delay
    std::array<std::vector<float>, 2> preDelayBuf;
    int preDelayWritePos = 0;
    int preDelayBufLen = 0;

    // Modulation
    float lfoPhase = 0.0f;

    // Ducking
    float duckEnvelope = 0.0f;

    // Wet output buffer
    juce::AudioBuffer<float> wetBuf;

    void updateDecayCoeffs()
    {
        if (sr <= 0) return;

        // Decay gain per sample from RT60 target
        // Longer delay lines need less feedback to achieve same RT60
        float avgDelaySeconds = 0.0f;
        for (int i = 0; i < kFDNSize; ++i)
            avgDelaySeconds += static_cast<float>(fdnDelayLen[i]);
        avgDelaySeconds /= (kFDNSize * static_cast<float>(sr));

        if (avgDelaySeconds > 0.0f && decayTime > 0.0f)
            decayGain = std::pow(0.001f, avgDelaySeconds / decayTime);
        else
            decayGain = 0.85f;

        decayGain = juce::jlimit(0.0f, 0.998f, decayGain);

        // LP coefficient controls brightness (higher = more damping of highs)
        float cutoffNorm = postEQCutoff / static_cast<float>(sr);
        lpCoeff = 1.0f - std::exp(-6.2831853f * cutoffNorm * brightness);
        lpCoeff = juce::jlimit(0.05f, 0.95f, lpCoeff);

        // HP coefficient removes low-end mud from tail
        hpCoeff = 0.005f + (1.0f - space) * 0.02f;
    }
};

} // namespace humvocal
