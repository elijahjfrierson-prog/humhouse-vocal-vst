#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>

namespace humvocal
{

// Real-time pitch correction engine using autocorrelation-based pitch
// detection (YIN variant) with PSOLA resynthesis.
// Optimized for low CPU: runs YIN at reduced rate, uses properly normalized
// overlap-add for artifact-free output.
class PitchEngine
{
public:
    // Scale bitmasks — 12 bools for C..B
    static constexpr std::array<bool, 12> kMajor = {true,false,true,false,true,true,false,true,false,true,false,true};
    static constexpr std::array<bool, 12> kMinor = {true,false,true,true,false,true,false,true,true,false,true,false};
    static constexpr std::array<bool, 12> kChromatic = {true,true,true,true,true,true,true,true,true,true,true,true};

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        maxBlock = blockSize;

        // YIN analysis window — target ~60 Hz minimum (vocal low end)
        // Smaller than before to reduce CPU (was sr/30*2 = 3200 at 48k)
        yinBufferSize = static_cast<int>(sr / 60.0) * 2; // ~1600 at 48k
        yinBuffer.resize(static_cast<size_t>(yinBufferSize), 0.0f);

        // Circular input buffer for overlap analysis
        inputRing.resize(static_cast<size_t>(yinBufferSize * 2), 0.0f);
        ringWritePos = 0;

        // PSOLA output overlap buffer
        olaBuffer.assign(static_cast<size_t>(maxBlock + 2048), 0.0f);
        olaWindow.assign(static_cast<size_t>(maxBlock + 2048), 0.0f);

        // Precompute Hann window table
        constexpr int kWindowSize = 512;
        windowTable.resize(kWindowSize);
        for (int i = 0; i < kWindowSize; ++i)
            windowTable[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) / static_cast<float>(kWindowSize)));

        smoothedPitch = 0.0;
        detectedHistory.fill(0.0f);
        histIdx = 0;
        yinSkipCounter = 0;
        lastDetectedHz = 0.0f;
        lastTargetHz = 0.0f;
        lastCorrectionCents = 0.0f;
    }

    void setReferenceFrequency (float hz) { referenceFreq = hz; }
    void setRootNote (int note) { rootNote = note % 12; }
    void setScaleType (int type) { scaleType = type; }
    void setRetuneSpeed (float speed01) { retuneSpeed = speed01; }
    void setHumanize (float h) { humanize = h; }
    void setSnapAmount (float s) { snapAmount = s; }
    void setPitchSustain (float s) { pitchSustain = s; }
    void setNoteStabilizer (bool on) { stabilizer = on; }
    void setFormantPreserve (bool /*on*/) { /* removed - no-op */ }

    float getDetectedPitchHz() const { return lastDetectedHz; }
    float getTargetPitchHz() const { return lastTargetHz; }
    float getCorrectionCents() const { return lastCorrectionCents; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        if (numChannels == 0 || numSamples == 0 || sr <= 0.0)
            return;

        // Feed channel 0 into ring buffer for pitch detection
        const float* readPtr = buffer.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            inputRing[static_cast<size_t>(ringWritePos)] = readPtr[i];
            ringWritePos = (ringWritePos + 1) % static_cast<int>(inputRing.size());
        }

        // Run YIN only every N blocks to save CPU (detection doesn't need to be per-block)
        float detectedHz = cachedDetectedHz;
        ++yinSkipCounter;
        if (yinSkipCounter >= kYinSkipBlocks)
        {
            yinSkipCounter = 0;
            detectedHz = detectPitchYIN();
            cachedDetectedHz = detectedHz;
        }
        lastDetectedHz = detectedHz;

        if (detectedHz < 60.0f || detectedHz > 2000.0f)
        {
            lastTargetHz = detectedHz;
            lastCorrectionCents = 0.0f;
            return; // Outside vocal range, pass through unmodified
        }

        // Note stabilizer
        if (stabilizer)
        {
            detectedHistory[static_cast<size_t>(histIdx)] = detectedHz;
            histIdx = (histIdx + 1) % kHistorySize;

            float avg = 0.0f;
            for (auto v : detectedHistory) avg += v;
            avg /= static_cast<float>(kHistorySize);

            if (avg > 0.0f)
            {
                float centsDiff = 1200.0f * std::log2(detectedHz / avg);
                if (std::abs(centsDiff) < 20.0f * pitchSustain)
                    detectedHz = avg;
            }
        }

        // Find target note in scale
        float targetHz = findTargetFrequency(detectedHz);
        lastTargetHz = targetHz;

        // Correction in cents
        float correctionCents = 1200.0f * std::log2(targetHz / detectedHz);
        lastCorrectionCents = correctionCents;

        // Apply humanize and snap
        correctionCents *= (1.0f - humanize);
        correctionCents *= snapAmount;

        // Smooth the correction (retune speed)
        float speedCoeff = std::exp(-1.0f / (static_cast<float>(sr) * (0.001f + (1.0f - retuneSpeed) * 0.1f)));
        smoothedPitch = smoothedPitch * speedCoeff + static_cast<double>(correctionCents) * (1.0 - speedCoeff);

        // If correction is tiny, skip pitch shifting entirely
        if (std::abs(static_cast<float>(smoothedPitch)) < 1.0f)
            return;

        float shiftRatio = std::pow(2.0f, static_cast<float>(smoothedPitch) / 1200.0f);

        // Apply pitch shift to all channels
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            applyPitchShift(data, numSamples, shiftRatio);
        }
    }

private:
    static constexpr int kHistorySize = 8;
    static constexpr int kYinSkipBlocks = 3; // Only run YIN every 3 blocks

    double sr = 44100.0;
    int maxBlock = 512;
    int yinBufferSize = 0;
    std::vector<float> yinBuffer;
    std::vector<float> inputRing;
    int ringWritePos = 0;
    std::vector<float> olaBuffer;   // overlap-add output
    std::vector<float> olaWindow;   // overlap-add normalization
    std::vector<float> windowTable;

    float referenceFreq = 440.0f;
    int rootNote = 0;
    int scaleType = 0;
    float retuneSpeed = 0.5f;
    float humanize = 0.0f;
    float snapAmount = 1.0f;
    float pitchSustain = 0.5f;
    bool stabilizer = true;

    double smoothedPitch = 0.0;
    int yinSkipCounter = 0;
    float cachedDetectedHz = 0.0f;

    float lastDetectedHz = 0.0f;
    float lastTargetHz = 0.0f;
    float lastCorrectionCents = 0.0f;

    std::array<float, kHistorySize> detectedHistory {};
    int histIdx = 0;

    float detectPitchYIN()
    {
        const int W = yinBufferSize / 2;
        if (W < 2) return 0.0f;

        // Step 1: Difference function
        for (int tau = 0; tau < W; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < W; ++j)
            {
                int idx1 = (ringWritePos - W + j + static_cast<int>(inputRing.size())) % static_cast<int>(inputRing.size());
                int idx2 = (idx1 + tau) % static_cast<int>(inputRing.size());
                float diff = inputRing[static_cast<size_t>(idx1)] - inputRing[static_cast<size_t>(idx2)];
                sum += diff * diff;
            }
            yinBuffer[static_cast<size_t>(tau)] = sum;
        }

        // Step 2: Cumulative mean normalized difference
        yinBuffer[0] = 1.0f;
        float runningSum = 0.0f;
        for (int tau = 1; tau < W; ++tau)
        {
            runningSum += yinBuffer[static_cast<size_t>(tau)];
            yinBuffer[static_cast<size_t>(tau)] *= static_cast<float>(tau) / (runningSum > 0.0f ? runningSum : 1.0f);
        }

        // Step 3: Absolute threshold
        constexpr float threshold = 0.15f;
        int tauEstimate = -1;
        for (int tau = 2; tau < W; ++tau)
        {
            if (yinBuffer[static_cast<size_t>(tau)] < threshold)
            {
                while (tau + 1 < W && yinBuffer[static_cast<size_t>(tau + 1)] < yinBuffer[static_cast<size_t>(tau)])
                    ++tau;
                tauEstimate = tau;
                break;
            }
        }

        if (tauEstimate < 1)
            return 0.0f;

        // Step 4: Parabolic interpolation
        float betterTau = static_cast<float>(tauEstimate);
        if (tauEstimate > 0 && tauEstimate < W - 1)
        {
            float s0 = yinBuffer[static_cast<size_t>(tauEstimate - 1)];
            float s1 = yinBuffer[static_cast<size_t>(tauEstimate)];
            float s2 = yinBuffer[static_cast<size_t>(tauEstimate + 1)];
            float denom = 2.0f * (2.0f * s1 - s2 - s0);
            if (std::abs(denom) > 1e-9f)
                betterTau += (s0 - s2) / denom;
        }

        if (betterTau <= 0.0f)
            return 0.0f;

        return static_cast<float>(sr) / betterTau;
    }

    float findTargetFrequency (float detectedHz)
    {
        float midiNote = 69.0f + 12.0f * std::log2(detectedHz / referenceFreq);

        const auto& scale = (scaleType == 1) ? kMinor
                          : (scaleType == 2) ? kChromatic
                          : kMajor;

        int nearestMidi = static_cast<int>(std::round(midiNote));
        int noteInOctave = ((nearestMidi % 12) - rootNote + 12) % 12;

        if (!scale[static_cast<size_t>(noteInOctave)])
        {
            for (int offset = 1; offset <= 6; ++offset)
            {
                int up = (noteInOctave + offset) % 12;
                int down = (noteInOctave - offset + 12) % 12;
                if (scale[static_cast<size_t>(up)])  { nearestMidi += offset; break; }
                if (scale[static_cast<size_t>(down)]) { nearestMidi -= offset; break; }
            }
        }

        return referenceFreq * std::pow(2.0f, (static_cast<float>(nearestMidi) - 69.0f) / 12.0f);
    }

    void applyPitchShift (float* data, int numSamples, float ratio)
    {
        if (std::abs(ratio - 1.0f) < 0.001f)
            return;

        // Properly normalized overlap-add (OLA) pitch shifting
        const int grainSize = 256;
        const int hopSize = grainSize / 4; // 75% overlap for smooth output

        int olaLen = numSamples + grainSize;
        if (static_cast<int>(olaBuffer.size()) < olaLen)
        {
            olaBuffer.resize(static_cast<size_t>(olaLen), 0.0f);
            olaWindow.resize(static_cast<size_t>(olaLen), 0.0f);
        }

        // Clear OLA accumulators
        std::fill(olaBuffer.begin(), olaBuffer.begin() + olaLen, 0.0f);
        std::fill(olaWindow.begin(), olaWindow.begin() + olaLen, 0.0f);

        for (int pos = 0; pos < numSamples; pos += hopSize)
        {
            int grainLen = std::min(grainSize, numSamples - pos);

            for (int i = 0; i < grainLen; ++i)
            {
                // Read from input at shifted rate
                float srcPos = static_cast<float>(i) * ratio;
                int srcBase = pos + static_cast<int>(srcPos);
                float frac = srcPos - std::floor(srcPos);

                float sample = 0.0f;
                if (srcBase >= 0 && srcBase < numSamples - 1)
                {
                    sample = data[srcBase] * (1.0f - frac) + data[srcBase + 1] * frac;
                }
                else if (srcBase >= 0 && srcBase < numSamples)
                {
                    sample = data[srcBase];
                }

                // Window lookup
                float windowPos = static_cast<float>(i) / static_cast<float>(grainLen) * static_cast<float>(windowTable.size() - 1);
                int wIdx = juce::jlimit(0, static_cast<int>(windowTable.size()) - 2, static_cast<int>(windowPos));
                float wFrac = windowPos - static_cast<float>(wIdx);
                float window = windowTable[static_cast<size_t>(wIdx)] * (1.0f - wFrac) + windowTable[static_cast<size_t>(wIdx + 1)] * wFrac;

                // Accumulate with window
                olaBuffer[static_cast<size_t>(pos + i)] += sample * window;
                olaWindow[static_cast<size_t>(pos + i)] += window;
            }
        }

        // Normalize by accumulated window weights (prevents amplitude artifacts)
        for (int i = 0; i < numSamples; ++i)
        {
            float norm = olaWindow[static_cast<size_t>(i)];
            if (norm > 0.001f)
                data[i] = olaBuffer[static_cast<size_t>(i)] / norm;
            // else leave original sample (fallback)
        }
    }
};

} // namespace humvocal
