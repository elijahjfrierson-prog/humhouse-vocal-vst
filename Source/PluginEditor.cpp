#include "PluginEditor.h"

// ===========================================================================
// PitchHeatMap
// ===========================================================================
HumHouseVocalsEditor::PitchHeatMap::PitchHeatMap() {}

void HumHouseVocalsEditor::PitchHeatMap::pushSample (float detectedHz, float targetHz, float cents)
{
    history[static_cast<size_t>(writeIdx)] = { detectedHz, targetHz, cents };
    writeIdx = (writeIdx + 1) % kHistorySize;
}

void HumHouseVocalsEditor::PitchHeatMap::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(humvocal::HumHousePalette::kPanel));
    g.fillRoundedRectangle(bounds, 4.0f);

    float barW = bounds.getWidth() / static_cast<float>(kHistorySize);

    for (int i = 0; i < kHistorySize; ++i)
    {
        int idx = (writeIdx + i) % kHistorySize;
        auto& s = history[static_cast<size_t>(idx)];

        juce::Colour col;
        if (s.detected < 50.0f)
        {
            col = juce::Colour(humvocal::HumHousePalette::kPanel);
        }
        else
        {
            float centsClamped = juce::jlimit(-100.0f, 100.0f, s.cents);
            float t = (centsClamped + 100.0f) / 200.0f; // 0 = flat, 0.5 = in tune, 1 = sharp

            if (t < 0.5f)
                col = juce::Colour(humvocal::HumHousePalette::kHeatCold).interpolatedWith(
                    juce::Colour(humvocal::HumHousePalette::kHeatNeutral), t * 2.0f);
            else
                col = juce::Colour(humvocal::HumHousePalette::kHeatNeutral).interpolatedWith(
                    juce::Colour(humvocal::HumHousePalette::kHeatHot), (t - 0.5f) * 2.0f);
        }

        float x = bounds.getX() + static_cast<float>(i) * barW;
        g.setColour(col);
        g.fillRect(x, bounds.getY() + 2.0f, barW, bounds.getHeight() - 4.0f);
    }

    // Border
    g.setColour(juce::Colour(humvocal::HumHousePalette::kModuleBorder));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
}

// ===========================================================================
// ModuleStrip
// ===========================================================================
HumHouseVocalsEditor::ModuleStrip::ModuleStrip (const juce::String& name)
    : moduleName (name)
{
    activeButton.setButtonText(name);
    addAndMakeVisible(activeButton);
}

void HumHouseVocalsEditor::ModuleStrip::addKnob (const juce::String& label, const juce::String& tooltip)
{
    auto* knob = knobs.add(new juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag,
                                             juce::Slider::NoTextBox));
    knob->setPopupDisplayEnabled(true, true, this);
    if (tooltip.isNotEmpty())
        knob->setTooltip(tooltip);
    else
        knob->setTooltip(label);
    addAndMakeVisible(knob);

    auto* lbl = knobLabels.add(new juce::Label({}, label));
    lbl->setJustificationType(juce::Justification::centred);
    lbl->setFont(juce::Font(10.0f).italicised());
    if (tooltip.isNotEmpty())
        lbl->setTooltip(tooltip);
    else
        lbl->setTooltip(label);
    addAndMakeVisible(lbl);
}

void HumHouseVocalsEditor::ModuleStrip::addCombo (const juce::StringArray& items)
{
    auto* combo = combos.add(new juce::ComboBox());
    combo->addItemList(items, 1);
    addAndMakeVisible(combo);
}

void HumHouseVocalsEditor::ModuleStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    bool isOn = activeButton.getToggleState();

    g.setColour(juce::Colour(isOn ? humvocal::HumHousePalette::kModuleActive
                                  : humvocal::HumHousePalette::kModuleInactive));
    g.fillRoundedRectangle(bounds, 6.0f);

    g.setColour(juce::Colour(humvocal::HumHousePalette::kModuleBorder));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
}

void HumHouseVocalsEditor::ModuleStrip::resized()
{
    auto area = getLocalBounds().reduced(4);

    // Toggle button at top
    activeButton.setBounds(area.removeFromTop(22));

    // Combos (if any) below the toggle
    for (auto* combo : combos)
    {
        combo->setBounds(area.removeFromTop(22).reduced(2, 0));
    }

    // Knobs fill remaining space
    if (knobs.size() > 0)
    {
        int knobH = area.getHeight() - 14;
        int knobW = area.getWidth() / std::max(1, knobs.size());

        for (int i = 0; i < knobs.size(); ++i)
        {
            auto col = area.removeFromLeft(knobW);
            auto knobArea = col.removeFromTop(knobH - 14);
            knobs[i]->setBounds(knobArea.reduced(2));
            if (i < knobLabels.size())
                knobLabels[i]->setBounds(col.removeFromTop(14));
        }
    }
}

// ===========================================================================
// Editor
// ===========================================================================
HumHouseVocalsEditor::HumHouseVocalsEditor (HumHouseVocalsProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      autoTuneSection (p),
      eqCurveDisplay (p),
      mbMeterDisplay (p)
{
    setLookAndFeel(&lnf);
    setSize(1100, 720);

    // Title
    titleLabel.setFont(juce::Font(28.0f).boldened().italicised());
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(humvocal::HumHousePalette::kAccent));
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    subtitleLabel.setFont(juce::Font(12.0f).italicised());
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colour(humvocal::HumHousePalette::kMuted));
    subtitleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(subtitleLabel);

    // Pitch heatmap
    addAndMakeVisible(pitchHeatMap);

    // AutoTune section
    addAndMakeVisible(autoTuneSection);

    // Visual EQ curve display
    addAndMakeVisible(eqCurveDisplay);
    // Multiband compressor meter display
    addAndMakeVisible(mbMeterDisplay);

    // Scale selectors (inside AutoTune section area)
    rootNoteBox.addItemList({"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}, 1);
    rootNoteBox.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(rootNoteBox);

    scaleTypeBox.addItemList({"Major","Minor","Chromatic"}, 1);
    scaleTypeBox.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(scaleTypeBox);

    // Master section
    for (auto* s : { &inputGainSlider, &outputGainSlider, &dryWetSlider })
    {
        s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s->setPopupDisplayEnabled(true, true, this);
        addAndMakeVisible(s);
    }
    for (auto* l : { &inputGainLabel, &outputGainLabel, &dryWetLabel })
    {
        l->setJustificationType(juce::Justification::centred);
        l->setFont(juce::Font(10.0f).italicised());
        addAndMakeVisible(l);
    }

    setupModuleStrips();
    attachParameters();
    setupPresetControls();
    setupScaleControls();

    // Restore persisted UI scale
    applyUIScale(processorRef.getUIScale());

    startTimerHz(15);
}

// ===========================================================================
// Preset controls
// ===========================================================================
void HumHouseVocalsEditor::setupPresetControls()
{
    addAndMakeVisible(presetBox);
    refreshPresetList();

    presetBox.onChange = [this]
    {
        int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0)
            processorRef.getPresetManager().loadPreset(idx);
    };

    savePresetBtn.setColour(juce::TextButton::buttonColourId,
                            juce::Colour(humvocal::HumHousePalette::kAccentDeep));
    addAndMakeVisible(savePresetBtn);
    savePresetBtn.onClick = [this]
    {
        auto dlg = std::make_shared<juce::AlertWindow>("Save Preset",
            "Enter a name for the preset:",
            juce::MessageBoxIconType::QuestionIcon);
        dlg->addTextEditor("name", "My Preset");
        dlg->addButton("Save", 1);
        dlg->addButton("Cancel", 0);
        dlg->enterModalState(true,
            juce::ModalCallbackFunction::create([this, dlg](int result)
            {
                if (result == 1)
                {
                    auto name = dlg->getTextEditorContents("name");
                    if (name.isNotEmpty())
                    {
                        processorRef.getPresetManager().savePreset(name);
                        refreshPresetList();
                        // Select the newly saved preset
                        for (int i = 0; i < presetBox.getNumItems(); ++i)
                        {
                            if (presetBox.getItemText(i) == name)
                            {
                                presetBox.setSelectedItemIndex(i, juce::dontSendNotification);
                                break;
                            }
                        }
                    }
                }
            }));
    };

    deletePresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colours::darkred);
    addAndMakeVisible(deletePresetBtn);
    deletePresetBtn.onClick = [this]
    {
        int idx = presetBox.getSelectedItemIndex();
        if (idx < 0) return;
        if (processorRef.getPresetManager().deletePreset(idx))
        {
            refreshPresetList();
            if (presetBox.getNumItems() > 0)
                presetBox.setSelectedItemIndex(0, juce::dontSendNotification);
        }
    };
}

void HumHouseVocalsEditor::refreshPresetList()
{
    presetBox.clear(juce::dontSendNotification);
    auto names = processorRef.getPresetManager().getPresetNames();
    for (int i = 0; i < names.size(); ++i)
        presetBox.addItem(names[i], i + 1);
    if (presetBox.getNumItems() > 0)
        presetBox.setSelectedItemIndex(0, juce::dontSendNotification);
}

// ===========================================================================
// UI Scale controls
// ===========================================================================
void HumHouseVocalsEditor::setupScaleControls()
{
    uiScaleSlider.setRange(0.5, 2.0, 0.1);
    uiScaleSlider.setValue(processorRef.getUIScale(), juce::dontSendNotification);
    uiScaleSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    uiScaleSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 36, 18);
    uiScaleSlider.setColour(juce::Slider::textBoxTextColourId,
                            juce::Colour(humvocal::HumHousePalette::kBone));
    addAndMakeVisible(uiScaleSlider);

    uiScaleSlider.onValueChange = [this]
    {
        float s = static_cast<float>(uiScaleSlider.getValue());
        applyUIScale(s);
    };

    uiScaleLabel.setFont(juce::Font(10.0f).italicised());
    uiScaleLabel.setColour(juce::Label::textColourId,
                           juce::Colour(humvocal::HumHousePalette::kMuted));
    uiScaleLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(uiScaleLabel);

    scaleDownBtn.setColour(juce::TextButton::buttonColourId,
                           juce::Colour(humvocal::HumHousePalette::kPanel));
    addAndMakeVisible(scaleDownBtn);
    scaleDownBtn.onClick = [this]
    {
        float s = static_cast<float>(uiScaleSlider.getValue()) - 0.1f;
        uiScaleSlider.setValue(s);
    };

    scaleUpBtn.setColour(juce::TextButton::buttonColourId,
                         juce::Colour(humvocal::HumHousePalette::kPanel));
    addAndMakeVisible(scaleUpBtn);
    scaleUpBtn.onClick = [this]
    {
        float s = static_cast<float>(uiScaleSlider.getValue()) + 0.1f;
        uiScaleSlider.setValue(s);
    };
}

void HumHouseVocalsEditor::applyUIScale (float newScale)
{
    newScale = juce::jlimit(0.5f, 2.0f, newScale);
    processorRef.setUIScale(newScale);
    int w = static_cast<int>(static_cast<float>(kBaseWidth)  * newScale);
    int h = static_cast<int>(static_cast<float>(kBaseHeight) * newScale);
    setSize(w, h);
}

HumHouseVocalsEditor::~HumHouseVocalsEditor()
{
    setLookAndFeel(nullptr);
}

// ===========================================================================
// Setup module strips with knobs
// ===========================================================================
void HumHouseVocalsEditor::setupModuleStrips()
{
    // AUTO-TUNE — Retune Speed, Humanize, Snap, Sustain, Detune
    pitchStrip.addKnob("RETUNE", "Retune Speed - How fast pitch corrects");
    pitchStrip.addKnob("HUMAN", "Humanize - Natural variation amount");
    pitchStrip.addKnob("SNAP", "Snap Amount - Pitch correction strength");
    pitchStrip.addKnob("SUSTAIN", "Sustain - Note hold stability");
    pitchStrip.addKnob("DETUNE", "Detune - Reference frequency (Hz)");
    addAndMakeVisible(pitchStrip);

    // NOISE GATE — Threshold, Ratio, Attack, Hold, Release, Range
    gateStrip.addKnob("THRESH", "Gate Threshold (dB)");
    gateStrip.addKnob("RATIO", "Gate Ratio (100 = hard gate)");
    gateStrip.addKnob("ATK", "Gate Attack Time (ms)");
    gateStrip.addKnob("HOLD", "Gate Hold Time (ms)");
    gateStrip.addKnob("REL", "Gate Release Time (ms)");
    gateStrip.addKnob("RANGE", "Gate Range - Max attenuation (dB)");
    addAndMakeVisible(gateStrip);

    // FORMANT — Shift, Mix, Smooth
    formantStrip.addKnob("SHIFT", "Formant Shift (semitones)");
    formantStrip.addKnob("MIX", "Formant Mix - Wet/Dry blend");
    formantStrip.addKnob("SMOOTH", "Formant Smoothing");
    addChildComponent(formantStrip);

    // VISUAL EQ — 12 bands are controlled via the EQ curve display; strip just has master gain
    eqStrip.addKnob("BAND 1", "EQ Band 1 Gain (Low - 30 Hz)");
    eqStrip.addKnob("BAND 6", "EQ Band 6 Gain (Mid - 800 Hz)");
    eqStrip.addKnob("BAND 12", "EQ Band 12 Gain (High - 16 kHz)");
    addAndMakeVisible(eqStrip);

    // COMP — Threshold, Ratio, Attack, Release, Makeup
    compStrip.addKnob("THRESH", "Threshold (dB)");
    compStrip.addKnob("RATIO", "Compression Ratio");
    compStrip.addKnob("ATK", "Attack Time (ms)");
    compStrip.addKnob("REL", "Release Time (ms)");
    compStrip.addKnob("MAKEUP", "Makeup Gain (dB)");
    compStrip.addKnob("GAIN", "Compressor Output Gain (dB)");
    compStrip.addCombo({"THD Off","THD Soft","THD Hard"});
    addAndMakeVisible(compStrip);

    // MULTIBAND COMP — abbreviated controls
    mbCompStrip.addKnob("BODY", "Body Band (0-200 Hz)");
    mbCompStrip.addKnob("MUD", "Mud Band (200-600 Hz)");
    mbCompStrip.addKnob("CLARITY", "Clarity Band (600-3k Hz)");
    mbCompStrip.addKnob("PRES", "Presence Band (3k-8k Hz)");
    mbCompStrip.addKnob("AIR", "Air Band (8k+ Hz)");
    mbCompStrip.addKnob("GAIN", "Multiband Output Gain (dB)");
    addAndMakeVisible(mbCompStrip);

    // DE-ESSER — Freq, Threshold, Reduction
    deEsserStrip.addKnob("FREQ", "De-Esser Center Frequency (Hz)");
    deEsserStrip.addKnob("THRESH", "De-Esser Threshold (dB)");
    deEsserStrip.addKnob("REDUCE", "De-Esser Reduction Amount (dB)");
    addAndMakeVisible(deEsserStrip);

    // SATURATION — Drive, Mix
    satStrip.addKnob("DRIVE", "Saturation Drive Amount");
    satStrip.addKnob("MIX", "Saturation Mix - Wet/Dry blend");
    satStrip.addCombo({"Tube","Tape","Transformer"});
    addAndMakeVisible(satStrip);

    // TAPE — IPS, Flutter, Drive
    tapeStrip.addKnob("IPS", "Tape Speed (inches per second)");
    tapeStrip.addKnob("FLUTTER", "Tape Flutter Amount");
    tapeStrip.addKnob("DRIVE", "Tape Drive/Saturation");
    addAndMakeVisible(tapeStrip);

    // WIDTH — Amount
    widthStrip.addKnob("AMOUNT", "Stereo Width Amount");
    widthStrip.addCombo({"M/S","Haas","Freq Spread"});
    addAndMakeVisible(widthStrip);

    // DOUBLER — Mix, Detune, Delay
    doublerStrip.addKnob("MIX", "Doubler Mix - Wet/Dry blend");
    doublerStrip.addKnob("DETUNE", "Doubler Detune Amount (cents)");
    doublerStrip.addKnob("DELAY", "Doubler Delay Time (ms)");
    addAndMakeVisible(doublerStrip);

    // REVERB — Short, Long, Duck
    reverbStrip.addKnob("SHORT", "Short Reverb Decay");
    reverbStrip.addKnob("LONG", "Long Reverb Decay");
    reverbStrip.addKnob("DUCK", "Reverb Ducking Amount");
    addAndMakeVisible(reverbStrip);

    // DELAY — Time, Feedback, Mix, Duck
    delayStrip.addKnob("TIME", "Delay Time (ms)");
    delayStrip.addKnob("FB", "Delay Feedback Amount");
    delayStrip.addKnob("MIX", "Delay Mix - Wet/Dry blend");
    delayStrip.addKnob("DUCK", "Delay Ducking Amount");
    addAndMakeVisible(delayStrip);

    // LO-FI — HP, LP, Bits, DS
    lofiStrip.addKnob("HP", "Lo-Fi High Pass Filter (Hz)");
    lofiStrip.addKnob("LP", "Lo-Fi Low Pass Filter (Hz)");
    lofiStrip.addKnob("BITS", "Bit Depth Reduction");
    lofiStrip.addKnob("CRUSH", "Sample Rate Crush");
    addAndMakeVisible(lofiStrip);

    // LIMITER — Ceiling, Release
    limiterStrip.addKnob("CEIL", "Limiter Ceiling (dB)");
    limiterStrip.addKnob("REL", "Limiter Release Time (ms)");
    limiterStrip.addKnob("GAIN", "Limiter Output Gain (dB)");
    addAndMakeVisible(limiterStrip);
}

// ===========================================================================
// Attach APVTS parameters to UI controls
// ===========================================================================
void HumHouseVocalsEditor::attachParameters()
{
    auto& apvts = processorRef.getAPVTS();

    auto attachSlider = [&](juce::Slider& slider, const juce::String& paramId) {
        sliderAttachments.push_back(
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramId, slider));
    };

    auto attachButton = [&](juce::ToggleButton& button, const juce::String& paramId) {
        buttonAttachments.push_back(
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, paramId, button));
    };

    auto attachCombo = [&](juce::ComboBox& combo, const juce::String& paramId) {
        comboAttachments.push_back(
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, paramId, combo));
    };

    // Pitch
    attachButton(pitchStrip.activeButton, "pitchActive");
    attachSlider(*pitchStrip.knobs[0], "retuneSpeed");
    attachSlider(*pitchStrip.knobs[1], "humanize");
    attachSlider(*pitchStrip.knobs[2], "snapAmount");
    attachSlider(*pitchStrip.knobs[3], "pitchSustain");
    attachSlider(*pitchStrip.knobs[4], "detune");

    // Noise Gate
    attachButton(gateStrip.activeButton, "gateActive");
    attachSlider(*gateStrip.knobs[0], "gateThreshold");
    attachSlider(*gateStrip.knobs[1], "gateRatio");
    attachSlider(*gateStrip.knobs[2], "gateAttack");
    attachSlider(*gateStrip.knobs[3], "gateHold");
    attachSlider(*gateStrip.knobs[4], "gateRelease");
    attachSlider(*gateStrip.knobs[5], "gateRange");

    // Formant
    attachButton(formantStrip.activeButton, "formantActive");
    attachSlider(*formantStrip.knobs[0], "formantShift");
    attachSlider(*formantStrip.knobs[1], "formantMix");
    attachSlider(*formantStrip.knobs[2], "formantSmooth");

    // Visual EQ (quick access bands 1, 6, 12)
    attachButton(eqStrip.activeButton, "veqActive");
    attachSlider(*eqStrip.knobs[0], "veqG1");
    attachSlider(*eqStrip.knobs[1], "veqG6");
    attachSlider(*eqStrip.knobs[2], "veqG12");

    // Compressor
    attachButton(compStrip.activeButton, "compActive");
    attachSlider(*compStrip.knobs[0], "compThreshold");
    attachSlider(*compStrip.knobs[1], "compRatio");
    attachSlider(*compStrip.knobs[2], "compAttack");
    attachSlider(*compStrip.knobs[3], "compRelease");
    attachSlider(*compStrip.knobs[4], "compMakeup");
    attachSlider(*compStrip.knobs[5], "compOutputGain");
    attachCombo(*compStrip.combos[0], "thdMode");

    // Multiband Compressor
    attachButton(mbCompStrip.activeButton, "mbActive");
    attachSlider(*mbCompStrip.knobs[0], "mbThresh1");
    attachSlider(*mbCompStrip.knobs[1], "mbThresh2");
    attachSlider(*mbCompStrip.knobs[2], "mbThresh3");
    attachSlider(*mbCompStrip.knobs[3], "mbThresh4");
    attachSlider(*mbCompStrip.knobs[4], "mbThresh5");
    attachSlider(*mbCompStrip.knobs[5], "mbOutputGain");

    // De-Esser
    attachButton(deEsserStrip.activeButton, "deEsserActive");
    attachSlider(*deEsserStrip.knobs[0], "deEsserFreq");
    attachSlider(*deEsserStrip.knobs[1], "deEsserThresh");
    attachSlider(*deEsserStrip.knobs[2], "deEsserReduce");

    // Saturation
    attachButton(satStrip.activeButton, "satActive");
    attachSlider(*satStrip.knobs[0], "satDrive");
    attachSlider(*satStrip.knobs[1], "satMix");
    attachCombo(*satStrip.combos[0], "satMode");

    // Tape
    attachButton(tapeStrip.activeButton, "tapeActive");
    attachSlider(*tapeStrip.knobs[0], "tapeSpeed");
    attachSlider(*tapeStrip.knobs[1], "tapeFlutter");
    attachSlider(*tapeStrip.knobs[2], "tapeDrive");

    // Width
    attachButton(widthStrip.activeButton, "widthActive");
    attachSlider(*widthStrip.knobs[0], "widthAmount");
    attachCombo(*widthStrip.combos[0], "widthMode");

    // Doubler
    attachButton(doublerStrip.activeButton, "doublerActive");
    attachSlider(*doublerStrip.knobs[0], "doublerMix");
    attachSlider(*doublerStrip.knobs[1], "doublerDetune");
    attachSlider(*doublerStrip.knobs[2], "doublerDelay");

    // Reverb
    attachButton(reverbStrip.activeButton, "reverbActive");
    attachSlider(*reverbStrip.knobs[0], "reverbShortMix");
    attachSlider(*reverbStrip.knobs[1], "reverbLongMix");
    attachSlider(*reverbStrip.knobs[2], "reverbDuck");

    // Delay
    attachButton(delayStrip.activeButton, "delayActive");
    attachSlider(*delayStrip.knobs[0], "delayTime");
    attachSlider(*delayStrip.knobs[1], "delayFeedback");
    attachSlider(*delayStrip.knobs[2], "delayMix");
    attachSlider(*delayStrip.knobs[3], "delayDuck");

    // Lo-Fi
    attachButton(lofiStrip.activeButton, "lofiActive");
    attachSlider(*lofiStrip.knobs[0], "lofiHP");
    attachSlider(*lofiStrip.knobs[1], "lofiLP");
    attachSlider(*lofiStrip.knobs[2], "lofiBits");
    attachSlider(*lofiStrip.knobs[3], "lofiDS");

    // Limiter
    attachButton(limiterStrip.activeButton, "limiterActive");
    attachSlider(*limiterStrip.knobs[0], "limiterCeiling");
    attachSlider(*limiterStrip.knobs[1], "limiterRelease");
    attachSlider(*limiterStrip.knobs[2], "limiterOutputGain");

    // Scale selectors
    attachCombo(rootNoteBox, "rootNote");
    attachCombo(scaleTypeBox, "scaleType");

    // Master
    attachSlider(inputGainSlider, "inputGain");
    attachSlider(outputGainSlider, "outputGain");
    attachSlider(dryWetSlider, "dryWet");
}

// ===========================================================================
// AutoTune Section — prominent detected note display + pitch info
// ===========================================================================
void HumHouseVocalsEditor::AutoTuneSection::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Dark panel background
    g.setColour(juce::Colour(humvocal::HumHousePalette::kPanel));
    g.fillRoundedRectangle(bounds, 8.0f);

    // Accent border
    g.setColour(juce::Colour(humvocal::HumHousePalette::kAccentDeep));
    g.drawRoundedRectangle(bounds, 8.0f, 1.5f);

    // Title
    g.setColour(juce::Colour(humvocal::HumHousePalette::kAccent));
    g.setFont(juce::Font(11.0f).boldened());
    g.drawText("AUTO-TUNE", bounds.reduced(10, 4), juce::Justification::topLeft);

    // Large detected note display
    auto noteName = proc.getDetectedNoteName();
    float cents = proc.getCorrectionCents();
    float hz = proc.getDetectedPitchHz();

    // Note name — big and centered
    auto noteArea = bounds.reduced(8).withTrimmedTop(16);
    g.setColour(juce::Colour(humvocal::HumHousePalette::kBone));
    g.setFont(juce::Font(36.0f).boldened());
    g.drawText(noteName, noteArea.removeFromLeft(noteArea.getWidth() * 0.4f),
               juce::Justification::centred);

    // Cents deviation indicator
    auto infoArea = noteArea;
    float centsClamped = juce::jlimit(-50.0f, 50.0f, cents);
    juce::Colour centsCol;
    if (std::abs(centsClamped) < 5.0f)
        centsCol = juce::Colour(0xff2aaa2a); // green — in tune
    else if (std::abs(centsClamped) < 20.0f)
        centsCol = juce::Colour(0xffaaaa2a); // yellow — close
    else
        centsCol = juce::Colour(0xffaa3a2a); // red — off

    g.setColour(centsCol);
    g.setFont(juce::Font(16.0f).boldened());
    juce::String centsStr = (cents >= 0 ? "+" : "") + juce::String(cents, 0) + " ct";
    g.drawText(centsStr, infoArea.removeFromTop(infoArea.getHeight() / 2),
               juce::Justification::centredLeft);

    // Frequency readout
    g.setColour(juce::Colour(humvocal::HumHousePalette::kMuted));
    g.setFont(juce::Font(12.0f));
    juce::String hzStr = hz > 50.0f ? juce::String(hz, 1) + " Hz" : "-- Hz";
    g.drawText(hzStr, infoArea, juce::Justification::centredLeft);
}

// ===========================================================================
// EQ Curve Display — interactive 12-band with draggable dot handles
// ===========================================================================
float HumHouseVocalsEditor::EQCurveDisplay::freqToX (float freq, float width) const
{
    return (std::log2(freq / kMinFreq)) / (std::log2(kMaxFreq / kMinFreq)) * width;
}

float HumHouseVocalsEditor::EQCurveDisplay::xToFreq (float x, float width) const
{
    float norm = juce::jlimit(0.0f, 1.0f, x / width);
    return kMinFreq * std::pow(kMaxFreq / kMinFreq, norm);
}

float HumHouseVocalsEditor::EQCurveDisplay::gainToY (float gainDb, float height) const
{
    return (1.0f - ((gainDb + kDbRange) / (2.0f * kDbRange))) * height;
}

float HumHouseVocalsEditor::EQCurveDisplay::yToGain (float y, float height) const
{
    float norm = juce::jlimit(0.0f, 1.0f, y / height);
    return (1.0f - norm) * 2.0f * kDbRange - kDbRange;
}

int HumHouseVocalsEditor::EQCurveDisplay::findBandAt (float mx, float my) const
{
    auto bounds = getLocalBounds().toFloat();
    float hitRadius = 10.0f;
    int closest = -1;
    float closestDist = hitRadius * hitRadius;

    for (int i = 0; i < HumHouseVocalsProcessor::kNumEQBands; ++i)
    {
        auto& bs = proc.getEQBandState(i);
        float bx = bounds.getX() + freqToX(bs.frequency, bounds.getWidth());
        float by = bounds.getY() + gainToY(bs.gain, bounds.getHeight());
        float dx = mx - bx;
        float dy = my - by;
        float dist = dx * dx + dy * dy;
        if (dist < closestDist)
        {
            closestDist = dist;
            closest = i;
        }
    }
    return closest;
}

void HumHouseVocalsEditor::EQCurveDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragBand = findBandAt(static_cast<float>(e.x), static_cast<float>(e.y));
}

void HumHouseVocalsEditor::EQCurveDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0) return;
    auto bounds = getLocalBounds().toFloat();
    float newFreq = xToFreq(static_cast<float>(e.x) - bounds.getX(), bounds.getWidth());
    float newGain = yToGain(static_cast<float>(e.y) - bounds.getY(), bounds.getHeight());
    newFreq = juce::jlimit(kMinFreq, kMaxFreq, newFreq);
    newGain = juce::jlimit(-kDbRange, kDbRange, newGain);

    auto si = juce::String(dragBand + 1);
    if (auto* pFreq = proc.getAPVTS().getParameter("veqF" + si))
        pFreq->setValueNotifyingHost(pFreq->convertTo0to1(newFreq));
    if (auto* pGain = proc.getAPVTS().getParameter("veqG" + si))
        pGain->setValueNotifyingHost(pGain->convertTo0to1(newGain));

    repaint();
}

void HumHouseVocalsEditor::EQCurveDisplay::mouseUp (const juce::MouseEvent&)
{
    dragBand = -1;
}

void HumHouseVocalsEditor::EQCurveDisplay::mouseMove (const juce::MouseEvent& e)
{
    int newHover = findBandAt(static_cast<float>(e.x), static_cast<float>(e.y));
    if (newHover != hoverBand)
    {
        hoverBand = newHover;
        setMouseCursor(hoverBand >= 0 ? juce::MouseCursor::PointingHandCursor
                                      : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void HumHouseVocalsEditor::EQCurveDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(juce::Colour(humvocal::HumHousePalette::kPanel));
    g.fillRoundedRectangle(bounds, 6.0f);

    // ---- Grid ----
    g.setFont(juce::Font(7.0f));

    // dB grid lines: -12, -6, 0, +6, +12
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        float y = bounds.getY() + gainToY(db, bounds.getHeight());
        g.setColour(juce::Colour(humvocal::HumHousePalette::kModuleBorder).withAlpha(db == 0.0f ? 0.5f : 0.2f));
        g.drawHorizontalLine(static_cast<int>(y), bounds.getX() + 4, bounds.getRight() - 4);

        g.setColour(juce::Colour(humvocal::HumHousePalette::kMuted).withAlpha(0.6f));
        juce::String label = (db > 0 ? "+" : "") + juce::String(static_cast<int>(db));
        g.drawText(label, static_cast<int>(bounds.getX() + 2), static_cast<int>(y - 6), 22, 12, juce::Justification::left);
    }

    // Frequency grid
    for (float freq : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        float x = bounds.getX() + freqToX(freq, bounds.getWidth());
        g.setColour(juce::Colour(humvocal::HumHousePalette::kModuleBorder).withAlpha(0.2f));
        g.drawVerticalLine(static_cast<int>(x), bounds.getY() + 4, bounds.getBottom() - 10);

        g.setColour(juce::Colour(humvocal::HumHousePalette::kMuted).withAlpha(0.6f));
        juce::String label = freq >= 1000.0f ? juce::String(static_cast<int>(freq / 1000)) + "k"
                                              : juce::String(static_cast<int>(freq));
        g.drawText(label, static_cast<int>(x - 10), static_cast<int>(bounds.getBottom() - 11), 20, 10, juce::Justification::centred);
    }

    // ---- Magnitude response curve ----
    juce::Path curvePath;
    bool started = false;

    for (float px = 0; px < bounds.getWidth(); px += 1.0f)
    {
        float normX = px / bounds.getWidth();
        double freq = static_cast<double>(kMinFreq) * std::pow(static_cast<double>(kMaxFreq / kMinFreq), static_cast<double>(normX));
        float mag = proc.getEQMagnitudeAtFrequency(freq);
        float db = juce::Decibels::gainToDecibels(mag, -kDbRange);
        float y = bounds.getY() + gainToY(db, bounds.getHeight());

        if (!started) { curvePath.startNewSubPath(bounds.getX() + px, y); started = true; }
        else          { curvePath.lineTo(bounds.getX() + px, y); }
    }

    g.setColour(juce::Colour(humvocal::HumHousePalette::kAccent).withAlpha(0.8f));
    g.strokePath(curvePath, juce::PathStrokeType(2.0f));

    // Fill under curve
    juce::Path fillPath(curvePath);
    fillPath.lineTo(bounds.getRight(), bounds.getBottom());
    fillPath.lineTo(bounds.getX(), bounds.getBottom());
    fillPath.closeSubPath();
    g.setColour(juce::Colour(humvocal::HumHousePalette::kAccent).withAlpha(0.1f));
    g.fillPath(fillPath);

    // ---- Draggable band dots ----
    const auto accent = juce::Colour(humvocal::HumHousePalette::kAccent);
    const auto bone = juce::Colour(humvocal::HumHousePalette::kBone);

    for (int i = 0; i < HumHouseVocalsProcessor::kNumEQBands; ++i)
    {
        auto& bs = proc.getEQBandState(i);
        float dotX = bounds.getX() + freqToX(bs.frequency, bounds.getWidth());
        float dotY = bounds.getY() + gainToY(bs.gain, bounds.getHeight());
        float radius = (i == dragBand || i == hoverBand) ? 7.0f : 5.0f;

        // Outer glow for active/hovered
        if (i == dragBand || i == hoverBand)
        {
            g.setColour(accent.withAlpha(0.3f));
            g.fillEllipse(dotX - radius - 2, dotY - radius - 2, (radius + 2) * 2, (radius + 2) * 2);
        }

        // Dot fill
        g.setColour(accent);
        g.fillEllipse(dotX - radius, dotY - radius, radius * 2, radius * 2);

        // Dot border
        g.setColour(bone);
        g.drawEllipse(dotX - radius, dotY - radius, radius * 2, radius * 2, 1.0f);

        // Band number inside dot
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(radius > 5.0f ? 9.0f : 7.0f).boldened());
        g.drawText(juce::String(i + 1), static_cast<int>(dotX - radius), static_cast<int>(dotY - radius),
                   static_cast<int>(radius * 2), static_cast<int>(radius * 2), juce::Justification::centred);
    }

    // Border
    g.setColour(juce::Colour(humvocal::HumHousePalette::kModuleBorder));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    // Title
    g.setColour(bone);
    g.setFont(juce::Font(10.0f).boldened());
    g.drawText("EQUALIZER", bounds.reduced(6, 2), juce::Justification::topLeft);
}

// ===========================================================================
// Multiband Compressor Meter Display — 3D-style bar meters
// ===========================================================================
void HumHouseVocalsEditor::MBMeterDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background with 3D gradient
    juce::ColourGradient bgGrad (
        juce::Colour(humvocal::HumHousePalette::kPanel).brighter(0.1f), bounds.getX(), bounds.getY(),
        juce::Colour(humvocal::HumHousePalette::kInk), bounds.getRight(), bounds.getBottom(), false);
    g.setGradientFill(bgGrad);
    g.fillRoundedRectangle(bounds, 6.0f);

    const char* bandNames[] = { "BODY", "MUD", "CLAR", "PRES", "AIR" };
    int numBands = 5;
    float barW = (bounds.getWidth() - 12.0f) / static_cast<float>(numBands);
    float maxBarH = bounds.getHeight() - 30.0f;

    for (int b = 0; b < numBands; ++b)
    {
        float gr = proc.getMBGainReduction(b); // negative dB
        float normGR = juce::jlimit(0.0f, 1.0f, std::abs(gr) / 30.0f);

        float x = bounds.getX() + 6.0f + static_cast<float>(b) * barW;
        float barH = normGR * maxBarH;
        float y = bounds.getBottom() - 16.0f - barH;

        // 3D bar with gradient
        juce::ColourGradient barGrad (
            juce::Colour(humvocal::HumHousePalette::kAccent), x, y,
            juce::Colour(humvocal::HumHousePalette::kAccentDeep), x + barW - 4.0f, y + barH, false);
        g.setGradientFill(barGrad);
        g.fillRoundedRectangle(x, y, barW - 4.0f, barH, 3.0f);

        // 3D shadow
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillRoundedRectangle(x + 2.0f, y + 2.0f, barW - 4.0f, barH, 3.0f);

        // Re-draw bar on top of shadow
        g.setGradientFill(barGrad);
        g.fillRoundedRectangle(x, y, barW - 4.0f, barH, 3.0f);

        // Band label
        g.setColour(juce::Colour(humvocal::HumHousePalette::kMuted));
        g.setFont(juce::Font(8.0f).boldened());
        g.drawText(bandNames[b], static_cast<int>(x), static_cast<int>(bounds.getBottom() - 14.0f),
                   static_cast<int>(barW - 4.0f), 12, juce::Justification::centred);
    }

    // Border
    g.setColour(juce::Colour(humvocal::HumHousePalette::kModuleBorder));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    // Title
    g.setColour(juce::Colour(humvocal::HumHousePalette::kBone));
    g.setFont(juce::Font(10.0f).boldened());
    g.drawText("MULTIBAND COMP", bounds.reduced(6, 2), juce::Justification::topLeft);
}

// ===========================================================================
// Timer — update pitch heatmap + visual displays
// ===========================================================================
void HumHouseVocalsEditor::timerCallback()
{
    pitchHeatMap.pushSample(
        processorRef.getDetectedPitchHz(),
        processorRef.getTargetPitchHz(),
        processorRef.getCorrectionCents());
    pitchHeatMap.repaint();
    autoTuneSection.repaint();
    eqCurveDisplay.repaint();
    mbMeterDisplay.repaint();
}

// ===========================================================================
// Paint — sunburst background + branding
// ===========================================================================
void HumHouseVocalsEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Sunburst gradient (left-to-right: black → amber → gold)
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(humvocal::HumHousePalette::kSunLeft),  bounds.getX(), bounds.getCentreY(),
        juce::Colour(humvocal::HumHousePalette::kSunRight), bounds.getRight(), bounds.getCentreY(),
        false));
    g.fillRect(bounds);

    // Darken overlay for readability
    g.setColour(juce::Colour(humvocal::HumHousePalette::kInk).withAlpha(0.75f));
    g.fillRect(bounds);

    // Thin gold separator under title
    g.setColour(juce::Colour(humvocal::HumHousePalette::kAccentDeep));
    g.fillRect(bounds.getX() + 20.0f, 60.0f, bounds.getWidth() - 40.0f, 1.0f);

    // Signal flow arrow labels
    g.setColour(juce::Colour(humvocal::HumHousePalette::kMuted));
    g.setFont(juce::Font(9.0f).italicised());
    g.drawText("SIGNAL FLOW \u2192", 10, 92, 100, 14, juce::Justification::centredLeft);
}

// ===========================================================================
// Layout
// ===========================================================================
void HumHouseVocalsEditor::resized()
{
    auto area = getLocalBounds();

    // Title bar (with preset controls on the right)
    auto titleArea = area.removeFromTop(62);

    // Preset controls in the top-right
    auto presetArea = titleArea.removeFromRight(360);
    auto presetRow1 = presetArea.removeFromTop(30).reduced(4, 4);
    deletePresetBtn.setBounds(presetRow1.removeFromRight(40));
    savePresetBtn.setBounds(presetRow1.removeFromRight(50).reduced(2, 0));
    presetBox.setBounds(presetRow1);

    // Scale controls below presets
    auto scaleCtrlArea = presetArea.removeFromTop(26).reduced(4, 2);
    uiScaleLabel.setBounds(scaleCtrlArea.removeFromLeft(50));
    scaleDownBtn.setBounds(scaleCtrlArea.removeFromLeft(22));
    uiScaleSlider.setBounds(scaleCtrlArea.removeFromLeft(100));
    scaleUpBtn.setBounds(scaleCtrlArea.removeFromLeft(22));

    titleLabel.setBounds(titleArea.removeFromTop(36));
    subtitleLabel.setBounds(titleArea);

    // === AutoTune Section: note display + heatmap + key/scale selectors ===
    auto autoArea = area.removeFromTop(100).reduced(10, 4);
    // Left: AutoTune note display panel
    autoTuneSection.setBounds(autoArea.removeFromLeft(220).reduced(2));
    // Center: Key + Scale selectors stacked above the heatmap
    auto keyScaleArea = autoArea.removeFromLeft(130);
    rootNoteBox.setBounds(keyScaleArea.removeFromTop(28).reduced(4, 2));
    scaleTypeBox.setBounds(keyScaleArea.removeFromTop(28).reduced(4, 2));
    // Rest: Pitch heatmap
    pitchHeatMap.setBounds(autoArea.reduced(4, 2));

    // Master controls at bottom
    auto masterArea = area.removeFromBottom(80).reduced(20, 0);
    int masterKnobW = 70;
    auto inArea = masterArea.removeFromLeft(masterKnobW);
    inputGainSlider.setBounds(inArea.removeFromTop(55));
    inputGainLabel.setBounds(inArea);

    auto outArea = masterArea.removeFromLeft(masterKnobW);
    outputGainSlider.setBounds(outArea.removeFromTop(55));
    outputGainLabel.setBounds(outArea);

    auto dwArea = masterArea.removeFromLeft(masterKnobW);
    dryWetSlider.setBounds(dwArea.removeFromTop(55));
    dryWetLabel.setBounds(dwArea);

    // EQ Curve display + MB Meter display
    auto vizArea = area.removeFromTop(140).reduced(10, 4);
    auto eqVizArea = vizArea.removeFromLeft(vizArea.getWidth() * 2 / 3);
    eqCurveDisplay.setBounds(eqVizArea.reduced(2));
    mbMeterDisplay.setBounds(vizArea.reduced(2));

    // Module strips — 2 rows of 7
    auto stripArea = area.reduced(10, 4);
    int stripH = stripArea.getHeight() / 2;
    int stripW = stripArea.getWidth() / 7;

    ModuleStrip* row1[] = { &pitchStrip, &gateStrip, &eqStrip, &compStrip, &mbCompStrip, &deEsserStrip, &satStrip };
    ModuleStrip* row2[] = { &tapeStrip, &widthStrip, &doublerStrip, &reverbStrip, &delayStrip, &lofiStrip, &limiterStrip };

    auto row1Area = stripArea.removeFromTop(stripH);
    for (auto* strip : row1)
    {
        strip->setBounds(row1Area.removeFromLeft(stripW).reduced(3));
    }

    for (auto* strip : row2)
    {
        strip->setBounds(stripArea.removeFromLeft(stripW).reduced(3));
    }
}
