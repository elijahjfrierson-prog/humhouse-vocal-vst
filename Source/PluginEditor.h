#pragma once

#include "HumHouseLookAndFeel.h"
#include "PluginProcessor.h"

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <vector>

class HumHouseVocalsEditor : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit HumHouseVocalsEditor (HumHouseVocalsProcessor&);
    ~HumHouseVocalsEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    // Module strip — a labelled section with on/off toggle
    class ModuleStrip : public juce::Component
    {
    public:
        ModuleStrip (const juce::String& name);
        void paint (juce::Graphics&) override;
        void resized() override;

        juce::ToggleButton activeButton;
        juce::OwnedArray<juce::Slider> knobs;
        juce::OwnedArray<juce::Label>  knobLabels;
        juce::OwnedArray<juce::ComboBox> combos;

        void addKnob (const juce::String& label, const juce::String& tooltip = {});
        void addCombo (const juce::StringArray& items);

    private:
        juce::String moduleName;
    };

    // =======================================================================
    // ChainStrip — draggable effect chain reordering UI
    // =======================================================================
    class ChainStrip : public juce::Component
    {
    public:
        ChainStrip (HumHouseVocalsProcessor& p) : proc (p) {}
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseMove (const juce::MouseEvent& e) override;
    private:
        HumHouseVocalsProcessor& proc;
        int dragSlot = -1;
        int hoverSlot = -1;
        int dropTarget = -1;
        juce::Point<int> dragOffset;

        juce::Rectangle<int> getChipBounds (int slot) const;
        int slotAtPosition (int x) const;
    };

    void timerCallback() override;

    HumHouseVocalsProcessor& processorRef;
    humvocal::HumHouseLookAndFeel lnf;

    juce::Label titleLabel     { {}, "HUMHOUSE  VOCALS" };
    juce::Label subtitleLabel  { {}, "tone \u00b7 shape \u00b7 space" };

    // Preset controls (top-right corner)
    juce::ComboBox presetBox;
    juce::TextButton savePresetBtn   { "Save" };
    juce::TextButton deletePresetBtn { "Del" };

    // UI scale controls
    juce::Slider     uiScaleSlider;
    juce::Label      uiScaleLabel { {}, "UI Scale" };
    juce::TextButton scaleDownBtn { "-" };
    juce::TextButton scaleUpBtn   { "+" };

    void refreshPresetList();
    void applyUIScale (float newScale);
    static constexpr int kBaseWidth  = 1100;
    static constexpr int kBaseHeight = 820;

    ChainStrip   chainStrip;

    // Visual EQ display component with draggable band dots
    class EQCurveDisplay : public juce::Component
    {
    public:
        EQCurveDisplay (HumHouseVocalsProcessor& p) : proc (p) {}
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseMove (const juce::MouseEvent& e) override;
    private:
        HumHouseVocalsProcessor& proc;
        int dragBand = -1;
        int hoverBand = -1;
        static constexpr float kMinFreq = 20.0f;
        static constexpr float kMaxFreq = 20000.0f;
        static constexpr float kDbRange = 24.0f;

        float freqToX (float freq, float width) const;
        float xToFreq (float x, float width) const;
        float gainToY (float gainDb, float height) const;
        float yToGain (float y, float height) const;
        int findBandAt (float x, float y) const;
    };

    // Multiband compressor meter display
    class MBMeterDisplay : public juce::Component
    {
    public:
        MBMeterDisplay (HumHouseVocalsProcessor& p) : proc (p) {}
        void paint (juce::Graphics& g) override;
    private:
        HumHouseVocalsProcessor& proc;
    };

    EQCurveDisplay eqCurveDisplay;
    MBMeterDisplay mbMeterDisplay;

    // EQ Detail Section — 12 Q knobs + 12 dynamic toggle buttons
    class EQDetailSection : public juce::Component
    {
    public:
        EQDetailSection();
        void paint (juce::Graphics& g) override;
        void resized() override;

        std::array<juce::Slider, 12>        qKnobs;
        std::array<juce::Label, 12>         qLabels;
        std::array<juce::ToggleButton, 12>  dynButtons;
        std::array<juce::Label, 12>         bandLabels;
    };

    EQDetailSection eqDetailSection;

    // Module strips
    ModuleStrip gateStrip     { "GATE" };
    ModuleStrip eqStrip       { "VISUAL EQ" };
    ModuleStrip compStrip     { "COMP" };
    ModuleStrip mbCompStrip   { "MULTIBAND" };
    ModuleStrip deEsserStrip  { "DE-ESS" };
    ModuleStrip satStrip      { "SATURATE" };
    ModuleStrip tapeStrip     { "TAPE" };
    ModuleStrip widthStrip    { "WIDTH" };
    ModuleStrip doublerStrip  { "DOUBLER" };
    ModuleStrip reverbStrip   { "REVERB" };
    ModuleStrip convStrip     { "CONV" };
    ModuleStrip delayStrip    { "DELAY" };
    ModuleStrip lofiStrip     { "LO-FI" };
    ModuleStrip limiterStrip  { "LIMITER" };


    // Master section
    juce::Slider inputGainSlider, outputGainSlider, dryWetSlider;
    juce::Label  inputGainLabel  { {}, "INPUT" };
    juce::Label  outputGainLabel { {}, "OUTPUT" };
    juce::Label  dryWetLabel     { {}, "DRY/WET" };

    // APVTS attachments
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>>  sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>>  buttonAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;

    void setupModuleStrips();
    void attachParameters();
    void setupPresetControls();
    void setupScaleControls();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumHouseVocalsEditor)
};
