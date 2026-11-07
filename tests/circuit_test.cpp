// Test del modello del circuito (senza JUCE).
// Esce con codice 1 se un controllo fallisce.
#include "FSHCircuit.h"
#include "FSHCircuitRef.h"   // modello della versione 1.0, per il confronto
#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>
#include <ctime>

using alea::FSHCircuit;
static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (! ok) ++failures;
}

static constexpr double PI = 3.14159265358979323846;

// guadagno (dB) del ramo filtrato a una frequenza, Iabc fissa, segnale piccolo
static double gainAt (double fs, double iabc, double f, double amp = 0.002)
{
    FSHCircuit c; c.prepare (fs); c.setIabcOverride (iabc);
    const int n = (int) (fs * 0.6), skip = (int) (fs * 0.4);
    double s = 0.0, si = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double x = amp * std::sin (2.0 * PI * f * i / fs);
        const auto o = c.process (x);
        if (i >= skip) { s += o.wet * o.wet; si += x * x; }
    }
    return 10.0 * std::log10 (s / si);
}

static void peakOf (double fs, double iabc, double& fPeak, double& gPeak, double& q)
{
    double best = -1e9, bf = 0.0;
    std::vector<std::pair<double, double>> curve;
    for (double f = 40.0; f < 20000.0; f *= 1.03)
    {
        const double g = gainAt (fs, iabc, f);
        if (g > best) { best = g; bf = f; }
    }
    const double centre = bf; best = -1e9;
    for (double f = centre * 0.8; f < centre * 1.25; f *= 1.004)   // griglia fine attorno al picco
    {
        const double g = gainAt (fs, iabc, f);
        curve.push_back ({ f, g });
        if (g > best) { best = g; bf = f; }
    }
    // larghezza a -3 dB
    double lo = bf, hi = bf;
    for (auto& p : curve) if (p.second >= best - 3.0) { lo = std::min (lo, p.first); hi = std::max (hi, p.first); }
    fPeak = bf; gPeak = best; q = bf / std::max (1.0, hi - lo);
}

int main()
{
    const double fs = 192000.0;
    std::printf ("Filtro (Iabc fissa, piccolo segnale)\n");
    for (double iabc : { 2e-6, 10e-6, 50e-6 })
    {
        double f, g, q; peakOf (fs, iabc, f, g, q);
        std::printf ("  Iabc %5.1f uA: picco %7.1f Hz (teorico %7.1f), guadagno %5.1f dB, Q %.1f\n",
                     iabc * 1e6, f, FSHCircuit::centreHz (iabc), g, q);
        check (std::abs (std::log (f / FSHCircuit::centreHz (iabc))) < 0.06, "frequenza centrale come da teoria");
        check (g > 17.0 && g < 22.0, "guadagno al centro ~ +20 dB (a/b)");
        check (q > 7.0 && q < 13.0, "Q ~ 10");
    }

    std::printf ("Clock S/H\n");
    for (double p : { 0.0, 0.5, 1.0 })
    {
        FSHCircuit c; c.prepare (fs); c.setControls (0.5, p, true);
        int edges = 0; bool last = false; int highSamples = 0;
        const int n = (int) (fs * 4.0);
        for (int i = 0; i < n; ++i)
        {
            c.process (0.0);
            const bool h = c.clockIsHigh();
            if (h && ! last) ++edges;
            if (h) ++highSamples;
            last = h;
        }
        const double hz = edges / 4.0, pulseMs = edges > 0 ? 1000.0 * highSamples / fs / edges : 0.0;
        std::printf ("  SPEED %.1f: %5.2f Hz (calcolo %5.2f), impulso %.2f ms\n", p, hz, FSHCircuit::clockHz (p), pulseMs);
        check (std::abs (hz - FSHCircuit::clockHz (p)) < 0.6, "frequenza come da formula");
    }

    std::printf ("Inviluppo (FILTER RANGE)\n");
    {
        for (double r : { 0.0, 0.5, 1.0 })
        {
            FSHCircuit c; c.prepare (fs); c.setControls (r, 0.5, false);
            double iMin = 1, iMax = 0, iRest = 0;
            for (int i = 0; i < (int) (fs * 0.5); ++i) c.process (0.0);
            iRest = c.getIabc();
            for (int i = 0; i < (int) (fs * 0.5); ++i)
            {
                const double x = 0.3 * std::sin (2.0 * PI * 196.0 * i / fs);
                c.process (x);
                iMin = std::min (iMin, c.getIabc()); iMax = std::max (iMax, c.getIabc());
            }
            std::printf ("  RANGE %.1f: riposo %.0f Hz, con 0.3 V a 196 Hz fino a %.0f Hz\n", r,
                         FSHCircuit::centreHz (iRest), FSHCircuit::centreHz (iMax));
            if (r == 0.0) check (iMax < iRest * 1.05, "RANGE a zero: il filtro non si apre");
            if (r == 1.0) check (FSHCircuit::centreHz (iMax) > 3000.0, "RANGE al massimo: apertura ampia");
        }
    }

    std::printf ("S/H: distribuzione delle frequenze (trimmer di fabbrica)\n");
    {
        FSHCircuit c; c.prepare (fs); c.setControls (0.5, 1.0, true);
        c.setTrims (0.30, 0.45, 3.0);
        std::vector<double> held;
        bool last = false; int sinceFall = 0;
        for (int i = 0; i < (int) (fs * 40.0); ++i)
        {
            c.process (0.0);
            const bool h = c.clockIsHigh();
            if (! h && last) sinceFall = 0;
            if (! h && ++sinceFall == (int) (fs * 0.04)) held.push_back (FSHCircuit::centreHz (c.getIabc()));
            last = h;
        }
        std::sort (held.begin(), held.end());
        auto pct = [&] (double p) { return held[(size_t) (p * (held.size() - 1))]; };
        std::printf ("  %zu gradini: 10%% %.0f Hz, 50%% %.0f Hz, 90%% %.0f Hz\n", held.size(), pct (0.1), pct (0.5), pct (0.9));
        check (pct (0.9) / pct (0.1) > 4.0, "gradini ben distribuiti");
    }

    std::printf ("Stabilità e livelli\n");
    for (double sr : { 44100.0, 96000.0, 176400.0, 192000.0 })
    {
        bool ok = true; double peak = 0.0;
        for (int mode = 0; mode < 2; ++mode)
            for (double r : { 0.0, 1.0 })
            {
                FSHCircuit c; c.prepare (sr); c.setControls (r, 1.0, mode == 1); c.setTrims (1.0, 1.0, 6.0);
                for (int i = 0; i < (int) (sr * 1.0); ++i)
                {
                    const double x = 10.0 * std::sin (2.0 * PI * 3000.0 * i / sr) * (i % 9000 < 4000 ? 1 : 0);
                    const auto o = c.process (x);
                    if (! std::isfinite (o.wet)) ok = false;
                    peak = std::max (peak, std::abs (o.wet));
                }
            }
        std::printf ("  %.1f kHz: picco con 10 V in ingresso %.2f V\n", sr / 1000.0, peak);
        check (ok && peak < 10.0, "nessuna instabilità, uscita entro i binari");
    }

    // livello: chitarra simulata (corda pizzicata ricca di armoniche), bypass vs effetto
    std::printf ("Livello rispetto al bypass (nota pizzicata 0.3 V picco)\n");
    for (int mode = 0; mode < 2; ++mode)
    {
        FSHCircuit c; c.prepare (fs); c.setControls (0.6, 0.5, mode == 1); c.setTrims (0.30, 0.45, 3.0);
        double sw = 0, sd = 0; double phase = 0;
        for (int i = 0; i < (int) (fs * 6.0); ++i)
        {
            const double t = std::fmod (i / fs, 1.0);
            phase += 110.0 / fs; phase -= std::floor (phase);
            const double x = 0.3 * std::exp (-3.0 * t) * (2.0 * phase - 1.0);
            const auto o = c.process (x);
            if (i > fs) { sw += o.wet * o.wet; sd += o.dry * o.dry; }
        }
        std::printf ("  %s: effetto %+5.1f dB rispetto al bypass\n", mode ? "S/H   " : "FILTER", 10.0 * std::log10 (sw / sd));
    }


    std::printf ("Confronto con il modello 1.0 (stesse ipotesi: Q2 a guadagno 1, controllo a 192 kHz)\n");
    {
        alea::FSHCircuit n; n.prepare (fs, 1); n.setFollowerGain (1.0); n.setControls (0.7, 0.5, false);
        alearef::FSHCircuit o; o.prepare (fs); o.setControls (0.7, 0.5, false);
        double se = 0, ss = 0;
        for (int i = 0; i < (int) (fs * 3.0); ++i)
        {
            const double t = std::fmod (i / fs, 0.5);
            const double x = 0.4 * std::exp (-4.0 * t) * std::sin (2.0 * PI * 147.0 * i / fs);
            const double a = n.process (x).wet, b = o.process (x).wet;
            if (i > fs * 0.5) { se += (a - b) * (a - b); ss += b * b; }   // a regime
        }
        const double snr = 10.0 * std::log10 (ss / std::max (1e-30, se));
        std::printf ("  FILTER: differenza a %.1f dB sotto il segnale\n", -snr);
        check (snr > 40.0, "le ottimizzazioni non cambiano il suono");
    }

    std::printf ("Controllo alla frequenza base (48 kHz x4) rispetto a 192 kHz pieni\n");
    {
        alea::FSHCircuit a; a.prepare (fs, 4); a.setControls (0.7, 0.5, false);
        alea::FSHCircuit b; b.prepare (fs, 1); b.setControls (0.7, 0.5, false);
        double se = 0, ss = 0;
        for (int i = 0; i < (int) (fs * 3.0); ++i)
        {
            const double t = std::fmod (i / fs, 0.5);
            const double x = 0.4 * std::exp (-4.0 * t) * std::sin (2.0 * PI * 147.0 * i / fs);
            const double ya = a.process (x).wet, yb = b.process (x).wet;
            if (i > fs * 0.5) { se += (ya - yb) * (ya - yb); ss += yb * yb; }
        }
        const double snr = 10.0 * std::log10 (ss / std::max (1e-30, se));
        std::printf ("  differenza a %.1f dB sotto il segnale\n", -snr);
        check (snr > 30.0, "controllo decimato equivalente");
        for (double p : { 0.0, 1.0 })
        {
            alea::FSHCircuit c; c.prepare (fs, 4); c.setControls (0.5, p, true);
            int edges = 0; bool last = false;
            for (int i = 0; i < (int) (fs * 4.0); ++i) { c.process (0.0); if (c.clockIsHigh() && ! last) ++edges; last = c.clockIsHigh(); }
            std::printf ("  clock con SPEED %.0f: %.2f Hz (calcolo %.2f)\n", p * 10, edges / 4.0, FSHCircuit::clockHz (p));
            check (std::abs (edges / 4.0 - FSHCircuit::clockHz (p)) < 0.6, "clock corretto a frequenza base");
        }
    }

    std::printf ("Esemplari (tolleranze dei componenti)\n");
    {
        double fMin = 1e9, fMax = 0, qMin = 1e9, qMax = 0;
        for (unsigned serial : { 0u, 1u, 7u, 42u, 1254u })
        {
            FSHCircuit c; c.setUnit (serial); c.prepare (fs);
            const auto& P = c.parts();
            const double g1 = 20e-6 / P.R6 * 22e3 * P.gm1 / (2 * FSHCircuit::Vt), g2 = 20e-6 / P.R13 * 22e3 * P.gm2 / (2 * FSHCircuit::Vt);
            const double cd = P.R10 / (P.R9 + P.R10) * P.q2Gain;
            const double w0 = std::sqrt (g1 * g2 * cd / (P.C3 * P.C5));
            const double G = 1 / P.R4 + 1 / P.R7 + 1 / P.R5, b = (1 / P.R7) / G * P.q2Gain;
            const double q = w0 * P.C3 / (g1 * b);
            std::printf ("  n. %4u: centro %6.0f Hz a 20 uA, Q %4.1f, clock %4.1f-%4.1f Hz\n", serial, w0 / (2 * PI), q,
                         FSHCircuit::clockHz (0.0, P), FSHCircuit::clockHz (1.0, P));
            fMin = std::min (fMin, w0); fMax = std::max (fMax, w0); qMin = std::min (qMin, q); qMax = std::max (qMax, q);
        }
        check (fMax / fMin > 1.05 && fMax / fMin < 2.0, "gli esemplari differiscono in modo plausibile");
        check (qMin > 5.0 && qMax < 20.0, "Q sempre nell'intervallo del circuito");
    }

    std::printf ("Carico del pickup (ingresso del pedale ~100 kOhm)\n");
    {
        auto lvl = [&] (double f, bool on)
        {
            FSHCircuit c; c.prepare (fs); c.setPickupLoading (on);
            double s = 0, si = 0;
            for (int i = 0; i < (int) (fs * 0.3); ++i)
            {
                const double x = 0.01 * std::sin (2 * PI * f * i / fs);
                const double y = c.process (x).dry;
                if (i > fs * 0.1) { s += y * y; si += x * x; }
            }
            return 10 * std::log10 (s / si);
        };
        const double d200 = lvl (200, true) - lvl (200, false), d4k = lvl (4600, true) - lvl (4600, false);
        std::printf ("  200 Hz %+.1f dB, 4,6 kHz %+.1f dB\n", d200, d4k);
        check (std::abs (d200) < 1.0 && d4k < -6.0, "toglie la risonanza del pickup, lascia i bassi");
    }

    std::printf ("Effetto escluso: filtro sospeso\n");
    {
        FSHCircuit c; c.prepare (fs);
        bool ok = true;
        for (int i = 0; i < 1000; ++i) { auto o = c.process (0.2 * std::sin (i * 0.01), false); ok = ok && o.wet == 0.0; }
        check (ok, "nessun calcolo del filtro in bypass");
    }

    std::printf ("Prestazioni (48 kHz x4, 10 s di audio)\n");
    {
        auto run = [&] (auto& c)
        {
            const auto t0 = std::clock(); double acc = 0;
            for (int i = 0; i < 192000 * 10; ++i)
                acc += c.process (0.3 * std::sin (i * 0.0071) * std::exp (-std::fmod (i / 192000.0, 0.5) * 4)).wet;
            return double (std::clock() - t0) / CLOCKS_PER_SEC + acc * 0.0;
        };
        alearef::FSHCircuit o; o.prepare (192000.0); o.setControls (0.7, 0.6, false);
        alea::FSHCircuit n; n.prepare (192000.0, 4); n.setControls (0.7, 0.6, false);
        const double to = run (o), tn = run (n);
        std::printf ("  versione 1.0: %.2f %% di un core, nuova: %.2f %% (%.1fx piu' veloce)\n", to * 10, tn * 10, to / tn);
        check (tn < to * 0.5, "almeno il doppio della velocità");
    }

    std::printf (failures ? "\n%d controlli falliti\n" : "\nTutti i controlli superati\n", failures);
    return failures ? 1 : 0;
}
