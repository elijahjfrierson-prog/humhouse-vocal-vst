#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>

namespace humvocal
{

// Real-time pitch correction engine.
// Detection: YIN autocorrelation (efficient, runs every N blocks).
// Shifting: Per-channel dual-head ring buffer with cosine crossfade — zero-click,
// artifact-free pitch shifting suitable for real-time vocal processing.
class PitchEngine
{
public:
    static constexpr std::array<bool, 12> kMajor = {true,false,true,false,true,true,false,true,false,true,false,true};
    static constexpr std::array<bool, 12> kMinor = {true,false,true,true,false,true,false,true,true,false,true,false};
    static constexpr std::array<bool, 12> kChromatic = {true,true,true,true,true,true,true,true,true,true,true,true};

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        maxBlock = blockSize;

        // Per-channel ring buffers for pitch shifting
        ringSize = 4096;
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            channels[ch].ringBuffer.assign(static_cast<size_t>(ringSize), 0.0f);
            channels[ch].writePos = 0;
            channels[ch].readPosA = 0.0;
            channels[ch].readPosB = static_cast<double>(ringSize) / 2.0;
        }

        // Shared ratio state (same pitch correction for all channels)
        smoothedRatio = 1.0;
        targetRatio = 1.0;

        // YIN detection buffer (~60 Hz minimum)
        yinBufferSize = static_cast<int>(sr / 60.0) * 2;
        yinBuffer.resize(static_cast<size_t>(yinBufferSize), 0.0f);
        inputRing.resize(static_cast<size_t>(yinBufferSize * 2), 0.0f);
        yinWritePos = 0;

        // State reset
        yinSkipCounter = 0;
        cachedDetectedHz = 0.0f;
        lastDetectedHz = 0.0f;
        lastTargetHz = 0.0f;
        lastCorrectionCents = 0.0f;
        detectedHistory.fill(0.0f);
        histIdx = 0;
    }

    void setReferenceFrequency (float hz) { referenceFreq = hz; }
    void setRootNote (int note) { rootNote = note % 12; }
    void setScaleType (int type) { scaleType = type; }
    void setRetuneSpeed (float speed01) { retuneSpeed = speed01; }
    void setHumanize (float h) { humanize = h; }
    void setSnapAmount (float s) { snapAmount = s; }
    void setPitchSustain (float s) { pitchSustain = s; }
    void setNoteStabilizer (bool on) { stabilizer = on; }
    void setFormantPreserve (bool /*on*/) { /* no-op */ }

    float getDetectedPitchHz() const { return lastDetectedHz; }
    float getTargetPitchHz() const { return lastTargetHz; }
    float getCorrectionCents() const { return lastCorrectionCents; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);

        if (numChannels == 0 || numSamples == 0 || sr <= 0.0)
            return;

        // Feed channel 0 into YIN ring buffer for pitch detection
        const float* ch0 = buffer.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            inputRing[static_cast<size_t>(yinWritePos)] = ch0[i];
            yinWritePos = (yinWritePos + 1) % static_cast<int>(inputRing.size());
        }

        // Run YIN pitch detection periodically
        float detectedHz = cachedDetectedHz;
        ++yinSkipCounter;
        if (yinSkipCounter >= kYinSkipBlocks)
        {
            yinSkipCounter = 0;
            detectedHz = detectPitchYIN();
            cachedDetectedHz = detectedHz;
        }
        lastDetectedHz = detectedHz;

        // Determine pitch shift ratio
        double newTargetRatio = 1.0;

        if (detectedHz >= 60.0f && detectedHz <= 2000.0f)
        {
            float stableHz = detectedHz;

            // Note stabilizer — locks onto a note and holds it
            if (stabilizer)
            {
                detectedHistory[static_cast<size_t>(histIdx)] = detectedHz;
                histIdx = (histIdx + 1) % kHistorySize;

                float avg = 0.0f;
                int count = 0;
                for (auto v : detectedHistory)
                {
                    if (v > 0.0f) { avg += v; ++count; }
                }
                if (count > 0)
                {
                    avg /= static_cast<float>(count);
                    float centsDiff = 1200.0f * std::log2(detectedHz / avg);
                    // Wider lock zone: holds note more aggressively
                    // pitchSustain 0-1 maps to 30-80 cent lock zone
                    float lockZone = 30.0f + pitchSustain * 50.0f;
                    if (std::abs(centsDiff) < lockZone)
                        stableHz = avg;
                }
            }

            // Find target note
            float targetHz = findTargetFrequency(stableHz);
            lastTargetHz = targetHz;

            // Correction in cents
            float corrCents = 1200.0f * std::log2(targetHz / stableHz);
            lastCorrectionCents = corrCents;

            // Apply humanize and snap
            corrCents *= (1.0f - humanize);
            corrCents *= snapAmount;

            // Apply correction if above noise floor (0.5 cents)
            if (std::abs(corrCents) > 0.5f)
                newTargetRatio = std::pow(2.0, static_cast<double>(corrCents) / 1200.0);
        }
        else
        {
            lastTargetHz = detectedHz;
            lastCorrectionCents = 0.0f;
        }

        // Smooth ratio change (retune speed controls smoothing)
        // Speed 0.0 → 50ms convergence (natural), Speed 1.0 → 0.5ms (robotic/instant)
        // Using exponential mapping for musical feel
        double timeConstant = 0.0005 + (1.0 - static_cast<double>(retuneSpeed)) * (1.0 - static_cast<double>(retuneSpeed)) * 0.05;
        double smoothCoeff = 1.0 - std::exp(-1.0 / (sr * timeConstant));
        targetRatio = newTargetRatio;

        // Apply pitch shift — process all channels simultaneously per sample
        // This ensures shared smoothedRatio advances once per sample (not per channel)
        for (int i = 0; i < numSamples; ++i)
        {
            // Advance smoothed ratio once per sample
            smoothedRatio += smoothCoeff * (targetRatio - smoothedRatio);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& state = channels[ch];
                float* data = buffer.getWritePointer(ch);

                // Write input to this channel's ring buffer
                state.ringBuffer[static_cast<size_t>(state.writePos)] = data[i];

                // Advance this channel's read pointers at shifted rate
                state.readPosA += smoothedRatio;
                state.readPosB += smoothedRatio;

                // Wrap read positions
                if (state.readPosA >= static_cast<double>(ringSize)) state.readPosA -= static_cast<double>(ringSize);
                if (state.readPosB >= static_cast<double>(ringSize)) state.readPosB -= static_cast<double>(ringSize);
                if (state.readPosA < 0.0) state.readPosA += static_cast<double>(ringSize);
                if (state.readPosB < 0.0) state.readPosB += static_cast<double>(ringSize);

                // Read with linear interpolation from both heads
                float sampleA = readInterpolated(state.ringBuffer, state.readPosA);
                float sampleB = readInterpolated(state.ringBuffer, state.readPosB);

                // Crossfade based on distance from write pointer
                float fadeA = computeCrossfade(state.readPosA, state.writePos);
                float fadeB = computeCrossfade(state.readPosB, state.writePos);

                // Normalize so sum of fades = 1
                float totalFade = fadeA + fadeB;
                if (totalFade > 0.001f)
                {
                    fadeA /= totalFade;
                    fadeB /= totalFade;
                }
                else
                {
                    fadeA = 0.5f;
                    fadeB = 0.5f;
                }

                data[i] = sampleA * fadeA + sampleB * fadeB;

                // Advance write pointer
                state.writePos = (state.writePos + 1) % ringSize;
            }
        }
    }

private:
    static constexpr int kHistorySize = 12;
    static constexpr int kYinSkipBlocks = 2;
    static constexpr int kMaxChannels = 2;

    double sr = 44100.0;
    int maxBlock = 512;

    // Per-channel pitch shifting state
    struct ChannelState
    {
        std::vector<float> ringBuffer;
        int writePos = 0;
        double readPosA = 0.0;
        double readPosB = 0.0;
    };

    int ringSize = 4096;
    std::array<ChannelState, kMaxChannels> channels;
    double smoothedRatio = 1.0;  // Shared — same correction for both channels
    double targetRatio = 1.0;

    // YIN detection (uses channel 0 only)
    int yinBufferSize = 0;
    std::vector<float> yinBuffer;
    std::vector<float> inputRing;
    int yinWritePos = 0;
    int yinSkipCounter = 0;
    float cachedDetectedHz = 0.0f;

    // Parameters
    float referenceFreq = 440.0f;
    int rootNote = 0;
    int scaleType = 0;
    float retuneSpeed = 0.5f;
    float humanize = 0.0f;
    float snapAmount = 1.0f;
    float pitchSustain = 0.5f;
    bool stabilizer = true;

    // Feedback
    float lastDetectedHz = 0.0f;
    float lastTargetHz = 0.0f;
    float lastCorrectionCents = 0.0f;
    std::array<float, kHistorySize> detectedHistory {};
    int histIdx = 0;

    float readInterpolated (const std::vector<float>& ring, double pos) const
    {
        int idx0 = static_cast<int>(pos) % ringSize;
        int idx1 = (idx0 + 1) % ringSize;
        float frac = static_cast<float>(pos - std::floor(pos));
        return ring[static_cast<size_t>(idx0)] * (1.0f - frac)
             + ring[static_cast<size_t>(idx1)] * frac;
    }

    float computeCrossfade (double readPos, int wPos) const
    {
        double dist = static_cast<double>(wPos) - readPos;
        if (dist < 0.0) dist += static_cast<double>(ringSize);

        double fadeZone = static_cast<double>(ringSize) / 4.0;

        if (dist < fadeZone)
        {
            return static_cast<float>(0.5 * (1.0 + std::cos(juce::MathConstants<double>::pi * (1.0 - dist / fadeZone))));
        }
        else if (dist > static_cast<double>(ringSize) - fadeZone)
        {
            double d = static_cast<double>(ringSize) - dist;
            return static_cast<float>(0.5 * (1.0 + std::cos(juce::MathConstants<double>::pi * (1.0 - d / fadeZone))));
        }

        return 1.0f;
    }

    float detectPitchYIN()
    {
        const int W = yinBufferSize / 2;
        if (W < 2) return 0.0f;

        for (int tau = 0; tau < W; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < W; ++j)
            {
                int idx1 = (yinWritePos - W + j + static_cast<int>(inputRing.size())) % static_cast<int>(inputRing.size());
                int idx2 = (idx1 + tau) % static_cast<int>(inputRing.size());
                float diff = inputRing[static_cast<size_t>(idx1)] - inputRing[static_cast<size_t>(idx2)];
                sum += diff * diff;
            }
            yinBuffer[static_cast<size_t>(tau)] = sum;
        }

        yinBuffer[0] = 1.0f;
        float runningSum = 0.0f;
        for (int tau = 1; tau < W; ++tau)
        {
            runningSum += yinBuffer[static_cast<size_t>(tau)];
            yinBuffer[static_cast<size_t>(tau)] *= static_cast<float>(tau) / (runningSum > 0.0f ? runningSum : 1.0f);
        }

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

        if (tauEstimate < 1) return 0.0f;

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

        if (betterTau <= 0.0f) return 0.0f;
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
};

} // namespace humvocal
