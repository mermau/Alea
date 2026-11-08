#include "PluginProcessor.h"
#include "PluginEditor.h"

const std::array<const char*, AleaProcessor::numLearnable> AleaProcessor::learnableIDs {
    ParamIDs::range, ParamIDs::speed, ParamIDs::mode, ParamIDs::bypass,
    ParamIDs::input, ParamIDs::output, ParamIDs::offset, ParamIDs::level, ParamIDs::noise
};

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AleaProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto knob = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return String (v * 10.0f, 1); }));
    };
    auto trim = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (-24.0f, 24.0f, 0.1f), def,
            AudioParameterFloatAttributes().withLabel ("dB"));
    };

    layout.add (knob (ParamIDs::range, "Filter Range", 0.5f));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::speed, 1 }, "S/H Speed", NormalisableRange<float> (0.0f, 1.0f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (alea::FSHCircuit::clockHz (v), 2) + " Hz"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::mode, 1 }, "Mode", false,
        AudioParameterBoolAttributes().withStringFromValueFunction (
            [] (bool b, int) { return String (b ? "Sample/Hold" : "Filter"); })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));
    // +10.8 dB: chitarra collegata direttamente all'ingresso di una scheda audio
    layout.add (trim (ParamIDs::input,  "Input Trim", Defaults::inputDb));
    layout.add (trim (ParamIDs::output, "Output Trim", 0.0f));

    auto pct = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
                [] (float v, int) { return String (juce::roundToInt (v * 100.0f)); }));
    };
    layout.add (pct (ParamIDs::offset, "S/H Offset (R26)", Factory::offset));
    layout.add (pct (ParamIDs::level,  "S/H Level (R27)",  Factory::level));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::noise, 1 }, "Q4 Noise", NormalisableRange<float> (0.5f, 8.0f, 0.01f), Factory::noise,
        AudioParameterFloatAttributes().withLabel ("V rms")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 2); })));
    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { ParamIDs::unit, 1 }, "Unit Serial", 0, 9999, 0,
        AudioParameterIntAttributes().withStringFromValueFunction (
            [] (int v, int) { return v == 0 ? String ("schema") : String (v); })
            .withValueFromStringFunction ([] (const String& t) { return t.trim().getIntValue(); })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::pickup, 1 }, "Pickup Load", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::limiter, 1 }, "Output Limiter", true));
    return layout;
}

AleaProcessor::AleaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    rangeRaw  = apvts.getRawParameterValue (ParamIDs::range);
    speedRaw  = apvts.getRawParameterValue (ParamIDs::speed);
    modeRaw   = apvts.getRawParameterValue (ParamIDs::mode);
    bypassRaw = apvts.getRawParameterValue (ParamIDs::bypass);
    inRaw     = apvts.getRawParameterValue (ParamIDs::input);
    outRaw    = apvts.getRawParameterValue (ParamIDs::output);
    offsetRaw = apvts.getRawParameterValue (ParamIDs::offset);
    levelRaw  = apvts.getRawParameterValue (ParamIDs::level);
    noiseRaw  = apvts.getRawParameterValue (ParamIDs::noise);
    unitRaw   = apvts.getRawParameterValue (ParamIDs::unit);
    pickupRaw = apvts.getRawParameterValue (ParamIDs::pickup);
    limitRaw  = apvts.getRawParameterValue (ParamIDs::limiter);
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamIDs::bypass));

    for (int i = 0; i < numLearnable; ++i)
        learnParams[(size_t) i] = apvts.getParameter (learnableIDs[(size_t) i]);

    resetDefaultMappings();
}

//==============================================================================
void AleaProcessor::resetDefaultMappings()
{
    for (auto& m : ccToParam) m.store (-1);
    // come gli altri plugin Collateral Effects: CC 20-22 per i comandi, 64 (sustain) per il footswitch
    ccToParam[20].store (0);  // FILTER RANGE
    ccToParam[21].store (1);  // S/H SPEED
    ccToParam[22].store (2);  // FILTER / S-H
    ccToParam[64].store (3);  // FOOTSWITCH
    ++mapVersion;
}

int AleaProcessor::getLearnIndex (const juce::String& paramID) const
{
    for (int i = 0; i < numLearnable; ++i)
        if (paramID == learnableIDs[(size_t) i]) return i;
    return -1;
}

int AleaProcessor::getCCForParam (int paramIndex) const
{
    for (int cc = 0; cc < 128; ++cc)
        if (ccToParam[(size_t) cc].load() == paramIndex) return cc;
    return -1;
}

void AleaProcessor::clearMappingForParam (int paramIndex)
{
    for (auto& m : ccToParam)
        if (m.load() == paramIndex) m.store (-1);
    ++mapVersion;
}

void AleaProcessor::handleCC (int cc, int value)
{
    lastCC.store (cc); lastCCValue.store (value); ++ccCounter;

    int learn = learningParam.load();
    if (learn >= 0 && learningParam.compare_exchange_strong (learn, -1))
    {
        for (auto& m : ccToParam)
            if (m.load() == learn) m.store (-1);
        ccToParam[(size_t) cc].store (learn);
        ccHigh[(size_t) cc] = value >= 64;
        ++mapVersion;
        return; // il messaggio usato per l'apprendimento non modifica il parametro
    }

    const int idx = ccToParam[(size_t) cc].load();
    if (idx < 0) return;
    auto* p = learnParams[(size_t) idx];

    auto setNorm = [p] (float v)
    {
        if (std::abs (p->getValue() - v) < 1.0e-6f) return;
        p->beginChangeGesture();
        p->setValueNotifyingHost (v);
        p->endChangeGesture();
    };

    if (isToggle (idx))   // commutano alla pressione (pulsante "gate" della KeyLab)
    {
        const bool high = value >= 64;
        if (high && ! ccHigh[(size_t) cc]) setNorm (p->getValue() > 0.5f ? 0.0f : 1.0f);
        ccHigh[(size_t) cc] = high;
    }
    else
    {
        setNorm ((float) value / 127.0f);
    }
}

//==============================================================================
bool AleaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return out == layouts.getMainInputChannelSet();
}

void AleaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (1, samplesPerBlock);
    osChannels = juce::jmax (1, getTotalNumOutputChannels());

    // il circuito gira a 176-192 kHz: 4x a 44.1/48 kHz, 2x a 88.2/96 kHz, 1x oltre
    const int order = sampleRate > 120000.0 ? 0 : (sampleRate > 60000.0 ? 1 : 2);
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        1, (size_t) order, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling->initProcessing ((size_t) maxBlock);
    setLatencySamples (juce::roundToInt (oversampling->getLatencyInSamples()));

    // il filtro gira alla frequenza sovracampionata, il controllo alla frequenza base
    const int factor = (int) oversampling->getOversamplingFactor();
    circuit.setUnit ((uint32_t) juce::roundToInt (unitRaw->load()));
    circuit.setPickupLoading (pickupRaw->load() > 0.5f);
    circuit.prepare (sampleRate * (double) factor, factor);
    circuit.setControls (rangeRaw->load(), speedRaw->load(), modeRaw->load() > 0.5f);
    circuit.setTrims (offsetRaw->load(), levelRaw->load(), noiseRaw->load());

    auto initSm = [sampleRate] (juce::SmoothedValue<float>& s, float v, double sec)
    { s.reset (sampleRate, sec); s.setCurrentAndTargetValue (v); };
    initSm (rangeSm,  rangeRaw->load(),  0.03);
    initSm (speedSm,  speedRaw->load(),  0.03);
    initSm (offsetSm, offsetRaw->load(), 0.05);
    initSm (levelSm,  levelRaw->load(),  0.05);
    initSm (noiseSm,  noiseRaw->load(),  0.05);
    initSm (inGainSm,  juce::Decibels::decibelsToGain (inRaw->load()),  0.03);
    initSm (outGainSm, juce::Decibels::decibelsToGain (outRaw->load()), 0.03);
    initSm (wetSm, bypassRaw->load() > 0.5f ? 0.0f : 1.0f, 0.008);   // SW2 senza schiocco
    limiter.prepare (sampleRate);
}

void AleaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isController()) handleCC (m.getControllerNumber(), m.getControllerValue());
    }
    midi.clear();

    const int numCh = juce::jmin (getTotalNumInputChannels(), osChannels);
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, buffer.getNumSamples());
    if (numCh < 1 || oversampling == nullptr) return;

    rangeSm.setTargetValue (rangeRaw->load());
    speedSm.setTargetValue (speedRaw->load());
    offsetSm.setTargetValue (offsetRaw->load());
    levelSm.setTargetValue (levelRaw->load());
    noiseSm.setTargetValue (noiseRaw->load());
    inGainSm.setTargetValue (juce::Decibels::decibelsToGain (inRaw->load()));
    outGainSm.setTargetValue (juce::Decibels::decibelsToGain (outRaw->load()));
    wetSm.setTargetValue (bypassRaw->load() > 0.5f ? 0.0f : 1.0f);
    const bool sh = modeRaw->load() > 0.5f;
    circuit.setUnit ((uint32_t) juce::roundToInt (unitRaw->load()));
    circuit.setPickupLoading (pickupRaw->load() > 0.5f);

    juce::dsp::AudioBlock<float> full (buffer);
    full = full.getSubsetChannelBlock (0, (size_t) numCh);
    const int factor = (int) oversampling->getOversamplingFactor();

    for (int offset = 0; offset < buffer.getNumSamples(); offset += maxBlock)
    {
        const int n = juce::jmin (maxBlock, buffer.getNumSamples() - offset);
        auto block = full.getSubBlock ((size_t) offset, (size_t) n);
        auto mono  = block.getSingleChannelBlock (0);
        auto* x = mono.getChannelPointer (0);

        // la chitarra può arrivare su uno qualsiasi dei due ingressi: se i canali sono
        // identici si usa il primo, altrimenti si sommano (il pedale è mono)
        if (numCh > 1)
        {
            const auto* r = block.getChannelPointer (1);
            bool identical = true;
            for (int i = 0; i < n && identical; ++i) identical = std::abs (x[i] - r[i]) < 1.0e-7f;
            if (! identical) juce::FloatVectorOperations::add (x, r, n);
        }
        for (int i = 0; i < n; ++i) x[i] *= inGainSm.getNextValue() * voltsPerFS;

        auto up = oversampling->processSamplesUp (mono);
        auto* d = up.getChannelPointer (0);

        for (int start = 0; start < n; start += controlBlock)
        {
            const int len = juce::jmin (controlBlock, n - start);
            circuit.setControls (rangeSm.skip (len), speedSm.skip (len), sh);
            circuit.setTrims (offsetSm.skip (len), levelSm.skip (len), noiseSm.skip (len));
            for (int i = start * factor, end = (start + len) * factor; i < end; ++i)
            {
                const float w = wetSm.getCurrentValue();
                if ((i % factor) == 0) wetSm.getNextValue();
                // a effetto escluso (e dissolvenza finita) il filtro audio non viene calcolato
                const bool needWet = w > 0.0f || wetSm.getTargetValue() > 0.0f;
                const auto o = circuit.process ((double) d[i], needWet);
                d[i] = (float) (w * o.wet + (1.0 - w) * o.dry);   // SW2

                const bool clk = circuit.clockIsHigh();
                if (clk && ! lastClock) clockPulses.fetch_add (1, std::memory_order_relaxed);
                lastClock = clk;
            }
        }
        centreHz.store ((float) circuit.currentCentreHz(), std::memory_order_relaxed);

        oversampling->processSamplesDown (mono);

        for (int i = 0; i < n; ++i) x[i] *= outGainSm.getNextValue() / voltsPerFS;

        // protezione dell'uscita: il pedale satura solo verso 7 V, ma in digitale tutto ciò che
        // supera 0 dBFS verrebbe tagliato di netto dalla scheda audio
        if (limitRaw->load() > 0.5f) limiter.process (x, n);
        else limiter.idle();
        // la spia tiene il valore più alto finché l'interfaccia non lo legge
        if (const float r = limiter.reductionDb(); r > gainReductionDb.load (std::memory_order_relaxed))
            gainReductionDb.store (r, std::memory_order_relaxed);
        for (int ch = 1; ch < numCh; ++ch)
            juce::FloatVectorOperations::copy (block.getChannelPointer ((size_t) ch), x, n);
    }
}

//==============================================================================
void AleaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("AleaState");
   #ifdef JucePlugin_VersionString
    root.setAttribute ("version", JucePlugin_VersionString);   // per eventuali conversioni future
   #endif
    if (auto params = apvts.copyState().createXml()) root.addChildElement (params.release());

    auto* map = root.createNewChildElement ("MIDIMAP");
    for (int cc = 0; cc < 128; ++cc)
        if (const int idx = ccToParam[(size_t) cc].load(); idx >= 0)
        {
            auto* e = map->createNewChildElement ("CC");
            e->setAttribute ("num", cc);
            e->setAttribute ("param", learnableIDs[(size_t) idx]);
        }
    copyXmlToBinary (root, destData);
}

void AleaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("AleaState")) return;

    if (auto* params = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*params));

    if (auto* map = xml->getChildByName ("MIDIMAP"))
    {
        for (auto& m : ccToParam) m.store (-1);
        for (auto* e : map->getChildWithTagNameIterator ("CC"))
        {
            const int cc = e->getIntAttribute ("num", -1);
            const int idx = getLearnIndex (e->getStringAttribute ("param"));
            if (juce::isPositiveAndBelow (cc, 128) && idx >= 0) ccToParam[(size_t) cc].store (idx);
        }
        ++mapVersion;
    }
}

juce::AudioProcessorEditor* AleaProcessor::createEditor() { return new AleaEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AleaProcessor(); }
