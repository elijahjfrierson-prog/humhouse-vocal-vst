#pragma once

#include "ConvolverReverb.h"
#include "DeEsser.h"
#include "LoFiFilter.h"
#include "OutputLimiter.h"
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
        kGate = 0, kEQ, kComp, kMBComp, kDeEss,
        kSat, kTape, kWidth, kDoubler, kReverb, kConvolver, kDelay, kLoFi, kLimiter,
        kNumModules
    };

    static constexpr const char* kModuleNames[] = {
        "GATE", "EQ", "COMP", "MB", "DE-ESS",
        "SAT", "TAPE", "WIDTH", "DBL", "VERB", "CONV", "DELAY", "LO-FI", "LIMIT"
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


    // Multiband compressor gain reduction feedback for UI
    float getMBGainReduction (int band) const { return multibandComp.getGainReduction(band); }

    // Visual EQ magnitude response for UI
    float getEQMagnitudeAtFrequency (double freq) const { return visualEQ.getMagnitudeAtFrequency(freq); }
    const humvocal::VisualEQ::BandState& getEQBandState (int i) const { return visualEQ.getBandState(i); }
    static constexpr int kNumEQBands = humvocal::VisualEQ::kNumBands;

    // Effect chain order — reorderable by the UI (thread-safe via SpinLock)
    std::array<int, kNumModules> getChainOrder() const
    {
        juce::SpinLock::ScopedLockType lock (chainLock);
        return chainOrder;
    }
    void setChainOrder (const std::array<int, kNumModules>& order)
    {
        juce::SpinLock::ScopedLockType lock (chainLock);
        chainOrder = order;
    }
    void swapChainModules (int posA, int posB)
    {
        juce::SpinLock::ScopedLockType lock (chainLock);
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
    mutable juce::SpinLock chainLock;
    std::array<int, kNumModules> chainOrder = {
        kGate, kEQ, kComp, kMBComp, kDeEss,
        kSat, kTape, kWidth, kDoubler, kReverb, kConvolver, kDelay, kLoFi, kLimiter
    };

    // DSP modules
    humvocal::NoiseGate         noiseGate;
    humvocal::VisualEQ          visualEQ;
    humvocal::VocalCompressor   compressor;
    humvocal::MultibandCompressor multibandComp;
    humvocal::DeEsser           deEsser;
    humvocal::SaturationEngine  saturation;
    humvocal::TapeEmulation     tapeEmulation;
    humvocal::StereoWidth       stereoWidth;
    humvocal::VocalDoubler      doubler;
    humvocal::VocalReverb       reverb;
    humvocal::ConvolverReverb   convolverReverb;
    humvocal::VocalDelay        delay;
    humvocal::LoFiFilter        lofiFilter;
    humvocal::OutputLimiter     limiter;

    // Pre-allocated dry buffer for dry/wet mix
    juce::AudioBuffer<float> dryBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumHouseVocalsProcessor)
};
