#include "PluginProcessor.h"
#include "PluginEditor.h"

// ---------------------------------------------------------------------------
// Parameter layout — every automatable knob in the plugin
// ---------------------------------------------------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout
HumHouseVocalsProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // --- PITCH CORRECTION ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("pitchActive",    "Pitch Active",    true));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("retuneSpeed",    "Retune Speed",    0.0f, 1.0f, 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("humanize",       "Humanize",        0.0f, 1.0f, 0.2f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("snapAmount",     "Snap",            0.0f, 1.0f, 0.8f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("pitchSustain",   "Sustain",         0.0f, 1.0f, 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("detune",         "Detune (Hz)",     400.0f, 500.0f, 440.0f));
    params.push_back (std::make_unique<juce::AudioParameterInt>   ("rootNote",       "Root Note",       0, 11, 0));   // C..B
    params.push_back (std::make_unique<juce::AudioParameterInt>   ("scaleType",      "Scale Type",      0, 2, 0));    // Major/Minor/Chromatic
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("noteStabilizer", "Note Stabilizer", true));
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("formantPreserve","Formant Preserve",true));

    // --- FORMANT SHIFTER ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("formantActive",    "Formant Active",    false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("formantShift",     "Formant Shift",     -12.0f, 12.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("formantMix",       "Formant Mix",       0.0f, 1.0f, 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("formantSmooth",    "Formant Smooth",    0.0f, 1.0f, 0.3f));

    // --- VISUAL EQ (12-band) ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("veqActive",  "Visual EQ Active",  true));
    for (int i = 0; i < 12; ++i)
    {
        auto si = juce::String(i + 1);
        float defaultFreqs[] = { 30.f, 80.f, 160.f, 300.f, 500.f, 800.f, 1200.f, 2500.f, 4000.f, 6000.f, 10000.f, 16000.f };
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("veqF" + si, "VEQ Freq " + si,  20.0f, 20000.0f, defaultFreqs[i]));
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("veqG" + si, "VEQ Gain " + si,  -24.0f, 24.0f, 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("veqQ" + si, "VEQ Q " + si,     0.1f, 30.0f, 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterInt>   ("veqT" + si, "VEQ Type " + si,  0, 5, (i == 0) ? 4 : (i == 11) ? 3 : 0)); // HP, LP, or Bell
    }

    // --- COMPRESSOR (single-band) ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("compActive",    "Comp Active",    true));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("compThreshold", "Comp Threshold", -60.0f, 0.0f, -18.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("compRatio",     "Comp Ratio",     1.0f, 20.0f, 4.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("compAttack",    "Comp Attack",    0.1f, 100.0f, 5.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("compRelease",   "Comp Release",   10.0f, 500.0f, 50.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("compMakeup",    "Comp Makeup",    -12.0f, 24.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("compKnee",      "Comp Knee",      0.0f, 24.0f, 6.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("compAutoGain",  "Comp Auto-Gain", true));
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("autoLevel",     "Auto-Level",     false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("autoLevelTarget","Auto-Level Target",-30.0f, 0.0f, -14.0f));
    params.push_back (std::make_unique<juce::AudioParameterInt>   ("thdMode",       "THD Mode",       0, 2, 0)); // off/soft/hard

    // --- DE-ESSER ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("deEsserActive", "De-Esser Active", true));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("deEsserFreq",   "De-Esser Freq",   3000.0f, 12000.0f, 7000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("deEsserThresh", "De-Esser Thresh",  -40.0f, 0.0f, -20.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("deEsserReduce", "De-Esser Reduce",  -24.0f, 0.0f, -12.0f));

    // --- SATURATION ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("satActive", "Saturation Active", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("satDrive",  "Sat Drive",         0.0f, 1.0f, 0.3f));
    params.push_back (std::make_unique<juce::AudioParameterInt>   ("satMode",   "Sat Mode",          0, 2, 0)); // Tube/Tape/Transformer
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("satMix",    "Sat Mix",           0.0f, 1.0f, 0.5f));

    // --- TAPE EMULATION ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("tapeActive",  "Tape Active",  false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("tapeSpeed",   "Tape IPS",     15.0f, 30.0f, 30.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("tapeFlutter", "Tape Flutter", 0.0f, 1.0f, 0.3f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("tapeDrive",   "Tape Drive",   0.0f, 1.0f, 0.2f));

    // --- STEREO WIDTH ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("widthActive", "Width Active", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("widthAmount", "Width Amount", 0.0f, 2.0f, 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterInt>   ("widthMode",   "Width Mode",   0, 2, 0)); // M/S, Haas, FreqSpread

    // --- DOUBLER ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("doublerActive", "Doubler Active", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("doublerMix",    "Doubler Mix",    0.0f, 1.0f, 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("doublerDetune", "Doubler Detune", 0.0f, 50.0f, 10.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("doublerDelay",  "Doubler Delay",  5.0f, 50.0f, 20.0f));

    // --- REVERB ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("reverbActive",    "Reverb Active",    false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("reverbShortSize", "Reverb Short Size",0.0f, 1.0f, 0.3f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("reverbShortDamp", "Reverb Short Damp",0.0f, 1.0f, 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("reverbShortMix",  "Reverb Short Mix", 0.0f, 1.0f, 0.2f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("reverbLongSize",  "Reverb Long Size", 0.0f, 1.0f, 0.7f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("reverbLongDamp",  "Reverb Long Damp", 0.0f, 1.0f, 0.4f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("reverbLongMix",   "Reverb Long Mix",  0.0f, 1.0f, 0.15f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("reverbDuck",      "Reverb Duck",      0.0f, 1.0f, 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("reverbPostEQ",    "Reverb Post EQ",   2000.0f, 16000.0f, 8000.0f));

    // --- DELAY ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("delayActive",   "Delay Active",   false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("delayTime",     "Delay Time",     10.0f, 2000.0f, 250.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("delayFeedback", "Delay Feedback", 0.0f, 0.9f, 0.3f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("delayMix",      "Delay Mix",      0.0f, 1.0f, 0.2f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("delayDuck",     "Delay Duck",     0.0f, 1.0f, 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("delayFilter",   "Delay Filter",   1000.0f, 12000.0f, 6000.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("delayPingPong", "Delay Ping-Pong",false));

    // --- LO-FI SIGNAL CUTOFF ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("lofiActive", "Lo-Fi Active", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("lofiHP",     "Lo-Fi HP",     200.0f, 1000.0f, 400.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("lofiLP",     "Lo-Fi LP",     1000.0f, 6000.0f, 3500.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("lofiBits",   "Lo-Fi Bits",   4.0f, 32.0f, 32.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("lofiDS",     "Lo-Fi Downsample", 1.0f, 16.0f, 1.0f));

    // --- MULTIBAND COMPRESSOR ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("mbActive",  "Multiband Active",  false));
    for (int b = 0; b < 5; ++b)
    {
        auto sb = juce::String(b + 1);
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("mbThresh" + sb, "MB Thresh " + sb, -60.0f, 0.0f, -18.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("mbRatio"  + sb, "MB Ratio "  + sb, 1.0f, 40.0f, 4.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("mbAttack" + sb, "MB Attack " + sb, 0.1f, 100.0f, 5.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("mbRel"    + sb, "MB Release "+ sb, 10.0f, 500.0f, 50.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("mbMakeup" + sb, "MB Makeup " + sb, -12.0f, 24.0f, 0.0f));
    }

    // --- OUTPUT LIMITER ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("limiterActive",  "Limiter Active",  true));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("limiterCeiling", "Limiter Ceiling", -12.0f, 0.0f, -0.3f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("limiterRelease", "Limiter Release", 10.0f, 500.0f, 50.0f));

    // --- MASTER ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("inputGain",  "Input Gain",  -24.0f, 24.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("outputGain", "Output Gain", -24.0f, 24.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("dryWet",     "Dry/Wet",     0.0f, 1.0f, 1.0f));

    return { params.begin(), params.end() };
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
HumHouseVocalsProcessor::HumHouseVocalsProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    presetManager = std::make_unique<humvocal::PresetManager>(apvts);
}

HumHouseVocalsProcessor::~HumHouseVocalsProcessor() = default;

// ---------------------------------------------------------------------------
// Prepare / release
// ---------------------------------------------------------------------------
void HumHouseVocalsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    pitchEngine.prepare(sampleRate, samplesPerBlock);
    formantShifter.prepare(sampleRate, samplesPerBlock);
    visualEQ.prepare(sampleRate, samplesPerBlock);
    compressor.prepare(sampleRate, samplesPerBlock);
    multibandComp.prepare(sampleRate, samplesPerBlock);
    deEsser.prepare(sampleRate, samplesPerBlock);
    saturation.prepare(sampleRate, samplesPerBlock);
    tapeEmulation.prepare(sampleRate, samplesPerBlock);
    stereoWidth.prepare(sampleRate, samplesPerBlock);
    doubler.prepare(sampleRate, samplesPerBlock);
    reverb.prepare(sampleRate, samplesPerBlock);
    delay.prepare(sampleRate, samplesPerBlock);
    lofiFilter.prepare(sampleRate, samplesPerBlock);
    limiter.prepare(sampleRate, samplesPerBlock);
}

void HumHouseVocalsProcessor::releaseResources() {}

bool HumHouseVocalsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// The main audio callback — zero-latency signal chain
// ---------------------------------------------------------------------------
void HumHouseVocalsProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    updateModuleParameters();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Save dry signal for dry/wet mix
    juce::AudioBuffer<float> dryBuffer;
    float dryWet = apvts.getRawParameterValue("dryWet")->load();
    bool needDry = (dryWet < 0.99f);
    if (needDry)
    {
        dryBuffer.makeCopyOf(buffer);
    }

    // Input gain
    float inputGain = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("inputGain")->load());
    if (std::abs(inputGain - 1.0f) > 0.001f)
        buffer.applyGain(inputGain);

    // === SIGNAL CHAIN (zero-latency, in-place processing) ===

    // 1. Pitch Correction
    if (apvts.getRawParameterValue("pitchActive")->load() > 0.5f)
        pitchEngine.process(buffer);

    // Update pitch feedback atomics
    detectedPitchHz.store(pitchEngine.getDetectedPitchHz());
    targetPitchHz.store(pitchEngine.getTargetPitchHz());
    correctionCents.store(pitchEngine.getCorrectionCents());

    // 2. Formant Shifter
    formantShifter.process(buffer);

    // 3. Visual EQ (12-band)
    visualEQ.process(buffer);

    // 4. Compressor
    compressor.process(buffer);

    // 4b. Multiband Compressor
    multibandComp.process(buffer);

    // 5. De-Esser
    deEsser.process(buffer);

    // 6. Saturation
    saturation.process(buffer);

    // 7. Tape Emulation
    tapeEmulation.process(buffer);

    // 8. Stereo Width
    stereoWidth.process(buffer);

    // 9. Doubler
    doubler.process(buffer);

    // 10. Reverb
    reverb.process(buffer);

    // 11. Delay
    delay.process(buffer);

    // 12. Lo-Fi Signal Cutoff
    lofiFilter.process(buffer);

    // 13. Output Limiter
    limiter.process(buffer);

    // Output gain
    float outputGain = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("outputGain")->load());
    if (std::abs(outputGain - 1.0f) > 0.001f)
        buffer.applyGain(outputGain);

    // Dry/Wet mix
    if (needDry)
    {
        float wet = dryWet;
        float dry = 1.0f - wet;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* wetData = buffer.getWritePointer(ch);
            const float* dryData = dryBuffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                wetData[i] = dryData[i] * dry + wetData[i] * wet;
        }
    }
}

// ---------------------------------------------------------------------------
// Push APVTS parameter values into each DSP module
// ---------------------------------------------------------------------------
void HumHouseVocalsProcessor::updateModuleParameters()
{
    // Pitch
    pitchEngine.setRetuneSpeed(apvts.getRawParameterValue("retuneSpeed")->load());
    pitchEngine.setHumanize(apvts.getRawParameterValue("humanize")->load());
    pitchEngine.setSnapAmount(apvts.getRawParameterValue("snapAmount")->load());
    pitchEngine.setPitchSustain(apvts.getRawParameterValue("pitchSustain")->load());
    pitchEngine.setReferenceFrequency(apvts.getRawParameterValue("detune")->load());
    pitchEngine.setRootNote(static_cast<int>(apvts.getRawParameterValue("rootNote")->load()));
    pitchEngine.setScaleType(static_cast<int>(apvts.getRawParameterValue("scaleType")->load()));
    pitchEngine.setNoteStabilizer(apvts.getRawParameterValue("noteStabilizer")->load() > 0.5f);
    pitchEngine.setFormantPreserve(apvts.getRawParameterValue("formantPreserve")->load() > 0.5f);

    // Formant Shifter
    formantShifter.setActive(apvts.getRawParameterValue("formantActive")->load() > 0.5f);
    formantShifter.setShift(apvts.getRawParameterValue("formantShift")->load());
    formantShifter.setMix(apvts.getRawParameterValue("formantMix")->load());
    formantShifter.setSmoothing(apvts.getRawParameterValue("formantSmooth")->load());

    // Visual EQ (12-band)
    visualEQ.setActive(apvts.getRawParameterValue("veqActive")->load() > 0.5f);
    for (int i = 0; i < 12; ++i)
    {
        auto si = juce::String(i + 1);
        float freq = apvts.getRawParameterValue("veqF" + si)->load();
        float gain = apvts.getRawParameterValue("veqG" + si)->load();
        float q    = apvts.getRawParameterValue("veqQ" + si)->load();
        int   type = static_cast<int>(apvts.getRawParameterValue("veqT" + si)->load());
        visualEQ.setBand(i, freq, gain, q, static_cast<humvocal::EQBandType>(type), true);
    }

    // Compressor
    compressor.setActive(apvts.getRawParameterValue("compActive")->load() > 0.5f);
    compressor.setThreshold(apvts.getRawParameterValue("compThreshold")->load());
    compressor.setRatio(apvts.getRawParameterValue("compRatio")->load());
    compressor.setAttack(apvts.getRawParameterValue("compAttack")->load());
    compressor.setRelease(apvts.getRawParameterValue("compRelease")->load());
    compressor.setMakeupGain(apvts.getRawParameterValue("compMakeup")->load());
    compressor.setKnee(apvts.getRawParameterValue("compKnee")->load());
    compressor.setAutoGain(apvts.getRawParameterValue("compAutoGain")->load() > 0.5f);
    compressor.setAutoLevel(apvts.getRawParameterValue("autoLevel")->load() > 0.5f);
    compressor.setAutoLevelTarget(apvts.getRawParameterValue("autoLevelTarget")->load());
    compressor.setTHDMode(static_cast<int>(apvts.getRawParameterValue("thdMode")->load()));

    // Multiband Compressor
    multibandComp.setActive(apvts.getRawParameterValue("mbActive")->load() > 0.5f);
    for (int b = 0; b < 5; ++b)
    {
        auto sb = juce::String(b + 1);
        multibandComp.setBandParams(b,
            apvts.getRawParameterValue("mbThresh" + sb)->load(),
            apvts.getRawParameterValue("mbRatio"  + sb)->load(),
            apvts.getRawParameterValue("mbAttack" + sb)->load(),
            apvts.getRawParameterValue("mbRel"    + sb)->load(),
            apvts.getRawParameterValue("mbMakeup" + sb)->load(),
            1.0f);
    }

    // De-Esser
    deEsser.setActive(apvts.getRawParameterValue("deEsserActive")->load() > 0.5f);
    deEsser.setFrequency(apvts.getRawParameterValue("deEsserFreq")->load());
    deEsser.setThreshold(apvts.getRawParameterValue("deEsserThresh")->load());
    deEsser.setReduction(apvts.getRawParameterValue("deEsserReduce")->load());

    // Saturation
    saturation.setActive(apvts.getRawParameterValue("satActive")->load() > 0.5f);
    saturation.setDrive(apvts.getRawParameterValue("satDrive")->load());
    saturation.setMode(static_cast<int>(apvts.getRawParameterValue("satMode")->load()));
    saturation.setMix(apvts.getRawParameterValue("satMix")->load());

    // Tape
    tapeEmulation.setActive(apvts.getRawParameterValue("tapeActive")->load() > 0.5f);
    tapeEmulation.setSpeed(apvts.getRawParameterValue("tapeSpeed")->load());
    tapeEmulation.setFlutter(apvts.getRawParameterValue("tapeFlutter")->load());
    tapeEmulation.setDrive(apvts.getRawParameterValue("tapeDrive")->load());

    // Width
    stereoWidth.setActive(apvts.getRawParameterValue("widthActive")->load() > 0.5f);
    stereoWidth.setAmount(apvts.getRawParameterValue("widthAmount")->load());
    stereoWidth.setMode(static_cast<int>(apvts.getRawParameterValue("widthMode")->load()));

    // Doubler
    doubler.setActive(apvts.getRawParameterValue("doublerActive")->load() > 0.5f);
    doubler.setMix(apvts.getRawParameterValue("doublerMix")->load());
    doubler.setDetune(apvts.getRawParameterValue("doublerDetune")->load());
    doubler.setDelay(apvts.getRawParameterValue("doublerDelay")->load());

    // Reverb
    reverb.setActive(apvts.getRawParameterValue("reverbActive")->load() > 0.5f);
    reverb.setShortSize(apvts.getRawParameterValue("reverbShortSize")->load());
    reverb.setShortDamping(apvts.getRawParameterValue("reverbShortDamp")->load());
    reverb.setShortMix(apvts.getRawParameterValue("reverbShortMix")->load());
    reverb.setLongSize(apvts.getRawParameterValue("reverbLongSize")->load());
    reverb.setLongDamping(apvts.getRawParameterValue("reverbLongDamp")->load());
    reverb.setLongMix(apvts.getRawParameterValue("reverbLongMix")->load());
    reverb.setDuckAmount(apvts.getRawParameterValue("reverbDuck")->load());
    reverb.setPostEQFreq(apvts.getRawParameterValue("reverbPostEQ")->load());

    // Delay
    delay.setActive(apvts.getRawParameterValue("delayActive")->load() > 0.5f);
    delay.setTimeMs(apvts.getRawParameterValue("delayTime")->load());
    delay.setFeedback(apvts.getRawParameterValue("delayFeedback")->load());
    delay.setMix(apvts.getRawParameterValue("delayMix")->load());
    delay.setDuckAmount(apvts.getRawParameterValue("delayDuck")->load());
    delay.setFilterFreq(apvts.getRawParameterValue("delayFilter")->load());
    delay.setPingPong(apvts.getRawParameterValue("delayPingPong")->load() > 0.5f);

    // Lo-Fi
    lofiFilter.setActive(apvts.getRawParameterValue("lofiActive")->load() > 0.5f);
    lofiFilter.setHighCut(apvts.getRawParameterValue("lofiHP")->load());
    lofiFilter.setLowCut(apvts.getRawParameterValue("lofiLP")->load());
    lofiFilter.setBitDepth(apvts.getRawParameterValue("lofiBits")->load());
    lofiFilter.setDownsample(apvts.getRawParameterValue("lofiDS")->load());

    // Limiter
    limiter.setActive(apvts.getRawParameterValue("limiterActive")->load() > 0.5f);
    limiter.setCeiling(apvts.getRawParameterValue("limiterCeiling")->load());
    limiter.setRelease(apvts.getRawParameterValue("limiterRelease")->load());
}

// ---------------------------------------------------------------------------
// State persistence (save/load all parameters)
// ---------------------------------------------------------------------------
void HumHouseVocalsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("uiScale", uiScale.load(), nullptr);
    state.setProperty("presetIndex", presetManager ? presetManager->getCurrentPresetIndex() : -1, nullptr);
    auto xml = state.createXml();
    copyXmlToBinary (*xml, destData);
}

void HumHouseVocalsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (state.hasProperty("uiScale"))
            uiScale.store(static_cast<float>(state.getProperty("uiScale")));
        apvts.replaceState (state);
    }
}

// ---------------------------------------------------------------------------
// Editor creation
// ---------------------------------------------------------------------------
juce::AudioProcessorEditor* HumHouseVocalsProcessor::createEditor()
{
    return new HumHouseVocalsEditor (*this);
}

// ---------------------------------------------------------------------------
// JUCE plugin instantiation
// ---------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HumHouseVocalsProcessor();
}
