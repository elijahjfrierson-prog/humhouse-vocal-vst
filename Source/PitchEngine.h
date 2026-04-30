#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>

namespace humvocal
{

// ============================================================================
// PitchEngine v5 — Dual-Head Crossfading Pitch Shifter + YIN CMND Detection
//
// Detection:  FFT-accelerated YIN with CMND normalization (O(N log N))
//             Prevents octave errors; aggressive threshold for note capture
// Shifting:   Two crossfading read heads with ACCUMULATED position
//             Read heads advance at ratio speed — shift is immediate & strong
//             Hann crossfade at grain boundaries hides discontinuities
// Smoothing:  juce::LinearSmoothedValue for per-sample retune speed control
//
// Reference:  Cheveigné & Kawahara (2002) "YIN, a fundamental frequency
//             estimator for speech and music"
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

        // Difference function buffer for YIN CMND
        yinDiff.assign (static_cast<size_t>(kAnalysisSize / 2), 0.0f);

        // Reset per-channel state
        for (auto& ch : channels)
        {
            ch.ring.fill (0.0f);
            ch.writePos = kLatency;
            ch.readPosA = 0.0;
            ch.readPosB = 0.0;
            ch.grainPhase = 0;
        }

        grainSize = 512;

        // Reset detection
        detectBuf.fill (0.0f);
        detectWritePos = kAnalysisSize;
        analysisCounter = 0;
        cachedDetectedHz = 0.0f;

        // Reset smoothing
        smoothedRatio.reset (sr, 0.005);
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
            // ---- Write input to per-channel ring buffers ----
            for (int ch = 0; ch < numChannels; ++ch)
            {
                channels[ch].ring[static_cast<size_t>(channels[ch].writePos)] =
                    buffer.getSample (ch, i);
            }

            // ---- Feed detection buffer from channel 0 ----
            detectBuf[static_cast<size_t>(detectWritePos)] = buffer.getSample (0, i);
            detectWritePos = (detectWritePos + 1) % kBufSize;

            // ---- Periodic pitch detection ----
            ++analysisCounter;
            if (analysisCounter >= kAnalysisHop)
            {
                analysisCounter = 0;
                float hz = detectPitchYIN();
                if (hz > 0.0f)
                {
                    cachedDetectedHz = hz;
                    int period = static_cast<int>(std::round (sr / static_cast<double>(hz)));
                    period = juce::jlimit (kMinPeriod, kMaxPeriod, period);
                    grainSize = juce::jlimit (kMinGrainSize, kMaxGrainSize, period * 2);
                }
                updateCorrection();
            }

            // ---- Get smoothed ratio for this sample ----
            double ratio = smoothedRatio.getNextValue();

            // ---- Dual-head crossfading output per channel ----
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& s = channels[ch];
                int gs = grainSize;
                int halfGs = gs / 2;

                // Phase positions for the two heads (staggered by half grain)
                int phaseA = s.grainPhase;
                int phaseB = (s.grainPhase + halfGs) % gs;

                // Reset each head at its Hann zero-crossing (phase 0)
                // This is the only place positions reset — otherwise they accumulate
                if (phaseA == 0)
                    s.readPosA = static_cast<double>((s.writePos - kLatency + kBufSize) % kBufSize);
                if (phaseB == 0)
                    s.readPosB = static_cast<double>((s.writePos - kLatency + kBufSize) % kBufSize);

                // Hann crossfade windows (complementary: wA + wB ≈ 1.0)
                float wA = 0.5f - 0.5f * std::cos (2.0f * kPi * static_cast<float>(phaseA) / static_cast<float>(gs));
                float wB = 0.5f - 0.5f * std::cos (2.0f * kPi * static_cast<float>(phaseB) / static_cast<float>(gs));

                // Read from both heads with cubic interpolation
                float sA = cubicInterp (s.ring, s.readPosA);
                float sB = cubicInterp (s.ring, s.readPosB);

                // Mix via crossfade
                float output = sA * wA + sB * wB;
                buffer.setSample (ch, i, output);

                // Advance read positions at SHIFTED rate (this is the pitch shift)
                s.readPosA += ratio;
                s.readPosB += ratio;

                // Wrap read positions within buffer
                while (s.readPosA >= static_cast<double>(kBufSize)) s.readPosA -= static_cast<double>(kBufSize);
                while (s.readPosA < 0.0) s.readPosA += static_cast<double>(kBufSize);
                while (s.readPosB >= static_cast<double>(kBufSize)) s.readPosB -= static_cast<double>(kBufSize);
                while (s.readPosB < 0.0) s.readPosB += static_cast<double>(kBufSize);

                // Advance write position
                s.writePos = (s.writePos + 1) % kBufSize;

                // Advance shared grain phase
                if (ch == 0)
                    s.grainPhase = (s.grainPhase + 1) % gs;
            }

            // Sync grainPhase across channels (use channel 0 as master)
            for (int ch = 1; ch < numChannels; ++ch)
                channels[ch].grainPhase = channels[0].grainPhase;
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

    // Pitch shifting
    static constexpr int kBufSize          = 16384;
    static constexpr int kLatency          = 1024;               // Read-behind distance
    static constexpr int kMinPeriod        = 48;                 // ~1000Hz @ 48kHz
    static constexpr int kMaxPeriod        = 800;                // ~60Hz @ 48kHz
    static constexpr int kMinGrainSize     = 96;
    static constexpr int kMaxGrainSize     = 1600;
    static constexpr int kHistorySize      = 12;

    // YIN parameters
    static constexpr float kYINThreshold   = 0.10f;             // Aggressive capture

    double sr = 48000.0;
    int grainSize = 512;

    // Analysis window
    std::array<float, kAnalysisSize> analysisWindow {};

    // FFT
    juce::dsp::FFT fft { kFFTOrder };
    std::vector<float> fftWork;
    std::vector<float> yinDiff;

    // Detection buffer
    std::array<float, kBufSize> detectBuf {};
    int detectWritePos = 0;
    int analysisCounter = 0;
    float cachedDetectedHz = 0.0f;

    // Per-channel state — dual crossfading read heads
    struct ChannelState
    {
        std::array<float, kBufSize> ring {};
        int writePos = 0;
        double readPosA = 0.0;
        double readPosB = 0.0;
        int grainPhase = 0;
    };

    std::array<ChannelState, kMaxChannels> channels;

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

    // UI feedback
    float lastDetectedHz      = 0.0f;
    float lastTargetHz        = 0.0f;
    float lastCorrectionCents = 0.0f;
    std::array<float, kHistorySize> detectedHistory {};
    int histIdx = 0;

    // ========================================================================
    //  YIN pitch detection with CMND normalization (FFT-accelerated)
    // ========================================================================
    float detectPitchYIN()
    {
        // ---- Step 1: Compute autocorrelation via FFT ----
        for (int i = 0; i < kAnalysisSize; ++i)
        {
            int idx = (detectWritePos - kAnalysisSize + i + kBufSize) % kBufSize;
            fftWork[static_cast<size_t>(i)] =
                detectBuf[static_cast<size_t>(idx)] * analysisWindow[static_cast<size_t>(i)];
        }
        for (int i = kAnalysisSize; i < kFFTSize * 2; ++i)
            fftWork[static_cast<size_t>(i)] = 0.0f;

        fft.performRealOnlyForwardTransform (fftWork.data(), true);

        for (int k = 0; k < kFFTSize; ++k)
        {
            float re = fftWork[static_cast<size_t>(2 * k)];
            float im = fftWork[static_cast<size_t>(2 * k + 1)];
            fftWork[static_cast<size_t>(2 * k)]     = re * re + im * im;
            fftWork[static_cast<size_t>(2 * k + 1)] = 0.0f;
        }

        fft.performRealOnlyInverseTransform (fftWork.data());

        // fftWork[tau] now contains the autocorrelation r(tau)
        float acf0 = fftWork[0];
        if (acf0 <= 0.0f) return 0.0f;

        // ---- Step 2: Compute YIN difference function from autocorrelation ----
        // d(tau) = 2 * r(0) - 2 * r(tau)
        // (Simplified since we're using a single window — energy terms cancel)
        int halfSize = kAnalysisSize / 2;
        for (int tau = 0; tau < halfSize; ++tau)
            yinDiff[static_cast<size_t>(tau)] = 2.0f * acf0 - 2.0f * fftWork[static_cast<size_t>(tau)];

        // ---- Step 3: Cumulative Mean Normalized Difference (CMND) ----
        // d'(0) = 1, d'(tau) = d(tau) / ((1/tau) * sum(d(j), j=1..tau))
        yinDiff[0] = 1.0f;
        float runningSum = 0.0f;
        for (int tau = 1; tau < halfSize; ++tau)
        {
            runningSum += yinDiff[static_cast<size_t>(tau)];
            if (runningSum > 0.0f)
                yinDiff[static_cast<size_t>(tau)] *= static_cast<float>(tau) / runningSum;
            else
                yinDiff[static_cast<size_t>(tau)] = 1.0f;
        }

        // ---- Step 4: Absolute thresholding — find first dip below threshold ----
        int minLag = juce::jmax (2, static_cast<int>(sr / 1000.0));
        int maxLag = juce::jmin (halfSize - 2, static_cast<int>(sr / 60.0));

        int tauEstimate = -1;
        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            if (yinDiff[static_cast<size_t>(tau)] < kYINThreshold)
            {
                // Walk to the local minimum
                while (tau + 1 <= maxLag
                       && yinDiff[static_cast<size_t>(tau + 1)] < yinDiff[static_cast<size_t>(tau)])
                    ++tau;

                tauEstimate = tau;
                break;
            }
        }

        // Fallback: if no dip below threshold, find the global minimum
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
            // Only accept if reasonably good
            if (bestVal > 0.4f)
                return 0.0f;
        }

        if (tauEstimate < 1) return 0.0f;

        // ---- Step 5: Parabolic interpolation for sub-sample accuracy ----
        float betterTau = static_cast<float>(tauEstimate);
        if (tauEstimate > minLag && tauEstimate < maxLag)
        {
            float s0 = yinDiff[static_cast<size_t>(tauEstimate - 1)];
            float s1 = yinDiff[static_cast<size_t>(tauEstimate)];
            float s2 = yinDiff[static_cast<size_t>(tauEstimate + 1)];
            float denom = 2.0f * (2.0f * s1 - s2 - s0);
            if (std::abs (denom) > 1e-9f)
                betterTau += (s0 - s2) / denom;
        }

        return static_cast<float>(sr) / betterTau;
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
            double current = smoothedRatio.getCurrentValue();
            double target  = smoothedRatio.getTargetValue();
            smoothedRatio.reset (sr, rampSeconds);
            smoothedRatio.setCurrentAndTargetValue (current);
            smoothedRatio.setTargetValue (target);
        }
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
