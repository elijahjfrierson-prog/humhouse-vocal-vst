#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>
#include <algorithm>

namespace humvocal
{

// ============================================================================
// PitchEngine v6 — TD-PSOLA (formant-preserving pitch correction)
//
// Detection:  FFT-accelerated YIN with CMND normalization (O(N log N))
// Shifting:   TD-PSOLA — grains played at normal speed, spacing changes pitch
//             Each grain is a Hann-windowed copy of one pitch period played back
//             WITHOUT resampling. Formants are naturally preserved because the
//             spectral content of each grain is identical to the original.
// Stabilizer: Median-filtered pitch history + hysteresis (prevents flutter)
// Snap:       Negative retune speed — overshoot target for aggressive snap
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

        // FFT work buffer
        fftWork.assign (static_cast<size_t>(kFFTSize * 2), 0.0f);

        // Difference function buffer for YIN CMND
        yinDiff.assign (static_cast<size_t>(kAnalysisSize / 2), 0.0f);

        // Reset per-channel ring buffers
        for (auto& ch : channels)
        {
            ch.ring.fill (0.0f);
            ch.writePos = kLatency;
        }

        // Reset grain pool
        for (auto& g : grains)
            g.active = false;

        // Reset PSOLA synthesis state
        analysisPos = 0.0;
        synthPhaseCounter = 0;
        detectedPeriod = 256;
        targetPeriod = 256;

        // Reset detection
        detectBuf.fill (0.0f);
        detectWritePos = kAnalysisSize;
        analysisCounter = 0;
        cachedDetectedHz = 0.0f;
        lastValidDetectedHz = 0.0f;

        // Detection LP prefilter coefficient: cutoff ~1200Hz
        {
            double fc = 1200.0;
            detectLPCoeff = static_cast<float>(
                2.0 * kPi * fc / (2.0 * kPi * fc + sr));
            detectLPState = 0.0f;
        }

        // Reset smoothing
        smoothedRatio.reset (sr, 0.005);
        smoothedRatio.setCurrentAndTargetValue (1.0);
        updateSmoothRamp();

        // Reset stabilizer state
        stabHistory.fill (0.0f);
        stabHistIdx = 0;
        lockedNoteHz = 0.0f;
        noteHoldCounter = 0;

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
    void setFormantPreserve (bool /*on*/)     { /* always on with PSOLA */ }
    void setBypass (bool on)                  { bypassed = on; }

    // --- Readback for UI ---
    float getDetectedPitchHz() const  { return lastDetectedHz; }
    float getTargetPitchHz()   const  { return lastTargetHz; }
    float getCorrectionCents()  const { return lastCorrectionCents; }

    // Latency in samples — DAW should call setLatencySamples() with this
    int getLatencySamples() const     { return kLatency; }

    // =======================================================================
    //  Main process — TD-PSOLA pitch correction
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
            // ---- Write input to per-channel ring buffers ----
            for (int ch = 0; ch < numChannels; ++ch)
            {
                channels[static_cast<size_t>(ch)].ring[static_cast<size_t>(
                    channels[static_cast<size_t>(ch)].writePos)] =
                    buffer.getSample (ch, i);
            }

            // ---- Feed detection buffer from channel 0 (low-pass filtered) ----
            float raw = buffer.getSample (0, i);
            detectLPState += detectLPCoeff * (raw - detectLPState);
            detectBuf[static_cast<size_t>(detectWritePos)] = detectLPState;
            detectWritePos = (detectWritePos + 1) % kDetectBufSize;

            // ---- Periodic pitch detection ----
            ++analysisCounter;
            if (analysisCounter >= kAnalysisHop)
            {
                analysisCounter = 0;
                float hz = detectPitchYIN();
                if (hz > 0.0f)
                {
                    cachedDetectedHz = hz;
                    detectedPeriod = juce::jlimit (kMinPeriod, kMaxPeriod,
                        static_cast<int>(std::round (sr / static_cast<double>(hz))));
                }
                updateCorrection();
            }

            // ---- Get smoothed ratio for this sample ----
            double ratio = smoothedRatio.getNextValue();

            // ---- Compute target period from ratio ----
            // targetPeriod = detectedPeriod / ratio (output grain spacing)
            int curTargetPeriod = juce::jlimit (kMinPeriod, kMaxPeriod,
                static_cast<int>(std::round (
                    static_cast<double>(detectedPeriod) / ratio)));

            // ---- Check if bypassed or ratio ~1.0 → direct passthrough ----
            bool directPassthrough = bypassed
                || (std::abs (ratio - 1.0) < 0.001);

            if (directPassthrough)
            {
                // Simple ring buffer readback at fixed latency — no PSOLA
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto& s = channels[static_cast<size_t>(ch)];
                    int readIdx = (s.writePos - kLatency + kBufSize) % kBufSize;
                    buffer.setSample (ch, i,
                        s.ring[static_cast<size_t>(readIdx)]);
                    s.writePos = (s.writePos + 1) % kBufSize;
                }
                // Keep analysis position in sync
                analysisPos = static_cast<double>(
                    (channels[0].writePos - kLatency + kBufSize) % kBufSize);
                synthPhaseCounter = 0;
                // Deactivate all grains during passthrough
                for (auto& g : grains)
                    g.active = false;
                continue;
            }

            // ==== TD-PSOLA synthesis ====

            // ---- Check if it's time to spawn a new grain ----
            ++synthPhaseCounter;
            if (synthPhaseCounter >= curTargetPeriod)
            {
                synthPhaseCounter = 0;
                spawnGrain (detectedPeriod);
            }

            // ---- Compute output from active grains (per channel) ----
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& s = channels[static_cast<size_t>(ch)];
                float output = 0.0f;
                float windowSum = 0.0f;

                for (auto& g : grains)
                {
                    if (! g.active) continue;

                    // Hann window
                    float t = static_cast<float>(g.readOffset)
                            / static_cast<float>(g.length);
                    float w = 0.5f - 0.5f * std::cos (2.0f * kPi * t);

                    // Read position: grain starts at startPos, reads forward
                    double readPos = g.startPos
                        + static_cast<double>(g.readOffset);
                    // Wrap to buffer
                    while (readPos >= static_cast<double>(kBufSize))
                        readPos -= static_cast<double>(kBufSize);
                    while (readPos < 0.0)
                        readPos += static_cast<double>(kBufSize);

                    output += linearInterp (s.ring, readPos) * w;
                    windowSum += w;
                }

                // Normalize by window sum to maintain unity gain
                if (windowSum > 0.01f)
                    output /= windowSum;

                buffer.setSample (ch, i, output);
                s.writePos = (s.writePos + 1) % kBufSize;
            }

            // ---- Advance all grain read offsets (shared timing) ----
            for (auto& g : grains)
            {
                if (! g.active) continue;
                ++g.readOffset;
                if (g.readOffset >= g.length)
                    g.active = false;
            }

            // ---- Advance analysis position ----
            // analysisPos tracks where we're "reading from" in input time.
            // It advances at `ratio` per output sample to maintain duration.
            analysisPos += ratio;
            while (analysisPos >= static_cast<double>(kBufSize))
                analysisPos -= static_cast<double>(kBufSize);

            // ---- Safety: keep analysisPos behind writePos ----
            double gap = circularDist (channels[0].writePos, analysisPos);
            if (gap < kSafeGap || gap > static_cast<double>(kBufSize) - kSafeGap)
            {
                analysisPos = static_cast<double>(
                    (channels[0].writePos - kLatency + kBufSize) % kBufSize);
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

    // Ring buffer / PSOLA parameters
    static constexpr int kBufSize          = 16384;
    static constexpr int kDetectBufSize    = 16384;
    static constexpr int kLatency          = 1024;               // Read-behind distance
    static constexpr int kMinPeriod        = 48;                 // ~1000Hz @ 48kHz
    static constexpr int kMaxPeriod        = 800;                // ~60Hz @ 48kHz
    static constexpr int kHistorySize      = 12;

    // PSOLA grain pool
    static constexpr int kMaxGrains        = 8;

    // Safety margin for analysis position drift
    static constexpr double kSafeGap       = 256.0;

    // YIN parameters
    static constexpr float kYINThreshold   = 0.08f;
    static constexpr float kHarmonicLockThreshold = 0.85f;

    // Stabilizer parameters
    static constexpr int   kStabHistSize   = 8;
    static constexpr float kNoteEntryThreshold = 50.0f;
    static constexpr float kNoteExitThreshold  = 80.0f;
    static constexpr int   kNoteHoldMin    = 3;

    // Overshoot for "negative speed" snap
    static constexpr float kOvershootFactor = 1.2f;

    double sr = 48000.0;

    // FFT
    juce::dsp::FFT fft { kFFTOrder };
    std::vector<float> fftWork;
    std::vector<float> yinDiff;

    // Detection buffer (separate from audio ring)
    std::array<float, kDetectBufSize> detectBuf {};
    int detectWritePos = 0;
    int analysisCounter = 0;
    float cachedDetectedHz = 0.0f;
    float lastValidDetectedHz = 0.0f;

    // Detection low-pass prefilter
    float detectLPState = 0.0f;
    float detectLPCoeff = 0.15f;

    // Per-channel ring buffers
    struct ChannelState
    {
        std::array<float, kBufSize> ring {};
        int writePos = 0;
    };
    std::array<ChannelState, kMaxChannels> channels;

    // ---- TD-PSOLA grain pool ----
    struct Grain
    {
        double startPos = 0.0;   // start position in ring buffer
        int readOffset  = 0;     // current read offset within grain
        int length      = 0;     // total grain length in samples
        bool active     = false;
    };
    std::array<Grain, kMaxGrains> grains {};

    // PSOLA synthesis state
    double analysisPos       = 0.0;  // current read position in input
    int    synthPhaseCounter = 0;    // counts up to targetPeriod
    int    detectedPeriod    = 256;  // from YIN (input period in samples)
    int    targetPeriod      = 256;  // output period (detectedPeriod / ratio)

    // Smoothed pitch ratio
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
    bool  bypassed      = false;

    // Note stabilizer state
    std::array<float, kStabHistSize> stabHistory {};
    int stabHistIdx = 0;
    float lockedNoteHz = 0.0f;
    int noteHoldCounter = 0;

    // UI feedback
    float lastDetectedHz      = 0.0f;
    float lastTargetHz        = 0.0f;
    float lastCorrectionCents = 0.0f;
    std::array<float, kHistorySize> detectedHistory {};
    int histIdx = 0;

    // ========================================================================
    //  Spawn a new PSOLA grain at the current analysis position
    // ========================================================================
    void spawnGrain (int inputPeriod)
    {
        // Find an inactive grain slot
        Grain* slot = nullptr;
        for (auto& g : grains)
        {
            if (! g.active)
            {
                slot = &g;
                break;
            }
        }

        if (slot == nullptr)
        {
            // All slots full — steal the oldest (most progressed) grain
            int maxProgress = -1;
            for (auto& g : grains)
            {
                if (g.readOffset > maxProgress)
                {
                    maxProgress = g.readOffset;
                    slot = &g;
                }
            }
        }

        if (slot == nullptr) return;

        // Grain length = 2 * inputPeriod (one period on each side of center)
        int grainLen = juce::jlimit (kMinPeriod * 2, kMaxPeriod * 2, inputPeriod * 2);

        // Grain starts one period before analysisPos
        double start = analysisPos - static_cast<double>(inputPeriod);
        while (start < 0.0) start += static_cast<double>(kBufSize);

        slot->startPos   = start;
        slot->readOffset = 0;
        slot->length     = grainLen;
        slot->active     = true;
    }

    // ========================================================================
    //  Circular distance: how far readPos is behind writePos
    // ========================================================================
    double circularDist (int writeP, double readP) const
    {
        double d = static_cast<double>(writeP) - readP;
        if (d < 0.0) d += static_cast<double>(kBufSize);
        return d;
    }

    // ========================================================================
    //  Linear interpolation from circular buffer
    // ========================================================================
    float linearInterp (const std::array<float, kBufSize>& buf, double pos) const
    {
        int i0 = static_cast<int>(pos) % kBufSize;
        if (i0 < 0) i0 += kBufSize;
        int i1 = (i0 + 1) % kBufSize;
        float frac = static_cast<float>(pos - std::floor (pos));
        return buf[static_cast<size_t>(i0)] * (1.0f - frac)
             + buf[static_cast<size_t>(i1)] * frac;
    }

    // ========================================================================
    //  YIN pitch detection with CMND normalization (FFT-accelerated)
    // ========================================================================
    float detectPitchYIN()
    {
        // Fill FFT input from detection buffer (no analysis window — windowing
        // biases autocorrelation for YIN; zero-padding prevents circular artifacts)
        for (int i = 0; i < kAnalysisSize; ++i)
        {
            int idx = (detectWritePos - kAnalysisSize + i + kDetectBufSize) % kDetectBufSize;
            fftWork[static_cast<size_t>(i)] = detectBuf[static_cast<size_t>(idx)];
        }
        for (int i = kAnalysisSize; i < kFFTSize * 2; ++i)
            fftWork[static_cast<size_t>(i)] = 0.0f;

        fft.performRealOnlyForwardTransform (fftWork.data(), true);

        // Power spectrum
        for (int k = 0; k < kFFTSize; ++k)
        {
            float re = fftWork[static_cast<size_t>(2 * k)];
            float im = fftWork[static_cast<size_t>(2 * k + 1)];
            fftWork[static_cast<size_t>(2 * k)]     = re * re + im * im;
            fftWork[static_cast<size_t>(2 * k + 1)] = 0.0f;
        }

        fft.performRealOnlyInverseTransform (fftWork.data());

        float acf0 = fftWork[0];
        if (acf0 <= 0.0f) return 0.0f;

        // YIN difference function from autocorrelation
        int halfSize = kAnalysisSize / 2;
        for (int tau = 0; tau < halfSize; ++tau)
            yinDiff[static_cast<size_t>(tau)] =
                2.0f * acf0 - 2.0f * fftWork[static_cast<size_t>(tau)];

        // CMND normalization
        yinDiff[0] = 1.0f;
        float runningSum = 0.0f;
        for (int tau = 1; tau < halfSize; ++tau)
        {
            runningSum += yinDiff[static_cast<size_t>(tau)];
            if (runningSum > 0.0f)
                yinDiff[static_cast<size_t>(tau)] *=
                    static_cast<float>(tau) / runningSum;
            else
                yinDiff[static_cast<size_t>(tau)] = 1.0f;
        }

        // Absolute thresholding
        int minLag = juce::jmax (2, static_cast<int>(sr / 1000.0));
        int maxLag = juce::jmin (halfSize - 2, static_cast<int>(sr / 60.0));

        int tauEstimate = -1;
        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            if (yinDiff[static_cast<size_t>(tau)] < kYINThreshold)
            {
                while (tau + 1 <= maxLag
                       && yinDiff[static_cast<size_t>(tau + 1)]
                              < yinDiff[static_cast<size_t>(tau)])
                    ++tau;

                tauEstimate = tau;
                break;
            }
        }

        // Fallback: global minimum
        if (tauEstimate < 0)
        {
            float bestVal = 1.0f;
            for (int tau = minLag; tau <= maxLag; ++tau)
            {
                if (yinDiff[static_cast<size_t>(tau)] < bestVal)
                {
                    bestVal = yinDiff[static_cast<size_t>(tau)];
                    tauEstimate = tau;
                }
            }
            if (bestVal > 0.4f)
                return applyHarmonicHysteresis (acf0);
        }

        if (tauEstimate < 1)
            return applyHarmonicHysteresis (acf0);

        // Parabolic interpolation
        float betterTau = static_cast<float>(tauEstimate);
        if (tauEstimate > minLag && tauEstimate < maxLag)
        {
            float s0 = yinDiff[static_cast<size_t>(tauEstimate - 1)];
            float s1 = yinDiff[static_cast<size_t>(tauEstimate)];
            float s2 = yinDiff[static_cast<size_t>(tauEstimate + 1)];
            float denom = 2.0f * (s0 - 2.0f * s1 + s2);
            if (std::abs (denom) > 1e-9f)
                betterTau += (s0 - s2) / denom;
        }

        float resultHz = static_cast<float>(sr) / betterTau;
        lastValidDetectedHz = resultHz;
        return resultHz;
    }

    // ========================================================================
    //  Harmonic hysteresis — force-lock to last valid pitch if signal weakens
    // ========================================================================
    float applyHarmonicHysteresis (float acf0)
    {
        if (lastValidDetectedHz <= 0.0f || acf0 <= 0.0f || sr <= 0.0)
            return 0.0f;

        int lastLag = static_cast<int>(std::round (
            sr / static_cast<double>(lastValidDetectedHz)));
        if (lastLag < 1 || lastLag >= kAnalysisSize / 2)
            return 0.0f;

        float acfAtLag = fftWork[static_cast<size_t>(lastLag)];
        float normalizedStrength = acfAtLag / acf0;

        if (normalizedStrength > kHarmonicLockThreshold)
            return lastValidDetectedHz;

        return 0.0f;
    }

    // ========================================================================
    //  MetaTune-style note stabilizer — median filter + hysteresis
    // ========================================================================
    float stabilizeNote (float rawHz)
    {
        stabHistory[static_cast<size_t>(stabHistIdx)] = rawHz;
        stabHistIdx = (stabHistIdx + 1) % kStabHistSize;

        std::array<float, kStabHistSize> sorted {};
        int validCount = 0;
        for (auto v : stabHistory)
        {
            if (v > 0.0f)
                sorted[static_cast<size_t>(validCount++)] = v;
        }

        if (validCount == 0) return rawHz;

        std::sort (sorted.begin(), sorted.begin() + validCount);
        float medianHz = sorted[static_cast<size_t>(validCount / 2)];

        float exitThresh = kNoteExitThreshold + pitchSustain * 50.0f;
        int holdMin = static_cast<int>(kNoteHoldMin + pitchSustain * 5.0f);

        if (lockedNoteHz > 0.0f)
        {
            float centsDiff = 1200.0f * std::log2 (medianHz / lockedNoteHz);

            if (std::abs (centsDiff) < exitThresh)
            {
                noteHoldCounter = 0;
                return lockedNoteHz;
            }

            ++noteHoldCounter;
            if (noteHoldCounter < holdMin)
                return lockedNoteHz;
        }

        lockedNoteHz = medianHz;
        noteHoldCounter = 0;
        return medianHz;
    }

    // ========================================================================
    //  Update correction from detected pitch
    // ========================================================================
    void updateCorrection()
    {
        if (bypassed)
        {
            smoothedRatio.setTargetValue (1.0);
            lastDetectedHz = 0.0f;
            lastTargetHz = 0.0f;
            lastCorrectionCents = 0.0f;
            return;
        }

        float detectedHz = cachedDetectedHz;
        lastDetectedHz = detectedHz;

        double newTargetRatio = 1.0;

        if (detectedHz >= 60.0f && detectedHz <= 1200.0f)
        {
            float stableHz = stabilizer ? stabilizeNote (detectedHz) : detectedHz;
            float targetHz = findTargetFrequency (stableHz);
            lastTargetHz = targetHz;

            float corrCents = 1200.0f * std::log2 (targetHz / stableHz);
            lastCorrectionCents = corrCents;

            corrCents *= (1.0f - humanize);
            corrCents *= snapAmount;

            if (std::abs (corrCents) > 0.5f)
            {
                float overshoot = 1.0f;
                if (retuneSpeed > 0.85f)
                {
                    float overshootBlend = (retuneSpeed - 0.85f) / 0.15f;
                    overshoot = 1.0f + overshootBlend * (kOvershootFactor - 1.0f);
                }

                double effectiveCents = static_cast<double>(corrCents)
                    * static_cast<double>(overshoot);
                newTargetRatio = std::pow (2.0, effectiveCents / 1200.0);
            }
        }
        else
        {
            lastTargetHz = detectedHz;
            lastCorrectionCents = 0.0f;
        }

        smoothedRatio.setTargetValue (newTargetRatio);
    }

    // ========================================================================
    //  Update LinearSmoothedValue ramp when retune speed changes
    // ========================================================================
    void updateSmoothRamp()
    {
        double inv = 1.0 - static_cast<double>(retuneSpeed);
        double rampSeconds = 0.00002 + inv * inv * 0.4;
        cachedRampSeconds = rampSeconds;
        if (sr > 0.0)
        {
            double current = smoothedRatio.getCurrentValue();
            double target  = smoothedRatio.getTargetValue();
            smoothedRatio.reset (sr, rampSeconds);
            smoothedRatio.setCurrentAndTargetValue (current);
            smoothedRatio.setTargetValue (target);
        }
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

        return referenceFreq * std::pow (2.0f,
            (static_cast<float>(nearestMidi) - 69.0f) / 12.0f);
    }
};

} // namespace humvocal
