#pragma once
// Modello circuitale del Filter / Sample-Hold (schema di riferimento JD Sleep 2001/05/14).
// Nessuna dipendenza da JUCE: compilabile anche nei test.
//
// Blocchi (sigle dello schema):
//   ingresso     Q1 inseguitore, C1/R2 e C2 (passa-alto)
//   inviluppo    R15 (FILTER RANGE), R16, IC3 741 raddrizzatore di precisione (D2/D3, R17),
//                R18/C6 carica, R19 scarica
//   S/H          Q4 rumore a valanga -> IC4 (C8/R25) -> R27 trim -> Q5 interruttore JFET -> C9
//                -> Q6 inseguitore; clock IC5 (R28 S/H SPEED + R29, C10, D1/R32, R33/R34)
//   SW1          sceglie inviluppo o S/H; nel modo S/H collega anche R23 al trim R26
//   Q3           sorgente di corrente a base comune (R21, R22, R23), C7/R20 sul collettore,
//                R6/R13 verso i pin 5 (Iabc) dei due 3080
//   filtro       IC1 e IC2 CA3080 integratori (C3, C5), Q2 inseguitore; R4/R5/R7 e R9/R10:
//                passa-banda a variabili di stato, uscita da Q2 tramite R11/C4
//   SW2          footswitch: uscita diretta (dopo il buffer) o filtrata
//
// Unità: volt, ampere, secondi.

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace alearef
{

// potenziometro logaritmico (curva A, 15 % a metà corsa)
inline double audioTaper (double p)
{
    constexpr double b = 32.111;   // ((1 - 0.15) / 0.15)^2
    p = std::clamp (p, 0.0, 1.0);
    return (std::pow (b, p) - 1.0) / (b - 1.0);
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

class FSHCircuit
{
public:
    //==========================================================================
    // componenti
    static constexpr double Vt   = 0.02585;  // tensione termica (300 K)
    static constexpr double Vrail = 8.0;      // escursione dei 741 alimentati a +-9 V

    static constexpr double R4 = 100e3, R5 = 3.3e3, R7 = 1e6;
    static constexpr double R9 = 100e3, R10 = 3.3e3;
    static constexpr double C3 = 0.001e-6, C5 = 0.03e-6;

    static constexpr double R15 = 100e3, R16 = 22e3, R17 = 1e6, R18 = 3.3e3, R19 = 100e3;
    static constexpr double C6 = 4.7e-6;

    static constexpr double R21 = 1e6, R22 = 22e3, R23 = 100e3, R26 = 100e3;
    static constexpr double R20 = 2.7e6, R6 = 22e3, R13 = 22e3, C7 = 0.15e-6;
    static constexpr double Vpin5 = -8.0 + 0.55;      // ingresso Iabc dei 3080 (V- + Vbe)
    static constexpr double IsQ3 = 2.0e-14, alphaQ3 = 0.995;

    static constexpr double R27 = 10e3, C9 = 0.047e-6, RonQ5 = 250.0, VpQ5 = -2.5;
    static constexpr double R31 = 22e3, IdssQ6 = 6e-3, VpQ6 = -2.5;

    static constexpr double R28 = 2e6, R29 = 330e3, R32 = 3.3e3, R33 = 22e3, R34 = 47e3;
    static constexpr double C10 = 0.15e-6, Vd1 = 0.6;

    //==========================================================================
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        T = 1.0 / fs;

        // ingresso: C1 .047u su R2 100k (34 Hz), C2 .15u sul carico R4+R5 // R15 (~51k, 21 Hz)
        hpC1.prepare (fs, 1.0 / (2.0 * pi * 100e3 * 0.047e-6));
        hpC2.prepare (fs, 1.0 / (2.0 * pi * 51e3 * 0.15e-6));
        // uscita: C4 .15u su R14 100k // ingresso ampli 1M
        hpC4.prepare (fs, 1.0 / (2.0 * pi * 90.9e3 * 0.15e-6));

        // rumore: IC4 derivatore (C8 .047u, R25 1M) limitato dal prodotto banda-guadagno
        // del 741 (1 MHz): passa-banda centrato a sqrt(GBW / (R25 C8)) ~ 1.84 kHz, Q ~ 1.5
        const double fn = std::sqrt (1.0e6 / (2.0 * pi * 1e6 * 0.047e-6));
        noiseG = std::tan (pi * std::min (fn, 0.45 * fs) / fs);
        noiseK = 1.0 / 1.5;
        // banda equivalente di rumore del passa-banda a guadagno unitario: pi f0 / (2 Q);
        // si normalizza perché il valore efficace non dipenda dalla frequenza di campionamento
        noiseGainNorm = std::sqrt (fs / (pi * std::min (fn, 0.45 * fs) * noiseK));

        reset();
    }

    void reset()
    {
        hpC1.reset(); hpC2.reset(); hpC4.reset();
        v1 = v2 = i1 = i2 = 0.0;
        ve = 0.45; vx = 0.55;
        vc10 = -5.0; clockHigh = false;
        vc9 = 0.0; ns1 = ns2 = 0.0;
        // collettore di Q3 al riposo
        ic = 4e-6;
        vcol = (ic + Vpin5 / Rpin5) / (1.0 / R20 + 1.0 / Rpin5);
        settleControl = true;
    }

    // comandi del frontale (0..1) e trimmer interni (0..1)
    void setControls (double filterRange, double shSpeed, bool sampleHold)
    {
        range = std::clamp (filterRange, 0.0, 1.0);
        speed = std::clamp (shSpeed, 0.0, 1.0);
        shMode = sampleHold;
    }
    void setTrims (double offsetR26, double levelR27, double noiseQ4)
    {
        trimOffset = std::clamp (offsetR26, 0.0, 1.0);
        trimLevel  = std::clamp (levelR27, 0.0, 1.0);
        noiseRms   = std::max (0.0, noiseQ4);
    }

    struct Out { double wet, dry; };

    // un campione di ingresso (volt) -> uscita filtrata e diretta (volt)
    Out process (double vinRaw)
    {
        // Q1 e condensatori d'ingresso
        const double vin = hpC2.process (hpC1.process (0.985 * vinRaw));

        updateControl (vin);
        const double iabc = iabcOverride >= 0.0 ? iabcOverride
                                                : std::max (0.0, (vcol - Vpin5) / R6);   // per ciascun 3080
        lastIabc = iabc;
        filterStep (vin, iabc);

        return { hpC4.process (v1), vin };
    }

    // solo per i test: forza la corrente Iabc (valori negativi = circuito normale)
    void setIabcOverride (double a) { iabcOverride = a; }

    // stato per l'interfaccia
    bool   clockIsHigh() const    { return clockHigh; }
    double getIabc() const        { return lastIabc; }
    double getEnvelope() const    { return ve; }
    // frequenza centrale del passa-banda (piccolo segnale) per una data Iabc
    static double centreHz (double iabc)
    {
        const double gm = iabc / (2.0 * Vt);
        return gm * std::sqrt (cDiv / (C3 * C5)) / (2.0 * pi);
    }
    // frequenza del clock S/H per una posizione di S/H SPEED
    static double clockHz (double speedPos)
    {
        const double beta = R34 / (R33 + R34), vth = beta * Vrail;
        const double rs = R29 + R28 * audioTaper (1.0 - speedPos);
        const double tLow = rs * C10 * std::log ((Vrail + vth) / (Vrail - vth));
        const double rp = 1.0 / (1.0 / rs + 1.0 / R32);   // fase alta: D1/R32 in parallelo
        const double vinf = (Vrail / rs + (Vrail - Vd1) / R32) * rp;
        const double tHigh = rp * C10 * std::log ((vinf + vth) / (vinf - vth));
        return 1.0 / (tLow + tHigh);
    }

private:
    static constexpr double pi = 3.14159265358979323846;
    static constexpr double Rpin5 = 1.0 / (1.0 / R6 + 1.0 / R13);
    // partitori agli ingressi degli OTA
    static constexpr double Gm  = 1.0 / R4 + 1.0 / R7 + 1.0 / R5;
    static constexpr double aIn = (1.0 / R4) / Gm;        // ingresso -> pin 2 di IC1
    static constexpr double bFb = (1.0 / R7) / Gm;        // Q2 -> pin 2 di IC1 (smorzamento)
    static constexpr double cDiv = R10 / (R9 + R10);      // Q2 -> pin 2 di IC2

    //==========================================================================
    void updateControl (double vin)
    {
        // ---- inviluppo: IC3 raddrizzatore invertente, uscita positiva sulle semionde negative
        const double k = audioTaper (range);
        const double rw = k * (1.0 - k) * R15;                    // impedenza del cursore
        const double gain = R17 / (R16 + rw);
        const double target = std::min (-gain * k * vin, Vrail - Vd1);

        // ---- clock IC5: trigger di Schmitt con C10
        {
            const double vo = clockHigh ? Vrail : -Vrail;
            const double rs = R29 + R28 * audioTaper (1.0 - speed);
            double cur = (vo - vc10) / rs;
            if (vo - vc10 > Vd1) cur += (vo - vc10 - Vd1) / R32;
            vc10 += cur * T / C10;
            const double vth = R34 / (R33 + R34) * vo;
            if (clockHigh ? (vc10 >= vth) : (vc10 <= vth)) clockHigh = ! clockHigh;
        }

        // ---- rumore: Q4 (bianco) -> IC4 passa-banda, saturazione ai binari
        {
            const double w = gauss() * noiseRms * noiseGainNorm;
            const double g = noiseG, kk = noiseK;
            const double hp = (w - (kk + g) * ns1 - ns2) / (1.0 + g * (kk + g));
            const double bp = g * hp + ns1; ns1 = g * hp + bp;
            const double lp = g * bp + ns2; ns2 = g * bp + lp;
            const double vn = Vrail * std::tanh (bp * kk / Vrail);
            // R27: cursore e sua impedenza; Q5 conduce durante l'impulso alto del clock
            const double vw = trimLevel * vn;
            const double rsrc = trimLevel * (1.0 - trimLevel) * R27 + 75.0;
            double ron = -1.0;
            if (clockHigh) ron = RonQ5;
            else
            {
                const double vg = -Vrail;                          // gate tramite R30
                const double vgs = vg - std::min (vw, vc9);
                if (vgs > VpQ5) ron = RonQ5 / (1.0 - vgs / VpQ5);  // conduzione parziale
            }
            if (ron > 0.0)
                vc9 += (vw - vc9) * (1.0 - std::exp (-T / (C9 * (ron + rsrc))));
        }

        // ---- Q6 inseguitore di source (legge quadratica), carico R31 verso -9 V
        const double vsh = jfetFollower (vc9);

        // ---- nodo di emettitore di Q3
        double g = 1.0 / R21, iin = 9.0 / R21;
        const double vsrc = shMode ? vsh : ve;
        g += 1.0 / R22; iin += vsrc / R22;
        if (shMode)
        {
            const double vtr = -9.0 + 18.0 * trimOffset;
            const double r = R23 + trimOffset * (1.0 - trimOffset) * R26;
            g += 1.0 / r; iin += vtr / r;
        }
        const double vth = iin / g, rth = 1.0 / g;
        vx = solveEmitter (vth, rth, vx);
        const double ie = std::max (0.0, (vth - vx) / rth);
        ic = alphaQ3 * ie;

        // ---- C6: carica da IC3 tramite R18 (D3 in conduzione), scarica su R19 e su R22
        {
            double cur = -ve / R19;
            if (target > ve) cur += (target - ve) / R18;
            else             cur -= ve / (R17 + R18);
            if (! shMode) cur += (vx - ve) / R22;
            else          cur += 0.0;   // SW1 in S/H: R22 scollegata dall'inviluppo
            ve += cur * T / C6;
        }

        // ---- collettore di Q3: C7 // R20, R6 // R13 verso i pin 5 (soluzione esatta)
        {
            const double gt = 1.0 / R20 + 1.0 / Rpin5;
            const double vinf = (ic + Vpin5 / Rpin5) / gt;
            if (settleControl) { vcol = vinf; settleControl = false; }
            vcol = vinf + (vcol - vinf) * std::exp (-T * gt / C7);
        }
    }

    // Is (exp(Vx/Vt) - 1) = (Vth - Vx) / Rth, Newton da destra (funzione convessa)
    static double solveEmitter (double vth, double rth, double prev)
    {
        if (vth <= 0.0) return vth;
        auto fx = [=] (double x) { return IsQ3 * (std::exp (x / Vt) - 1.0) - (vth - x) / rth; };
        double x = std::min (vth, Vt * std::log (vth / (rth * IsQ3) + 1.0));
        // partenza dal valore precedente se è ancora a destra della radice (meno iterazioni)
        if (prev < x && prev > 0.0 && fx (prev) >= 0.0) x = prev;
        for (int it = 0; it < 30; ++it)
        {
            const double e = IsQ3 * std::exp (x / Vt);
            const double f = e - IsQ3 - (vth - x) / rth;
            const double d = e / Vt + 1.0 / rth;
            const double dx = f / d;
            x -= dx;
            if (std::abs (dx) < 1e-7) break;
        }
        return x;
    }

    // Vs = Vg + x,  (Vg + x + 9) / R31 = Idss (1 - x / |Vp|)^2,  0 <= x <= |Vp|
    static double jfetFollower (double vg)
    {
        const double p = -VpQ6, A = IdssQ6 * R31 / (p * p);
        const double rhs = vg + 9.0;          // A (p - x)^2 = x + rhs
        if (rhs <= 0.0) return -9.0;
        // A x^2 - (2 A p + 1) x + (A p^2 - rhs) = 0, radice minore
        const double bq = 2.0 * A * p + 1.0, cq = A * p * p - rhs;
        const double disc = std::max (0.0, bq * bq - 4.0 * A * cq);
        const double x = (bq - std::sqrt (disc)) / (2.0 * A);
        return std::min (vg + std::clamp (x, 0.0, p), 8.3);
    }

    // filtro a due OTA, regola trapezoidale, Newton 2x2
    void filterStep (double vin, double iabc)
    {
        const double h1 = T / (2.0 * C3), h2 = T / (2.0 * C5), k2 = 1.0 / (2.0 * Vt);
        double x1 = v1 + 2.0 * h1 * i1, x2 = v2 + 2.0 * h2 * i2;   // predittore
        double I1 = 0.0, I2 = 0.0;
        for (int it = 0; it < 6; ++it)
        {
            const double t1 = std::tanh ((x2 - aIn * vin - bFb * x1) * k2);
            const double t2 = std::tanh ((-cDiv * x1) * k2);
            I1 = iabc * t1; I2 = iabc * t2;
            const double d1 = iabc * (1.0 - t1 * t1) * k2;
            const double d2 = iabc * (1.0 - t2 * t2) * k2;
            const double F1 = x1 - v1 - h1 * (i1 + I1);
            const double F2 = x2 - v2 - h2 * (i2 + I2);
            const double J11 = 1.0 + h1 * d1 * bFb, J12 = -h1 * d1, J21 = h2 * d2 * cDiv;
            const double det = J11 - J12 * J21;
            const double dx1 = (F1 - J12 * F2) / det;
            const double dx2 = (J11 * F2 - J21 * F1) / det;
            x1 -= dx1; x2 -= dx2;
            if (std::abs (dx1) < 1e-7 && std::abs (dx2) < 1e-9) break;
        }
        // escursione dell'uscita del 3080 / di Q2 (+-9 V meno le cadute)
        constexpr double knee = 6.5, span = 1.0;
        if (std::abs (x1) > knee)
            x1 = std::copysign (knee + span * std::tanh ((std::abs (x1) - knee) / span), x1);
        // correnti coerenti con lo stato finale
        I1 = iabc * std::tanh ((x2 - aIn * vin - bFb * x1) * k2);
        I2 = iabc * std::tanh ((-cDiv * x1) * k2);
        if (! std::isfinite (x1) || ! std::isfinite (x2)) { x1 = x2 = I1 = I2 = 0.0; }
        v1 = x1; v2 = x2; i1 = I1; i2 = I2;
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
        return (s - 2.0) * 1.7320508;   // varianza unitaria
    }

    //==========================================================================
    double fs = 48000.0, T = 1.0 / 48000.0;
    OnePoleHP hpC1, hpC2, hpC4;

    double range = 0.5, speed = 0.5;
    bool shMode = false;
    double trimOffset = 0.5, trimLevel = 0.5, noiseRms = 3.0;

    double v1 = 0.0, v2 = 0.0, i1 = 0.0, i2 = 0.0;   // filtro
    double ve = 0.45, vx = 0.55, ic = 0.0, vcol = 0.0, lastIabc = 0.0;
    double vc10 = -5.0; bool clockHigh = false;
    double vc9 = 0.0, ns1 = 0.0, ns2 = 0.0, noiseG = 0.1, noiseK = 0.67;
    // il passa-banda del rumore ha guadagno di picco 1/k: si normalizza a valore efficace ~ noiseRms
    double noiseGainNorm = 1.0;
    uint64_t rng = 0x9e3779b97f4a7c15ull;
    bool settleControl = true;
    double iabcOverride = -1.0;
};

} // namespace alearef
