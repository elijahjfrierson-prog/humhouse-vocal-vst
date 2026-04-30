#pragma once

#include "DeEsser.h"
#include "LoFiFilter.h"
#include "OutputLimiter.h"
#include "PitchEngine.h"
#include "SaturationEngine.h"
#include "StereoWidth.h"
#include "TapeEmulation.h"
#include "VocalCompressor.h"
#include "VocalDelay.h"
#include "VocalDoubler.h"
#include "VisualEQ.h"
#include "MultibandCompressor.h"
#include "NoiseGate.h"
#include "VocalReverb.h"
#include "PresetManager.h"

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <array>

class HumHouseVocalsProcessor : public juce::AudioProcessor
{
public:
    // Module IDs for the reorderable effect chain
    enum ModuleID
    {
        kGate = 0, kPitch, kEQ, kComp, kMBComp, kDeEss,
        kSat, kTape, kWidth, kDoubler, kReverb, kDelay, kLoFi, kLimiter,
        kNumModules
    };

    static constexpr const char* kModuleNames[] = {
        "GATE", "TUNE", "EQ", "COMP", "MB", "DE-ESS",
        "SAT", "TAPE", "WIDTH", "DBL", "VERB", "DELAY", "LO-FI", "LIMIT"
    };

    HumHouseVocalsProcessor();
    ~HumHouseVocalsProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    humvocal::PresetManager& getPresetManager() { return *presetManager; }

    // UI scale (persisted with state)
    float getUIScale() const { return uiScale.load(); }
    void  setUIScale (float s) { uiScale.store(juce::jlimit(0.5f, 2.0f, s)); }

    // Pitch feedback for the heatmap visualizer
    float getDetectedPitchHz() const { return detectedPitchHz.load(); }
    float getTargetPitchHz() const { return targetPitchHz.load(); }
    float getCorrectionCents() const { return correctionCents.load(); }

    // Current detected note name for display
    juce::String getDetectedNoteName() const
    {
        float hz = detectedPitchHz.load();
        if (hz < 30.0f) return "--";
        static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        float midi = 69.0f + 12.0f * std::log2(hz / 440.0f);
        int note = static_cast<int>(std::round(midi));
        int octave = (note / 12) - 1;
        int pc = note % 12;
        if (pc < 0) pc += 12;
        return juce::String(names[pc]) + juce::String(octave);
    }

    // Multiband compressor gain reduction feedback for UI
    float getMBGainReduction (int band) const { return multibandComp.getGainReduction(band); }

    // Visual EQ magnitude response for UI
    float getEQMagnitudeAtFrequency (double freq) const { return visualEQ.getMagnitudeAtFrequency(freq); }
    const humvocal::VisualEQ::BandState& getEQBandState (int i) const { return visualEQ.getBandState(i); }
    static constexpr int kNumEQBands = humvocal::VisualEQ::kNumBands;

    // Effect chain order — reorderable by the UI
    std::array<int, kNumModules> getChainOrder() const { return chainOrder; }
    void setChainOrder (const std::array<int, kNumModules>& order) { chainOrder = order; }
    void swapChainModules (int posA, int posB)
    {
        if (posA >= 0 && posA < kNumModules && posB >= 0 && posB < kNumModules)
            std::swap (chainOrder[static_cast<size_t>(posA)],
                       chainOrder[static_cast<size_t>(posB)]);
    }

private:
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void updateModuleParameters();
    void processModule (int moduleId, juce::AudioBuffer<float>& buffer, bool inputSilent);

    std::unique_ptr<humvocal::PresetManager> presetManager;
    std::atomic<float> uiScale { 1.0f };

    // Effect chain order (default: Gate → Pitch → EQ → ... → Limiter)
    std::array<int, kNumModules> chainOrder = {
        kGate, kPitch, kEQ, kComp, kMBComp, kDeEss,
        kSat, kTape, kWidth, kDoubler, kReverb, kDelay, kLoFi, kLimiter
    };

    // DSP modules
    humvocal::NoiseGate         noiseGate;
    humvocal::PitchEngine       pitchEngine;
    humvocal::VisualEQ          visualEQ;
    humvocal::VocalCompressor   compressor;
    humvocal::MultibandCompressor multibandComp;
    humvocal::DeEsser           deEsser;
    humvocal::SaturationEngine  saturation;
    humvocal::TapeEmulation     tapeEmulation;
    humvocal::StereoWidth       stereoWidth;
    humvocal::VocalDoubler      doubler;
    humvocal::VocalReverb       reverb;
    humvocal::VocalDelay        delay;
    humvocal::LoFiFilter        lofiFilter;
    humvocal::OutputLimiter     limiter;

    // Pre-allocated dry buffer for dry/wet mix
    juce::AudioBuffer<float> dryBuffer;

    // Dry path delay line — compensates for pitch engine latency
    juce::AudioBuffer<float> dryDelayBuffer;
    int dryDelayWritePos = 0;
    int dryDelaySize = 0;

    // Atomic pitch feedback
    std::atomic<float> detectedPitchHz { 0.0f };
    std::atomic<float> targetPitchHz   { 0.0f };
    std::atomic<float> correctionCents { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumHouseVocalsProcessor)
};
