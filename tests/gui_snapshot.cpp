// Strumento di sviluppo: disegna l'interfaccia in PNG senza aprire finestre.
// uso: gui_snapshot uscita.png [scala] [interno 0/1] [sh 0/1]
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <numeric>
#include <algorithm>
#include <vector>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 1 && juce::String (argv[1]) == "procbench")      // banco di prova del processor
    {
        const int bs = argc > 2 ? std::atoi (argv[2]) : 64;
        const double sr = argc > 3 ? std::atof (argv[3]) : 48000.0;
        AleaProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, bs);
        proc.prepareToPlay (sr, bs);
        if (argc > 4 && std::atoi (argv[4]) == 1) proc.apvts.getParameter (ParamIDs::mode)->setValueNotifyingHost (1.0f);
        proc.apvts.getParameter (ParamIDs::range)->setValueNotifyingHost (0.7f);
        juce::AudioBuffer<float> buf (2, bs);
        juce::MidiBuffer midi;
        juce::Random r (3);
        // chitarra sintetica (Karplus-Strong) sul canale sinistro, rumore di fondo sul destro
        std::vector<std::vector<float>> str (6);
        std::vector<size_t> pos (6, 0);
        const double fr[6] = { 82.4, 110, 146.8, 196, 246.9, 329.6 };
        const int blocks = (int) (sr * 30.0 / bs);
        std::vector<double> times; times.reserve ((size_t) blocks);
        double pk = 0, lrDiff = 0; long n = 0;
        juce::File outF = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 5 ? argv[5] : "proc_out.raw");
        outF.deleteFile();
        juce::FileOutputStream os (outF);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < bs; ++i, ++n)
            {
                if (n % (long) (sr * 0.25) == 0)
                    for (int s = 0; s < 6; ++s)
                    {
                        str[(size_t) s].assign ((size_t) (sr / (fr[s] * (1 + (r.nextInt (2))))), 0.0f);
                        for (auto& v : str[(size_t) s]) v = 0.15f * (r.nextFloat() * 2 - 1);
                        pos[(size_t) s] = 0;
                    }
                float x = 0;
                for (int s = 0; s < 6; ++s)
                {
                    auto& v = str[(size_t) s]; auto& p = pos[(size_t) s];
                    const size_t j = (p + 1) % v.size();
                    const float y = v[p]; v[p] = 0.4985f * (v[p] + v[j]); p = j; x += y;
                }
                buf.setSample (0, i, x);
                buf.setSample (1, i, 0.0005f * (r.nextFloat() * 2 - 1));
            }
            const auto t0 = juce::Time::getHighResolutionTicks();
            proc.processBlock (buf, midi);
            times.push_back (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0));
            for (int i = 0; i < bs; ++i)
            {
                pk = std::max (pk, (double) std::abs (buf.getSample (0, i)));
                lrDiff = std::max (lrDiff, (double) std::abs (buf.getSample (0, i) - buf.getSample (1, i)));
                const float l = buf.getSample (0, i);
                os.write (&l, 4);
            }
        }
        {   // i 8 blocchi più lenti, con la loro posizione
            std::vector<std::pair<double, int>> idx;
            for (int i = 0; i < (int) times.size(); ++i) idx.push_back ({ times[(size_t) i], i });
            std::sort (idx.begin(), idx.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            std::printf ("  piu' lenti:");
            for (int i = 0; i < 8; ++i) std::printf (" #%d=%.0fus", idx[(size_t) i].second, 1e6 * idx[(size_t) i].first);
            std::printf ("\n");
        }
        std::sort (times.begin(), times.end());
        const double budget = bs / sr;
        std::printf ("blocco %d a %.0f Hz: tempo medio %.1f us, 99,9%% %.1f us, massimo %.1f us (budget %.0f us); picco uscita %.3f, diff L/R max %.3g\n",
                     bs, sr, 1e6 * std::accumulate (times.begin(), times.end(), 0.0) / (double) times.size(),
                     1e6 * times[(size_t) (0.999 * (double) (times.size() - 1))], 1e6 * times.back(), 1e6 * budget, pk, lrDiff);
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "footswitch")      // stati del footswitch, ingranditi
    {
        juce::Image img (juce::Image::ARGB, 4 * 240, 240, true);
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xffe9e0cb));
        for (int i = 0; i < 2; ++i)
        {
            Footswitch fs; fs.setBounds (0, 0, 200, 200);
            juce::Graphics::ScopedSaveState ss (g);
            g.setOrigin (20 + i * 480, 20);
            fs.paintButton (g, false, i == 1);
            g.setOrigin (240, 0);
            g.addTransform (juce::AffineTransform::scale (0.46f).translated (40, 50));
            fs.setBounds (0, 0, 200, 200);
            fs.paintButton (g, false, i == 1);
        }
        juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "footswitch.png");
        f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        return 0;
    }
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
