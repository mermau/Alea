#pragma once
#include <algorithm>
#include <cmath>

// Limitatore di picco a latenza zero: attacco istantaneo (l'uscita non supera mai la soglia),
// tenuta di 20 ms e rilascio esponenziale di 120 ms, per non modulare le note basse.
class PeakLimiter
{
public:
    void prepare (double fs)
    {
        holdLen = (int) (0.020 * fs);
        rel = (float) std::exp (-1.0 / (0.120 * fs));
        env = threshold; hold = 0;
    }
    void process (float* x, int n)
    {
        float minGain = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const float a = std::abs (x[i]);
            if (a >= env) { env = a; hold = holdLen; }
            else if (hold > 0) --hold;
            else env = std::max (a, threshold + (env - threshold) * rel);
            const float g = env > threshold ? threshold / env : 1.0f;
            x[i] *= g;
            minGain = std::min (minGain, g);
        }
        lastGain = minGain;
    }
    void idle() { lastGain = 1.0f; env = threshold; hold = 0; }
    float reductionDb() const { return -20.0f * std::log10 (std::max (1.0e-6f, lastGain)); }

private:
    static constexpr float threshold = 0.891f;   // -1 dBFS
    float env = threshold, rel = 0.9999f, lastGain = 1.0f;
    int hold = 0, holdLen = 960;
};
