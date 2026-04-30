#include "PluginProcessor.h"
#include "PluginEditor.h"

// ---------------------------------------------------------------------------
// Parameter layout — every automatable knob in the plugin
// ---------------------------------------------------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout
HumHouseVocalsProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;


    // --- NOISE GATE ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("gateActive",    "Gate Active",       false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("gateThreshold", "Gate Threshold",    -80.0f, 0.0f, -40.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("gateRatio",     "Gate Ratio",        1.0f, 100.0f, 100.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("gateAttack",    "Gate Attack",       0.01f, 100.0f, 0.1f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("gateHold",      "Gate Hold",         0.0f, 500.0f, 50.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("gateRelease",   "Gate Release",      1.0f, 2000.0f, 100.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("gateRange",     "Gate Range",        -120.0f, 0.0f, -80.0f));

    // --- VISUAL EQ (12-band) ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("veqActive",  "Visual EQ Active",  false));
    for (int i = 0; i < 12; ++i)
    {
        auto si = juce::String(i + 1);
        float defaultFreqs[] = { 30.f, 80.f, 160.f, 300.f, 500.f, 800.f, 1200.f, 2500.f, 4000.f, 6000.f, 10000.f, 16000.f };
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("veqF" + si, "VEQ Freq " + si,  20.0f, 20000.0f, defaultFreqs[i]));
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("veqG" + si, "VEQ Gain " + si,  -24.0f, 24.0f, 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> ("veqQ" + si, "VEQ Q " + si,     0.1f, 30.0f, 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterInt>   ("veqT" + si, "VEQ Type " + si,  0, 5, (i == 0) ? 4 : (i == 11) ? 3 : 0)); // HP, LP, or Bell
        params.push_back (std::make_unique<juce::AudioParameterBool>  ("veqDyn" + si, "VEQ Dynamic " + si, false));
    }

    // --- COMPRESSOR (single-band) ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("compActive",    "Comp Active",    false));
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
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("compOutputGain","Comp Output Gain", -24.0f, 24.0f, 0.0f));

    // --- DE-ESSER ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("deEsserActive", "De-Esser Active", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("deEsserFreq",   "De-Esser Freq",   3000.0f, 12000.0f, 7000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("deEsserThresh", "De-Esser Thresh",  -40.0f, 0.0f, -20.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("deEsserReduce", "De-Esser Reduce",  -24.0f, 0.0f, -12.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("deEsserBW",     "De-Esser BW",      0.5f, 6.0f, 2.0f));
    params.push_back (std::make_unique<juce::AudioParameterInt>   ("deEsserMode",   "De-Esser Mode",    0, 1, 0));
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("deEsserListen", "De-Esser Listen",  false));

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

    // --- CONVOLVER REVERB ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("convActive",    "Convolver Active",  false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("convSize",      "Conv Size",         0.1f, 6.0f, 1.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("convDamping",   "Conv Damping",      0.0f, 1.0f, 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("convMix",       "Conv Mix",          0.0f, 1.0f, 0.3f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("convPreDelay",  "Conv Pre-Delay",    0.0f, 200.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("convReverse",   "Conv Reverse",      false));

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
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("mbOutputGain", "MB Output Gain", -24.0f, 24.0f, 0.0f));

    // --- OUTPUT LIMITER ---
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("limiterActive",  "Limiter Active",  false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("limiterCeiling", "Limiter Ceiling", -12.0f, 0.0f, -0.3f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("limiterRelease", "Limiter Release", 10.0f, 500.0f, 50.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("limiterOutputGain", "Limiter Output Gain", -24.0f, 24.0f, 0.0f));

    // --- MASTER ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("inputGain",  "Input Gain",  -24.0f, 24.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("outputGain", "Output Gain", -24.0f, 24.0f, 6.0f));
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
    noiseGate.prepare(sampleRate, samplesPerBlock);
    visualEQ.prepare(sampleRate, samplesPerBlock);
    compressor.prepare(sampleRate, samplesPerBlock);
    multibandComp.prepare(sampleRate, samplesPerBlock);
    deEsser.prepare(sampleRate, samplesPerBlock);
    saturation.prepare(sampleRate, samplesPerBlock);
    tapeEmulation.prepare(sampleRate, samplesPerBlock);
    stereoWidth.prepare(sampleRate, samplesPerBlock);
    doubler.prepare(sampleRate, samplesPerBlock);
    reverb.prepare(sampleRate, samplesPerBlock);
    convolverReverb.prepare(sampleRate, samplesPerBlock);
    delay.prepare(sampleRate, samplesPerBlock);
    lofiFilter.prepare(sampleRate, samplesPerBlock);
    limiter.prepare(sampleRate, samplesPerBlock);

    // Pre-allocate dry buffer
    dryBuffer.setSize(2, samplesPerBlock);

    setLatencySamples(0);
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
// Process a single module by ID (used by the reorderable chain)
// ---------------------------------------------------------------------------
void HumHouseVocalsProcessor::processModule (int moduleId,
                                             juce::AudioBuffer<float>& buffer,
                                             bool inputSilent)
{
    switch (moduleId)
    {
        case kGate:
            if (!inputSilent) noiseGate.process(buffer);
            break;

        case kEQ:
            if (!inputSilent) visualEQ.process(buffer);
            break;

        case kComp:
            if (!inputSilent) compressor.process(buffer);
            break;

        case kMBComp:
            if (!inputSilent) multibandComp.process(buffer);
            break;

        case kDeEss:
            if (!inputSilent) deEsser.process(buffer);
            break;

        case kSat:
            if (!inputSilent) saturation.process(buffer);
            break;

        case kTape:
            if (!inputSilent) tapeEmulation.process(buffer);
            break;

        case kWidth:
            if (!inputSilent) stereoWidth.process(buffer);
            break;

        case kDoubler:
            if (!inputSilent) doubler.process(buffer);
            break;

        case kReverb:
            reverb.process(buffer);
            break;

        case kConvolver:
            convolverReverb.process(buffer);
            break;

        case kDelay:
            delay.process(buffer);
            break;

        case kLoFi:
            if (!inputSilent) lofiFilter.process(buffer);
            break;

        case kLimiter:
            limiter.process(buffer);
            break;

        default: break;
    }
}

// ---------------------------------------------------------------------------
// The main audio callback — reorderable signal chain
// ---------------------------------------------------------------------------
void HumHouseVocalsProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0 || numChannels == 0)
        return;

    float maxRMS = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        maxRMS = std::max(maxRMS, buffer.getRMSLevel(ch, 0, numSamples));

    bool inputSilent = (maxRMS < 1e-6f);

    updateModuleParameters();

    // Input gain
    float inputGain = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("inputGain")->load());
    if (std::abs(inputGain - 1.0f) > 0.001f)
        buffer.applyGain(inputGain);

    // Save dry signal for dry/wet mix
    float dryWet = apvts.getRawParameterValue("dryWet")->load();
    bool needDry = (dryWet < 0.99f);
    if (needDry)
    {
        dryBuffer.setSize(numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);
    }

    // === REORDERABLE SIGNAL CHAIN ===
    std::array<int, kNumModules> localChain;
    {
        juce::SpinLock::ScopedLockType lock (chainLock);
        localChain = chainOrder;
    }
    for (int slot = 0; slot < kNumModules; ++slot)
    {
        processModule (localChain[static_cast<size_t>(slot)], buffer, inputSilent);

        // Re-check silence after modules that produce tails (reverb, delay, pitch)
        // so downstream modules don't skip processing non-silent output
        int mod = localChain[static_cast<size_t>(slot)];
        if (inputSilent && (mod == kReverb || mod == kConvolver || mod == kDelay || mod == kDoubler))
        {
            float postRms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
            if (postRms > 1e-6f)
                inputSilent = false;
        }
    }

    // Output gain
    float outputGain = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("outputGain")->load());
    if (std::abs(outputGain - 1.0f) > 0.001f)
        buffer.applyGain(outputGain);

    // Safety: clamp output
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i)
        {
            if (std::isnan(data[i]) || std::isinf(data[i]))
                data[i] = 0.0f;
            else
                data[i] = juce::jlimit(-2.0f, 2.0f, data[i]);
        }
    }

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

    // Noise Gate
    noiseGate.setActive(apvts.getRawParameterValue("gateActive")->load() > 0.5f);
    noiseGate.setThreshold(apvts.getRawParameterValue("gateThreshold")->load());
    noiseGate.setRatio(apvts.getRawParameterValue("gateRatio")->load());
    noiseGate.setAttack(apvts.getRawParameterValue("gateAttack")->load());
    noiseGate.setHold(apvts.getRawParameterValue("gateHold")->load());
    noiseGate.setRelease(apvts.getRawParameterValue("gateRelease")->load());
    noiseGate.setRange(apvts.getRawParameterValue("gateRange")->load());

    // Visual EQ (12-band)
    visualEQ.setActive(apvts.getRawParameterValue("veqActive")->load() > 0.5f);
    for (int i = 0; i < 12; ++i)
    {
        auto si = juce::String(i + 1);
        float freq = apvts.getRawParameterValue("veqF" + si)->load();
        float gain = apvts.getRawParameterValue("veqG" + si)->load();
        float q    = apvts.getRawParameterValue("veqQ" + si)->load();
        int   type = static_cast<int>(apvts.getRawParameterValue("veqT" + si)->load());
        bool  dyn  = apvts.getRawParameterValue("veqDyn" + si)->load() > 0.5f;
        visualEQ.setBand(i, freq, gain, q, static_cast<humvocal::EQBandType>(type), true);
        visualEQ.setBandDynamic(i, dyn);
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
    compressor.setOutputGain(apvts.getRawParameterValue("compOutputGain")->load());

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
    multibandComp.setOutputGain(apvts.getRawParameterValue("mbOutputGain")->load());

    // De-Esser
    deEsser.setActive(apvts.getRawParameterValue("deEsserActive")->load() > 0.5f);
    deEsser.setFrequency(apvts.getRawParameterValue("deEsserFreq")->load());
    deEsser.setThreshold(apvts.getRawParameterValue("deEsserThresh")->load());
    deEsser.setReduction(apvts.getRawParameterValue("deEsserReduce")->load());
    deEsser.setBandwidth(apvts.getRawParameterValue("deEsserBW")->load());
    deEsser.setMode(static_cast<int>(apvts.getRawParameterValue("deEsserMode")->load()));
    deEsser.setListen(apvts.getRawParameterValue("deEsserListen")->load() > 0.5f);

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

    // Convolver Reverb
    convolverReverb.setActive(apvts.getRawParameterValue("convActive")->load() > 0.5f);
    convolverReverb.setSize(apvts.getRawParameterValue("convSize")->load());
    convolverReverb.setDamping(apvts.getRawParameterValue("convDamping")->load());
    convolverReverb.setMix(apvts.getRawParameterValue("convMix")->load());
    convolverReverb.setPreDelay(apvts.getRawParameterValue("convPreDelay")->load());
    convolverReverb.setReverse(apvts.getRawParameterValue("convReverse")->load() > 0.5f);

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
    limiter.setOutputGain(apvts.getRawParameterValue("limiterOutputGain")->load());
}

// ---------------------------------------------------------------------------
// State persistence (save/load all parameters)
// ---------------------------------------------------------------------------
void HumHouseVocalsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("uiScale", uiScale.load(), nullptr);
    state.setProperty("presetIndex", presetManager ? presetManager->getCurrentPresetIndex() : -1, nullptr);

    // Persist chain order as comma-separated string
    juce::String chainStr;
    {
        juce::SpinLock::ScopedLockType lock (chainLock);
        for (int i = 0; i < kNumModules; ++i)
        {
            if (i > 0) chainStr += ",";
            chainStr += juce::String(chainOrder[static_cast<size_t>(i)]);
        }
    }
    state.setProperty("chainOrder", chainStr, nullptr);

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

        // Restore chain order (validate it's a valid permutation)
        if (state.hasProperty("chainOrder"))
        {
            auto chainStr = state.getProperty("chainOrder").toString();
            auto tokens = juce::StringArray::fromTokens(chainStr, ",", "");
            if (tokens.size() == kNumModules)
            {
                std::array<int, kNumModules> candidate {};
                std::array<bool, kNumModules> seen {};
                bool valid = true;
                for (int i = 0; i < kNumModules; ++i)
                {
                    int v = tokens[i].getIntValue();
                    if (v < 0 || v >= kNumModules || seen[static_cast<size_t>(v)])
                    { valid = false; break; }
                    seen[static_cast<size_t>(v)] = true;
                    candidate[static_cast<size_t>(i)] = v;
                }
                if (valid)
                {
                    juce::SpinLock::ScopedLockType lock (chainLock);
                    chainOrder = candidate;
                }
            }
        }

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
