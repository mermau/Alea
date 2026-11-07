#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "FSHCircuit.h"
#include "PeakLimiter.h"

namespace ParamIDs
{
    inline constexpr const char* range  = "range";    // FILTER RANGE (R15)
    inline constexpr const char* speed  = "speed";    // S/H SPEED (R28)
    inline constexpr const char* mode   = "mode";     // SW1: Filter / Sample-Hold
    inline constexpr const char* bypass = "bypass";   // SW2: footswitch
    inline constexpr const char* input  = "input";
    inline constexpr const char* output = "output";
    // pannello interno
    inline constexpr const char* offset = "shOffset"; // trimmer R26 (centro del S/H)
    inline constexpr const char* level  = "shLevel";  // trimmer R27 (ampiezza del S/H)
    inline constexpr const char* noise  = "noiseQ4";  // rumore del transistor Q4 ("selected for noise")
    inline constexpr const char* unit   = "unit";     // esemplare: tolleranze dei componenti (0 = schema)
    inline constexpr const char* pickup = "pickupLoad"; // carico di ~100 kOhm su un pickup passivo
    inline constexpr const char* limiter = "limiter";   // protezione dell'uscita digitale
}

namespace Defaults
{
    inline constexpr float inputDb = 10.8f;
}



namespace Factory
{
    inline constexpr float offset = 0.30f, level = 0.45f, noise = 3.0f;
}

//==============================================================================
class AleaProcessor : public juce::AudioProcessor
{
public:
    AleaProcessor();
    ~AleaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;

    //==========================================================================
    // MIDI CC: mappa e MIDI Learn (thread-safe, senza lock)
    static constexpr int numLearnable = 9;
    static const std::array<const char*, numLearnable> learnableIDs;
    static bool isToggle (int idx) { return idx == 2 || idx == 3; }   // MODE e FOOTSWITCH

    int  getLearnIndex (const juce::String& paramID) const;
    void startLearn (int paramIndex)  { learningParam.store (paramIndex); }
    void cancelLearn()                { learningParam.store (-1); }
    int  getLearningParam() const     { return learningParam.load(); }
    int  getCCForParam (int paramIndex) const;
    void clearMappingForParam (int paramIndex);
    void resetDefaultMappings();
    int  getMapVersion() const        { return mapVersion.load(); }

    std::atomic<int> lastCC { -1 }, lastCCValue { 0 }, ccCounter { 0 };

    // per l'interfaccia: impulsi del clock S/H e frequenza del filtro
    std::atomic<int>   clockPulses { 0 };
    std::atomic<float> centreHz { 300.0f };
    std::atomic<float> gainReductionDb { 0.0f };   // per la spia LIMIT

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleCC (int cc, int value);

    alea::FSHCircuit circuit;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    juce::SmoothedValue<float> rangeSm, speedSm, inGainSm, outGainSm, wetSm, offsetSm, levelSm, noiseSm;

    std::atomic<float>* rangeRaw  = nullptr;
    std::atomic<float>* speedRaw  = nullptr;
    std::atomic<float>* modeRaw   = nullptr;
    std::atomic<float>* bypassRaw = nullptr;
    std::atomic<float>* inRaw     = nullptr;
    std::atomic<float>* outRaw    = nullptr;
    std::atomic<float>* offsetRaw = nullptr;
    std::atomic<float>* levelRaw  = nullptr;
    std::atomic<float>* noiseRaw  = nullptr;
    std::atomic<float>* unitRaw   = nullptr;
    std::atomic<float>* pickupRaw = nullptr;
    std::atomic<float>* limitRaw  = nullptr;
    PeakLimiter limiter;
    juce::AudioParameterBool* bypassParam = nullptr;

    std::array<juce::RangedAudioParameter*, numLearnable> learnParams {};
    std::array<std::atomic<int>, 128> ccToParam;
    std::array<bool, 128> ccHigh {};
    std::atomic<int> learningParam { -1 }, mapVersion { 0 };

    int maxBlock = 512, osChannels = 2;
    bool lastClock = false;

    static constexpr int controlBlock = 16;        // campioni (frequenza base)
    static constexpr float voltsPerFS = 1.0f;      // 0 dBFS = 1 V di picco all'ingresso

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AleaProcessor)
};
