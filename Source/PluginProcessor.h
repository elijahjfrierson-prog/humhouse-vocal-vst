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
#include "FormantShifter.h"
#include "MultibandCompressor.h"
#include "VocalReverb.h"
#include "PresetManager.h"

#include <JuceHeader.h>
#include <atomic>
#include <memory>

class HumHouseVocalsProcessor : public juce::AudioProcessor
{
public:
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

private:
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void updateModuleParameters();

    std::unique_ptr<humvocal::PresetManager> presetManager;
    std::atomic<float> uiScale { 1.0f };

    // DSP modules — signal chain order
    humvocal::PitchEngine       pitchEngine;
    humvocal::FormantShifter    formantShifter;
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

    // Pre-allocated dry buffer for dry/wet mix (avoid audio-thread allocation)
    juce::AudioBuffer<float> dryBuffer;

    // Atomic pitch feedback
    std::atomic<float> detectedPitchHz { 0.0f };
    std::atomic<float> targetPitchHz   { 0.0f };
    std::atomic<float> correctionCents { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumHouseVocalsProcessor)
};
