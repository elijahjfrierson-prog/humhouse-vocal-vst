#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>

namespace humvocal
{

// ============================================================================
// PitchEngine v4 — TD-PSOLA with MPM-style detection
//
// Detection:  FFT-based NSDF with MPM peak picking (O(N log N))
// Shifting:   Time-Domain Pitch Synchronous Overlap-Add (TD-PSOLA)
//             Grain size adapts to detected pitch period — eliminates graininess
// Smoothing:  juce::LinearSmoothValue for per-sample retune speed control
//
// Based on:
//   - McLeod Pitch Method (2005) for robust peak picking
//   - TD-PSOLA literature for pitch-synchronous shifting
//   - Autotalent (Tom Baran) for overall architecture
// ============================================================================
class PitchEngine
{
public:
    static constexpr std::array<bool, 12> kMajor     = {true,false,true,false,true,true,false,true,false,true,false,true};
    static constexpr std::array<bool, 12> kMinor     = {true,false,true,true,false,true,false,true,true,false,true,false};
    static constexpr std::array<bool, 12> kChromatic = {true,true,true,true,true,true,true,true,true,true,true,true};

    void prepare (double sampleRate, int /*blockSize*/)
    {
        sr = sampleRate;

        // Pre-compute analysis Hann window
        for (int i = 0; i < kAnalysisSize; ++i)
            analysisWindow[static_cast<size_t>(i)] =
                0.5f - 0.5f * std::cos (2.0f * kPi * static_cast<float>(i)
                                         / static_cast<float>(kAnalysisSize));

        // FFT work buffer
        fftWork.assign (static_cast<size_t>(kFFTSize * 2), 0.0f);

        // Reset per-channel state
        for (auto& ch : channels)
        {
            ch.inputBuf.fill (0.0f);
            ch.writePos = kDefaultGrainSize + kDefaultGrainSize / 2;
            for (auto& g : ch.grains) g = {};
            ch.nextGrainIdx = 0;
            ch.samplesUntilNextGrain = 1;
            ch.currentHopSize = kDefaultGrainSize / 2;
        }

        // Reset detection
        detectBuf.fill (0.0f);
        detectWritePos = kAnalysisSize;
        analysisCounter = 0;
        cachedDetectedHz = 0.0f;
        detectedPeriodSamples = kDefaultGrainSize / 2;

        // Reset smoothing
        smoothedRatio.reset (sr, 0.005); // default 5ms ramp
        smoothedRatio.setCurrentAndTargetValue (1.0);
        updateSmoothRamp();

        // Reset history
        detectedHistory.fill (0.0f);
        histIdx = 0;
        lastDetectedHz = 0.0f;
        lastTargetHz = 0.0f;
        lastCorrectionCents = 0.0f;
    }

    // --- Parameter setters ---
    void setReferenceFrequency (float hz)     { referenceFreq = hz; }
    void setRootNote (int note)               { rootNote = note % 12; }
    void setScaleType (int type)              { scaleType = type; }
    void setRetuneSpeed (float speed01)
    {
        if (std::abs (speed01 - retuneSpeed) < 1e-6f) return;
        retuneSpeed = speed01;
        updateSmoothRamp();
    }
    void setHumanize (float h)                { humanize = h; }
    void setSnapAmount (float s)              { snapAmount = s; }
    void setPitchSustain (float s)            { pitchSustain = s; }
    void setNoteStabilizer (bool on)          { stabilizer = on; }
    void setFormantPreserve (bool /*on*/)     { /* no-op */ }

    // --- Readback for UI ---
    float getDetectedPitchHz() const  { return lastDetectedHz; }
    float getTargetPitchHz()   const  { return lastTargetHz; }
    float getCorrectionCents()  const { return lastCorrectionCents; }

    // =======================================================================
    //  Main process
    // =======================================================================
    void process (juce::AudioBuffer<float>& buffer)
    {
        juce::ScopedNoDenormals noDenormals;

        const int numSamples  = buffer.getNumSamples();
        const int numChannels = juce::jmin (buffer.getNumChannels(), kMaxChannels);

        if (numChannels == 0 || numSamples == 0 || sr <= 0.0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            // ---- Write input to per-channel buffers & detection buffer ----
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch].inputBuf[static_cast<size_t>(channels[ch].writePos)] =
                    buffer.getSample (ch, i);

            detectBuf[static_cast<size_t>(detectWritePos)] = buffer.getSample (0, i);
            detectWritePos = (detectWritePos + 1) % kBufSize;

            // ---- Periodic pitch detection ----
            ++analysisCounter;
            if (analysisCounter >= kAnalysisHop)
            {
                analysisCounter = 0;
                float hz = detectPitchMPM();
                if (hz > 0.0f)
                {
                    cachedDetectedHz = hz;
                    detectedPeriodSamples = juce::jlimit (
                        kMinPeriod, kMaxPeriod,
                        static_cast<int>(std::round (sr / static_cast<double>(hz))));
                }
                updateCorrection();
            }

            // ---- Get smoothed ratio for this sample ----
            double ratio = smoothedRatio.getNextValue();

            // ---- TD-PSOLA output per channel ----
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& state = channels[ch];
                float output = 0.0f;

                // Sum active grains
                for (auto& g : state.grains)
                {
                    if (! g.active) continue;

                    // Hann window computed inline (pitch-adaptive size)
                    float w = 0.5f - 0.5f * std::cos (
                        2.0f * kPi * static_cast<float>(g.phase)
                        / static_cast<float>(g.grainSize));

                    float sample = cubicInterp (state.inputBuf, g.readPos);
                    output += sample * w;

                    // Advance read position at shifted rate
                    g.readPos += ratio;
                    while (g.readPos >= static_cast<double>(kBufSize))
                        g.readPos -= static_cast<double>(kBufSize);
                    while (g.readPos < 0.0)
                        g.readPos += static_cast<double>(kBufSize);

                    ++g.phase;
                    if (g.phase >= g.grainSize)
                        g.active = false;
                }

                // Start new grain at pitch-synchronous intervals
                --state.samplesUntilNextGrain;
                if (state.samplesUntilNextGrain <= 0)
                {
                    int period = detectedPeriodSamples;
                    int grainSz = period * 2;                        // 2 pitch periods
                    grainSz = juce::jlimit (kMinGrainSize, kMaxGrainSize, grainSz);

                    state.currentHopSize = grainSz / 2;             // 50% overlap
                    state.samplesUntilNextGrain = state.currentHopSize;

                    // Find next grain slot
                    auto& g = state.grains[static_cast<size_t>(state.nextGrainIdx)];
                    state.nextGrainIdx = (state.nextGrainIdx + 1) % kMaxGrains;

                    g.active = true;
                    g.phase = 0;
                    g.grainSize = grainSz;

                    // Start reading from grainSize samples behind write position
                    // Then search for a nearby peak to center the grain (phase alignment)
                    int rawStart = (state.writePos - grainSz + kBufSize) % kBufSize;
                    g.readPos = static_cast<double>(findNearbyPeak (state.inputBuf, rawStart, period));
                }

                buffer.setSample (ch, i, output);
                state.writePos = (state.writePos + 1) % kBufSize;
            }
        }
    }

private:
    static constexpr float kPi = juce::MathConstants<float>::pi;
    static constexpr int kMaxChannels = 2;

    // FFT pitch detection
    static constexpr int kFFTOrder     = 12;                     // 2^12 = 4096
    static constexpr int kFFTSize      = 1 << kFFTOrder;         // 4096
    static constexpr int kAnalysisSize = kFFTSize / 2;           // 2048 samples
    static constexpr int kAnalysisHop  = 512;                    // ~10ms @ 48kHz

    // TD-PSOLA
    static constexpr int kBufSize          = 16384;              // Large circular buffer
    static constexpr int kMaxGrains        = 4;                  // Overlapping grains
    static constexpr int kMinPeriod        = 48;                 // ~1000Hz @ 48kHz
    static constexpr int kMaxPeriod        = 800;                // ~60Hz @ 48kHz
    static constexpr int kMinGrainSize     = 96;                 // 2 * minPeriod
    static constexpr int kMaxGrainSize     = 1600;               // 2 * maxPeriod
    static constexpr int kDefaultGrainSize = 480;                // 2 * ~200Hz period
    static constexpr int kHistorySize      = 12;

    // MPM peak picking
    static constexpr float kMPMCutoff      = 0.93f;
    static constexpr float kMPMSmallCutoff = 0.5f;

    double sr = 48000.0;

    // Analysis window
    std::array<float, kAnalysisSize> analysisWindow {};

    // FFT
    juce::dsp::FFT fft { kFFTOrder };
    std::vector<float> fftWork;

    // Detection buffer
    std::array<float, kBufSize> detectBuf {};
    int detectWritePos = 0;
    int analysisCounter = 0;
    float cachedDetectedHz = 0.0f;
    int detectedPeriodSamples = kDefaultGrainSize / 2;

    // Per-channel TD-PSOLA state
    struct Grain
    {
        double readPos  = 0.0;
        int    phase    = 0;
        int    grainSize = kDefaultGrainSize;
        bool   active   = false;
    };

    struct ChannelState
    {
        std::array<float, kBufSize> inputBuf {};
        int writePos = 0;
        std::array<Grain, kMaxGrains> grains {};
        int nextGrainIdx = 0;
        int samplesUntilNextGrain = 0;
        int currentHopSize = kDefaultGrainSize / 2;
    };

    std::array<ChannelState, kMaxChannels> channels;

    // Smoothed pitch ratio (per-sample via LinearSmoothValue)
    juce::LinearSmoothedValue<double> smoothedRatio { 1.0 };
    double cachedRampSeconds = 0.005;

    // Parameters
    float referenceFreq = 440.0f;
    int   rootNote      = 0;
    int   scaleType     = 0;
    float retuneSpeed   = 0.5f;
    float humanize      = 0.0f;
    float snapAmount    = 1.0f;
    float pitchSustain  = 0.5f;
    bool  stabilizer    = true;

    // UI feedback
    float lastDetectedHz      = 0.0f;
    float lastTargetHz        = 0.0f;
    float lastCorrectionCents = 0.0f;
    std::array<float, kHistorySize> detectedHistory {};
    int histIdx = 0;

    // ========================================================================
    //  MPM-style pitch detection via FFT NSDF
    // ========================================================================
    float detectPitchMPM()
    {
        // Fill FFT buffer with windowed input, zero-padded
        for (int i = 0; i < kAnalysisSize; ++i)
        {
            int idx = (detectWritePos - kAnalysisSize + i + kBufSize) % kBufSize;
            fftWork[static_cast<size_t>(i)] =
                detectBuf[static_cast<size_t>(idx)] * analysisWindow[static_cast<size_t>(i)];
        }
        for (int i = kAnalysisSize; i < kFFTSize * 2; ++i)
            fftWork[static_cast<size_t>(i)] = 0.0f;

        // Forward FFT
        fft.performRealOnlyForwardTransform (fftWork.data(), true);

        // Power spectrum in-place
        for (int k = 0; k < kFFTSize; ++k)
        {
            float re = fftWork[static_cast<size_t>(2 * k)];
            float im = fftWork[static_cast<size_t>(2 * k + 1)];
            fftWork[static_cast<size_t>(2 * k)]     = re * re + im * im;
            fftWork[static_cast<size_t>(2 * k + 1)] = 0.0f;
        }

        // Inverse FFT → autocorrelation
        fft.performRealOnlyInverseTransform (fftWork.data());

        float acf0 = fftWork[0];
        if (acf0 <= 0.0f) return 0.0f;

        // Normalize to get NSDF-like values (0 to 1 range)
        float invAcf0 = 1.0f / acf0;

        int minLag = juce::jmax (1, static_cast<int>(sr / 1000.0));
        int maxLag = juce::jmin (kAnalysisSize - 1, static_cast<int>(sr / 60.0));

        // ---- MPM peak picking ----
        // Find peaks in each positive lobe of the NSDF
        struct Peak { int lag; float val; };
        Peak bestPeak { 0, 0.0f };
        bool inPositiveLobe = false;
        Peak currentLobePeak { 0, -1.0f };

        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            float val = fftWork[static_cast<size_t>(lag)] * invAcf0;

            if (val > 0.0f)
            {
                if (! inPositiveLobe)
                {
                    inPositiveLobe = true;
                    currentLobePeak = { lag, val };
                }
                else if (val > currentLobePeak.val)
                {
                    currentLobePeak = { lag, val };
                }
            }
            else if (inPositiveLobe)
            {
                // End of positive lobe — check this peak
                inPositiveLobe = false;

                if (currentLobePeak.val > kMPMSmallCutoff)
                {
                    // MPM: accept the first peak above the cutoff threshold
                    if (currentLobePeak.val >= kMPMCutoff)
                    {
                        bestPeak = currentLobePeak;
                        break; // first peak above cutoff wins
                    }

                    // Track the overall best peak as fallback
                    if (currentLobePeak.val > bestPeak.val)
                        bestPeak = currentLobePeak;
                }
            }
        }

        // Handle case where we're still in a positive lobe at maxLag
        if (inPositiveLobe && currentLobePeak.val > bestPeak.val
            && currentLobePeak.val > kMPMSmallCutoff)
            bestPeak = currentLobePeak;

        if (bestPeak.val < 0.3f || bestPeak.lag < 1)
            return 0.0f;

        // Parabolic interpolation for sub-sample accuracy
        float betterLag = static_cast<float>(bestPeak.lag);
        if (bestPeak.lag > minLag && bestPeak.lag < maxLag)
        {
            float y0 = fftWork[static_cast<size_t>(bestPeak.lag - 1)] * invAcf0;
            float y1 = bestPeak.val;
            float y2 = fftWork[static_cast<size_t>(bestPeak.lag + 1)] * invAcf0;
            float denom = 2.0f * (y0 - 2.0f * y1 + y2);
            if (std::abs (denom) > 1e-9f)
                betterLag += (y0 - y2) / denom;
        }

        return static_cast<float>(sr) / betterLag;
    }

    // ========================================================================
    //  Update correction from detected pitch
    // ========================================================================
    void updateCorrection()
    {
        float detectedHz = cachedDetectedHz;
        lastDetectedHz = detectedHz;

        double newTargetRatio = 1.0;

        if (detectedHz >= 60.0f && detectedHz <= 1200.0f)
        {
            float stableHz = detectedHz;

            if (stabilizer)
            {
                detectedHistory[static_cast<size_t>(histIdx)] = detectedHz;
                histIdx = (histIdx + 1) % kHistorySize;

                float avg = 0.0f;
                int count = 0;
                for (auto v : detectedHistory)
                    if (v > 0.0f) { avg += v; ++count; }

                if (count > 0)
                {
                    avg /= static_cast<float>(count);
                    float centsDiff = 1200.0f * std::log2 (detectedHz / avg);
                    float lockZone = 30.0f + pitchSustain * 50.0f;
                    if (std::abs (centsDiff) < lockZone)
                        stableHz = avg;
                }
            }

            float targetHz = findTargetFrequency (stableHz);
            lastTargetHz = targetHz;

            float corrCents = 1200.0f * std::log2 (targetHz / stableHz);
            lastCorrectionCents = corrCents;

            corrCents *= (1.0f - humanize);
            corrCents *= snapAmount;

            if (std::abs (corrCents) > 0.5f)
                newTargetRatio = std::pow (2.0, static_cast<double>(corrCents) / 1200.0);
        }
        else
        {
            lastTargetHz = detectedHz;
            lastCorrectionCents = 0.0f;
        }

        smoothedRatio.setTargetValue (newTargetRatio);
    }

    // ========================================================================
    //  Update LinearSmoothValue ramp when retune speed changes
    // ========================================================================
    void updateSmoothRamp()
    {
        // Speed 0 → slow glide (400ms), Speed 1 → instant snap (0.3ms)
        double rampSeconds = 0.0003 + (1.0 - static_cast<double>(retuneSpeed))
                                    * (1.0 - static_cast<double>(retuneSpeed)) * 0.4;
        cachedRampSeconds = rampSeconds;
        if (sr > 0.0)
        {
            // Preserve current value — don't snap to target
            double current = smoothedRatio.getCurrentValue();
            double target  = smoothedRatio.getTargetValue();
            smoothedRatio.reset (sr, rampSeconds);
            smoothedRatio.setCurrentAndTargetValue (current);
            smoothedRatio.setTargetValue (target);
        }
    }

    // ========================================================================
    //  Find nearby peak in input buffer for phase-aligned grain start
    // ========================================================================
    int findNearbyPeak (const std::array<float, kBufSize>& buf, int center, int searchRange) const
    {
        int halfSearch = searchRange / 2;
        int bestIdx = center;
        float bestAbs = 0.0f;

        for (int off = -halfSearch; off <= halfSearch; ++off)
        {
            int idx = (center + off + kBufSize) % kBufSize;
            float val = std::abs (buf[static_cast<size_t>(idx)]);
            if (val > bestAbs)
            {
                bestAbs = val;
                bestIdx = idx;
            }
        }
        return bestIdx;
    }

    // ========================================================================
    //  Catmull-Rom cubic interpolation from circular buffer
    // ========================================================================
    float cubicInterp (const std::array<float, kBufSize>& buf, double pos) const
    {
        int   i1   = static_cast<int>(pos) % kBufSize;
        float frac = static_cast<float>(pos - std::floor (pos));

        int i0 = (i1 - 1 + kBufSize) % kBufSize;
        int i2 = (i1 + 1) % kBufSize;
        int i3 = (i1 + 2) % kBufSize;

        float y0 = buf[static_cast<size_t>(i0)];
        float y1 = buf[static_cast<size_t>(i1)];
        float y2 = buf[static_cast<size_t>(i2)];
        float y3 = buf[static_cast<size_t>(i3)];

        float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        float a1 =  y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float a2 = -0.5f * y0 + 0.5f * y2;
        float a3 =  y1;

        return ((a0 * frac + a1) * frac + a2) * frac + a3;
    }

    // ========================================================================
    //  Find the nearest in-scale target frequency
    // ========================================================================
    float findTargetFrequency (float hz) const
    {
        float midiNote = 69.0f + 12.0f * std::log2 (hz / referenceFreq);

        const auto& scale = (scaleType == 1) ? kMinor
                          : (scaleType == 2) ? kChromatic
                          : kMajor;

        int nearestMidi = static_cast<int>(std::round (midiNote));
        int noteInOctave = ((nearestMidi % 12) - rootNote + 12) % 12;

        if (! scale[static_cast<size_t>(noteInOctave)])
        {
            for (int offset = 1; offset <= 6; ++offset)
            {
                int up   = (noteInOctave + offset) % 12;
                int down = (noteInOctave - offset + 12) % 12;
                if (scale[static_cast<size_t>(up)])   { nearestMidi += offset; break; }
                if (scale[static_cast<size_t>(down)])  { nearestMidi -= offset; break; }
            }
        }

        return referenceFreq * std::pow (2.0f, (static_cast<float>(nearestMidi) - 69.0f) / 12.0f);
    }
};

} // namespace humvocal
