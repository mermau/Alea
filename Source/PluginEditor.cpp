#include "PluginEditor.h"
#include "BinaryData.h"
#include <cstdint>

namespace
{
    // palette Collateral Effects "ALEA": smalto crema, grafite, senape
    const juce::Colour enamel   (0xffe9e0cb);
    const juce::Colour enamelDk (0xffcabd9f);
    const juce::Colour ink      (0xff1e1f22);
    const juce::Colour mustard  (0xffd9a12a);
    const juce::Colour column   (0xff232528);
    const juce::Colour barCol   (0xff111214);
    const juce::Colour cream    (0xffe8e2d4);
    const juce::Colour learnCol (0xffffb020);
    const juce::Colour ledRed   (0xffff3b2f);

    const char* displayNames[] = { "FILTER RANGE", "S/H SPEED", "FILTER / S-H", "FOOTSWITCH",
                                   "INPUT", "OUTPUT", "S/H OFFSET (R26)", "S/H LEVEL (R27)", "RUMORE Q4" };

    juce::Typeface::Ptr textFace()
    {
        static auto tf = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_otf,
                                                                  BinaryData::InterSemiBold_otfSize);
        return tf;
    }

    juce::Font uiFont (float h)
    {
        return juce::Font (juce::FontOptions (textFace()).withHeight (h));
    }

    juce::Font faceFont (juce::Typeface::Ptr tf, float h, float tracking = 0.0f)
    {
        juce::Font f (juce::FontOptions (tf).withHeight (h));
        f.setExtraKerningFactor (tracking);
        return f;
    }

    void text (juce::Graphics& g, const juce::Font& f, const juce::String& s, float cx, float baseline,
               juce::Justification j = juce::Justification::horizontallyCentred)
    {
        const float w = juce::GlyphArrangement::getStringWidth (f, s);
        float x = cx;
        if (j.testFlags (juce::Justification::horizontallyCentred)) x -= w * 0.5f;
        else if (j.testFlags (juce::Justification::right)) x -= w;
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, s, x, baseline);
        ga.draw (g);
    }

    float hash2 (int x, int y)
    {
        uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return (float) (h & 0xffffffu) / 16777215.0f;
    }

    // smalto crema con leggera granulosità
    juce::Image makeEnamel (int w, int h)
    {
        juce::Image img (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), false);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                const float n = hash2 (x, y) - 0.5f;
                const float sh = 1.0f - 0.07f * (float) y / (float) img.getHeight();   // luce dall'alto
                const float k = sh * (1.0f + 0.035f * n);
                bd.setPixelColour (x, y, juce::Colour ((juce::uint8) juce::jlimit (0.0f, 255.0f, 233.0f * k),
                                                       (juce::uint8) juce::jlimit (0.0f, 255.0f, 224.0f * k),
                                                       (juce::uint8) juce::jlimit (0.0f, 255.0f, 203.0f * k)));
            }
        return img;
    }

    juce::ColourGradient chrome (juce::Point<float> c, float r, float darken = 0.0f)
    {
        juce::ColourGradient cg (juce::Colours::white.darker (darken), c.x - 0.35f * r, c.y - 0.45f * r,
                                 juce::Colour (0xff4f5358).darker (darken), c.x + 0.9f * r, c.y + 0.9f * r, true);
        cg.addColour (0.35, juce::Colour (0xffd8dadd).darker (darken));
        cg.addColour (0.75, juce::Colour (0xff85898f).darker (darken));
        return cg;
    }

    void screw (juce::Graphics& g, juce::Point<float> c, float r)
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre (c.translated (0, 1.2f)));
        g.setGradientFill (chrome (c, r, 0.1f));
        g.fillEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre (c));
        g.setColour (juce::Colour (0xff3a3c40));
        juce::Path p;
        p.addRectangle (-r * 0.75f, -r * 0.12f, r * 1.5f, r * 0.24f);
        p.addRectangle (-r * 0.12f, -r * 0.75f, r * 0.24f, r * 1.5f);
        g.fillPath (p, juce::AffineTransform::rotation (0.4f).translated (c));
    }

    constexpr float rotStart = juce::MathConstants<float>::pi * 1.25f;
    constexpr float rotEnd   = juce::MathConstants<float>::pi * 2.75f;

    // geometria del frontale (coordinate di progetto 720 x 600)
    const juce::Rectangle<float> bodyR   { 16.0f, 60.0f, 688.0f, 528.0f };
    const juce::Rectangle<float> columnR { 470.0f, 82.0f, 212.0f, 484.0f };
    const juce::Rectangle<float> scopeR  { 44.0f, 214.0f, 398.0f, 150.0f };
    const juce::Point<float> rangeC { 576.0f, 196.0f }, speedC { 576.0f, 470.0f }, footC { 243.0f, 486.0f };
    const juce::Point<float> ledC { 243.0f, 408.0f };
    const juce::Rectangle<float> switchR { 524.0f, 316.0f, 104.0f, 40.0f };
    constexpr float knobD = 116.0f;
}

//==============================================================================
PedalLookAndFeel::PedalLookAndFeel()
{
    using namespace juce;
    setColour (PopupMenu::backgroundColourId, Colour (0xff1a1b1d));
    setColour (PopupMenu::textColourId, cream);
    setColour (PopupMenu::headerTextColourId, mustard);
    setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff6d4f10));
    setColour (BubbleComponent::backgroundColourId, Colour (0xee111111));
    setColour (BubbleComponent::outlineColourId, cream.withAlpha (0.4f));
    setColour (TooltipWindow::textColourId, cream);
    setColour (TextButton::buttonColourId, Colour (0xff1c1d20));
    setColour (TextButton::buttonOnColourId, Colour (0xff6d4f10));
    setColour (TextButton::textColourOffId, cream);
    setColour (TextButton::textColourOnId, cream);
    setColour (ComboBox::outlineColourId, Colour (0xff3a3c40));
    setDefaultSansSerifTypeface (textFace());
}

void PedalLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& slider)
{
    using namespace juce;
    const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto c = b.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const auto style = slider.getProperties()["style"].toString();
    const float R = jmin (b.getWidth(), b.getHeight()) * 0.5f;

    if (style == "trimmer")   // trimmer multigiro blu da circuito stampato
    {
        const float side = R * 1.3f, r = side * 0.36f;
        const auto sq = Rectangle<float> (side, side).withCentre (c);
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (sq.translated (0, 2.0f), side * 0.08f);
        g.setGradientFill (ColourGradient (Colour (0xff3d6fd1), sq.getX(), sq.getY(),
                                           Colour (0xff1c3c86), sq.getRight(), sq.getBottom(), false));
        g.fillRoundedRectangle (sq, side * 0.08f);
        g.setColour (Colour (0xffd9dde4));
        g.fillEllipse (Rectangle<float> (2 * r, 2 * r).withCentre (c));
        Path slot; slot.addRoundedRectangle (-r * 0.18f, -r * 0.9f, r * 0.36f, r * 1.8f, r * 0.1f);
        g.setColour (Colour (0xff3a3d42));
        g.fillPath (slot, AffineTransform::rotation (angle).translated (c));
        g.setColour (cream.withAlpha (0.55f));
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
            g.fillEllipse (Rectangle<float> (2.6f, 2.6f).withCentre (c.getPointOnCircumference (R * 0.92f, a)));
        }
        return;
    }

    if (style == "mini")      // manopole di servizio della barra superiore
    {
        const float r = R * 0.78f;
        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillEllipse (Rectangle<float> (2 * r, 2 * r).withCentre (c.translated (0, 1.5f)));
        g.setGradientFill (ColourGradient (Colour (0xff4a4c51), c.x - r * 0.4f, c.y - r * 0.5f,
                                           Colour (0xff141517), c.x + r * 0.6f, c.y + r * 0.8f, true));
        g.fillEllipse (Rectangle<float> (2 * r, 2 * r).withCentre (c));
        Path p; p.addRoundedRectangle (-r * 0.07f, -r * 0.92f, r * 0.14f, r * 0.5f, r * 0.07f);
        g.setColour (mustard);
        g.fillPath (p, AffineTransform::rotation (angle).translated (c));
        Path arc; arc.addCentredArc (c.x, c.y, R * 0.98f, R * 0.98f, 0.0f, startAngle, angle, true);
        g.setColour (mustard.withAlpha (0.5f));
        g.strokePath (arc, PathStrokeType (1.6f));
        return;
    }

    // manopola del frontale: gonna zigrinata nera, cappello in alluminio, indice senape
    const float rs = R * 0.92f, rc = R * 0.58f;
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillEllipse (Rectangle<float> (2 * rs, 2 * rs).withCentre (c.translated (0, R * 0.07f)));

    g.setGradientFill (ColourGradient (Colour (0xff3c3e42), c.x - rs * 0.5f, c.y - rs * 0.6f,
                                       Colour (0xff09090a), c.x + rs * 0.6f, c.y + rs * 0.8f, true));
    g.fillEllipse (Rectangle<float> (2 * rs, 2 * rs).withCentre (c));
    // zigrinatura
    for (int i = 0; i < 48; ++i)
    {
        const float a = angle + (float) i * MathConstants<float>::twoPi / 48.0f;
        const auto p0 = c.getPointOnCircumference (rs * 0.80f, a), p1 = c.getPointOnCircumference (rs * 0.985f, a);
        const float lit = 0.5f + 0.5f * std::cos (a + 0.9f);
        g.setColour (Colour (0xff5d6066).withAlpha (0.25f + 0.45f * lit));
        g.drawLine ({ p0, p1 }, jmax (1.0f, R * 0.025f));
    }
    // cappello
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillEllipse (Rectangle<float> (2 * rc + 3, 2 * rc + 3).withCentre (c.translated (0, 1.5f)));
    ColourGradient cap (Colour (0xffe4e6e8), c.x - rc * 0.5f, c.y - rc * 0.6f, Colour (0xff7d8187), c.x + rc * 0.8f, c.y + rc, true);
    cap.addColour (0.5, Colour (0xffc4c7cb));
    g.setGradientFill (cap);
    g.fillEllipse (Rectangle<float> (2 * rc, 2 * rc).withCentre (c));
    // cerchi concentrici dell'alluminio tornito
    for (int i = 1; i < 6; ++i)
    {
        g.setColour (Colours::white.withAlpha (i % 2 ? 0.10f : 0.05f));
        g.drawEllipse (Rectangle<float> (2 * rc * (float) i / 6.0f, 2 * rc * (float) i / 6.0f).withCentre (c), 0.8f);
    }
    // indice: tacca senape lungo tutto il raggio
    Path ptr;
    ptr.addRoundedRectangle (-R * 0.045f, -rs * 0.97f, R * 0.09f, rs * 0.62f, R * 0.04f);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillPath (ptr, AffineTransform::rotation (angle).translated (c.translated (0, 1.0f)));
    g.setColour (mustard);
    g.fillPath (ptr, AffineTransform::rotation (angle).translated (c));
}

//==============================================================================
void LearnableSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onLearnMenu) onLearnMenu(); return; }
    Slider::mouseDown (e);
}

void LearnableToggle::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onLearnMenu) onLearnMenu(); return; }
    Button::mouseDown (e);
}

void Footswitch::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float R = jmin (b.getWidth(), b.getHeight()) * 0.5f;

    // dado esagonale
    Path hex;
    for (int i = 0; i < 6; ++i)
    {
        const auto p = c.getPointOnCircumference (R * 0.96f, (float) i * MathConstants<float>::pi / 3.0f);
        if (i == 0) hex.startNewSubPath (p); else hex.lineTo (p);
    }
    hex.closeSubPath();
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillPath (hex, AffineTransform::translation (0, 3.0f));
    g.setGradientFill (chrome (c, R, 0.25f));
    g.fillPath (hex);
    g.setColour (Colours::black.withAlpha (0.3f));
    g.strokePath (hex, PathStrokeType (1.0f));

    // anello e pulsante
    const float ring = R * 0.66f, dome = R * (down ? 0.46f : 0.5f);
    g.setGradientFill (chrome (c, ring, 0.35f));
    g.fillEllipse (Rectangle<float> (2 * ring, 2 * ring).withCentre (c));
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillEllipse (Rectangle<float> (2 * dome + 4, 2 * dome + 4).withCentre (c.translated (0, 2.0f)));
    g.setGradientFill (chrome (c, dome, down ? 0.25f : (highlighted ? -0.05f : 0.0f)));
    g.fillEllipse (Rectangle<float> (2 * dome, 2 * dome).withCentre (c));
    g.setColour (Colours::white.withAlpha (0.35f));
    g.fillEllipse (Rectangle<float> (dome * 0.7f, dome * 0.42f).withCentre (c.translated (-dome * 0.28f, -dome * 0.42f)));
}

void ModeSwitch::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat().reduced (2.0f);
    const float h = b.getHeight();
    // piastrina cromata con asola
    g.setGradientFill (ColourGradient (Colour (0xffe1e3e6), b.getX(), b.getY(), Colour (0xff767a80), b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, h * 0.18f);
    const auto slot = b.reduced (h * 0.22f, h * 0.30f);
    g.setColour (Colour (0xff0b0b0c));
    g.fillRoundedRectangle (slot, slot.getHeight() * 0.5f);
    // levetta
    const float lw = slot.getWidth() * 0.46f;
    auto lever = Rectangle<float> (lw, h * 0.86f).withCentre ({ getToggleState() ? slot.getRight() - lw * 0.5f
                                                                                 : slot.getX() + lw * 0.5f,
                                                               b.getCentreY() });
    g.setColour (Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (lever.translated (0, 2.0f), 3.0f);
    g.setGradientFill (ColourGradient (Colour (highlighted ? 0xff45484d : 0xff3a3c40), lever.getX(), lever.getY(),
                                       Colour (0xff0e0f10), lever.getX(), lever.getBottom(), false));
    g.fillRoundedRectangle (lever, 3.0f);
    g.setColour (Colours::white.withAlpha (0.12f));
    for (int i = 1; i < 6; ++i)
    {
        const float xx = lever.getX() + lever.getWidth() * (float) i / 6.0f;
        g.drawLine (xx, lever.getY() + 4.0f, xx, lever.getBottom() - 4.0f, 1.0f);
    }
}

//==============================================================================
InteriorPanel::InteriorPanel (AleaProcessor& p, juce::Typeface::Ptr tf,
                              std::function<void (int, juce::Component*)> learnMenu)
    : proc (p), face (tf)
{
    using namespace juce;
    auto setup = [&] (LearnableSlider& s, int idx, const char* id, const char* tip, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& a)
    {
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (rotStart, rotEnd, true);
        s.getProperties().set ("style", "trimmer");
        s.setPopupDisplayEnabled (true, true, this);
        s.setTooltip (tip);
        s.onLearnMenu = [learnMenu, idx, &s] { learnMenu (idx, &s); };
        addAndMakeVisible (s);
        a = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, s);
    };
    setup (offset, 6, ParamIDs::offset, "R26: tensione di polarizzazione del S/H (centro dei gradini)", offsetA);
    setup (level,  7, ParamIDs::level,  "R27: quanto rumore arriva all'interruttore Q5 (ampiezza dei gradini)", levelA);
    setup (noise,  8, ParamIDs::noise,  "Rumore a valanga di Q4: cambia da un transistor all'altro", noiseA);
    offset.setDoubleClickReturnValue (true, Factory::offset);
    level.setDoubleClickReturnValue (true, Factory::level);
    noise.setDoubleClickReturnValue (true, Factory::noise);

    // esemplare: 0 = valori dello schema, ogni altro numero un pedale con le sue tolleranze
    unit.setSliderStyle (Slider::IncDecButtons);
    unit.setTextBoxStyle (Slider::TextBoxLeft, false, 70, 26);
    unit.setIncDecButtonsMode (Slider::incDecButtonsDraggable_Vertical);
    unit.setTooltip (String::fromUTF8 ("Numero di serie dell'esemplare: ogni pedale aveva componenti con tolleranze diverse. 0 = valori nominali dello schema."));
    unit.setColour (Slider::textBoxTextColourId, cream);
    unit.setColour (Slider::textBoxBackgroundColourId, Colour (0xff10261a));
    unit.setColour (Slider::textBoxOutlineColourId, cream.withAlpha (0.3f));
    addAndMakeVisible (unit);
    unitA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ParamIDs::unit, unit);
    auto setUnit = [this] (int v)
    {
        if (auto* prm = proc.apvts.getParameter (ParamIDs::unit))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) v));
            prm->endChangeGesture();
        }
    };
    randomBtn.setTooltip ("Pesca un esemplare a caso");
    randomBtn.onClick = [setUnit] { setUnit (1 + Random::getSystemRandom().nextInt (9999)); };
    schemaBtn.setTooltip ("Torna ai valori nominali dello schema");
    schemaBtn.onClick = [setUnit] { setUnit (0); };
    addAndMakeVisible (randomBtn);
    addAndMakeVisible (schemaBtn);

    pickupBtn.setClickingTogglesState (true);
    pickupBtn.setTooltip (String::fromUTF8 ("Per una chitarra con pickup passivi registrata in DI: simula il carico di ~100 kΩ dell'ingresso del pedale, che smorza la risonanza del pickup (anche a effetto escluso)."));
    addAndMakeVisible (pickupBtn);
    pickupA = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::pickup, pickupBtn);

    resetBtn.onClick = [this] { resetToFactory(); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (resetBtn);
    addAndMakeVisible (closeBtn);
}

juce::Rectangle<int> InteriorPanel::area (float x, float y, float w, float h) const
{
    return juce::Rectangle<float> (x * k, y * k, w * k, h * k).toNearestInt();
}

void InteriorPanel::resized()
{
    k = (float) getWidth() / baseW;
    offset.setBounds (area (108, 136, 84, 84));
    level.setBounds  (area (302, 136, 84, 84));
    noise.setBounds  (area (496, 136, 84, 84));
    unit.setBounds      (area (88, 380, 150, 30));
    randomBtn.setBounds (area (246, 380, 76, 30));
    schemaBtn.setBounds (area (88, 416, 76, 24));
    pickupBtn.setBounds (area (424, 380, 176, 30));
    resetBtn.setBounds (area (190, 460, 180, 32));
    closeBtn.setBounds (area (386, 460, 112, 32));
}

juce::Component* InteriorPanel::controlFor (int idx)
{
    switch (idx) { case 6: return &offset; case 7: return &level; case 8: return &noise; default: return nullptr; }
}

void InteriorPanel::resetToFactory()
{
    auto set = [this] (const char* id, float v)
    {
        if (auto* p = proc.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
    };
    set (ParamIDs::offset, Factory::offset);
    set (ParamIDs::level,  Factory::level);
    set (ParamIDs::noise,  Factory::noise);
}

void InteriorPanel::paint (juce::Graphics& g)
{
    using namespace juce;
    g.addTransform (AffineTransform::scale (k));
    const auto r = Rectangle<float> (0, 0, baseW, baseH);

    // circuito stampato verde con piste
    g.setGradientFill (ColourGradient (Colour (0xff2f5a3a), 0, 0, Colour (0xff1c3a26), baseW, baseH, false));
    g.fillRoundedRectangle (r, 22.0f);
    g.setColour (Colour (0xff4a7a52).withAlpha (0.35f));
    for (int i = 0; i < 14; ++i)
    {
        Path p;
        const float y0 = 30.0f + (float) i * 36.0f;
        p.startNewSubPath (20.0f, y0);
        p.lineTo (120.0f + (float) (i * 37 % 90), y0);
        p.lineTo (160.0f + (float) (i * 37 % 90), y0 + 18.0f);
        p.lineTo (baseW - 30.0f, y0 + 18.0f);
        g.strokePath (p, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.reduced (30.0f, 26.0f), 12.0f);

    g.setColour (cream);
    text (g, faceFont (face, 24.0f, 0.06f), "INTERNO", baseW * 0.5f, 68.0f);
    g.setColour (cream.withAlpha (0.7f));
    text (g, uiFont (12.5f), juce::String::fromUTF8 ("Trimmer del circuito stampato: agiscono solo in modalità S/H."), baseW * 0.5f, 92.0f);

    struct Info { float x; const char* ref; const char* name; const char* l1; const char* l2; };
    const Info infos[] = {
        { 150.0f, "R26 100k", "S/H OFFSET", "sposta i gradini in alto", "o in basso" },
        { 344.0f, "R27 10k",  "S/H LEVEL",  "ampiezza dei gradini:", "quanto rumore passa a Q5" },
        { 538.0f, "Q4 2N3904", "RUMORE Q4", "l'originale usa un Q4", "scelto per il rumore" }
    };
    for (auto& in : infos)
    {
        g.setColour (cream.withAlpha (0.55f));
        text (g, uiFont (11.5f), in.ref, in.x, 126.0f);
        g.setColour (cream);
        text (g, faceFont (face, 15.0f, 0.05f), in.name, in.x, 244.0f);
        g.setColour (cream.withAlpha (0.7f));
        text (g, uiFont (12.0f), in.l1, in.x, 264.0f);
        text (g, uiFont (12.0f), juce::String::fromUTF8 (in.l2), in.x, 280.0f);
    }

    auto val = [this] (const char* id)
    {
        auto* p = proc.apvts.getParameter (id);
        return p->getCurrentValueAsText() + (p->getLabel().isNotEmpty() ? " " + p->getLabel() : String());
    };
    g.setColour (mustard);
    text (g, uiFont (13.5f), val (ParamIDs::offset), infos[0].x, 304.0f);
    text (g, uiFont (13.5f), val (ParamIDs::level),  infos[1].x, 304.0f);
    text (g, uiFont (13.5f), val (ParamIDs::noise),  infos[2].x, 304.0f);

    // seconda fila: esemplare e ingresso
    g.setColour (cream.withAlpha (0.18f));
    g.fillRect (60.0f, 326.0f, baseW - 120.0f, 1.0f);
    g.setColour (cream);
    text (g, faceFont (face, 15.0f, 0.05f), "ESEMPLARE", 88.0f, 364.0f, Justification::left);
    text (g, faceFont (face, 15.0f, 0.05f), "INGRESSO", 424.0f, 364.0f, Justification::left);
    g.setColour (cream.withAlpha (0.7f));
    text (g, uiFont (11.5f), "tolleranze dei componenti", 174.0f, 433.0f, Justification::left);
    text (g, uiFont (11.5f), juce::String::fromUTF8 ("chitarra passiva registrata in DI:"), 424.0f, 426.0f, Justification::left);
    text (g, uiFont (11.5f), juce::String::fromUTF8 ("il pedale la carica con ~100 kΩ"), 424.0f, 441.0f, Justification::left);
}

//==============================================================================
AleaEditor::AleaEditor (AleaProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    using namespace juce;
    face = Typeface::createSystemTypefaceFor (BinaryData::ArchivoBlackRegular_ttf, BinaryData::ArchivoBlackRegular_ttfSize);
    setLookAndFeel (&lnf);
    trace.fill (std::log (300.0f));

    auto setupKnob = [this] (LearnableSlider& s, int idx, const char* style, const juce::String& tip)
    {
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (rotStart, rotEnd, true);
        s.getProperties().set ("style", style);
        s.setPopupDisplayEnabled (true, true, this);
        s.setVelocityBasedMode (false);
        s.setMouseDragSensitivity (220);
        s.setTooltip (tip);
        s.onLearnMenu = [this, idx, &s] { showLearnMenu (idx, &s); };
        addAndMakeVisible (s);
    };
    setupKnob (range, 0, "main", juce::String::fromUTF8 ("FILTER RANGE (R15): sensibilità dell'inviluppo, cioè quanto si apre il filtro "
                                 "suonando. Nel modo S/H non ha effetto, come sull'originale."));
    setupKnob (speed, 1, "main", juce::String::fromUTF8 ("S/H SPEED (R28): velocità del clock di campionamento, da 1,7 a 12 Hz circa."));
    setupKnob (inTrim, 4, "mini", "INPUT: livello in ingresso (0 dBFS = 1 V di picco al jack del pedale)");
    setupKnob (outTrim, 5, "mini", "OUTPUT: livello d'uscita");

    rangeA = std::make_unique<SA> (proc.apvts, ParamIDs::range, range);
    speedA = std::make_unique<SA> (proc.apvts, ParamIDs::speed, speed);
    inA    = std::make_unique<SA> (proc.apvts, ParamIDs::input, inTrim);
    outA   = std::make_unique<SA> (proc.apvts, ParamIDs::output, outTrim);
    for (auto* s : { &range, &speed }) s->setDoubleClickReturnValue (true, 0.5);
    for (auto* s : { &inTrim, &outTrim }) s->setDoubleClickReturnValue (true, 0.0);

    footswitch.setTooltip ("Footswitch: effetto inserito / escluso (clic destro: MIDI Learn)");
    footswitch.onLearnMenu = [this] { showLearnMenu (3, &footswitch); };
    addAndMakeVisible (footswitch);
    bypassA = std::make_unique<BA> (proc.apvts, ParamIDs::bypass, footswitch);

    modeSwitch.setTooltip ("SW1: FILTER (inviluppo) / S/H (campiona e mantieni)");
    modeSwitch.onLearnMenu = [this] { showLearnMenu (2, &modeSwitch); };
    addAndMakeVisible (modeSwitch);
    modeA = std::make_unique<BA> (proc.apvts, ParamIDs::mode, modeSwitch);

    interiorBtn.setClickingTogglesState (false);
    interiorBtn.setTooltip ("Apre il fondo del pedale: trimmer R26, R27 e rumore di Q4");
    interiorBtn.onClick = [this]
    {
        if (interior != nullptr) { interior.reset(); interiorBtn.setToggleState (false, dontSendNotification); resized(); return; }
        interior = std::make_unique<InteriorPanel> (proc, face, [this] (int i, Component* c) { showLearnMenu (i, c); });
        interior->onClose = [this] { MessageManager::callAsync ([sp = SafePointer<AleaEditor> (this)] { if (sp) sp->interiorBtn.triggerClick(); }); };
        addAndMakeVisible (*interior);
        interiorBtn.setToggleState (true, dontSendNotification);
        resized();
    };
    addAndMakeVisible (interiorBtn);

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / (double) baseH);
    setResizeLimits (baseW / 2, baseH / 2, baseW * 2, baseH * 2);
    setSize (baseW, baseH);
    startTimerHz (40);
}

AleaEditor::~AleaEditor()
{
    proc.cancelLearn();
    setLookAndFeel (nullptr);
}

juce::Rectangle<int> AleaEditor::scaled (float x, float y, float w, float h) const
{
    return juce::Rectangle<float> (x * scale, y * scale, w * scale, h * scale).toNearestInt();
}

void AleaEditor::resized()
{
    scale = (float) getWidth() / (float) baseW;
    range.setBounds (scaled (rangeC.x - knobD * 0.5f, rangeC.y - knobD * 0.5f, knobD, knobD));
    speed.setBounds (scaled (speedC.x - knobD * 0.5f, speedC.y - knobD * 0.5f, knobD, knobD));
    footswitch.setBounds (scaled (footC.x - 46.0f, footC.y - 46.0f, 92.0f, 92.0f));
    modeSwitch.setBounds (scaled (switchR.getX(), switchR.getY(), switchR.getWidth(), switchR.getHeight()));
    inTrim.setBounds  (scaled (78.0f, 7.0f, 34.0f, 34.0f));
    outTrim.setBounds (scaled (226.0f, 7.0f, 34.0f, 34.0f));
    interiorBtn.setBounds (scaled (598.0f, 10.0f, 106.0f, 28.0f));
    if (interior != nullptr)
        interior->setBounds (scaled (bodyR.getX(), bodyR.getY(), bodyR.getWidth(), bodyR.getHeight()));

    // sfondo statico alla risoluzione effettiva (anche sugli schermi Retina)
    float ds = 1.0f;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) ds = (float) d->scale;
    const float px = scale * juce::jlimit (1.0f, 2.0f, ds);
    body = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (baseW * px)),
                        juce::jmax (1, juce::roundToInt (baseH * px)), true);
    {
        using namespace juce;
        Graphics g (body);
        g.addTransform (AffineTransform::scale (px));

        // barra di servizio
        g.setColour (barCol);
        g.fillRect (0.0f, 0.0f, (float) baseW, 48.0f);
        g.setColour (Colour (0xff2a2c30));
        g.drawHorizontalLine (47, 0.0f, (float) baseW);
        g.setColour (cream.withAlpha (0.75f));
        text (g, uiFont (11.5f), "INPUT", 18.0f, 28.5f, Justification::left);
        text (g, uiFont (11.5f), "OUTPUT", 160.0f, 28.5f, Justification::left);

        // sfondo e ombra del pedale
        g.setGradientFill (ColourGradient (Colour (0xff2b2c2f), 0, 48, Colour (0xff18191b), 0, (float) baseH, false));
        g.fillRect (0.0f, 48.0f, (float) baseW, (float) baseH - 48.0f);
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (bodyR.translated (0, 6.0f).expanded (2.0f), 24.0f);

        // contenitore smaltato
        Path bodyPath; bodyPath.addRoundedRectangle (bodyR, 22.0f);
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (bodyPath);
            auto en = makeEnamel (roundToInt (bodyR.getWidth() * px), roundToInt (bodyR.getHeight() * px));
            g.setOpacity (1.0f);   // drawImage usa l'opacità del colore corrente
            g.drawImage (en, bodyR, RectanglePlacement::stretchToFit);
        }
        g.setColour (enamelDk);
        g.strokePath (bodyPath, PathStrokeType (2.0f));
        g.setColour (Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (bodyR.reduced (3.0f), 19.0f, 1.0f);

        // marchio
        g.setColour (ink);
        text (g, faceFont (face, 88.0f, -0.01f), "ALEA", 40.0f, 160.0f, Justification::left);
        g.setColour (mustard);
        g.fillRect (44.0f, 172.0f, 398.0f, 5.0f);
        g.setColour (ink);
        text (g, faceFont (face, 13.0f, 0.22f), juce::String (juce::CharPointer_UTF8 ("FILTER  \xc2\xb7  SAMPLE/HOLD")), 44.0f, 199.0f, Justification::left);
        g.setColour (ink.withAlpha (0.75f));
        text (g, uiFont (13.0f), "by Collateral Effects", 442.0f, 199.0f, Justification::right);

        // finestra della traccia
        g.setColour (Colours::white.withAlpha (0.5f));
        g.fillRoundedRectangle (scopeR.translated (0, 1.5f), 10.0f);
        g.setColour (ink);
        g.fillRoundedRectangle (scopeR, 10.0f);
        const auto inner = scopeR.reduced (10.0f, 10.0f);
        g.setColour (cream.withAlpha (0.10f));
        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            const float yy = inner.getBottom() - (std::log (f / 80.0f) / std::log (14000.0f / 80.0f)) * inner.getHeight();
            g.drawHorizontalLine (roundToInt (yy), inner.getX() + 26.0f, inner.getRight());
        }
        g.setColour (cream.withAlpha (0.35f));
        auto yOf = [&] (float f) { return inner.getBottom() - (std::log (f / 80.0f) / std::log (14000.0f / 80.0f)) * inner.getHeight(); };
        text (g, uiFont (9.5f), "100", inner.getX() + 20.0f, yOf (100.0f) + 3.0f, Justification::right);
        text (g, uiFont (9.5f), "1k", inner.getX() + 20.0f, yOf (1000.0f) + 3.0f, Justification::right);
        text (g, uiFont (9.5f), "10k", inner.getX() + 20.0f, yOf (10000.0f) + 3.0f, Justification::right);

        // dicitura del footswitch
        g.setColour (ink.withAlpha (0.8f));
        text (g, faceFont (face, 10.5f, 0.2f), "ON / OFF", footC.x, 560.0f);

        // colonna dei comandi
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (columnR.translated (0, 2.0f), 16.0f);
        g.setColour (column);
        g.fillRoundedRectangle (columnR, 16.0f);
        g.setColour (cream);
        text (g, faceFont (face, 14.0f, 0.12f), "FILTER RANGE", rangeC.x, 116.0f);
        text (g, faceFont (face, 14.0f, 0.12f), "S/H SPEED", speedC.x, 390.0f);
        text (g, faceFont (face, 11.0f, 0.14f), "FILTER", switchR.getX() + 22.0f, switchR.getY() - 10.0f);
        text (g, faceFont (face, 11.0f, 0.14f), "S/H", switchR.getRight() - 20.0f, switchR.getY() - 10.0f);
        // scale delle manopole
        for (auto c : { rangeC, speedC })
        {
            for (int i = 0; i <= 10; ++i)
            {
                const float a = rotStart + (float) i / 10.0f * (rotEnd - rotStart);
                const bool major = i % 5 == 0;
                g.setColour (major ? mustard : cream.withAlpha (0.55f));
                g.drawLine ({ c.getPointOnCircumference (knobD * 0.5f + 3.0f, a),
                              c.getPointOnCircumference (knobD * 0.5f + (major ? 11.0f : 8.0f), a) }, major ? 2.4f : 1.6f);
            }
            g.setColour (cream.withAlpha (0.6f));
            text (g, uiFont (10.5f), "0",  c.getPointOnCircumference (knobD * 0.5f + 18.0f, rotStart).x, c.getPointOnCircumference (knobD * 0.5f + 18.0f, rotStart).y + 4.0f);
            text (g, uiFont (10.5f), "10", c.getPointOnCircumference (knobD * 0.5f + 20.0f, rotEnd).x, c.getPointOnCircumference (knobD * 0.5f + 20.0f, rotEnd).y + 4.0f);
        }

        // ghiera del LED
        g.setGradientFill (chrome (ledC, 9.0f, 0.2f));
        g.fillEllipse (Rectangle<float> (18.0f, 18.0f).withCentre (ledC));

        // viti del fondo
        for (auto c : { Point<float> (34, 78), Point<float> (686, 78), Point<float> (34, 570), Point<float> (686, 570) })
            screw (g, c, 6.0f);
    }
    repaint();
}

//==============================================================================
void AleaEditor::paint (juce::Graphics& g)
{
    using namespace juce;
    g.setOpacity (1.0f);
    g.drawImage (body, getLocalBounds().toFloat(), RectanglePlacement::stretchToFit);
    g.addTransform (AffineTransform::scale (scale));

    // valori dei trim nella barra
    g.setColour (mustard);
    auto dB = [this] (const char* id)
    {
        const float v = proc.apvts.getRawParameterValue (id)->load();
        return (v > 0.05f ? "+" : "") + String (v, 1) + " dB";
    };
    text (g, uiFont (11.5f), dB (ParamIDs::input), 118.0f, 28.5f, Justification::left);
    text (g, uiFont (11.5f), dB (ParamIDs::output), 266.0f, 28.5f, Justification::left);

    // stato MIDI
    const int learn = proc.getLearningParam();
    g.setColour (learn >= 0 ? learnCol : cream.withAlpha (0.6f));
    text (g, uiFont (11.5f), learn >= 0 ? String ("MIDI LEARN: muovi un controllo per ") + displayNames[learn] : midiText,
          446.0f, 28.5f);

    // traccia della frequenza centrale del filtro
    paintScope (g, scopeR.reduced (10.0f, 10.0f));

    // LED: acceso con l'effetto inserito, lampeggia agli impulsi del clock S/H
    const bool on = ! footswitch.getToggleState();
    const float glow = on ? 0.75f + 0.25f * flash : 0.0f;
    const auto lr = Rectangle<float> (12.0f, 12.0f).withCentre (ledC);
    if (on)
    {
        g.setGradientFill (ColourGradient (ledRed.withAlpha (0.45f * glow), ledC.x, ledC.y,
                                           ledRed.withAlpha (0.0f), ledC.x + 26.0f, ledC.y, true));
        g.fillEllipse (lr.expanded (20.0f));
    }
    g.setGradientFill (ColourGradient (on ? Colour (0xffffd1c9).interpolatedWith (ledRed, 1.0f - 0.5f * glow) : Colour (0xff5a1612),
                                       ledC.x - 2.0f, ledC.y - 2.5f,
                                       on ? ledRed.darker (0.2f) : Colour (0xff2a0a08), ledC.x + 6.0f, ledC.y + 6.0f, true));
    g.fillEllipse (lr);
    g.setColour (Colours::white.withAlpha (on ? 0.55f : 0.15f));
    g.fillEllipse (Rectangle<float> (4.0f, 3.0f).withCentre (ledC.translated (-2.0f, -2.5f)));
}

void AleaEditor::paintScope (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace juce;
    const float x0 = r.getX() + 28.0f, w = r.getRight() - x0;
    const float lo = std::log (80.0f), hi = std::log (14000.0f);
    Path p;
    for (int i = 0; i < traceLen; ++i)
    {
        const float v = trace[(size_t) ((traceHead + i) % traceLen)];
        const float y = r.getBottom() - jlimit (0.0f, 1.0f, (v - lo) / (hi - lo)) * r.getHeight();
        const float x = x0 + w * (float) i / (float) (traceLen - 1);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (mustard.withAlpha (0.18f));
    g.strokePath (p, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (mustard);
    g.strokePath (p, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
}

void AleaEditor::paintOverChildren (juce::Graphics& g)
{
    const int learn = proc.getLearningParam();
    if (learn < 0) return;
    if (auto* c = controlFor (learn))
    {
        const auto r = c->getBounds().toFloat().expanded (4.0f * scale);
        g.setColour (learnCol.withAlpha (0.35f + 0.5f * pulse));
        g.drawRoundedRectangle (r, 8.0f * scale, 2.5f * scale);
    }
}

//==============================================================================
void AleaEditor::timerCallback()
{
    // traccia
    trace[(size_t) traceHead] = std::log (juce::jmax (20.0f, proc.centreHz.load (std::memory_order_relaxed)));
    traceHead = (traceHead + 1) % traceLen;

    const int pulses = proc.clockPulses.load (std::memory_order_relaxed);
    const bool sh = proc.apvts.getRawParameterValue (ParamIDs::mode)->load() > 0.5f;
    if (pulses != lastPulses && sh) flash = 1.0f;
    else flash *= 0.55f;
    lastPulses = pulses;

    const int learn = proc.getLearningParam();
    pulse = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.008f);

    const int cc = proc.ccCounter.load();
    if (cc != lastCCCount || proc.getMapVersion() != lastMapVersion)
    {
        lastCCCount = cc; lastMapVersion = proc.getMapVersion();
        const int num = proc.lastCC.load();
        midiText = num >= 0 ? "MIDI  CC " + juce::String (num) + "  =  " + juce::String (proc.lastCCValue.load())
                            : juce::String ("MIDI: clic destro su un comando per il Learn");
    }

    if (learn != lastLearn || learn >= 0) { lastLearn = learn; repaint(); return; }
    repaint (scaled (0, 0, (float) baseW, 48.0f));
    repaint (scaled (scopeR.getX(), scopeR.getY(), scopeR.getWidth(), scopeR.getHeight()));
    repaint (scaled (ledC.x - 34.0f, ledC.y - 34.0f, 68.0f, 68.0f));
    if (interior != nullptr) interior->repaint();
}

juce::Component* AleaEditor::controlFor (int idx)
{
    switch (idx)
    {
        case 0: return &range;
        case 1: return &speed;
        case 2: return &modeSwitch;
        case 3: return &footswitch;
        case 4: return &inTrim;
        case 5: return &outTrim;
        default: return interior != nullptr ? interior->controlFor (idx) : &interiorBtn;
    }
}

void AleaEditor::showLearnMenu (int idx, juce::Component* target)
{
    using namespace juce;
    PopupMenu m;
    const int cc = proc.getCCForParam (idx);
    m.addSectionHeader (String (displayNames[idx]) + (cc >= 0 ? "   (CC " + String (cc) + ")" : String ("   (nessun CC)")));
    const bool learning = proc.getLearningParam() == idx;
    m.addItem (1, learning ? "Annulla MIDI Learn" : "MIDI Learn");
    m.addItem (2, cc >= 0 ? "Rimuovi assegnazione CC " + String (cc) : String ("Rimuovi assegnazione"), cc >= 0);
    m.addSeparator();
    m.addItem (3, "Ripristina assegnazioni predefinite");
    if (AleaProcessor::isToggle (idx))
        m.addItem (4, "(i pulsanti della KeyLab vanno in modo Gate)", false);

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (target),
                     [sp = SafePointer<AleaEditor> (this), idx, learning] (int r)
                     {
                         if (sp == nullptr) return;
                         auto& pr = sp->proc;
                         if (r == 1) { if (learning) pr.cancelLearn(); else pr.startLearn (idx); }
                         if (r == 2) pr.clearMappingForParam (idx);
                         if (r == 3) pr.resetDefaultMappings();
                         sp->repaint();
                     });
}
