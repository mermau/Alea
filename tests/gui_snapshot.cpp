// Strumento di sviluppo: disegna l'interfaccia in PNG senza aprire finestre.
// uso: gui_snapshot uscita.png [scala] [interno 0/1] [sh 0/1]
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "snapshot.png";
    const float scale = argc > 2 ? (float) std::atof (argv[2]) : 1.0f;
    const bool interior = argc > 3 && std::atoi (argv[3]) != 0;
    const bool sh = argc > 4 && std::atoi (argv[4]) != 0;

    AleaProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    if (sh) proc.apvts.getParameter (ParamIDs::mode)->setValueNotifyingHost (1.0f);
    proc.apvts.getParameter (ParamIDs::range)->setValueNotifyingHost (0.62f);

    // un po' di segnale per la traccia
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (juce::roundToInt (AleaEditor::baseW * scale), juce::roundToInt (AleaEditor::baseH * scale));
    double ph = 0.0;
    for (int b = 0; b < 400; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double t = std::fmod ((b * 512 + i) / 48000.0, 0.5);
            ph += 110.0 / 48000.0; ph -= std::floor (ph);
            const float x = (float) (0.3 * std::exp (-5.0 * t) * (2.0 * ph - 1.0));
            buf.setSample (0, i, x); buf.setSample (1, i, x);
        }
        proc.processBlock (buf, midi);
        if (b % 3 == 0)
            if (auto* e = dynamic_cast<AleaEditor*> (ed.get())) e->tickForSnapshot();
    }
    if (interior)
        if (auto* e = dynamic_cast<AleaEditor*> (ed.get())) e->openInteriorForSnapshot();

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (out);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("salvato %s (%d x %d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    ed.reset();
    return 0;
}
