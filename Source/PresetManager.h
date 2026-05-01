#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>

namespace humvocal
{

struct PresetData
{
    juce::String name;
    juce::String category;   // "Factory" or "User"
    juce::XmlElement* stateXml = nullptr;
    std::unique_ptr<juce::XmlElement> ownedXml;

    PresetData() = default;
    PresetData (const juce::String& n, const juce::String& cat)
        : name (n), category (cat) {}
};

class PresetManager
{
public:
    PresetManager (juce::AudioProcessorValueTreeState& apvts)
        : valueTreeState (apvts)
    {
        userPresetsDir = juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory)
            .getChildFile("HumHouse")
            .getChildFile("HumHouse Vocals")
            .getChildFile("Presets");

        if (!userPresetsDir.exists())
            userPresetsDir.createDirectory();

        buildFactoryPresets();
        scanUserPresets();
    }

    // -----------------------------------------------------------------------
    // Preset access
    // -----------------------------------------------------------------------
    int getNumPresets() const { return static_cast<int>(allPresets.size()); }

    juce::String getPresetName (int index) const
    {
        if (index >= 0 && index < static_cast<int>(allPresets.size()))
            return allPresets[static_cast<size_t>(index)].name;
        return {};
    }

    juce::String getPresetCategory (int index) const
    {
        if (index >= 0 && index < static_cast<int>(allPresets.size()))
            return allPresets[static_cast<size_t>(index)].category;
        return {};
    }

    juce::StringArray getPresetNames() const
    {
        juce::StringArray names;
        for (auto& p : allPresets)
            names.add(p.name);
        return names;
    }

    int getCurrentPresetIndex() const { return currentPresetIndex; }
    void setCurrentPresetIndex(int idx) { currentPresetIndex = idx; }

    // -----------------------------------------------------------------------
    // Load preset
    // -----------------------------------------------------------------------
    void loadPreset (int index)
    {
        if (index < 0 || index >= static_cast<int>(allPresets.size()))
            return;

        auto& preset = allPresets[static_cast<size_t>(index)];

        if (preset.category == "Factory")
        {
            applyFactoryPreset(preset.name);
        }
        else if (preset.ownedXml)
        {
            auto state = juce::ValueTree::fromXml(*preset.ownedXml);
            if (state.isValid())
                valueTreeState.replaceState(state);
        }

        currentPresetIndex = index;
    }

    // -----------------------------------------------------------------------
    // Save user preset
    // -----------------------------------------------------------------------
    void savePreset (const juce::String& name)
    {
        auto state = valueTreeState.copyState();
        auto xml = state.createXml();

        // Store original name inside the XML for lossless roundtrip
        xml->setAttribute("presetName", name);

        auto file = userPresetsDir.getChildFile(
            name.replaceCharacters(" /\\:", "____") + ".xml");

        xml->writeTo(file);

        // Check if this name already exists in user presets
        bool found = false;
        for (size_t i = 0; i < allPresets.size(); ++i)
        {
            if (allPresets[i].name == name && allPresets[i].category == "User")
            {
                allPresets[i].ownedXml = std::move(xml);
                currentPresetIndex = static_cast<int>(i);
                found = true;
                break;
            }
        }

        if (!found)
        {
            PresetData pd (name, "User");
            pd.ownedXml = std::move(xml);
            allPresets.push_back(std::move(pd));
            currentPresetIndex = static_cast<int>(allPresets.size()) - 1;
        }
    }

    // -----------------------------------------------------------------------
    // Delete user preset
    // -----------------------------------------------------------------------
    bool deletePreset (int index)
    {
        if (index < 0 || index >= static_cast<int>(allPresets.size()))
            return false;
        if (allPresets[static_cast<size_t>(index)].category == "Factory")
            return false;

        auto name = allPresets[static_cast<size_t>(index)].name;
        auto file = userPresetsDir.getChildFile(
            name.replaceCharacters(" /\\:", "____") + ".xml");
        file.deleteFile();

        allPresets.erase(allPresets.begin() + index);
        if (currentPresetIndex >= static_cast<int>(allPresets.size()))
            currentPresetIndex = static_cast<int>(allPresets.size()) - 1;
        return true;
    }

    // -----------------------------------------------------------------------
    // Rename user preset
    // -----------------------------------------------------------------------
    bool renamePreset (int index, const juce::String& newName)
    {
        if (index < 0 || index >= static_cast<int>(allPresets.size()))
            return false;
        if (allPresets[static_cast<size_t>(index)].category == "Factory")
            return false;

        auto& preset = allPresets[static_cast<size_t>(index)];
        auto oldFile = userPresetsDir.getChildFile(
            preset.name.replaceCharacters(" /\\:", "____") + ".xml");
        auto newFile = userPresetsDir.getChildFile(
            newName.replaceCharacters(" /\\:", "____") + ".xml");

        oldFile.moveFileTo(newFile);
        preset.name = newName;
        return true;
    }

    // -----------------------------------------------------------------------
    // Get user presets directory (for browsing)
    // -----------------------------------------------------------------------
    juce::File getUserPresetsDir() const { return userPresetsDir; }

private:
    juce::AudioProcessorValueTreeState& valueTreeState;
    juce::File userPresetsDir;
    std::vector<PresetData> allPresets;
    int currentPresetIndex = -1;

    // -----------------------------------------------------------------------
    // Scan user presets from disk
    // -----------------------------------------------------------------------
    void scanUserPresets()
    {
        auto files = userPresetsDir.findChildFiles(
            juce::File::findFiles, false, "*.xml");

        files.sort();

        for (auto& file : files)
        {
            auto xml = juce::XmlDocument::parse(file);
            if (xml)
            {
                // Prefer the stored name; fall back to filename
                juce::String presetName = xml->getStringAttribute("presetName",
                    file.getFileNameWithoutExtension());
                PresetData pd (presetName, "User");
                pd.ownedXml = std::move(xml);
                allPresets.push_back(std::move(pd));
            }
        }
    }

    // -----------------------------------------------------------------------
    // Factory presets — based on Gamma Vocal Suite & genre research
    // -----------------------------------------------------------------------
    void buildFactoryPresets()
    {
        // Default / Init
        addFactoryPreset("Default (Init)");

        // --- TRAP / HIP-HOP (8 presets) ---
        addFactoryPreset("Trap Hard AutoTune");
        addFactoryPreset("Trap Melodic");
        addFactoryPreset("Trap Dark & Wet");
        addFactoryPreset("Trap Adlib Bright");
        addFactoryPreset("Trap Mumble Smooth");
        addFactoryPreset("Drill Raw Vocal");
        addFactoryPreset("Rage Beat Vocal");
        addFactoryPreset("808 Bass Vocal");

        // --- R&B / SOUL (6 presets) ---
        addFactoryPreset("Smooth R&B");
        addFactoryPreset("R&B Warm Intimate");
        addFactoryPreset("Neo Soul Vintage");
        addFactoryPreset("R&B Falsetto Air");
        addFactoryPreset("90s R&B Classic");
        addFactoryPreset("Bedroom R&B");

        // --- POP (5 presets) ---
        addFactoryPreset("Pop Radio Ready");
        addFactoryPreset("Pop Bright & Airy");
        addFactoryPreset("Pop Ballad Lush");
        addFactoryPreset("K-Pop Crystal");
        addFactoryPreset("Pop Punk Grit");

        // --- ROCK / METAL (4 presets) ---
        addFactoryPreset("Rock Aggressive");
        addFactoryPreset("Rock Warm Analog");
        addFactoryPreset("Metal Scream");
        addFactoryPreset("Indie Folk Natural");

        // --- LO-FI / CHARACTER (5 presets) ---
        addFactoryPreset("Lo-Fi Tape Vocal");
        addFactoryPreset("Telephone / Radio");
        addFactoryPreset("Vintage Saturated");
        addFactoryPreset("Vinyl Crackle Vox");
        addFactoryPreset("Bitcrushed Glitch");

        // --- CREATIVE / FX (7 presets) ---
        addFactoryPreset("Robotic AutoTune");
        addFactoryPreset("Ethereal Wide");
        addFactoryPreset("Doubled & Thick");
        addFactoryPreset("Reverb Wash");
        addFactoryPreset("Slapback Echo");
        addFactoryPreset("Underwater Dream");
        addFactoryPreset("Cathedral Choir");

        // --- GENRE-SPECIFIC (5 presets) ---
        addFactoryPreset("Gospel Powerful");
        addFactoryPreset("Country Twang");
        addFactoryPreset("Latin Reggaeton");
        addFactoryPreset("Afrobeats Vocal");
        addFactoryPreset("EDM Festival Drop");

        // --- SIGNATURE / ARTIST (7 presets) ---
        addFactoryPreset("Drocett Smooth Melodies");
        addFactoryPreset("Drocett Trap Soul");
        addFactoryPreset("Drocett Late Night");
        addFactoryPreset("Drocett Falsetto Vibe");
        addFactoryPreset("Nu Rock Dry Scream");
        addFactoryPreset("Nu Rock Raw Edge");
        addFactoryPreset("Nu Rock Grit & Growl");

        // --- MIX-READY / UTILITY (5 presets) ---
        addFactoryPreset("Clean Vocal Chain");
        addFactoryPreset("Broadcast / Podcast");
        addFactoryPreset("Voiceover Warmth");
        addFactoryPreset("Live Performance");
        addFactoryPreset("Mastered Vocal Bus");
    }

    void addFactoryPreset (const juce::String& name)
    {
        allPresets.push_back(PresetData(name, "Factory"));
    }

    // -----------------------------------------------------------------------
    // Apply factory preset values to the APVTS
    // -----------------------------------------------------------------------
    void applyFactoryPreset (const juce::String& name)
    {
        // Helper to set a parameter value
        std::function<void(const juce::String&, float)> set =
            [this](const juce::String& paramId, float value) {
                if (auto* param = valueTreeState.getParameter(paramId))
                    param->setValueNotifyingHost(param->convertTo0to1(value));
            };

        std::function<void(const juce::String&, bool)> setBool =
            [&set](const juce::String& paramId, bool value) {
                set(paramId, value ? 1.0f : 0.0f);
            };

        // Reset everything to defaults first
        resetToDefaults(set, setBool);

        // --- FACTORY PRESET DEFINITIONS ---
        // All presets modeled after Pop Radio Ready style:
        // - Multiband compression enabled (balanced per-band control)
        // - Tube saturation for warmth
        // - Compression with studio-standard thresholds
        // - Reverb capped at 0.20 max for clarity
        // - Low-end cleaned (HPF + boxiness cuts)
        // - Output gain boosted for loudness

        if (name == "Default (Init)")
        {
            // Already reset to defaults
        }
        else if (name == "Trap Hard AutoTune")
        {
            set("veqF1", 120.0f);
            set("veqG4", -2.0f);
            set("veqG5", -1.0f);
            set("veqG6", 2.0f);
            set("veqG8", 3.0f);
            set("veqG9", 1.5f);
            set("veqG11", 2.5f);
            set("veqQ6", 1.8f);
            set("veqQ8", 2.0f);

            setBool("mbActive", true);
            set("mbThresh1", -22.0f);
            set("mbRatio1", 5.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.5f);
            set("mbMakeup4", 1.5f);
            set("mbMakeup5", 2.0f);

            set("compThreshold", -24.0f);
            set("compRatio", 6.0f);
            set("compAttack", 3.0f);
            set("compRelease", 40.0f);
            set("compKnee", 3.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.2f);
            set("satMode", 0.0f);
            set("satMix", 0.3f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("delayActive", true);
            set("delayTime", 180.0f);
            set("delayFeedback", 0.2f);
            set("delayMix", 0.12f);
            set("delayDuck", 0.6f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.10f);
            set("reverbLongMix", 0.05f);
            set("reverbDuck", 0.7f);

            setBool("convActive", true);
            set("convSize", 0.8f);
            set("convDamping", 0.6f);
            set("convMix", 0.10f);
            setBool("convReverse", false);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Trap Melodic")
        {
            set("veqF1", 100.0f);
            set("veqG4", -1.5f);
            set("veqG5", -1.0f);
            set("veqG6", 1.5f);
            set("veqG8", 2.0f);
            set("veqG10", 1.5f);
            set("veqG11", 3.0f);
            set("veqQ11", 0.8f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);
            set("mbMakeup4", 1.0f);
            set("mbMakeup5", 1.5f);

            set("compThreshold", -22.0f);
            set("compRatio", 4.5f);
            set("compAttack", 5.0f);
            set("compRelease", 50.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.18f);
            set("satMode", 0.0f);
            set("satMix", 0.25f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbLongMix", 0.08f);
            set("reverbDuck", 0.5f);

            setBool("convActive", true);
            set("convSize", 1.2f);
            set("convDamping", 0.4f);
            set("convMix", 0.12f);
            setBool("convReverse", false);

            setBool("delayActive", true);
            set("delayTime", 330.0f);
            set("delayFeedback", 0.25f);
            set("delayMix", 0.14f);
            set("delayDuck", 0.5f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("doublerActive", true);
            set("doublerMix", 0.18f);
            set("doublerDetune", 8.0f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Trap Dark & Wet")
        {
            set("veqF1", 90.0f);
            set("veqF12", 14000.0f);
            set("veqG3", 1.5f);
            set("veqG4", -1.5f);
            set("veqG5", -1.0f);
            set("veqG8", -1.0f);
            set("veqQ3", 0.7f);

            setBool("mbActive", true);
            set("mbThresh1", -22.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.5f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.0f);
            set("mbMakeup5", 1.0f);

            set("compThreshold", -24.0f);
            set("compRatio", 5.0f);
            set("compAttack", 3.0f);
            set("compRelease", 45.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.25f);
            set("satMode", 0.0f);
            set("satMix", 0.3f);

            setBool("tapeActive", true);
            set("tapeDrive", 0.12f);
            set("tapeFlutter", 0.08f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.15f);
            set("reverbLongMix", 0.12f);
            set("reverbDuck", 0.5f);
            set("reverbPostEQ", 5000.0f);

            setBool("convActive", true);
            set("convSize", 1.8f);
            set("convDamping", 0.7f);
            set("convMix", 0.15f);
            setBool("convReverse", true);

            setBool("delayActive", true);
            set("delayTime", 400.0f);
            set("delayFeedback", 0.3f);
            set("delayMix", 0.15f);
            set("delayFilter", 4000.0f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Smooth R&B")
        {
            set("veqF1", 80.0f);
            set("veqG3", 1.5f);
            set("veqG4", -1.0f);
            set("veqG5", -1.5f);
            set("veqG6", -1.0f);
            set("veqG8", 2.0f);
            set("veqG10", 1.0f);
            set("veqG11", 2.5f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 2.5f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);
            set("mbMakeup4", 1.5f);
            set("mbMakeup5", 1.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 3.5f);
            set("compAttack", 6.0f);
            set("compRelease", 60.0f);
            set("compKnee", 8.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.15f);
            set("satMode", 0.0f);
            set("satMix", 0.25f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbLongMix", 0.06f);
            set("reverbDuck", 0.5f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.2f);
            set("widthMode", 0.0f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "R&B Warm Intimate")
        {
            set("veqF1", 70.0f);
            set("veqG3", 2.0f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG6", -1.5f);
            set("veqG8", 1.5f);
            set("veqG11", 2.0f);

            setBool("mbActive", true);
            set("mbThresh1", -18.0f);
            set("mbRatio1", 2.5f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbMakeup4", 1.0f);
            set("mbMakeup5", 1.0f);

            set("compThreshold", -18.0f);
            set("compRatio", 3.0f);
            set("compAttack", 8.0f);
            set("compRelease", 80.0f);
            set("compKnee", 10.0f);

            setBool("satActive", true);
            set("satDrive", 0.15f);
            set("satMode", 0.0f);
            set("satMix", 0.25f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("tapeActive", true);
            set("tapeFlutter", 0.12f);
            set("tapeDrive", 0.12f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.10f);
            set("reverbLongMix", 0.05f);
            set("reverbDuck", 0.5f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Neo Soul Vintage")
        {
            set("veqF1", 80.0f);
            set("veqG3", 2.5f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG6", -1.5f);
            set("veqG11", -0.5f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 2.5f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);

            set("compThreshold", -20.0f);
            set("compRatio", 3.5f);
            set("compAttack", 10.0f);
            set("compKnee", 8.0f);

            setBool("satActive", true);
            set("satDrive", 0.3f);
            set("satMode", 0.0f);
            set("satMix", 0.4f);

            setBool("tapeActive", true);
            set("tapeSpeed", 15.0f);
            set("tapeFlutter", 0.3f);
            set("tapeDrive", 0.25f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbLongMix", 0.06f);
            set("reverbDuck", 0.5f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Pop Radio Ready")
        {
            set("veqF1", 100.0f);
            set("veqG4", -1.5f);
            set("veqG5", -1.0f);
            set("veqG6", -1.0f);
            set("veqG8", 2.5f);
            set("veqG11", 3.0f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);
            set("mbMakeup4", 1.5f);
            set("mbMakeup5", 1.5f);

            set("compThreshold", -22.0f);
            set("compRatio", 4.5f);
            set("compAttack", 5.0f);
            set("compRelease", 50.0f);
            set("compKnee", 6.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);
            set("deEsserReduce", -10.0f);

            setBool("satActive", true);
            set("satDrive", 0.12f);
            set("satMode", 0.0f);
            set("satMix", 0.2f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbLongMix", 0.06f);
            set("reverbDuck", 0.6f);

            setBool("delayActive", true);
            set("delayTime", 250.0f);
            set("delayFeedback", 0.18f);
            set("delayMix", 0.08f);

            setBool("widthActive", true);
            set("widthAmount", 1.15f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Pop Bright & Airy")
        {
            set("veqF1", 110.0f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG8", 3.0f);
            set("veqG10", 1.5f);
            set("veqG11", 4.0f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -18.0f);
            set("mbRatio2", 2.5f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);
            set("mbMakeup4", 1.0f);
            set("mbMakeup5", 2.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 4.0f);
            set("compAttack", 5.0f);
            set("compRelease", 50.0f);
            set("compKnee", 6.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("satActive", true);
            set("satDrive", 0.1f);
            set("satMode", 0.0f);
            set("satMix", 0.18f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.14f);
            set("reverbLongMix", 0.08f);
            set("reverbDuck", 0.5f);

            setBool("widthActive", true);
            set("widthAmount", 1.25f);

            setBool("doublerActive", true);
            set("doublerMix", 0.12f);
            set("doublerDetune", 6.0f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Rock Aggressive")
        {
            set("veqF1", 150.0f);
            set("veqG4", -1.5f);
            set("veqG5", -1.0f);
            set("veqG6", 2.0f);
            set("veqG8", 3.0f);
            set("veqG11", 1.5f);

            setBool("mbActive", true);
            set("mbThresh1", -22.0f);
            set("mbRatio1", 4.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.0f);
            set("mbMakeup4", 1.0f);

            set("compThreshold", -26.0f);
            set("compRatio", 8.0f);
            set("compAttack", 2.0f);
            set("compRelease", 30.0f);
            set("compKnee", 2.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.4f);
            set("satMode", 2.0f);
            set("satMix", 0.4f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -14.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbLongMix", 0.04f);
            set("reverbDuck", 0.7f);

            setBool("delayActive", true);
            set("delayTime", 120.0f);
            set("delayFeedback", 0.1f);
            set("delayMix", 0.10f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Rock Warm Analog")
        {
            set("veqF1", 100.0f);
            set("veqG3", 1.5f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG6", 1.0f);
            set("veqG8", 2.0f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbMakeup4", 1.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 4.0f);
            set("compAttack", 8.0f);
            set("compKnee", 6.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.25f);
            set("satMode", 0.0f);
            set("satMix", 0.35f);

            setBool("tapeActive", true);
            set("tapeFlutter", 0.15f);
            set("tapeDrive", 0.2f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbLongMix", 0.06f);
            set("reverbDuck", 0.5f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Lo-Fi Tape Vocal")
        {
            set("veqF1", 200.0f);
            set("veqF12", 8000.0f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -18.0f);
            set("mbRatio2", 2.5f);

            set("compThreshold", -22.0f);
            set("compRatio", 4.5f);

            setBool("satActive", true);
            set("satDrive", 0.35f);
            set("satMode", 0.0f);
            set("satMix", 0.5f);

            setBool("tapeActive", true);
            set("tapeSpeed", 15.0f);
            set("tapeFlutter", 0.5f);
            set("tapeDrive", 0.35f);

            setBool("lofiActive", true);
            set("lofiHP", 300.0f);
            set("lofiLP", 4000.0f);
            set("lofiBits", 12.0f);
            set("lofiDS", 3.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.08f);
            set("reverbDuck", 0.6f);

            set("limiterCeiling", -0.5f);
        }
        else if (name == "Telephone / Radio")
        {
            set("veqF1", 400.0f);
            set("veqF12", 3500.0f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.0f);

            set("compThreshold", -22.0f);
            set("compRatio", 6.0f);
            set("compAttack", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.25f);
            set("satMode", 2.0f);
            set("satMix", 0.4f);

            setBool("lofiActive", true);
            set("lofiHP", 400.0f);
            set("lofiLP", 3500.0f);
            set("lofiBits", 10.0f);
            set("lofiDS", 4.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            set("limiterCeiling", -0.5f);
        }
        else if (name == "Vintage Saturated")
        {
            set("veqF1", 80.0f);
            set("veqF12", 12000.0f);
            set("veqG3", 2.5f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 3.5f);
            set("compAttack", 8.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.45f);
            set("satMode", 0.0f);
            set("satMix", 0.5f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("tapeActive", true);
            set("tapeSpeed", 15.0f);
            set("tapeFlutter", 0.3f);
            set("tapeDrive", 0.3f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.08f);
            set("reverbDuck", 0.6f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Robotic AutoTune")
        {
            set("veqF1", 100.0f);
            set("veqG4", -2.0f);
            set("veqG5", -1.0f);
            set("veqG6", 1.0f);
            set("veqG8", 3.5f);
            set("veqG9", 2.0f);
            set("veqG11", 2.5f);

            setBool("mbActive", true);
            set("mbThresh1", -22.0f);
            set("mbRatio1", 4.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.0f);
            set("mbMakeup4", 1.5f);
            set("mbMakeup5", 1.5f);

            set("compThreshold", -24.0f);
            set("compRatio", 6.0f);
            set("compAttack", 2.0f);
            set("compRelease", 40.0f);
            set("compKnee", 3.0f);

            setBool("satActive", true);
            set("satDrive", 0.2f);
            set("satMode", 0.0f);
            set("satMix", 0.3f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("delayActive", true);
            set("delayTime", 200.0f);
            set("delayFeedback", 0.18f);
            set("delayMix", 0.10f);
            set("delayDuck", 0.6f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.10f);
            set("reverbLongMix", 0.05f);
            set("reverbDuck", 0.7f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Ethereal Wide")
        {
            set("veqF1", 80.0f);
            set("veqG3", 1.0f);
            set("veqG4", -1.0f);
            set("veqG5", -1.5f);
            set("veqG8", 1.5f);
            set("veqG10", 2.0f);
            set("veqG11", 3.5f);

            setBool("mbActive", true);
            set("mbThresh1", -18.0f);
            set("mbRatio1", 2.5f);
            set("mbThresh2", -18.0f);
            set("mbRatio2", 2.5f);
            set("mbMakeup5", 1.5f);

            set("compThreshold", -18.0f);
            set("compRatio", 3.0f);
            set("compAttack", 8.0f);
            set("compRelease", 70.0f);
            set("compKnee", 10.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("satActive", true);
            set("satDrive", 0.1f);
            set("satMode", 0.0f);
            set("satMix", 0.18f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.18f);
            set("reverbLongSize", 0.8f);
            set("reverbLongMix", 0.20f);
            set("reverbDuck", 0.35f);

            setBool("widthActive", true);
            set("widthAmount", 1.6f);
            set("widthMode", 2.0f);

            setBool("doublerActive", true);
            set("doublerMix", 0.2f);
            set("doublerDetune", 10.0f);
            set("doublerDelay", 25.0f);

            setBool("delayActive", true);
            set("delayTime", 450.0f);
            set("delayFeedback", 0.25f);
            set("delayMix", 0.12f);
            set("delayDuck", 0.4f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Doubled & Thick")
        {
            set("veqF1", 80.0f);
            set("veqG3", 1.5f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG8", 2.0f);
            set("veqG11", 2.5f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);
            set("mbMakeup4", 1.0f);

            set("compThreshold", -22.0f);
            set("compRatio", 4.5f);
            set("compAttack", 5.0f);
            set("compRelease", 50.0f);
            set("compKnee", 6.0f);

            setBool("satActive", true);
            set("satDrive", 0.2f);
            set("satMode", 0.0f);
            set("satMix", 0.3f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("doublerActive", true);
            set("doublerMix", 0.4f);
            set("doublerDetune", 10.0f);
            set("doublerDelay", 20.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.4f);
            set("widthMode", 0.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.10f);
            set("reverbLongMix", 0.05f);
            set("reverbDuck", 0.6f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Reverb Wash")
        {
            set("veqF1", 80.0f);
            set("veqG3", 1.0f);
            set("veqG4", -1.0f);
            set("veqG5", -1.5f);
            set("veqG8", 1.5f);
            set("veqG11", 2.5f);

            setBool("mbActive", true);
            set("mbThresh1", -18.0f);
            set("mbRatio1", 2.5f);
            set("mbThresh2", -18.0f);
            set("mbRatio2", 2.5f);

            set("compThreshold", -20.0f);
            set("compRatio", 3.5f);
            set("compAttack", 8.0f);
            set("compRelease", 70.0f);
            set("compKnee", 8.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("satActive", true);
            set("satDrive", 0.1f);
            set("satMode", 0.0f);
            set("satMix", 0.2f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.20f);
            set("reverbLongSize", 0.85f);
            set("reverbLongMix", 0.20f);
            set("reverbLongDamp", 0.3f);
            set("reverbDuck", 0.25f);
            set("reverbPostEQ", 10000.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.5f);
            set("widthMode", 0.0f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Slapback Echo")
        {
            set("veqF1", 100.0f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG8", 2.0f);
            set("veqG11", 2.0f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -18.0f);
            set("mbRatio2", 2.5f);
            set("mbMakeup4", 1.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 4.0f);
            set("compAttack", 5.0f);
            set("compRelease", 50.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("satActive", true);
            set("satDrive", 0.12f);
            set("satMode", 0.0f);
            set("satMix", 0.2f);

            setBool("delayActive", true);
            set("delayTime", 80.0f);
            set("delayFeedback", 0.1f);
            set("delayMix", 0.18f);
            set("delayDuck", 0.4f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.10f);
            set("reverbLongMix", 0.04f);
            set("reverbDuck", 0.6f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Clean Vocal Chain")
        {
            set("veqF1", 80.0f);
            set("veqG4", -1.5f);
            set("veqG5", -1.0f);
            set("veqG6", -1.0f);
            set("veqG8", 2.0f);
            set("veqG11", 2.5f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);
            set("mbMakeup4", 1.0f);
            set("mbMakeup5", 1.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 3.5f);
            set("compAttack", 6.0f);
            set("compRelease", 55.0f);
            set("compKnee", 6.0f);

            setBool("satActive", true);
            set("satDrive", 0.1f);
            set("satMode", 0.0f);
            set("satMix", 0.15f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Broadcast / Podcast")
        {
            set("veqF1", 100.0f);
            set("veqF12", 16000.0f);
            set("veqG4", -1.5f);
            set("veqG5", -1.0f);
            set("veqG6", -2.0f);
            set("veqG8", 2.5f);

            setBool("mbActive", true);
            set("mbThresh1", -22.0f);
            set("mbRatio1", 3.5f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.0f);
            set("mbMakeup4", 1.5f);

            set("compThreshold", -24.0f);
            set("compRatio", 5.0f);
            set("compAttack", 5.0f);
            set("compRelease", 40.0f);
            set("compKnee", 4.0f);
            setBool("compAutoGain", true);
            setBool("autoLevel", true);
            set("autoLevelTarget", -16.0f);

            setBool("satActive", true);
            set("satDrive", 0.1f);
            set("satMode", 0.0f);
            set("satMix", 0.15f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            set("limiterCeiling", -1.0f);
        }
        else if (name == "Trap Adlib Bright")
        {
            set("veqF1", 200.0f);
            set("veqG7", 2.0f);
            set("veqG8", 3.5f);
            set("veqG10", 2.5f);
            set("veqG11", 4.0f);
            set("veqQ8", 2.5f);
            set("veqQ11", 0.7f);

            setBool("mbActive", true);
            set("mbThresh1", -22.0f);
            set("mbRatio1", 4.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.5f);
            set("mbMakeup4", 1.5f);
            set("mbMakeup5", 2.0f);

            set("compThreshold", -28.0f);
            set("compRatio", 8.0f);
            set("compAttack", 1.0f);
            set("compRelease", 25.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.3f);
            set("satMode", 0.0f);
            set("satMix", 0.35f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -14.0f);

            setBool("delayActive", true);
            set("delayTime", 100.0f);
            set("delayFeedback", 0.12f);
            set("delayMix", 0.15f);
            set("delayDuck", 0.8f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.08f);
            set("reverbDuck", 0.7f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Trap Mumble Smooth")
        {
            set("veqF1", 80.0f);
            set("veqF12", 15000.0f);
            set("veqG3", 2.0f);
            set("veqG4", -1.5f);
            set("veqG5", -1.5f);
            set("veqG8", 1.5f);
            set("veqQ3", 0.6f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);
            set("mbMakeup5", 1.0f);

            set("compThreshold", -22.0f);
            set("compRatio", 5.0f);
            set("compAttack", 4.0f);
            set("compKnee", 6.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.18f);
            set("satMode", 0.0f);
            set("satMix", 0.3f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbLongMix", 0.06f);
            set("reverbDuck", 0.6f);

            setBool("convActive", true);
            set("convSize", 1.0f);
            set("convDamping", 0.6f);
            set("convMix", 0.08f);
            setBool("convReverse", false);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.15f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Drill Raw Vocal")
        {
            set("veqF1", 150.0f);
            set("veqG4", -2.0f);
            set("veqG5", -1.0f);
            set("veqG6", 2.5f);
            set("veqG8", 3.5f);
            set("veqG10", 2.0f);
            set("veqQ6", 2.0f);
            set("veqQ8", 2.5f);

            setBool("mbActive", true);
            set("mbThresh1", -22.0f);
            set("mbRatio1", 5.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.5f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.0f);
            set("mbMakeup4", 1.5f);

            set("compThreshold", -28.0f);
            set("compRatio", 10.0f);
            set("compAttack", 1.0f);
            set("compRelease", 20.0f);
            set("compKnee", 1.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.35f);
            set("satMode", 2.0f);
            set("satMix", 0.4f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -14.0f);

            setBool("delayActive", true);
            set("delayTime", 150.0f);
            set("delayFeedback", 0.15f);
            set("delayMix", 0.10f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.06f);
            set("reverbDuck", 0.7f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Rage Beat Vocal")
        {
            set("veqF1", 180.0f);
            set("veqG4", -1.5f);
            set("veqG5", -2.0f);
            set("veqG6", 3.0f);
            set("veqG8", 4.0f);
            set("veqG9", 2.5f);
            set("veqG11", 2.0f);
            set("veqQ6", 2.5f);
            set("veqQ8", 3.0f);
            setBool("veqDyn8", true);

            setBool("mbActive", true);
            set("mbThresh1", -24.0f);
            set("mbRatio1", 5.0f);
            set("mbThresh2", -22.0f);
            set("mbRatio2", 4.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.5f);
            set("mbMakeup4", 2.0f);
            set("mbMakeup5", 1.5f);

            set("compThreshold", -30.0f);
            set("compRatio", 12.0f);
            set("compAttack", 0.5f);
            set("compRelease", 15.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.5f);
            set("satMode", 2.0f);
            set("satMix", 0.45f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -14.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.4f);
            set("widthMode", 1.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.08f);
            set("reverbDuck", 0.8f);

            setBool("convActive", true);
            set("convSize", 0.6f);
            set("convDamping", 0.3f);
            set("convMix", 0.12f);
            setBool("convReverse", true);

            setBool("delayActive", true);
            set("delayTime", 120.0f);
            set("delayFeedback", 0.1f);
            set("delayMix", 0.08f);
            set("delayDuck", 0.7f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "808 Bass Vocal")
        {
            set("veqF1", 60.0f);
            set("veqG3", 3.5f);
            set("veqG4", -1.0f);
            set("veqG6", 1.0f);
            set("veqG8", -0.5f);
            set("veqF12", 12000.0f);

            setBool("mbActive", true);
            set("mbThresh1", -18.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);

            set("compThreshold", -24.0f);
            set("compRatio", 6.0f);
            set("compAttack", 3.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.3f);
            set("satMode", 0.0f);
            set("satMix", 0.4f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("tapeActive", true);
            set("tapeFlutter", 0.1f);
            set("tapeDrive", 0.25f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.06f);
            set("reverbDuck", 0.6f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "R&B Falsetto Air")
        {
            set("veqF1", 120.0f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG8", 2.0f);
            set("veqG10", 1.5f);
            set("veqG11", 4.0f);

            setBool("mbActive", true);
            set("mbThresh1", -18.0f);
            set("mbRatio1", 2.5f);
            set("mbThresh2", -18.0f);
            set("mbRatio2", 2.5f);
            set("mbMakeup4", 1.0f);
            set("mbMakeup5", 2.0f);

            set("compThreshold", -18.0f);
            set("compRatio", 3.0f);
            set("compAttack", 10.0f);
            set("compRelease", 70.0f);
            set("compKnee", 10.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("satActive", true);
            set("satDrive", 0.1f);
            set("satMode", 0.0f);
            set("satMix", 0.18f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.15f);
            set("reverbLongMix", 0.08f);
            set("reverbDuck", 0.4f);

            setBool("widthActive", true);
            set("widthAmount", 1.3f);
            set("widthMode", 2.0f);

            setBool("doublerActive", true);
            set("doublerMix", 0.12f);
            set("doublerDetune", 5.0f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "90s R&B Classic")
        {
            set("veqF1", 70.0f);
            set("veqG3", 2.5f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG6", -1.0f);
            set("veqG8", 2.0f);
            set("veqF12", 14000.0f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbMakeup4", 1.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 3.5f);
            set("compAttack", 8.0f);
            set("compKnee", 8.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.2f);
            set("satMode", 0.0f);
            set("satMix", 0.3f);

            setBool("tapeActive", true);
            set("tapeSpeed", 30.0f);
            set("tapeFlutter", 0.15f);
            set("tapeDrive", 0.12f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.14f);
            set("reverbLongMix", 0.08f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("delayActive", true);
            set("delayTime", 280.0f);
            set("delayFeedback", 0.18f);
            set("delayMix", 0.08f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Bedroom R&B")
        {
            set("veqF1", 70.0f);
            set("veqG3", 1.5f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG11", 2.5f);

            setBool("mbActive", true);
            set("mbThresh1", -18.0f);
            set("mbRatio1", 2.5f);
            set("mbThresh2", -18.0f);
            set("mbRatio2", 2.5f);
            set("mbMakeup5", 1.0f);

            set("compThreshold", -18.0f);
            set("compRatio", 3.0f);
            set("compAttack", 8.0f);
            set("compKnee", 8.0f);

            setBool("satActive", true);
            set("satDrive", 0.15f);
            set("satMode", 0.0f);
            set("satMix", 0.22f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbLongMix", 0.08f);
            set("reverbDuck", 0.4f);

            setBool("widthActive", true);
            set("widthAmount", 1.2f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("doublerActive", true);
            set("doublerMix", 0.10f);
            set("doublerDetune", 6.0f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Pop Ballad Lush")
        {
            set("veqF1", 80.0f);
            set("veqG3", 1.5f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG8", 2.0f);
            set("veqG10", 1.0f);
            set("veqG11", 2.5f);

            setBool("mbActive", true);
            set("mbThresh1", -18.0f);
            set("mbRatio1", 2.5f);
            set("mbThresh2", -18.0f);
            set("mbRatio2", 2.5f);
            set("mbMakeup4", 1.0f);
            set("mbMakeup5", 1.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 3.5f);
            set("compAttack", 8.0f);
            set("compRelease", 65.0f);
            set("compKnee", 8.0f);
            set("thdMode", 1.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("satActive", true);
            set("satDrive", 0.1f);
            set("satMode", 0.0f);
            set("satMix", 0.2f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.18f);
            set("reverbLongMix", 0.12f);
            set("reverbLongDamp", 0.35f);
            set("reverbDuck", 0.4f);

            setBool("delayActive", true);
            set("delayTime", 400.0f);
            set("delayFeedback", 0.2f);
            set("delayMix", 0.08f);

            setBool("widthActive", true);
            set("widthAmount", 1.25f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "K-Pop Crystal")
        {
            set("veqF1", 120.0f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG8", 3.0f);
            set("veqG11", 4.5f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -18.0f);
            set("mbRatio2", 2.5f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.0f);
            set("mbMakeup4", 1.5f);
            set("mbMakeup5", 2.0f);

            set("compThreshold", -22.0f);
            set("compRatio", 4.5f);
            set("compAttack", 3.0f);
            set("compRelease", 35.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -14.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbLongMix", 0.06f);

            setBool("widthActive", true);
            set("widthAmount", 1.2f);
            set("widthMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.1f);
            set("satMode", 0.0f);
            set("satMix", 0.18f);

            setBool("doublerActive", true);
            set("doublerMix", 0.15f);
            set("doublerDetune", 8.0f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Pop Punk Grit")
        {
            set("veqF1", 130.0f);
            set("veqG4", -1.5f);
            set("veqG5", -1.0f);
            set("veqG6", 2.5f);
            set("veqG8", 3.0f);
            set("veqG11", 1.5f);

            setBool("mbActive", true);
            set("mbThresh1", -22.0f);
            set("mbRatio1", 4.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.0f);
            set("mbMakeup4", 1.5f);

            set("compThreshold", -26.0f);
            set("compRatio", 7.0f);
            set("compAttack", 2.0f);
            set("compRelease", 30.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.35f);
            set("satMode", 2.0f);
            set("satMix", 0.4f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -15.0f);

            setBool("delayActive", true);
            set("delayTime", 110.0f);
            set("delayFeedback", 0.1f);
            set("delayMix", 0.12f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.08f);
            set("reverbDuck", 0.7f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Metal Scream")
        {
            set("veqF1", 200.0f);
            set("veqG4", -1.0f);
            set("veqG6", 3.5f);
            set("veqG8", 4.5f);
            set("veqG11", -0.5f);

            setBool("mbActive", true);
            set("mbThresh1", -24.0f);
            set("mbRatio1", 5.0f);
            set("mbThresh2", -22.0f);
            set("mbRatio2", 4.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.5f);
            set("mbMakeup4", 2.0f);

            set("compThreshold", -30.0f);
            set("compRatio", 15.0f);
            set("compAttack", 0.5f);
            set("compRelease", 15.0f);
            set("compKnee", 1.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.6f);
            set("satMode", 2.0f);
            set("satMix", 0.55f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -12.0f);
            set("deEsserReduce", -16.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.10f);
            set("reverbDuck", 0.7f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Indie Folk Natural")
        {
            set("veqF1", 80.0f);
            set("veqG3", 1.5f);
            set("veqG4", -1.0f);
            set("veqG5", -0.5f);
            set("veqG8", 1.5f);
            set("veqG10", 1.0f);
            set("veqG11", 2.0f);

            setBool("mbActive", true);
            set("mbThresh1", -18.0f);
            set("mbRatio1", 2.5f);
            set("mbThresh2", -18.0f);
            set("mbRatio2", 2.5f);
            set("mbMakeup5", 1.0f);

            set("compThreshold", -18.0f);
            set("compRatio", 2.5f);
            set("compAttack", 12.0f);
            set("compRelease", 80.0f);
            set("compKnee", 10.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("satActive", true);
            set("satDrive", 0.08f);
            set("satMode", 0.0f);
            set("satMix", 0.15f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.14f);
            set("reverbLongMix", 0.06f);
            set("reverbDuck", 0.5f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Vinyl Crackle Vox")
        {
            set("veqF1", 150.0f);
            set("veqF12", 10000.0f);
            set("veqG3", 2.5f);
            set("veqG4", -1.0f);
            set("veqG11", -1.5f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -18.0f);
            set("mbRatio2", 2.5f);

            set("compThreshold", -22.0f);
            set("compRatio", 4.0f);

            setBool("satActive", true);
            set("satDrive", 0.4f);
            set("satMode", 0.0f);
            set("satMix", 0.45f);

            setBool("tapeActive", true);
            set("tapeSpeed", 15.0f);
            set("tapeFlutter", 0.6f);
            set("tapeDrive", 0.4f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("lofiActive", true);
            set("lofiHP", 250.0f);
            set("lofiLP", 5000.0f);
            set("lofiBits", 14.0f);
            set("lofiDS", 2.0f);

            set("limiterCeiling", -0.5f);
        }
        else if (name == "Bitcrushed Glitch")
        {
            set("veqF1", 200.0f);
            set("veqG8", 2.0f);

            setBool("mbActive", true);
            set("mbThresh1", -22.0f);
            set("mbRatio1", 4.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.0f);

            set("compThreshold", -26.0f);
            set("compRatio", 8.0f);
            set("compAttack", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.4f);
            set("satMode", 2.0f);
            set("satMix", 0.5f);

            setBool("lofiActive", true);
            set("lofiHP", 350.0f);
            set("lofiLP", 4500.0f);
            set("lofiBits", 6.0f);
            set("lofiDS", 8.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -14.0f);

            setBool("delayActive", true);
            set("delayTime", 170.0f);
            set("delayFeedback", 0.35f);
            set("delayMix", 0.15f);
            setBool("delayPingPong", true);

            set("limiterCeiling", -0.5f);
        }
        else if (name == "Underwater Dream")
        {
            set("veqF1", 70.0f);
            set("veqF12", 8000.0f);
            set("veqG3", 2.5f);
            set("veqG4", -1.0f);
            set("veqG8", -1.5f);
            set("veqG11", -2.0f);

            setBool("mbActive", true);
            set("mbThresh1", -18.0f);
            set("mbRatio1", 2.5f);
            set("mbThresh2", -18.0f);
            set("mbRatio2", 2.5f);

            set("compThreshold", -18.0f);
            set("compRatio", 3.0f);
            set("compKnee", 10.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.20f);
            set("reverbLongMix", 0.20f);
            set("reverbLongDamp", 0.2f);
            set("reverbDuck", 0.2f);
            set("reverbPostEQ", 5000.0f);

            setBool("delayActive", true);
            set("delayTime", 600.0f);
            set("delayFeedback", 0.4f);
            set("delayMix", 0.15f);
            set("delayFilter", 3000.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.6f);
            set("widthMode", 2.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("satActive", true);
            set("satDrive", 0.1f);
            set("satMode", 0.0f);
            set("satMix", 0.15f);

            set("dryWet", 0.85f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Cathedral Choir")
        {
            set("veqF1", 80.0f);
            set("veqG3", 1.5f);
            set("veqG4", -1.0f);
            set("veqG11", 2.5f);

            setBool("mbActive", true);
            set("mbThresh1", -18.0f);
            set("mbRatio1", 2.5f);
            set("mbThresh2", -18.0f);
            set("mbRatio2", 2.5f);
            set("mbMakeup5", 1.0f);

            set("compThreshold", -18.0f);
            set("compRatio", 2.5f);
            set("compKnee", 10.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.20f);
            set("reverbLongSize", 0.9f);
            set("reverbLongMix", 0.20f);
            set("reverbLongDamp", 0.2f);
            set("reverbDuck", 0.2f);

            setBool("doublerActive", true);
            set("doublerMix", 0.3f);
            set("doublerDetune", 10.0f);
            set("doublerDelay", 30.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.5f);
            set("widthMode", 0.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("satActive", true);
            set("satDrive", 0.08f);
            set("satMode", 0.0f);
            set("satMix", 0.12f);

            setBool("delayActive", true);
            set("delayTime", 700.0f);
            set("delayFeedback", 0.25f);
            set("delayMix", 0.08f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Gospel Powerful")
        {
            set("veqF1", 80.0f);
            set("veqG3", 2.0f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG6", -1.0f);
            set("veqG8", 3.0f);
            set("veqG11", 2.5f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);
            set("mbMakeup4", 1.5f);
            set("mbMakeup5", 1.0f);

            set("compThreshold", -22.0f);
            set("compRatio", 4.5f);
            set("compAttack", 5.0f);
            set("compRelease", 55.0f);
            set("compKnee", 6.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.15f);
            set("satMode", 0.0f);
            set("satMix", 0.25f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.18f);
            set("reverbLongMix", 0.10f);
            set("reverbDuck", 0.4f);

            setBool("widthActive", true);
            set("widthAmount", 1.25f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("doublerActive", true);
            set("doublerMix", 0.15f);
            set("doublerDetune", 8.0f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Country Twang")
        {
            set("veqF1", 100.0f);
            set("veqG3", 1.0f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG6", 2.0f);
            set("veqG8", 3.0f);
            set("veqG11", 1.5f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbMakeup4", 1.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 3.5f);
            set("compAttack", 8.0f);
            set("compKnee", 6.0f);

            setBool("satActive", true);
            set("satDrive", 0.18f);
            set("satMode", 0.0f);
            set("satMix", 0.25f);

            setBool("tapeActive", true);
            set("tapeFlutter", 0.12f);
            set("tapeDrive", 0.12f);

            setBool("delayActive", true);
            set("delayTime", 200.0f);
            set("delayFeedback", 0.12f);
            set("delayMix", 0.10f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbLongMix", 0.06f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Latin Reggaeton")
        {
            set("veqF1", 100.0f);
            set("veqG3", 1.5f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG6", 1.0f);
            set("veqG8", 2.5f);
            set("veqG11", 2.0f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);
            set("mbMakeup4", 1.0f);

            set("compThreshold", -22.0f);
            set("compRatio", 4.5f);
            set("compAttack", 4.0f);
            set("compRelease", 40.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.18f);
            set("satMode", 0.0f);
            set("satMix", 0.25f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbLongMix", 0.06f);
            set("reverbDuck", 0.6f);

            setBool("delayActive", true);
            set("delayTime", 330.0f);
            set("delayFeedback", 0.2f);
            set("delayMix", 0.12f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.15f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Afrobeats Vocal")
        {
            set("veqF1", 90.0f);
            set("veqG3", 1.5f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG8", 2.5f);
            set("veqG11", 3.0f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);
            set("mbMakeup4", 1.0f);
            set("mbMakeup5", 1.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 4.0f);
            set("compAttack", 5.0f);
            set("compRelease", 50.0f);
            set("compKnee", 6.0f);

            setBool("satActive", true);
            set("satDrive", 0.12f);
            set("satMode", 0.0f);
            set("satMix", 0.2f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.14f);
            set("reverbLongMix", 0.08f);
            set("reverbDuck", 0.5f);

            setBool("delayActive", true);
            set("delayTime", 375.0f);
            set("delayFeedback", 0.18f);
            set("delayMix", 0.10f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("doublerActive", true);
            set("doublerMix", 0.12f);
            set("doublerDetune", 7.0f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "EDM Festival Drop")
        {
            set("veqF1", 150.0f);
            set("veqG4", -1.0f);
            set("veqG8", 3.0f);
            set("veqG11", 3.5f);

            setBool("mbActive", true);
            set("mbThresh1", -22.0f);
            set("mbRatio1", 4.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.5f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.5f);
            set("mbMakeup4", 2.0f);
            set("mbMakeup5", 2.0f);

            set("compThreshold", -28.0f);
            set("compRatio", 8.0f);
            set("compAttack", 1.0f);
            set("compRelease", 20.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.35f);
            set("satMode", 2.0f);
            set("satMix", 0.4f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.14f);
            set("reverbLongMix", 0.08f);
            set("reverbDuck", 0.7f);

            setBool("delayActive", true);
            set("delayTime", 375.0f);
            set("delayFeedback", 0.3f);
            set("delayMix", 0.14f);
            setBool("delayPingPong", true);

            setBool("widthActive", true);
            set("widthAmount", 1.5f);
            set("widthMode", 1.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -14.0f);

            setBool("doublerActive", true);
            set("doublerMix", 0.25f);
            set("doublerDetune", 12.0f);

            set("limiterCeiling", -0.1f);
        }
        else if (name == "Voiceover Warmth")
        {
            set("veqF1", 80.0f);
            set("veqF12", 14000.0f);
            set("veqG3", 2.0f);
            set("veqG4", -1.5f);
            set("veqG5", -1.0f);
            set("veqG6", -2.0f);
            set("veqG8", 2.0f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);
            set("mbMakeup4", 1.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 4.0f);
            set("compAttack", 6.0f);
            set("compRelease", 70.0f);
            set("compKnee", 6.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.12f);
            set("satMode", 0.0f);
            set("satMix", 0.2f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            set("limiterCeiling", -1.0f);
        }
        else if (name == "Live Performance")
        {
            set("veqF1", 120.0f);
            set("veqG4", -1.5f);
            set("veqG5", -1.0f);
            set("veqG6", -1.5f);
            set("veqG8", 2.5f);
            set("veqG11", 1.5f);

            setBool("mbActive", true);
            set("mbThresh1", -22.0f);
            set("mbRatio1", 3.5f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.0f);
            set("mbMakeup4", 1.5f);

            set("compThreshold", -24.0f);
            set("compRatio", 5.0f);
            set("compAttack", 3.0f);
            set("compRelease", 35.0f);
            set("compKnee", 4.0f);

            setBool("satActive", true);
            set("satDrive", 0.1f);
            set("satMode", 0.0f);
            set("satMix", 0.15f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -15.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.10f);
            set("reverbDuck", 0.7f);

            set("limiterCeiling", -0.5f);
        }
        else if (name == "Mastered Vocal Bus")
        {
            set("veqF1", 80.0f);
            set("veqG3", 1.0f);
            set("veqG4", -1.5f);
            set("veqG5", -1.0f);
            set("veqG6", -1.0f);
            set("veqG8", 2.0f);
            set("veqG11", 2.5f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);
            set("mbMakeup4", 1.5f);
            set("mbMakeup5", 1.0f);

            set("compThreshold", -22.0f);
            set("compRatio", 4.0f);
            set("compAttack", 5.0f);
            set("compRelease", 50.0f);
            set("compKnee", 6.0f);
            setBool("compAutoGain", true);
            setBool("autoLevel", true);
            set("autoLevelTarget", -14.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.12f);
            set("satMode", 0.0f);
            set("satMix", 0.2f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.1f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Drocett Smooth Melodies")
        {
            set("veqF1", 80.0f);
            set("veqG3", 2.0f);
            set("veqG4", -1.5f);
            set("veqG5", -2.0f);
            set("veqG6", -0.5f);
            set("veqG7", 1.5f);
            set("veqG8", 2.5f);
            set("veqG10", 1.0f);
            set("veqG11", 3.0f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);
            set("mbThresh4", -16.0f);
            set("mbRatio4", 2.0f);
            set("mbMakeup4", 1.5f);
            set("mbMakeup5", 2.0f);

            set("compThreshold", -22.0f);
            set("compRatio", 4.0f);
            set("compAttack", 5.0f);
            set("compRelease", 50.0f);
            set("compKnee", 6.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.15f);
            set("satMode", 0.0f);
            set("satMix", 0.25f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbLongMix", 0.06f);
            set("reverbDuck", 0.5f);

            setBool("widthActive", true);
            set("widthAmount", 1.2f);
            set("widthMode", 0.0f);

            setBool("delayActive", true);
            set("delayTime", 300.0f);
            set("delayFeedback", 0.18f);
            set("delayMix", 0.08f);
            set("delayDuck", 0.6f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Drocett Trap Soul")
        {
            set("veqF1", 100.0f);
            set("veqG3", 1.5f);
            set("veqG4", -1.5f);
            set("veqG5", -1.0f);
            set("veqG8", 2.5f);
            set("veqG11", 2.5f);

            setBool("mbActive", true);
            set("mbThresh1", -22.0f);
            set("mbRatio1", 3.5f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 2.5f);
            set("mbMakeup4", 1.5f);
            set("mbMakeup5", 1.0f);

            set("compThreshold", -24.0f);
            set("compRatio", 5.0f);
            set("compAttack", 4.0f);
            set("compRelease", 40.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.2f);
            set("satMode", 0.0f);
            set("satMix", 0.3f);

            setBool("tapeActive", true);
            set("tapeFlutter", 0.1f);
            set("tapeDrive", 0.12f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbLongMix", 0.06f);
            set("reverbDuck", 0.55f);

            setBool("doublerActive", true);
            set("doublerMix", 0.1f);
            set("doublerDetune", 7.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("delayActive", true);
            set("delayTime", 220.0f);
            set("delayFeedback", 0.15f);
            set("delayMix", 0.10f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Drocett Late Night")
        {
            set("veqF1", 70.0f);
            set("veqF12", 13000.0f);
            set("veqG3", 2.0f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG6", -1.0f);
            set("veqG8", 1.5f);

            setBool("mbActive", true);
            set("mbThresh1", -20.0f);
            set("mbRatio1", 3.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbMakeup5", 1.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 3.5f);
            set("compAttack", 8.0f);
            set("compKnee", 8.0f);

            setBool("satActive", true);
            set("satDrive", 0.18f);
            set("satMode", 0.0f);
            set("satMix", 0.3f);

            setBool("tapeActive", true);
            set("tapeFlutter", 0.15f);
            set("tapeDrive", 0.15f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.14f);
            set("reverbLongMix", 0.10f);
            set("reverbDuck", 0.4f);
            set("reverbPostEQ", 6000.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.25f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Drocett Falsetto Vibe")
        {
            set("veqF1", 130.0f);
            set("veqG4", -1.0f);
            set("veqG5", -1.0f);
            set("veqG8", 2.5f);
            set("veqG11", 4.0f);

            setBool("mbActive", true);
            set("mbThresh1", -18.0f);
            set("mbRatio1", 2.5f);
            set("mbThresh2", -18.0f);
            set("mbRatio2", 2.5f);
            set("mbMakeup4", 1.0f);
            set("mbMakeup5", 2.0f);

            set("compThreshold", -18.0f);
            set("compRatio", 3.0f);
            set("compAttack", 10.0f);
            set("compKnee", 10.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.16f);
            set("reverbLongMix", 0.10f);
            set("reverbDuck", 0.4f);

            setBool("widthActive", true);
            set("widthAmount", 1.4f);
            set("widthMode", 2.0f);

            setBool("doublerActive", true);
            set("doublerMix", 0.15f);
            set("doublerDetune", 6.0f);
            set("doublerDelay", 20.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            setBool("satActive", true);
            set("satDrive", 0.08f);
            set("satMode", 0.0f);
            set("satMix", 0.12f);

            setBool("delayActive", true);
            set("delayTime", 400.0f);
            set("delayFeedback", 0.2f);
            set("delayMix", 0.10f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Nu Rock Dry Scream")
        {
            set("veqF1", 180.0f);
            set("veqF12", 16000.0f);
            set("veqG4", -1.0f);
            set("veqG6", 3.5f);
            set("veqG8", 4.5f);
            set("veqG11", -0.5f);

            setBool("mbActive", true);
            set("mbThresh1", -24.0f);
            set("mbRatio1", 5.0f);
            set("mbThresh2", -22.0f);
            set("mbRatio2", 4.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.5f);
            set("mbMakeup4", 2.0f);

            set("compThreshold", -28.0f);
            set("compRatio", 12.0f);
            set("compAttack", 0.5f);
            set("compRelease", 15.0f);
            set("compKnee", 1.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.55f);
            set("satMode", 2.0f);
            set("satMix", 0.5f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -12.0f);
            set("deEsserReduce", -16.0f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Nu Rock Raw Edge")
        {
            set("veqF1", 160.0f);
            set("veqG4", -1.0f);
            set("veqG6", 2.5f);
            set("veqG8", 3.5f);
            set("veqG11", 1.0f);

            setBool("mbActive", true);
            set("mbThresh1", -22.0f);
            set("mbRatio1", 4.0f);
            set("mbThresh2", -20.0f);
            set("mbRatio2", 3.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.0f);
            set("mbMakeup4", 1.5f);

            set("compThreshold", -26.0f);
            set("compRatio", 8.0f);
            set("compAttack", 1.0f);
            set("compRelease", 20.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.4f);
            set("satMode", 2.0f);
            set("satMix", 0.45f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -13.0f);

            setBool("reverbActive", true);
            set("reverbShortSize", 0.15f);
            set("reverbShortMix", 0.06f);
            set("reverbDuck", 0.8f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Nu Rock Grit & Growl")
        {
            set("veqF1", 200.0f);
            set("veqG3", -0.5f);
            set("veqG4", -1.0f);
            set("veqG6", 3.5f);
            set("veqG8", 3.5f);

            setBool("mbActive", true);
            set("mbThresh1", -24.0f);
            set("mbRatio1", 5.0f);
            set("mbThresh2", -22.0f);
            set("mbRatio2", 4.0f);
            set("mbThresh3", -18.0f);
            set("mbRatio3", 3.5f);
            set("mbMakeup4", 2.0f);

            set("compThreshold", -30.0f);
            set("compRatio", 15.0f);
            set("compAttack", 0.5f);
            set("compRelease", 12.0f);
            set("compKnee", 1.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.65f);
            set("satMode", 2.0f);
            set("satMix", 0.6f);

            setBool("tapeActive", true);
            set("tapeSpeed", 15.0f);
            set("tapeFlutter", 0.1f);
            set("tapeDrive", 0.45f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -10.0f);
            set("deEsserReduce", -18.0f);

            setBool("delayActive", true);
            set("delayTime", 90.0f);
            set("delayFeedback", 0.08f);
            set("delayMix", 0.08f);

            set("limiterCeiling", -0.3f);
        }
    }

    // -----------------------------------------------------------------------
    // Reset all params to safe defaults
    // -----------------------------------------------------------------------
    void resetToDefaults (const std::function<void(const juce::String&, float)>& set,
                          const std::function<void(const juce::String&, bool)>& setBool)
    {
        // Noise Gate
        setBool("gateActive", false);
        set("gateThreshold", -40.0f);
        set("gateRatio", 100.0f);
        set("gateAttack", 0.1f);
        set("gateHold", 50.0f);
        set("gateRelease", 100.0f);
        set("gateRange", -80.0f);

        // Visual EQ (12-band)
        setBool("veqActive", true);
        {
            float defaultFreqs[] = { 30.f, 80.f, 160.f, 300.f, 500.f, 800.f, 1200.f, 2500.f, 4000.f, 6000.f, 10000.f, 16000.f };
            for (int i = 0; i < 12; ++i)
            {
                auto si = juce::String(i + 1);
                set("veqF" + si, defaultFreqs[i]);
                set("veqG" + si, 0.0f);
                set("veqQ" + si, 1.0f);
                set("veqT" + si, (i == 0) ? 4.0f : (i == 11) ? 3.0f : 0.0f);
                setBool("veqDyn" + si, false);
            }
        }

        // Comp
        setBool("compActive", true);
        set("compThreshold", -18.0f);
        set("compRatio", 4.0f);
        set("compAttack", 5.0f);
        set("compRelease", 50.0f);
        set("compMakeup", 0.0f);
        set("compKnee", 6.0f);
        set("compOutputGain", 0.0f);
        setBool("compAutoGain", true);
        setBool("autoLevel", false);
        set("autoLevelTarget", -14.0f);
        set("thdMode", 0.0f);

        // Multiband Compressor
        setBool("mbActive", false);
        for (int b = 0; b < 5; ++b)
        {
            auto sb = juce::String(b + 1);
            set("mbThresh" + sb, -18.0f);
            set("mbRatio"  + sb, 4.0f);
            set("mbAttack" + sb, 5.0f);
            set("mbRel"    + sb, 50.0f);
            set("mbMakeup" + sb, 0.0f);
        }
        set("mbOutputGain", 0.0f);

        // De-Esser
        setBool("deEsserActive", true);
        set("deEsserFreq", 7000.0f);
        set("deEsserThresh", -20.0f);
        set("deEsserReduce", -12.0f);
        set("deEsserBW", 2.0f);
        set("deEsserMode", 0.0f);
        setBool("deEsserListen", false);

        // Saturation
        setBool("satActive", false);
        set("satDrive", 0.3f);
        set("satMode", 0.0f);
        set("satMix", 0.5f);

        // Tape
        setBool("tapeActive", false);
        set("tapeSpeed", 30.0f);
        set("tapeFlutter", 0.3f);
        set("tapeDrive", 0.2f);

        // Width
        setBool("widthActive", false);
        set("widthAmount", 1.0f);
        set("widthMode", 0.0f);

        // Doubler
        setBool("doublerActive", false);
        set("doublerMix", 0.5f);
        set("doublerDetune", 10.0f);
        set("doublerDelay", 20.0f);

        // Reverb
        setBool("reverbActive", false);
        set("reverbShortSize", 0.3f);
        set("reverbShortDamp", 0.5f);
        set("reverbShortMix", 0.2f);
        set("reverbLongSize", 0.7f);
        set("reverbLongDamp", 0.4f);
        set("reverbLongMix", 0.15f);
        set("reverbDuck", 0.5f);
        set("reverbPostEQ", 8000.0f);

        // Convolver Reverb
        setBool("convActive", false);
        set("convSize", 1.5f);
        set("convDamping", 0.5f);
        set("convMix", 0.3f);
        set("convPreDelay", 0.0f);
        setBool("convReverse", false);

        // Delay
        setBool("delayActive", false);
        set("delayTime", 250.0f);
        set("delayFeedback", 0.3f);
        set("delayMix", 0.2f);
        set("delayDuck", 0.5f);
        set("delayFilter", 6000.0f);
        setBool("delayPingPong", false);

        // Lo-Fi
        setBool("lofiActive", false);
        set("lofiHP", 400.0f);
        set("lofiLP", 3500.0f);
        set("lofiBits", 32.0f);
        set("lofiDS", 1.0f);

        // Limiter
        setBool("limiterActive", true);
        set("limiterCeiling", -0.3f);
        set("limiterRelease", 50.0f);
        set("limiterOutputGain", 0.0f);

        // Master
        set("inputGain", 0.0f);
        set("outputGain", 8.0f);
        set("dryWet", 1.0f);
    }
};

} // namespace humvocal
