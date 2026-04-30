#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>

namespace humvocal
{

// ============================================================================
// PitchEngine v3 — Autotalent-inspired algorithm
//
// Detection:  FFT-based normalized autocorrelation  (O(N log N), not O(N²))
// Shifting:   Twin-grain overlap-add with Hann window (SOLA-style)
//
// This replaces the dual-head ring buffer approach which caused clicks
// and the O(N²) YIN detector which killed CPU.
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

        // Pre-compute Hann windows
        for (int i = 0; i < kAnalysisSize; ++i)
            analysisWindow[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos (2.0f * kPi * static_cast<float>(i) / static_cast<float>(kAnalysisSize));

        for (int i = 0; i < kGrainSize; ++i)
            grainWindow[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos (2.0f * kPi * static_cast<float>(i) / static_cast<float>(kGrainSize));

        // FFT work buffer
        fftWork.assign (static_cast<size_t>(kFFTSize * 2), 0.0f);

        // Reset per-channel state
        for (auto& ch : channels)
        {
            ch.inputBuf.fill (0.0f);
            ch.writePos = kGrainSize + kGrainHop;
            ch.grains[0] = {};
            ch.grains[1] = {};
            ch.grainCounter = 1; // trigger first grain immediately
            ch.nextGrainIdx = 0;
        }

        // Reset detection
        detectBuf.fill (0.0f);
        detectWritePos = kAnalysisSize;
        analysisCounter = 0;
        cachedDetectedHz = 0.0f;

        // Reset correction
        smoothedRatio = 1.0;
        targetRatio = 1.0;

        // Compute smooth coefficient for default retune speed
        updateSmoothCoeff();

        // Reset note history
        detectedHistory.fill (0.0f);
        histIdx = 0;
        lastDetectedHz = 0.0f;
        lastTargetHz = 0.0f;
        lastCorrectionCents = 0.0f;
    }

    // --- Parameter setters (same API as before) ---
    void setReferenceFrequency (float hz)     { referenceFreq = hz; }
    void setRootNote (int note)               { rootNote = note % 12; }
    void setScaleType (int type)              { scaleType = type; }
    void setRetuneSpeed (float speed01)       { retuneSpeed = speed01; updateSmoothCoeff(); }
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
            // ---- Write input to per-channel circular buffers & detection buffer ----
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& state = channels[ch];
                state.inputBuf[static_cast<size_t>(state.writePos)] = buffer.getSample (ch, i);
            }

            // Detection uses channel 0 only
            detectBuf[static_cast<size_t>(detectWritePos)] = buffer.getSample (0, i);
            detectWritePos = (detectWritePos + 1) % kBufSize;

            // ---- Periodic pitch detection (every kAnalysisHop samples) ----
            ++analysisCounter;
            if (analysisCounter >= kAnalysisHop)
            {
                analysisCounter = 0;
                float hz = detectPitchFFT();
                if (hz > 0.0f)
                    cachedDetectedHz = hz;
                updateCorrection();
            }

            // ---- Smooth the correction ratio ----
            smoothedRatio += smoothCoeff * (targetRatio - smoothedRatio);

            // ---- Generate output via grain OLA (per channel) ----
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& state = channels[ch];
                float output = 0.0f;

                // Sum contributions from active grains
                for (auto& grain : state.grains)
                {
                    if (! grain.active)
                        continue;

                    float sample = cubicInterp (state.inputBuf, grain.readPos);
                    float window = grainWindow[static_cast<size_t>(grain.phase)];
                    output += sample * window;

                    // Advance read position at shifted rate
                    grain.readPos += smoothedRatio;
                    while (grain.readPos >= static_cast<double>(kBufSize))
                        grain.readPos -= static_cast<double>(kBufSize);
                    while (grain.readPos < 0.0)
                        grain.readPos += static_cast<double>(kBufSize);

                    ++grain.phase;
                    if (grain.phase >= kGrainSize)
                        grain.active = false;
                }

                // Start new grain every kGrainHop output samples
                --state.grainCounter;
                if (state.grainCounter <= 0)
                {
                    state.grainCounter = kGrainHop;
                    auto& g = state.grains[static_cast<size_t>(state.nextGrainIdx)];
                    g.active = true;
                    g.phase = 0;
                    // Start reading from kGrainSize samples behind current write position
                    g.readPos = static_cast<double>((state.writePos - kGrainSize + kBufSize) % kBufSize);
                    state.nextGrainIdx = 1 - state.nextGrainIdx;
                }

                buffer.setSample (ch, i, output);

                // Advance write pointer
                state.writePos = (state.writePos + 1) % kBufSize;
            }
        }
    }

private:
    // ---- Constants ----
    static constexpr float kPi = juce::MathConstants<float>::pi;
    static constexpr int kMaxChannels   = 2;

    // FFT pitch detection
    static constexpr int kFFTOrder      = 12;                    // 2^12 = 4096
    static constexpr int kFFTSize       = 1 << kFFTOrder;        // 4096
    static constexpr int kAnalysisSize  = kFFTSize / 2;          // 2048 samples analyzed
    static constexpr int kAnalysisHop   = 512;                   // detect every 512 samples (~10ms @ 48kHz)

    // Grain OLA pitch shifting
    static constexpr int kGrainSize     = 1024;                  // ~21ms @ 48kHz
    static constexpr int kGrainHop      = kGrainSize / 2;        // 50% overlap
    static constexpr int kBufSize       = 8192;                   // Circular buffer size

    // History for note stabilizer
    static constexpr int kHistorySize   = 12;

    // ---- State ----
    double sr = 48000.0;

    // Pre-computed windows
    std::array<float, kAnalysisSize> analysisWindow {};
    std::array<float, kGrainSize>    grainWindow {};

    // FFT
    juce::dsp::FFT fft { kFFTOrder };
    std::vector<float> fftWork;

    // Detection buffer (channel 0)
    std::array<float, kBufSize> detectBuf {};
    int detectWritePos = 0;
    int analysisCounter = 0;
    float cachedDetectedHz = 0.0f;

    // Per-channel grain OLA state
    struct Grain
    {
        double readPos = 0.0;
        int    phase   = 0;
        bool   active  = false;
    };

    struct ChannelState
    {
        std::array<float, kBufSize> inputBuf {};
        int writePos = 0;
        std::array<Grain, 2> grains {};
        int grainCounter = 0;
        int nextGrainIdx = 0;
    };

    std::array<ChannelState, kMaxChannels> channels;

    // Shared pitch correction
    double smoothedRatio = 1.0;
    double targetRatio   = 1.0;
    double smoothCoeff   = 0.01;

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
    //  FFT-based pitch detection (normalized autocorrelation)
    //  O(N log N) instead of O(N²) YIN
    // ========================================================================
    float detectPitchFFT()
    {
        // Fill first half of FFT buffer with windowed input from detection buffer
        for (int i = 0; i < kAnalysisSize; ++i)
        {
            int idx = (detectWritePos - kAnalysisSize + i + kBufSize) % kBufSize;
            fftWork[static_cast<size_t>(i)] = detectBuf[static_cast<size_t>(idx)] * analysisWindow[static_cast<size_t>(i)];
        }
        // Zero-pad the second half (for linear autocorrelation)
        for (int i = kAnalysisSize; i < kFFTSize; ++i)
            fftWork[static_cast<size_t>(i)] = 0.0f;

        // Clear the complex part workspace
        for (int i = kFFTSize; i < kFFTSize * 2; ++i)
            fftWork[static_cast<size_t>(i)] = 0.0f;

        // Forward FFT (real → complex)
        fft.performRealOnlyForwardTransform (fftWork.data(), true);

        // Compute power spectrum (magnitude²) in-place
        for (int k = 0; k < kFFTSize; ++k)
        {
            float re = fftWork[static_cast<size_t>(2 * k)];
            float im = fftWork[static_cast<size_t>(2 * k + 1)];
            fftWork[static_cast<size_t>(2 * k)]     = re * re + im * im;
            fftWork[static_cast<size_t>(2 * k + 1)] = 0.0f;
        }

        // Inverse FFT → autocorrelation
        fft.performRealOnlyInverseTransform (fftWork.data());

        // Normalize by zero-lag value
        float acf0 = fftWork[0];
        if (acf0 <= 0.0f)
            return 0.0f;

        float invAcf0 = 1.0f / acf0;

        // Search for the first strong peak between minLag and maxLag
        int minLag = static_cast<int>(sr / 1000.0);  // ~1kHz max vocal pitch
        int maxLag = static_cast<int>(sr / 60.0);     // ~60Hz min vocal pitch
        maxLag = juce::jmin (maxLag, kAnalysisSize - 1);

        // Find the highest peak in the valid lag range
        float bestVal = 0.0f;
        int   bestLag = 0;

        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            float val = fftWork[static_cast<size_t>(lag)] * invAcf0;

            if (val > bestVal)
            {
                bestVal = val;
                bestLag = lag;
            }
        }

        // Confidence check — reject weak or unvoiced
        if (bestVal < 0.3f || bestLag < 1)
            return 0.0f;

        // Parabolic interpolation for sub-sample accuracy
        float betterLag = static_cast<float>(bestLag);
        if (bestLag > minLag && bestLag < maxLag)
        {
            float y0 = fftWork[static_cast<size_t>(bestLag - 1)] * invAcf0;
            float y1 = bestVal;
            float y2 = fftWork[static_cast<size_t>(bestLag + 1)] * invAcf0;
            float denom = 2.0f * (2.0f * y1 - y2 - y0);
            if (std::abs (denom) > 1e-9f)
                betterLag += (y0 - y2) / denom;
        }

        return static_cast<float>(sr) / betterLag;
    }

    // ========================================================================
    //  Determine correction ratio from detected pitch
    // ========================================================================
    void updateCorrection()
    {
        float detectedHz = cachedDetectedHz;
        lastDetectedHz = detectedHz;

        double newTargetRatio = 1.0;

        if (detectedHz >= 60.0f && detectedHz <= 1200.0f)
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
                    float centsDiff = 1200.0f * std::log2 (detectedHz / avg);
                    float lockZone = 30.0f + pitchSustain * 50.0f;
                    if (std::abs (centsDiff) < lockZone)
                        stableHz = avg;
                }
            }

            // Find target note in scale
            float targetHz = findTargetFrequency (stableHz);
            lastTargetHz = targetHz;

            // Correction in cents
            float corrCents = 1200.0f * std::log2 (targetHz / stableHz);
            lastCorrectionCents = corrCents;

            // Apply humanize (reduce correction) and snap (strength)
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

        targetRatio = newTargetRatio;
    }

    // ========================================================================
    //  Update smooth coefficient when retune speed changes
    // ========================================================================
    void updateSmoothCoeff()
    {
        // Speed 0 → 50ms convergence (natural), Speed 1 → 0.3ms (robotic/instant)
        double tc = 0.0003 + (1.0 - static_cast<double>(retuneSpeed))
                           * (1.0 - static_cast<double>(retuneSpeed)) * 0.05;
        if (sr > 0.0)
            smoothCoeff = 1.0 - std::exp (-1.0 / (sr * tc));
    }

    // ========================================================================
    //  Cubic interpolation from circular buffer
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

        // Catmull-Rom cubic interpolation
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
