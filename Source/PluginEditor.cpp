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

void HumHouseVocalsEditor::ModuleStrip::addKnob (const juce::String& label)
{
    auto* knob = knobs.add(new juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag,
                                             juce::Slider::NoTextBox));
    knob->setPopupDisplayEnabled(true, true, this);
    addAndMakeVisible(knob);

    auto* lbl = knobLabels.add(new juce::Label({}, label));
    lbl->setJustificationType(juce::Justification::centred);
    lbl->setFont(juce::Font(10.0f).italicised());
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

    // Visual EQ curve display
    addAndMakeVisible(eqCurveDisplay);
    // Multiband compressor meter display
    addAndMakeVisible(mbMeterDisplay);

    // Scale selectors
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

    startTimerHz(30);
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
    // PITCH — Speed, Humanize, Snap, Sustain, Detune
    pitchStrip.addKnob("SPEED");
    pitchStrip.addKnob("HUMAN");
    pitchStrip.addKnob("SNAP");
    pitchStrip.addKnob("SUSTAIN");
    pitchStrip.addKnob("DETUNE");
    addAndMakeVisible(pitchStrip);

    // FORMANT — Shift, Mix, Smooth
    formantStrip.addKnob("SHIFT");
    formantStrip.addKnob("MIX");
    formantStrip.addKnob("SMOOTH");
    addAndMakeVisible(formantStrip);

    // VISUAL EQ — 12 bands are controlled via the EQ curve display; strip just has master gain
    eqStrip.addKnob("BAND 1");
    eqStrip.addKnob("BAND 6");
    eqStrip.addKnob("BAND 12");
    addAndMakeVisible(eqStrip);

    // COMP — Threshold, Ratio, Attack, Release, Makeup
    compStrip.addKnob("THRESH");
    compStrip.addKnob("RATIO");
    compStrip.addKnob("ATK");
    compStrip.addKnob("REL");
    compStrip.addKnob("MAKEUP");
    compStrip.addCombo({"THD Off","THD Soft","THD Hard"});
    addAndMakeVisible(compStrip);

    // MULTIBAND COMP — abbreviated controls
    mbCompStrip.addKnob("BODY");
    mbCompStrip.addKnob("MUD");
    mbCompStrip.addKnob("CLARITY");
    mbCompStrip.addKnob("PRES");
    mbCompStrip.addKnob("AIR");
    addAndMakeVisible(mbCompStrip);

    // DE-ESSER — Freq, Threshold, Reduction
    deEsserStrip.addKnob("FREQ");
    deEsserStrip.addKnob("THRESH");
    deEsserStrip.addKnob("REDUCE");
    addAndMakeVisible(deEsserStrip);

    // SATURATION — Drive, Mix
    satStrip.addKnob("DRIVE");
    satStrip.addKnob("MIX");
    satStrip.addCombo({"Tube","Tape","Transformer"});
    addAndMakeVisible(satStrip);

    // TAPE — IPS, Flutter, Drive
    tapeStrip.addKnob("IPS");
    tapeStrip.addKnob("FLUTTER");
    tapeStrip.addKnob("DRIVE");
    addAndMakeVisible(tapeStrip);

    // WIDTH — Amount
    widthStrip.addKnob("AMOUNT");
    widthStrip.addCombo({"M/S","Haas","Freq Spread"});
    addAndMakeVisible(widthStrip);

    // DOUBLER — Mix, Detune, Delay
    doublerStrip.addKnob("MIX");
    doublerStrip.addKnob("DETUNE");
    doublerStrip.addKnob("DELAY");
    addAndMakeVisible(doublerStrip);

    // REVERB — Short, Long, Duck
    reverbStrip.addKnob("SHORT");
    reverbStrip.addKnob("LONG");
    reverbStrip.addKnob("DUCK");
    addAndMakeVisible(reverbStrip);

    // DELAY — Time, Feedback, Mix, Duck
    delayStrip.addKnob("TIME");
    delayStrip.addKnob("FB");
    delayStrip.addKnob("MIX");
    delayStrip.addKnob("DUCK");
    addAndMakeVisible(delayStrip);

    // LO-FI — HP, LP, Bits, DS
    lofiStrip.addKnob("HP");
    lofiStrip.addKnob("LP");
    lofiStrip.addKnob("BITS");
    lofiStrip.addKnob("CRUSH");
    addAndMakeVisible(lofiStrip);

    // LIMITER — Ceiling, Release
    limiterStrip.addKnob("CEIL");
    limiterStrip.addKnob("REL");
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
    attachCombo(*compStrip.combos[0], "thdMode");

    // Multiband Compressor
    attachButton(mbCompStrip.activeButton, "mbActive");
    attachSlider(*mbCompStrip.knobs[0], "mbThresh1");
    attachSlider(*mbCompStrip.knobs[1], "mbThresh2");
    attachSlider(*mbCompStrip.knobs[2], "mbThresh3");
    attachSlider(*mbCompStrip.knobs[3], "mbThresh4");
    attachSlider(*mbCompStrip.knobs[4], "mbThresh5");

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

    // Scale selectors
    attachCombo(rootNoteBox, "rootNote");
    attachCombo(scaleTypeBox, "scaleType");

    // Master
    attachSlider(inputGainSlider, "inputGain");
    attachSlider(outputGainSlider, "outputGain");
    attachSlider(dryWetSlider, "dryWet");
}

// ===========================================================================
// EQ Curve Display — draws the 12-band frequency response
// ===========================================================================
void HumHouseVocalsEditor::EQCurveDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(juce::Colour(humvocal::HumHousePalette::kPanel));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Grid lines
    g.setColour(juce::Colour(humvocal::HumHousePalette::kModuleBorder).withAlpha(0.3f));
    // Horizontal 0dB line
    float midY = bounds.getCentreY();
    g.drawHorizontalLine(static_cast<int>(midY), bounds.getX() + 4, bounds.getRight() - 4);
    // Frequency grid: 100, 1k, 10k
    for (float freq : { 100.0f, 1000.0f, 10000.0f })
    {
        float normX = (std::log2(freq / 20.0f)) / (std::log2(20000.0f / 20.0f));
        float x = bounds.getX() + normX * bounds.getWidth();
        g.drawVerticalLine(static_cast<int>(x), bounds.getY() + 4, bounds.getBottom() - 4);
    }

    // Draw magnitude response curve
    juce::Path curvePath;
    bool started = false;
    float dbRange = 24.0f;

    for (float px = 0; px < bounds.getWidth(); px += 1.0f)
    {
        float normX = px / bounds.getWidth();
        double freq = 20.0 * std::pow(20000.0 / 20.0, static_cast<double>(normX));
        float mag = proc.getEQMagnitudeAtFrequency(freq);
        float db = juce::Decibels::gainToDecibels(mag, -dbRange);
        float normY = 1.0f - ((db + dbRange) / (2.0f * dbRange));
        float y = bounds.getY() + normY * bounds.getHeight();

        if (!started)
        {
            curvePath.startNewSubPath(bounds.getX() + px, y);
            started = true;
        }
        else
        {
            curvePath.lineTo(bounds.getX() + px, y);
        }
    }

    g.setColour(juce::Colour(humvocal::HumHousePalette::kAccent).withAlpha(0.8f));
    g.strokePath(curvePath, juce::PathStrokeType(2.0f));

    // Fill under curve
    juce::Path fillPath (curvePath);
    fillPath.lineTo(bounds.getRight(), bounds.getBottom());
    fillPath.lineTo(bounds.getX(), bounds.getBottom());
    fillPath.closeSubPath();
    g.setColour(juce::Colour(humvocal::HumHousePalette::kAccent).withAlpha(0.15f));
    g.fillPath(fillPath);

    // Border
    g.setColour(juce::Colour(humvocal::HumHousePalette::kModuleBorder));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    // Title
    g.setColour(juce::Colour(humvocal::HumHousePalette::kBone));
    g.setFont(juce::Font(10.0f).boldened());
    g.drawText("12-BAND VISUAL EQ", bounds.reduced(6, 2), juce::Justification::topLeft);
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

    // Pitch heatmap
    auto heatArea = area.removeFromTop(36).reduced(20, 4);
    // Scale selectors on the left of heatmap
    auto musicalScaleArea = heatArea.removeFromLeft(140);
    rootNoteBox.setBounds(musicalScaleArea.removeFromLeft(65).reduced(2));
    scaleTypeBox.setBounds(musicalScaleArea.reduced(2));
    pitchHeatMap.setBounds(heatArea);

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
    auto vizArea = area.removeFromTop(120).reduced(10, 4);
    auto eqVizArea = vizArea.removeFromLeft(vizArea.getWidth() * 2 / 3);
    eqCurveDisplay.setBounds(eqVizArea.reduced(2));
    mbMeterDisplay.setBounds(vizArea.reduced(2));

    // Module strips — 2 rows of 7
    auto stripArea = area.reduced(10, 4);
    int stripH = stripArea.getHeight() / 2;
    int stripW = stripArea.getWidth() / 7;

    ModuleStrip* row1[] = { &pitchStrip, &formantStrip, &eqStrip, &compStrip, &mbCompStrip, &deEsserStrip, &satStrip };
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
