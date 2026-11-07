#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

//==============================================================================
class PedalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PedalLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) buttonHeight * 0.42f), juce::Font::bold));
    }
};

//==============================================================================
// Slider con menu MIDI Learn sul clic destro
class LearnableSlider : public juce::Slider
{
public:
    std::function<void()> onLearnMenu;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) Slider::mouseDrag (e); }
    void mouseUp   (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) Slider::mouseUp (e); }
};

// pulsante a commutazione con menu MIDI Learn (footswitch, deviatore)
class LearnableToggle : public juce::Button
{
public:
    explicit LearnableToggle (const juce::String& name) : juce::Button (name) { setClickingTogglesState (true); }
    std::function<void()> onLearnMenu;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) Button::mouseUp (e); }
};

class Footswitch : public LearnableToggle
{
public:
    Footswitch() : LearnableToggle ("Footswitch") {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

// deviatore a slitta FILTER / S-H (SW1)
class ModeSwitch : public LearnableToggle
{
public:
    ModeSwitch() : LearnableToggle ("Mode") {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

//==============================================================================
// Pannello "interno": i trimmer del circuito stampato
class InteriorPanel : public juce::Component
{
public:
    InteriorPanel (AleaProcessor&, juce::Typeface::Ptr face,
                   std::function<void (int, juce::Component*)> learnMenu);

    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Component* controlFor (int paramIndex);
    std::function<void()> onClose;

    static constexpr float baseW = 688.0f, baseH = 528.0f;

private:
    juce::Rectangle<int> area (float x, float y, float w, float h) const;
    void resetToFactory();

    AleaProcessor& proc;
    juce::Typeface::Ptr face;
    LearnableSlider offset, level, noise;
    juce::Slider unit;                                   // numero di serie dell'esemplare
    juce::TextButton randomBtn { "A CASO" }, schemaBtn { "SCHEMA" };
    juce::TextButton pickupBtn { "CARICO PICKUP" };
    juce::TextButton resetBtn { "VALORI DI FABBRICA" }, closeBtn { "CHIUDI" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> offsetA, levelA, noiseA, unitA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> pickupA;
    float k = 1.0f;
};

//==============================================================================
class AleaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AleaEditor (AleaProcessor&);
    ~AleaEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;


    static constexpr int baseW = 720, baseH = 600;

    // per lo strumento di screenshot
    void tickForSnapshot() { timerCallback(); }
    void openInteriorForSnapshot() { if (interior == nullptr && interiorBtn.onClick) interiorBtn.onClick(); }

private:
    void timerCallback() override;
    void showLearnMenu (int paramIndex, juce::Component* target);
    juce::Component* controlFor (int paramIndex);
    juce::Rectangle<int> scaled (float x, float y, float w, float h) const;
    void paintScope (juce::Graphics&, juce::Rectangle<float> r);

    AleaProcessor& proc;
    PedalLookAndFeel lnf;

    LearnableSlider range, speed, inTrim, outTrim;
    Footswitch footswitch;
    ModeSwitch modeSwitch;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> rangeA, speedA, inA, outA;
    std::unique_ptr<BA> bypassA, modeA;

    juce::TextButton interiorBtn { "INTERNO" };
    std::unique_ptr<InteriorPanel> interior;

    juce::Typeface::Ptr face;
    juce::TooltipWindow tooltips { this, 700 };
    juce::Image body;        // sfondo pre-renderizzato alla scala attuale
    float scale = 1.0f;

    // traccia della frequenza del filtro (finestra sul frontale)
    static constexpr int traceLen = 160;
    std::array<float, traceLen> trace {};
    int traceHead = 0;
    int lastPulses = 0, lastLearn = -2, lastMapVersion = -1, lastCCCount = -1;
    float flash = 0.0f, pulse = 0.0f;
    juce::String midiText;
};
