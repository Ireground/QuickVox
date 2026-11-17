#pragma once
// QuickVox vocoder engine - plain C++, no JUCE dependency.
// The track's own audio is the modulator; the carrier is generated internally
// (pitch-follow, fixed note, or MIDI), so no sidechain routing is needed.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace qv
{
constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }

    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
    {
        b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb2 / a0);
        a1 = (float) (na1 / a0); a2 = (float) (na2 / a0);
    }
    void bandpass (double fs, double f, double q) noexcept
    {
        f = std::clamp (f, 10.0, fs * 0.45);
        const double w = 2.0 * kPi * f / fs, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        set (al, 0.0, -al, 1.0 + al, -2.0 * c, 1.0 - al);
    }
    void highpass (double fs, double f, double q) noexcept
    {
        const double w = 2.0 * kPi * f / fs, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        set ((1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + al, -2.0 * c, 1.0 - al);
    }
    void lowpass (double fs, double f, double q) noexcept
    {
        const double w = 2.0 * kPi * f / fs, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        set ((1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0, 1.0 + al, -2.0 * c, 1.0 - al);
    }
};

struct Params
{
    int   mode       = 0;     // 0 follow, 1 note, 2 midi
    int   note       = 48;    // C3
    int   chord      = 0;     // 0 off, 1 oct, 2 fifth, 3 major, 4 minor
    bool  snap       = true;
    int   bands      = 24;
    float formant    = 0.0f;  // semitones
    float air        = 0.35f; // 0..1
    float responseMs = 25.0f;
    float thick      = 0.2f;  // 0..1
};

class Engine
{
public:
    static constexpr int MaxBands = 40, MaxNotes = 8, MaxSaws = 3;
    enum Mode { Follow = 0, Note = 1, Midi = 2 };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        decim = std::max (1, (int) std::lround (fs / 12000.0));
        dfs = fs / decim;
        pdLp1.lowpass (fs, 1500.0, 0.707);
        pdLp2.lowpass (fs, 1500.0, 0.707);
        airHp1.highpass (fs, 4500.0, 0.707);
        airHp2.highpass (fs, 4500.0, 0.707);
        ring.assign (2048, 0.0f);
        ringPos = 0; decimCount = 0; hopCount = 0;
        curMidi = (float) p.note; tgtMidi = curMidi; detectedMidi = -1.0f;
        gate = 0.0f;
        for (auto& v : phases) for (auto& ph : v) ph = rnd01();
        rebuild (true);
    }

    void setParams (const Params& np)
    {
        const bool bandsChanged = np.bands != p.bands || std::abs (np.formant - p.formant) > 1.0e-4f;
        p = np;
        p.bands = std::clamp (p.bands, 4, MaxBands);
        if (bandsChanged) rebuild (false);
        const double rel = std::max (1.0, (double) p.responseMs) * 0.001;
        relCoef = (float) (1.0 - std::exp (-1.0 / (rel * fs)));
        attCoef = (float) (1.0 - std::exp (-1.0 / (0.0015 * fs)));
    }

    // ---- MIDI ----
    void noteOn (int n)
    {
        noteOff (n);
        if (numHeld == MaxNotes) { std::copy (held.begin() + 1, held.end(), held.begin()); --numHeld; }
        held[(size_t) numHeld++] = n;
    }
    void noteOff (int n)
    {
        for (int i = 0; i < numHeld; ++i)
            if (held[(size_t) i] == n)
            {
                std::copy (held.begin() + i + 1, held.begin() + numHeld, held.begin() + i);
                --numHeld;
                return;
            }
    }
    void allNotesOff() { numHeld = 0; }
    int  heldCount() const { return numHeld; }

    // ---- processing: mono in -> mono wet out ----
    void process (const float* in, float* out, int n)
    {
        const int nb = p.bands;
        const float glideCoef = (float) (1.0 - std::exp (-1.0 / ((p.snap ? 0.012 : 0.03) * fs)));
        const float gateA = (float) (1.0 - std::exp (-1.0 / (0.003 * fs)));
        const float gateR = (float) (1.0 - std::exp (-1.0 / (0.04 * fs)));
        const float carLevA = (float) (1.0 - std::exp (-1.0 / (0.02 * fs)));
        const float detuneCents = p.thick * 28.0f;
        const float sideLevel = p.thick * 0.75f;

        for (int s = 0; s < n; ++s)
        {
            const float x = in[s];

            // --- pitch tracking (always runs, cheap) ---
            const float lp = pdLp2.process (pdLp1.process (x));
            if (++decimCount >= decim)
            {
                decimCount = 0;
                ring[(size_t) ringPos] = lp;
                ringPos = (ringPos + 1) & (int) (ring.size() - 1);
                if (++hopCount >= 128) { hopCount = 0; detectPitch(); }
            }

            // --- carrier pitch set ---
            int nf = 0;
            std::array<float, MaxNotes> midiNotes {};
            bool sounding = true;
            if (p.mode == Midi)
            {
                for (int i = 0; i < numHeld; ++i) midiNotes[(size_t) nf++] = (float) held[(size_t) i];
                sounding = numHeld > 0;
                if (! sounding && lastNf > 0) { nf = lastNf; midiNotes = lastNotes; }   // keep ringing during release
            }
            else
            {
                tgtMidi = (p.mode == Follow && detectedMidi > 0.0f) ? detectedMidi : (float) p.note;
                if (p.mode == Note) curMidi = tgtMidi;
                else curMidi += (tgtMidi - curMidi) * glideCoef;
                static const int chordTab[5][3] = { { 0, 99, 99 }, { 0, -12, 99 }, { 0, 7, 99 }, { 0, 4, 7 }, { 0, 3, 7 } };
                const int c = std::clamp (p.chord, 0, 4);
                for (int k = 0; k < 3; ++k)
                    if (chordTab[c][k] != 99) midiNotes[(size_t) nf++] = curMidi + (float) chordTab[c][k];
            }
            if (p.mode == Midi && sounding) { lastNf = nf; lastNotes = midiNotes; }
            gate += ((sounding ? 1.0f : 0.0f) - gate) * (sounding ? gateA : gateR);

            // --- carrier oscillator (polyBLEP saws + a little noise) ---
            float car = 0.0f;
            for (int v = 0; v < nf; ++v)
            {
                const float f0 = 440.0f * std::pow (2.0f, (midiNotes[(size_t) v] - 69.0f) / 12.0f);
                auto& ph = phases[(size_t) v];
                car += saw (ph[0], f0 / (float) fs);
                if (sideLevel > 0.001f)
                {
                    const float r = std::pow (2.0f, detuneCents / 1200.0f);
                    car += sideLevel * (saw (ph[1], f0 * r / (float) fs) + saw (ph[2], f0 / r / (float) fs));
                }
            }
            car = car * 0.5f + 0.02f * (rnd01() * 2.0f - 1.0f);
            carLevel += (std::abs (car) - carLevel) * carLevA;
            const float floorLevel = 1.0e-5f + 0.01f * carLevel;

            // --- filter bank ---
            float y = 0.0f;
            for (int b = 0; b < nb; ++b)
            {
                auto& B = band[(size_t) b];
                const float m = B.m2.process (B.m1.process (x));
                const float am = std::abs (m);
                B.envM += (am - B.envM) * (am > B.envM ? attCoef : relCoef);

                const float c = B.c2.process (B.c1.process (car));
                const float ac = std::abs (c);
                B.envC += (ac - B.envC) * (ac > B.envC ? attCoef : relCoef);

                const float g = std::min (B.envM / std::max (B.envC, floorLevel), 40.0f);
                y += c * g;
            }
            y *= bandGain * gate;

            // --- air: let the original consonants/sibilance through ---
            if (p.air > 0.0f) y += airHp2.process (airHp1.process (x)) * p.air;
            else airHp2.process (airHp1.process (x));

            out[s] = y;
        }

        for (int b = 0; b < MaxBands; ++b)
            meter[(size_t) b].store (b < nb ? band[(size_t) b].envM : 0.0f, std::memory_order_relaxed);
        meterBands.store (nb, std::memory_order_relaxed);
        liveMidi.store (detectedMidi, std::memory_order_relaxed);
    }

    // GUI read-outs
    float bandMeter (int b) const { return meter[(size_t) b].load (std::memory_order_relaxed); }
    int   meterBandCount() const  { return meterBands.load (std::memory_order_relaxed); }
    float detectedNote() const    { return liveMidi.load (std::memory_order_relaxed); }

private:
    struct Band { Biquad m1, m2, c1, c2; float envM = 0, envC = 0; };

    void rebuild (bool reset)
    {
        const int nb = std::clamp (p.bands, 4, MaxBands);
        const double lo = 110.0, hi = 9000.0;
        const double r = std::pow (hi / lo, 1.0 / (nb - 1));
        const double q = std::sqrt (r) / (r - 1.0) * 1.25;   // per stage of a 2-stage cascade
        const double shift = std::pow (2.0, p.formant / 12.0);
        for (int b = 0; b < nb; ++b)
        {
            const double f = lo * std::pow (r, b);
            auto& B = band[(size_t) b];
            B.m1.bandpass (fs, f, q);       B.m2.bandpass (fs, f, q);
            B.c1.bandpass (fs, f * shift, q); B.c2.bandpass (fs, f * shift, q);
            if (reset) { B.m1.reset(); B.m2.reset(); B.c1.reset(); B.c2.reset(); B.envM = B.envC = 0; }
        }
        // overlapping bands: keep overall level near the dry vocal regardless of band count
        bandGain = (float) (1.6 * std::sqrt (24.0 / nb));
    }

    static inline float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)         { t /= dt;           return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt)  { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    static inline float saw (float& ph, float dt) noexcept
    {
        dt = std::min (dt, 0.45f);
        const float v = 2.0f * ph - 1.0f - polyBlep (ph, dt);
        ph += dt;
        if (ph >= 1.0f) ph -= 1.0f;
        return v;
    }
    inline float rnd01() noexcept
    {
        rng = rng * 1664525u + 1013904223u;
        return (float) (rng >> 8) * (1.0f / 16777216.0f);
    }

    // YIN on the decimated (~12 kHz) signal
    void detectPitch()
    {
        constexpr int W = 384;
        const int minTau = std::max (2, (int) (dfs / 1000.0));
        const int maxTau = std::min (600, (int) (dfs / 65.0));
        const int len = W + maxTau;
        const int mask = (int) ring.size() - 1;
        float buf[1024];
        double energy = 0;
        for (int i = 0; i < len; ++i)
        {
            buf[i] = ring[(size_t) ((ringPos - len + i) & mask)];
            if (i < W) energy += (double) buf[i] * buf[i];
        }
        if (std::sqrt (energy / W) < 0.004) { voiced = false; return; }   // ~ -48 dB: silence

        float d[640];
        d[0] = 1.0f;
        double run = 0;
        int best = -1;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            double sum = 0;
            for (int j = 0; j < W; ++j) { const float df = buf[j] - buf[j + tau]; sum += df * df; }
            run += sum;
            d[tau] = (float) (run > 0 ? sum * tau / run : 1.0);
        }
        for (int tau = minTau; tau < maxTau; ++tau)
            if (d[tau] < 0.15f)
            {
                while (tau + 1 < maxTau && d[tau + 1] < d[tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0) { voiced = false; return; }

        float t = (float) best;
        if (best > 1 && best < maxTau)
        {
            const float a = d[best - 1], b = d[best], c = d[best + 1];
            const float den = a - 2.0f * b + c;
            if (std::abs (den) > 1.0e-9f) t += 0.5f * (a - c) / den;
        }
        const float hz = (float) dfs / t;
        float m = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        if (p.snap) m = std::round (m);
        detectedMidi = m;
        voiced = true;
    }

    double fs = 48000.0, dfs = 12000.0;
    int decim = 4, decimCount = 0, hopCount = 0, ringPos = 0;
    std::vector<float> ring;
    Biquad pdLp1, pdLp2, airHp1, airHp2;
    bool voiced = false;
    float detectedMidi = -1.0f, curMidi = 48.0f, tgtMidi = 48.0f;

    Params p;
    std::array<Band, MaxBands> band;
    float bandGain = 1.0f, attCoef = 0.1f, relCoef = 0.001f, carLevel = 0.0f, gate = 0.0f;

    std::array<std::array<float, MaxSaws>, MaxNotes> phases {};
    std::array<int, MaxNotes> held {};
    int numHeld = 0, lastNf = 0;
    std::array<float, MaxNotes> lastNotes {};
    uint32_t rng = 0x12345678u;

    std::array<std::atomic<float>, MaxBands> meter {};
    std::atomic<int> meterBands { 24 };
    std::atomic<float> liveMidi { -1.0f };
};
} // namespace qv
