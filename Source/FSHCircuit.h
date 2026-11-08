#pragma once
// Modello circuitale del Filter / Sample-Hold (schema di riferimento JD Sleep 2001/05/14).
// Nessuna dipendenza da JUCE: compilabile anche nei test.
//
// Blocchi (sigle dello schema):
//   ingresso     carico del pickup su R2 (opzionale), Q1 inseguitore, C1/R2 e C2 (passa-alto)
//   inviluppo    R15 (FILTER RANGE), R16, IC3 741 raddrizzatore di precisione (D2/D3, R17),
//                R18/C6 carica, R19 scarica
//   S/H          Q4 rumore a valanga -> IC4 (C8/R25) -> R27 trim -> Q5 interruttore JFET -> C9
//                -> Q6 inseguitore; clock IC5 (R28 S/H SPEED + R29, C10, D1/R32, R33/R34)
//   SW1          sceglie inviluppo o S/H; nel modo S/H collega anche R23 al trim R26
//   Q3           sorgente di corrente a base comune (R21, R22, R23), C7/R20 sul collettore,
//                R6 e R13 verso i pin 5 (Iabc) dei due 3080
//   filtro       IC1 e IC2 CA3080 integratori (C3, C5), Q2 inseguitore (guadagno < 1);
//                R4/R5/R7 e R9/R10: passa-banda a variabili di stato, uscita da Q2 via R11/C4
//   SW2          footswitch: uscita diretta (dopo il buffer) o filtrata
//
// Struttura di calcolo:
//   - il filtro audio gira alla frequenza sovracampionata (trapezoidale + Newton 2x2);
//   - la parte di controllo (inviluppo, clock, rumore, S/H, Q3, C7) gira una volta ogni
//     `controlDiv` campioni (cioè alla frequenza base), con Iabc interpolata in mezzo;
//   - le costanti di tempo sono discretizzate in forma esatta e precalcolate.
//
// Unità: volt, ampere, secondi.

#include <cmath>
#include <cstdint>
#include <algorithm>

#ifndef NEWTON_COUNT
 #define NEWTON_COUNT
#endif
#ifndef NEWTON_TOL
 #define NEWTON_TOL 3e-4
#endif
// ingresso dei 3080: passo singolo agganciato (default) o soluzione completa (riferimento nei test)
#ifdef OTA_EXACT
 #define OTA_INPUT(w, k, u, t, s) { solveOTAInput (w, k, t, s); }
#else
 #define OTA_INPUT(w, k, u, t, s) stepOTAInput (w, k, u, t, s)
#endif

namespace alea
{

// potenziometro logaritmico (curva A, 15 % a metà corsa)
inline double audioTaper (double p)
{
    constexpr double b = 32.111;   // ((1 - 0.15) / 0.15)^2
    p = std::clamp (p, 0.0, 1.0);
    return (std::pow (b, p) - 1.0) / (b - 1.0);
}

// tanh razionale (Lambert 7/6): errore < 2e-6 per |x| < 4, poi saturato a +-1
inline double fastTanh (double x)
{
    if (x > 4.97) return 1.0;
    if (x < -4.97) return -1.0;
    const double x2 = x * x;
    const double n = x * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2)));
    const double d = 135135.0 + x2 * (62370.0 + x2 * (3150.0 + x2 * 28.0));
    return std::clamp (n / d, -1.0, 1.0);
}

// passa-alto del primo ordine (trasformazione bilineare)
struct OnePoleHP
{
    double a = 0.0, x1 = 0.0, y1 = 0.0;
    void prepare (double fs, double fc)
    {
        const double k = std::tan (3.14159265358979 * fc / fs);
        a = (1.0 - k) / (1.0 + k);
    }
    void reset() { x1 = y1 = 0.0; }
    double process (double x)
    {
        const double y = 0.5 * (1.0 + a) * (x - x1) + a * y1;
        x1 = x; y1 = y;
        return y;
    }
};

// biquad in forma trasposta II
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0.0; }
    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    // H(s) = (n0 + n1 s + n2 s^2) / (d0 + d1 s + d2 s^2), bilineare con pre-distorsione a fw
    void setAnalog (double n0, double n1, double n2, double d0, double d1, double d2, double fs, double fw)
    {
        const double w = 2.0 * 3.14159265358979 * fw;
        const double K = w / std::tan (w / (2.0 * fs));
        const double K2 = K * K;
        const double A0 = d0 + d1 * K + d2 * K2;
        b0 = (n0 + n1 * K + n2 * K2) / A0;
        b1 = (2.0 * n0 - 2.0 * n2 * K2) / A0;
        b2 = (n0 - n1 * K + n2 * K2) / A0;
        a1 = (2.0 * d0 - 2.0 * d2 * K2) / A0;
        a2 = (d0 - d1 * K + d2 * K2) / A0;
    }
};

//==============================================================================
// Valori dei componenti. I valori nominali sono quelli dello schema; un "esemplare"
// applica le tolleranze dell'epoca (resistenze 10 %, condensatori 10-20 %, OTA e JFET
// con la dispersione tipica dei datasheet), in modo deterministico dal numero di serie.
struct Parts
{
    // filtro
    double R4 = 100e3, R5 = 3.3e3, R7 = 1e6, R9 = 100e3, R10 = 3.3e3;
    double C3 = 0.001e-6, C5 = 0.03e-6;
    double gm1 = 1.0, gm2 = 1.0;      // dispersione della transconduttanza dei due 3080
    double beta = 125.0;              // 3080: corrente di base = Iabc / (2 beta) per ingresso
                                      // (datasheet: 2 uA a Iabc 500 uA, Ri 26 k)
    double R12 = 3.3e3;               // compensa la corrente di base sul pin 3 di IC2
    double otaHeadPos = 1.4, otaHeadNeg = 0.5;   // escursione d'uscita del 3080 dai binari +-8 V
    double q2Gain = 0.94;             // Q2 2N4303 inseguitore su R8 12k (gm Rs ~ 16)
    double q2Vgs = 1.67;              // tensione gate-source di Q2: C3 lavora a -1,67 V
    double q2Sat = 0.83;              // Vds minima di Q2 in saturazione (drain a +8 V)
    // inviluppo
    double R15 = 100e3, R16 = 22e3, R17 = 1e6, R18 = 3.3e3, R19 = 100e3, C6 = 4.7e-6;
    // sorgente di corrente
    double R21 = 1e6, R22 = 22e3, R23 = 100e3, R26 = 100e3, R20 = 2.7e6, R6 = 22e3, R13 = 22e3;
    double C7 = 0.15e-6, IsQ3 = 2.0e-14;
    // S/H
    double R27 = 10e3, C9 = 0.047e-6, RonQ5 = 250.0, VpQ5 = -2.5;
    double R31 = 22e3, IdssQ6 = 6e-3, VpQ6 = -2.5;
    // clock
    double R28 = 2e6, R29 = 330e3, R32 = 3.3e3, R33 = 22e3, R34 = 47e3, C10 = 0.15e-6;
    double vrail = 8.0;               // escursione dei 741 a +-9 V

    static Parts unit (uint32_t serial)
    {
        Parts p;
        if (serial == 0) return p;        // 0 = valori nominali dello schema
        uint64_t s = 0x9e3779b97f4a7c15ull * (serial + 0x632be59bd9b4e019ull);
        auto next = [&s]
        {
            s += 0x9e3779b97f4a7c15ull;
            uint64_t z = s;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return (double) ((z ^ (z >> 31)) >> 11) * (1.0 / 9007199254740992.0);
        };
        // gaussiana con sigma = tol / 2, limitata a +-tol
        auto tol = [&] (double v, double t)
        {
            const double u1 = std::max (1e-12, next()), u2 = next();
            const double g = std::sqrt (-2.0 * std::log (u1)) * std::cos (6.283185307179586 * u2);
            return v * (1.0 + std::clamp (0.5 * t * g, -t, t));
        };
        const double r = 0.10, c = 0.10;
        p.R4 = tol (p.R4, r); p.R5 = tol (p.R5, r); p.R7 = tol (p.R7, r);
        p.R9 = tol (p.R9, r); p.R10 = tol (p.R10, r);
        p.C3 = tol (p.C3, c); p.C5 = tol (p.C5, c);
        p.gm1 = tol (1.0, 0.25); p.gm2 = tol (1.0, 0.25);
        p.beta = tol (p.beta, 0.3); p.R12 = tol (p.R12, r);
        p.q2Vgs = tol (p.q2Vgs, 0.25);
        p.q2Gain = std::min (0.98, tol (p.q2Gain, 0.03));
        p.R15 = tol (p.R15, 0.20); p.R16 = tol (p.R16, r); p.R17 = tol (p.R17, r);
        p.R18 = tol (p.R18, r); p.R19 = tol (p.R19, r); p.C6 = tol (p.C6, 0.20);
        p.R21 = tol (p.R21, r); p.R22 = tol (p.R22, r); p.R23 = tol (p.R23, r);
        p.R20 = tol (p.R20, r); p.R6 = tol (p.R6, r); p.R13 = tol (p.R13, r);
        p.C7 = tol (p.C7, c); p.IsQ3 = tol (p.IsQ3, 0.5);
        p.R27 = tol (p.R27, 0.20); p.C9 = tol (p.C9, c);
        p.VpQ6 = tol (p.VpQ6, 0.3); p.IdssQ6 = tol (p.IdssQ6, 0.4); p.VpQ5 = tol (p.VpQ5, 0.3);
        p.R28 = tol (p.R28, 0.20); p.R29 = tol (p.R29, r); p.R32 = tol (p.R32, r);
        p.R33 = tol (p.R33, r); p.R34 = tol (p.R34, r); p.C10 = tol (p.C10, c);
        p.vrail = tol (p.vrail, 0.04);
        return p;
    }
};

class FSHCircuit
{
public:
    static constexpr double Vt = 0.02585;    // tensione termica (300 K)
    static constexpr double Vd1 = 0.6;

    //==========================================================================
    // sampleRate = frequenza a cui viene chiamato process(); controlDiv = fattore di
    // sovracampionamento (la parte di controllo gira a sampleRate / controlDiv)
    void prepare (double sampleRate, int controlDivision = 1)
    {
        fs = sampleRate;
        T = 1.0 / fs;
        div = std::max (1, controlDivision);
        Tc = T * div;
        prepared = true;
        rebuild();
        reset();
    }

    void reset()
    {
        hpC1.reset(); hpC2.reset(); hpC4.reset(); pickup.reset();
        v1 = v2 = i1 = i2 = v1p = v2p = ua1 = ua2 = ta1 = ta2 = 0.0;
        ve = 0.45; vx = 0.55; ic = 4e-6;
        vc10 = -5.0; clockHigh = false;
        vc9 = 0.0; ns1 = ns2 = 0.0;
        sub = 0; groupMin = 1e9;
        const double gt = 1.0 / P.R20 + 1.0 / Rpin5;
        vcol = (ic + Vpin5 / Rpin5) / gt;
        // assesta inviluppo, Q3 e C7 a riposo (mezzo secondo di silenzio): niente spazzata
        // del filtro quando il plugin si apre
        for (int i = 0, n = (int) (0.5 / Tc); i < n; ++i) controlTick (0.0);
        iabcPrev = iabcNext = std::max (0.0, vcol - Vpin5);
        lastDrive = iabcNext;
        updateWarp (iabcNext);
    }

    // comandi del frontale (0..1)
    void setControls (double filterRange, double shSpeed, bool sampleHold)
    {
        filterRange = std::clamp (filterRange, 0.0, 1.0);
        shSpeed = std::clamp (shSpeed, 0.0, 1.0);
        shMode = sampleHold;
        if (changed (filterRange, range)) { range = filterRange; updateRange(); }
        if (changed (shSpeed, speed))     { speed = shSpeed; updateClock(); }
    }
    // trimmer interni (0..1) e rumore di Q4 (V efficaci)
    void setTrims (double offsetR26, double levelR27, double noiseQ4)
    {
        offsetR26 = std::clamp (offsetR26, 0.0, 1.0);
        levelR27 = std::clamp (levelR27, 0.0, 1.0);
        noiseRms = std::max (0.0, noiseQ4);
        if (changed (offsetR26, trimOffset) || changed (levelR27, trimLevel))
        {
            trimOffset = offsetR26; trimLevel = levelR27;
            updateTrims();
        }
    }
    // esemplare (0 = schema) e carico del pickup passivo sull'ingresso da ~100 kOhm
    void setUnit (uint32_t serial)
    {
        if (serial == unitSerial) return;
        unitSerial = serial;
        P = Parts::unit (serial);
        if (prepared) rebuild();
    }
    void setPickupLoading (bool on)
    {
        if (on != pickupLoad) { pickupLoad = on; pickup.reset(); }
    }
    // solo per i test
    void setIabcOverride (double a) { iabcOverride = a; if (prepared && a >= 0.0) updateWarp (a * P.R6); }
    void setFollowerGain (double g)  { P.q2Gain = g; rebuild(); }
    // modello della versione 1.2 (senza correnti di base, escursione simmetrica, Q6 scarico,
    // senza correzione di frequenza): serve solo al test di regressione numerica
    void setLegacyModel (bool on)    { legacy = on; rebuild(); }

    struct Out { double wet, dry; };

    // un campione (volt). Con computeWet = false il filtro audio resta fermo (effetto escluso):
    // la parte di controllo continua a funzionare come nel pedale.
    Out process (double vinRaw, bool computeWet = true)
    {
        if (pickupLoad) vinRaw = pickup.process (vinRaw);
        const double vin = hpC2.process (hpC1.process (0.985 * vinRaw));

        groupMin = std::min (groupMin, vin);    // il raddrizzatore vede i picchi negativi
        if (++sub >= div)
        {
            sub = 0;
            controlTick (groupMin);
            groupMin = 1e9;
            iabcPrev = iabcNext;
            iabcNext = std::max (0.0, vcol - Vpin5);   // corrente totale verso i pin 5 x R
            updateWarp (iabcOverride >= 0.0 ? iabcOverride * P.R6 : iabcNext);
        }

        if (! computeWet)
        {
            if (! filterIdle) { v1 = v2 = i1 = i2 = v1p = v2p = ua1 = ua2 = ta1 = ta2 = 0.0; hpC4.reset(); filterIdle = true; }
            return { 0.0, vin };
        }
        filterIdle = false;

        double drive;   // (Vc - Vpin5): interpolata fra due aggiornamenti del controllo
        if (iabcOverride >= 0.0) drive = iabcOverride * P.R6;
        else drive = iabcPrev + (iabcNext - iabcPrev) * (double) (sub + 1) / (double) div;
        lastDrive = drive;
        filterStep (vin, drive / P.R6 * P.gm1, drive / P.R13 * P.gm2);
        return { hpC4.process (P.q2Gain * v1), vin };
    }

    // stato per l'interfaccia
    bool   clockIsHigh() const    { return clockHigh; }
    double getIabc() const        { return lastDrive / P.R6; }
    double getEnvelope() const    { return ve; }
    // frequenza centrale del passa-banda (piccolo segnale) con la Iabc attuale
    double currentCentreHz() const
    {
        const double g1 = lastDrive / P.R6 * P.gm1 / (2.0 * Vt), g2 = lastDrive / P.R13 * P.gm2 / (2.0 * Vt);
        return std::sqrt (std::max (0.0, g1 * g2) * cDiv * P.q2Gain / (P.C3 * P.C5)) / (2.0 * pi);
    }
    // valori nominali per l'interfaccia e i test
    static double centreHz (double iabc)
    {
        const Parts n;
        const double gm = iabc / (2.0 * Vt);
        return gm * std::sqrt (n.R10 / (n.R9 + n.R10) * n.q2Gain / (n.C3 * n.C5)) / (2.0 * pi);
    }
    static double clockHz (double speedPos, const Parts& p = Parts())
    {
        const double vth = p.R34 / (p.R33 + p.R34) * p.vrail;
        const double rs = p.R29 + p.R28 * audioTaper (1.0 - speedPos);
        const double tLow = rs * p.C10 * std::log ((p.vrail + vth) / (p.vrail - vth));
        const double rp = 1.0 / (1.0 / rs + 1.0 / p.R32);
        const double vinf = (p.vrail / rs + (p.vrail - Vd1) / p.R32) * rp;
        const double tHigh = rp * p.C10 * std::log ((vinf + vth) / (vinf - vth));
        return 1.0 / (tLow + tHigh);
    }
    const Parts& parts() const { return P; }

private:
    static constexpr double pi = 3.14159265358979323846;
    // i comandi arrivano già levigati: si ricalcolano i coefficienti solo se cambiano davvero
    static bool changed (double a, double b) { return std::abs (a - b) > 1e-9; }
    static constexpr double Vpin5 = -8.0 + 0.55;      // ingresso Iabc dei 3080 (V- + Vbe)

    //==========================================================================
    // coefficienti che dipendono da fs e dai componenti
    void rebuild()
    {
        if (! prepared) return;
        // ingresso: C1 .047u su R2 100k, C2 .15u sul carico R4+R5 // R15 (~51k)
        hpC1.prepare (fs, 1.0 / (2.0 * pi * 100e3 * 0.047e-6));
        hpC2.prepare (fs, 1.0 / (2.0 * pi * 51e3 * 0.15e-6));
        // uscita: C4 .15u su R14 100k // ingresso dell'amplificatore 1M
        hpC4.prepare (fs, 1.0 / (2.0 * pi * 90.9e3 * 0.15e-6));

        // pickup passivo (L 2.5 H, R 7 k, C con cavo 470 pF): registrato in DI su 1 MOhm,
        // qui caricato da R2 // ingresso di Q1 (~97 kOhm). Il filtro è il rapporto fra le due risposte.
        {
            const double L = 2.5, R = 7e3, C = 470e-12, Rdi = 1e6, Rp = 97e3;
            auto den = [&] (double Rl, double& d0, double& d1, double& d2)
            { d0 = Rl + R; d1 = L + R * Rl * C; d2 = L * Rl * C; };
            double n0, n1, n2, d0, d1, d2;
            den (Rdi, n0, n1, n2);
            den (Rp, d0, d1, d2);
            const double k = Rp / Rdi;
            pickup.setAnalog (k * n0, k * n1, k * n2, d0, d1, d2, fs, 4600.0);
        }

        // partitori del filtro
        const double Gm = 1.0 / P.R4 + 1.0 / P.R7 + 1.0 / P.R5;
        aIn = (1.0 / P.R4) / Gm;
        bFb = (1.0 / P.R7) / Gm * P.q2Gain;           // Q2 -> pin 2 di IC1
        cDiv = P.R10 / (P.R9 + P.R10);
        cFb = cDiv * P.q2Gain;                        // Q2 -> pin 2 di IC2
        h1 = T / (2.0 * P.C3); h2 = T / (2.0 * P.C5);
        Rm = 1.0 / Gm;                                // resistenza vista dal pin 2 di IC1
        Rc = 1.0 / (1.0 / P.R9 + 1.0 / P.R10);        // resistenza vista dal pin 2 di IC2
        inv2b = legacy ? 0.0 : 0.5 / P.beta;
        // escursione della tensione su C3 (in variazione rispetto al punto di lavoro -Vgs):
        // in alto la limita la saturazione di Q2, in basso l'uscita del 3080
        if (legacy) { v1Hi = 7.5; v1Lo = -7.5; }
        else
        {
            v1Hi = std::min (8.0 - P.otaHeadPos + P.q2Vgs, (8.0 - P.q2Sat) / P.q2Gain);
            v1Lo = -8.0 + P.otaHeadNeg + P.q2Vgs;
        }

        Rpin5 = 1.0 / (1.0 / P.R6 + 1.0 / P.R13);
        {
            const double gt = 1.0 / P.R20 + 1.0 / Rpin5;
            colGt = gt;
            colA = std::exp (-Tc * gt / P.C7);
        }

        // rumore: IC4 derivatore (C8 .047u, R25 1M) limitato dal prodotto banda-guadagno del
        // 741: passa-banda centrato a sqrt(GBW / (R25 C8)) ~ 1.84 kHz, Q ~ 1.5
        {
            const double fsc = 1.0 / Tc;
            const double fn = std::min (std::sqrt (1.0e6 / (2.0 * pi * 1e6 * 0.047e-6)), 0.45 * fsc);
            noiseG = std::tan (pi * fn / fsc);
            noiseK = 1.0 / 1.5;
            noiseH = 1.0 / (1.0 + noiseG * (noiseK + noiseG));
            noiseGainNorm = std::sqrt (fsc / (pi * fn * noiseK));
        }
        updateRange(); updateClock(); updateTrims();
    }

    // tan(w0 T / 2) / (w0 T / 2) alla frequenza centrale attuale: con questo fattore la regola
    // trapezoidale mette il picco del filtro alla frequenza analogica esatta
    void updateWarp (double drive)
    {
        warp = 1.0;
        if (legacy) return;
        const double k2 = 1.0 / (2.0 * Vt);
        const double g1 = drive / P.R6 * P.gm1 * k2, g2 = drive / P.R13 * P.gm2 * k2;
        const double x = std::min (1.4, 0.5 * T * std::sqrt (std::max (0.0, g1 * g2) * cFb / (P.C3 * P.C5)));
        if (x > 1e-4) warp = std::tan (x) / x;
    }

    void updateRange()
    {
        const double k = audioTaper (range);
        const double rw = k * (1.0 - k) * P.R15;          // impedenza del cursore di R15
        envGain = P.R17 / (P.R16 + rw) * k;
    }

    void updateClock()
    {
        const double rs = P.R29 + P.R28 * audioTaper (1.0 - speed);
        const double rp = 1.0 / (1.0 / rs + 1.0 / P.R32);
        clkHighInf = (P.vrail / rs + (P.vrail - Vd1) / P.R32) * rp;   // D1 in conduzione
        clkHighA = std::exp (-Tc / (rp * P.C10));
        clkLowA  = std::exp (-Tc / (rs * P.C10));
        clkVth = P.R34 / (P.R33 + P.R34) * P.vrail;
    }

    void updateTrims()
    {
        wiperRsrc = trimLevel * (1.0 - trimLevel) * P.R27 + 75.0;
        sampleA = 1.0 - std::exp (-Tc / (P.C9 * (P.RonQ5 + wiperRsrc)));
        trimV = -9.0 + 18.0 * trimOffset;
        trimR = P.R23 + trimOffset * (1.0 - trimOffset) * P.R26;
    }

    //==========================================================================
    void controlTick (double vinMin)
    {
        // ---- clock IC5: trigger di Schmitt con C10 (esponenziali esatte nelle due fasi)
        if (clockHigh) vc10 = clkHighInf + (vc10 - clkHighInf) * clkHighA;
        else           vc10 = -P.vrail + (vc10 + P.vrail) * clkLowA;
        if (clockHigh ? (vc10 >= clkVth) : (vc10 <= -clkVth)) clockHigh = ! clockHigh;

        // ---- rumore: Q4 (bianco) -> IC4 passa-banda, saturazione ai binari
        const double w = gauss() * noiseRms * noiseGainNorm;
        const double hp = (w - (noiseK + noiseG) * ns1 - ns2) * noiseH;
        const double bp = noiseG * hp + ns1; ns1 = noiseG * hp + bp;
        const double lp = noiseG * bp + ns2; ns2 = noiseG * bp + lp;
        const double vn = P.vrail * fastTanh (bp * noiseK / P.vrail);

        // ---- R27 e Q5: campiona durante l'impulso alto del clock
        const double vw = trimLevel * vn;
        if (clockHigh) vc9 += (vw - vc9) * sampleA;
        else
        {
            const double vgs = -P.vrail - std::min (vw, vc9);    // gate a -8 V tramite R30
            if (vgs > P.VpQ5)                                     // conduzione parziale
            {
                const double ron = P.RonQ5 / (1.0 - vgs / P.VpQ5);
                vc9 += (vw - vc9) * (1.0 - std::exp (-Tc / (P.C9 * (ron + wiperRsrc))));
            }
        }

        // ---- Q6 inseguitore di source: R31 verso -9 V e, in S/H, R22 verso l'emettitore di Q3
        const double vsh = (shMode && ! legacy) ? jfetFollower (vc9, 1.0 / P.R31 + 1.0 / P.R22, -9.0 / P.R31 + vx / P.R22)
                                                : jfetFollower (vc9, 1.0 / P.R31, -9.0 / P.R31);

        // ---- nodo di emettitore di Q3
        double g = 1.0 / P.R21, iin = 9.0 / P.R21;
        const double vsrc = shMode ? vsh : ve;
        g += 1.0 / P.R22; iin += vsrc / P.R22;
        if (shMode) { g += 1.0 / trimR; iin += trimV / trimR; }
        const double vth = iin / g, rth = 1.0 / g;
        vx = solveEmitter (vth, rth, vx);
        ic = 0.995 * std::max (0.0, (vth - vx) / rth);

        // ---- inviluppo: IC3 carica C6 tramite R18 (D3), scarica su R19 e su R22
        {
            const double target = std::min (-envGain * vinMin, P.vrail - Vd1);
            double cur = -ve / P.R19;
            if (target > ve) cur += (target - ve) / P.R18;
            else             cur -= ve / (P.R17 + P.R18);
            if (! shMode) cur += (vx - ve) / P.R22;   // in S/H SW1 scollega R22 dall'inviluppo
            ve += cur * Tc / P.C6;
        }

        // ---- collettore di Q3: C7 // R20, R6 // R13 verso i pin 5 (soluzione esatta)
        const double vinf = (ic + Vpin5 / Rpin5) / colGt;
        vcol = vinf + (vcol - vinf) * colA;
    }

    // Is (exp(Vx/Vt) - 1) = (Vth - Vx) / Rth, Newton da destra (funzione convessa)
    double solveEmitter (double vth, double rth, double prev) const
    {
        if (vth <= 0.0) return vth;
        const double is = P.IsQ3;
        double x = std::min (vth, Vt * std::log (vth / (rth * is) + 1.0));
        if (prev < x && prev > 0.0 && is * (std::exp (prev / Vt) - 1.0) >= (vth - prev) / rth) x = prev;
        for (int it = 0; it < 30; ++it)
        {
            const double e = is * std::exp (x / Vt);
            const double dx = (e - is - (vth - x) / rth) / (e / Vt + 1.0 / rth);
            x -= dx;
            if (std::abs (dx) < 1e-7) break;
        }
        return x;
    }

    // Vs = Vg + x, corrente nel carico (G, Iload = G Vs - i0) = Idss (1 - x / |Vp|)^2, 0 <= x <= |Vp|
    double jfetFollower (double vg, double G, double i0) const
    {
        const double p = -P.VpQ6, A = P.IdssQ6 / (G * p * p);
        const double rhs = vg - i0 / G;                  // tensione equivalente del carico
        if (rhs + p <= 0.0) return i0 / G;               // JFET interdetto
        const double bq = 2.0 * A * p + 1.0, cq = A * p * p - rhs;
        const double disc = std::max (0.0, bq * bq - 4.0 * A * cq);
        const double x = (bq - std::sqrt (disc)) / (2.0 * A);
        return std::min (vg + std::clamp (x, 0.0, p), 8.3);
    }

    // u + kappa tanh(u) = w  (ingresso di un 3080 degenerato dalla resistenza di polarizzazione).
    // Restituisce u e t = tanh(u); dudw = du/dw. Funzione monotona: 2-3 passi di Newton bastano.
    static void solveOTAInput (double w, double kappa, double& t, double& dudw)
    {
        if (kappa <= 0.0) { t = fastTanh (w); dudw = 1.0; return; }
        double u = w / (1.0 + kappa);
        for (int i = 0; i < 4; ++i)
        {
            const double tt = fastTanh (u);
            const double du = (u + kappa * tt - w) / (1.0 + kappa * (1.0 - tt * tt));
            u -= du;
            if (std::abs (du) < 1e-9) break;
        }
        t = fastTanh (u);
        dudw = 1.0 / (1.0 + kappa * (1.0 - t * t));
    }

    // stessa equazione, ma un solo passo di Newton partendo dalla soluzione precedente (u, t):
    // il segnale cambia poco fra un'iterazione e l'altra, quindi la soluzione resta agganciata
    // con una sola tanh per passo (verificato contro solveOTAInput nei test)
    static void stepOTAInput (double w, double kappa, double& u, double& t, double& dudw)
    {
        if (kappa <= 0.0) { u = w; t = fastTanh (w); dudw = 1.0; return; }
        const double inv = 1.0 / (1.0 + kappa * (1.0 - t * t));   // usata anche come derivata
        u -= (u + kappa * t - w) * inv;
        t = fastTanh (u);
        dudw = inv;
    }

    // filtro a due OTA, regola trapezoidale, Newton 2x2.
    // v1 = tensione su C3 (gate di Q2); Q2 la riporta con guadagno q2Gain.
    // Correnti di base dei 3080: Ib = Iabc (1 +- tanh) / (2 beta) per ingresso. Al pin 3 di IC1
    // escono da C5; al pin 2 di IC1 e ai pin di IC2 attraversano le resistenze di polarizzazione
    // (R4 // R5 // R7, R9 // R10, R12) e degenerano l'ingresso: u + kappa tanh(u) = w, risolto
    // in modo implicito a ogni iterazione.
    void filterStep (double vin, double iabc1, double iabc2)
    {
        const double k2 = 1.0 / (2.0 * Vt);
        // correzione della deformazione di frequenza della regola trapezoidale, calcolata
        // a ogni aggiornamento del controllo (vedi updateWarp)
        const double hw1 = h1 * warp, hw2 = h2 * warp;
        const double ib1 = iabc1 * inv2b, ib2 = iabc2 * inv2b;
        const double kap1 = Rm * ib1 * k2, kap2 = (Rc + P.R12) * ib2 * k2;
        const double off1 = Rm * ib1, off2 = (Rc - P.R12) * ib2;   // tensioni di offset (costanti)

        double x1 = 2.0 * v1 - v1p, x2 = 2.0 * v2 - v2p;   // predittore lineare
        double I1 = 0.0, I2 = 0.0, t1 = 0.0, t2 = 0.0, s1 = 1.0, s2 = 1.0;
        const double u0 = aIn * vin;
        for (int it = 0; it < 6; ++it)
        {
            OTA_INPUT ((x2 - u0 - bFb * x1 + off1) * k2, kap1, ua1, ta1, s1);
            OTA_INPUT ((-cFb * x1 + off2) * k2, kap2, ua2, ta2, s2);
            t1 = ta1; t2 = ta2;
            I1 = iabc1 * t1; I2 = iabc2 * t2;
            const double d1 = iabc1 * (1.0 - t1 * t1) * k2 * s1;    // dI1 / d(x2 - bFb x1)
            const double d2 = iabc2 * (1.0 - t2 * t2) * k2 * s2;    // dI2 / d(-cFb x1)
            const double J5 = I2 - ib1 * (1.0 + t1);                 // corrente netta in C5
            const double F1 = x1 - v1 - hw1 * (i1 + I1);
            const double F2 = x2 - v2 - hw2 * (i2 + J5);
            const double J11 = 1.0 + hw1 * d1 * bFb, J12 = -hw1 * d1;
            const double J21 = hw2 * (d2 * cFb - d1 * bFb * inv2b);
            const double J22 = 1.0 + hw2 * d1 * inv2b;
            const double det = J11 * J22 - J12 * J21;
            const double dx1 = (F1 * J22 - J12 * F2) / det;
            const double dx2 = (J11 * F2 - J21 * F1) / det;
            x1 -= dx1; x2 -= dx2;
            NEWTON_COUNT
            // convergenza misurata sull'argomento delle tanh: con Newton l'errore residuo è
            // circa il quadrato del passo, quindi sotto 3e-4 è trascurabile (< 1e-7)
            const double du = std::max (std::abs (dx2 - bFb * dx1), std::abs (cFb * dx1)) * k2;
            if (du < NEWTON_TOL) break;
        }
        // escursione: saturazione morbida verso i limiti (asimmetrici) di 3080 e Q2
        constexpr double span = 1.0;
        if (x1 > v1Hi - span)      x1 = v1Hi - span + span * fastTanh ((x1 - v1Hi + span) / span);
        else if (x1 < v1Lo + span) x1 = v1Lo + span + span * fastTanh ((x1 - v1Lo - span) / span);
        OTA_INPUT ((x2 - u0 - bFb * x1 + off1) * k2, kap1, ua1, ta1, s1);
        OTA_INPUT ((-cFb * x1 + off2) * k2, kap2, ua2, ta2, s2);
        t1 = ta1; t2 = ta2;
        I1 = iabc1 * t1; I2 = iabc2 * t2;
        double J5 = I2 - ib1 * (1.0 + t1);
        if (! std::isfinite (x1) || ! std::isfinite (x2)) { x1 = x2 = I1 = J5 = v1 = v2 = ua1 = ua2 = ta1 = ta2 = 0.0; }
        v1p = v1; v2p = v2;
        v1 = x1; v2 = x2; i1 = I1; i2 = J5;
    }

    // rumore gaussiano (xorshift + somma di uniformi)
    double gauss()
    {
        double s = 0.0;
        for (int i = 0; i < 4; ++i)
        {
            rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
            s += (double) (rng >> 11) * (1.0 / 9007199254740992.0);
        }
        return (s - 2.0) * 1.7320508;
    }

    //==========================================================================
    Parts P;
    uint32_t unitSerial = 0;
    bool prepared = false, pickupLoad = false, filterIdle = false;

    double fs = 48000.0, T = 1.0 / 48000.0, Tc = 1.0 / 48000.0;
    int div = 1, sub = 0;
    OnePoleHP hpC1, hpC2, hpC4;
    Biquad pickup;

    // comandi
    double range = 0.5, speed = 0.5;
    bool shMode = false;
    double trimOffset = 0.30, trimLevel = 0.45, noiseRms = 3.0;

    // coefficienti
    double Rm = 3.2e3, Rc = 3.2e3, inv2b = 0, v1Hi = 7.5, v1Lo = -7.5;
    bool legacy = false;
    double warp = 1.0;
    double aIn = 0, bFb = 0, cDiv = 0, cFb = 0, h1 = 0, h2 = 0, Rpin5 = 11e3, colGt = 1, colA = 0;
    double envGain = 0, clkHighInf = 0, clkHighA = 0, clkLowA = 0, clkVth = 5.4;
    double wiperRsrc = 0, sampleA = 0, trimV = 0, trimR = 1e5;
    double noiseG = 0.1, noiseK = 0.67, noiseH = 1, noiseGainNorm = 1;

    // stato
    double v1 = 0, v2 = 0, i1 = 0, i2 = 0, v1p = 0, v2p = 0, ua1 = 0, ua2 = 0, ta1 = 0, ta2 = 0;
    double ve = 0.45, vx = 0.55, ic = 0, vcol = 0;
    double iabcPrev = 0, iabcNext = 0, lastDrive = 0, groupMin = 1e9;
    double vc10 = -5.0; bool clockHigh = false;
    double vc9 = 0, ns1 = 0, ns2 = 0;
    uint64_t rng = 0x9e3779b97f4a7c15ull;
    double iabcOverride = -1.0;
};

} // namespace alea
