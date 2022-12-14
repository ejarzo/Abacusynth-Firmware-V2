// #include "daisy_seed.h"
#include "daisysp.h"
#include <math.h>

using namespace daisy;
using namespace daisysp;

template <size_t max_polyphony>
class RodOscillators
{
private:
    Oscillator oscillators[max_polyphony];

    /* Fundamental frequencies for each voice */
    float oscFreqs[max_polyphony];
    /* Real frequencies after multiplied by harmonic */
    float realFreqs[max_polyphony];
    /* Depth of vibrato for each voice */
    float vibratoDepths[max_polyphony];

    Svf flt;
    // Tone flt;
    Line gainLine;

    float lfoFreq;
    float lfoDepth;
    float prevDepth;

    size_t currentPolyphony;

    uint8_t waveform;
    uint8_t lfoTarget;
    uint8_t harmonicMultiplier;

    float filterCutoff;
    float prevFilterCutoff;

    float pitchBend;
    float vibratoDepth;
    float gain;
    uint8_t gainLineFinished;

    /*
      Efficient LFO
      https://www.earlevel.com/main/2003/03/02/the-digital-state-variable-filter/
    */
    float sinZ = 0.0;
    float cosZ = 1.0;

    void UpdateOscFreqs()
    {
        for (size_t i = 0; i < currentPolyphony; i++)
        {
            float fq = oscFreqs[i] * harmonicMultiplier;
            realFreqs[i] = fq;
            /* Vibrato depth is based on frequency */
            vibratoDepths[i] = fq * 0.015f;
            oscillators[i].SetFreq(fq);
        }
    }

public:
    RodOscillators(){};
    ~RodOscillators(){};

    void Init(float sample_rate)
    {
        for (size_t i = 0; i < max_polyphony; i++)
        {
            oscFreqs[i] = 0.0f;
            realFreqs[i] = 0.0f;
            oscillators[i].Init(sample_rate);
            oscillators[i].SetAmp(1.0f);
        }

        flt.Init(sample_rate);
        gainLine.Init(sample_rate);

        currentPolyphony = max_polyphony;

        gain = 1.0f;
        lfoFreq = 0.0f;
        lfoDepth = 0.0f;
        prevDepth = 0.0f;
        pitchBend = 1.0f;

        filterCutoff = 15000;
        prevFilterCutoff = 15000;

        harmonicMultiplier = 1;

        flt.SetFreq(filterCutoff);
        flt.SetRes(0.2f);
        SetLfoTarget(1);
    }

    void Loop()
    {
        /* TODO: confirm working */
        filterCutoff = filterCutoff * 0.08 + prevFilterCutoff * 0.92;
        flt.SetFreq(filterCutoff);
        prevFilterCutoff = filterCutoff;
    }

    void SetCurrentPolyphony(size_t numVoices)
    {
        currentPolyphony = numVoices;
    }

    float Process(float amps[max_polyphony])
    {
        // iterate LFO
        sinZ = sinZ + lfoFreq * cosZ;
        cosZ = cosZ - lfoFreq * sinZ;

        // Slide LFO Depth
        lfoDepth = lfoDepth * 0.05 + prevDepth * 0.95;
        prevDepth = lfoDepth;

        if (!gainLineFinished)
        {
            gain = gainLine.Process(&gainLineFinished);
        }

        float sum = 0.0f;

        for (size_t i = 0; i < currentPolyphony; i++)
        {
            float fq = realFreqs[i];
            if (pitchBend != 1.f)
            {
                fq *= pitchBend;
            }
            /* Vibrato */
            if (lfoTarget == 0)
            {
                float vibrato = lfoDepth * vibratoDepths[i] * sinZ;
                fq += vibrato;
                oscillators[i].SetFreq(fq + vibrato);
                /* Todo reset freq (once) if not vibrato */
            }
            // oscillators[i].SetFreq(fq);
            else if (pitchBend != 1.f)
            {
                oscillators[i].SetFreq(fq);
            }
            sum += oscillators[i].Process() * amps[i];
        }

        float sig = sum;

        /* Tremolo */
        if (lfoTarget == 1)
        {
            float modSig = sinZ * 0.5F + 1.0F;
            sig = sig * (1 - lfoDepth) + (sig * modSig) * lfoDepth;
        }

        float sigOut = sig;

        /* Only filter saw and square */
        if (isSaw(waveform) || isSquare(waveform))
        {
            flt.Process(sigOut);
            sigOut = flt.Low();
        }

        return sigOut * gain;
    }

    void SetLfoTarget(int target)
    {
        lfoTarget = target;
    }

    void SetPitchBend(float fq)
    {
        pitchBend = fq;
    }

    void IncrementLfoTarget()
    {
        lfoTarget++;
        lfoTarget = (lfoTarget % NUM_LFO_TARGETS + NUM_LFO_TARGETS) % NUM_LFO_TARGETS;
        SetLfoTarget(lfoTarget);
    }

    void SetHarmonic(int harmonic)
    {
        if (harmonic == int(harmonicMultiplier))
            return;

        harmonicMultiplier = float(constrain(harmonic, 1, 10));
        UpdateOscFreqs();
    }

    void SetOscWaveform(uint8_t wf)
    {
        if (waveform == wf)
            return;

        waveform = wf;
        for (size_t i = 0; i < max_polyphony; i++)
        {
            oscillators[i].SetWaveform(waveform);

            // adjust volumes
            if (isSaw(waveform))
            {
                oscillators[i].SetAmp(0.7F);
            }

            if (isSquare(waveform))
            {
                oscillators[i].SetAmp(0.8F);
            }
        }
    }

    void SetFundamentalFreq(float freq, int target)
    {
        oscFreqs[target] = freq;
        UpdateOscFreqs();
    }

    void SetLfoFreq(float freq)
    {
        lfoFreq = freq / 10000.0;

        /* Auto set depth based on freq */
        // SetLfoDepth(fclamp(freq / 4, 0.f, 1.f));

        lfoDepth = fclamp(freq / 4, 0.f, 1.f);
    }

    void SetLfoDepth(float depth)
    {
        lfoDepth = depth * 0.05 + prevDepth * 0.95;
        prevDepth = lfoDepth;
        // vibratoDepth = lfoDepth * (realFreq * 0.05);
    }

    void SetFilterCutoff(float freq)
    {
        /* TODO smooth? */
        filterCutoff = freq;
        flt.SetFreq(freq);
    }

    /* range from 0-1 */
    void SetRange(float range)
    {
        float freq = mtof(range * 80.f + 50);
        // float freq = rangeToFilterFreq(range);
        SetFilterCutoff(freq);

        /* Filter sawtooth and square waves */
        if (isSaw(waveform) || isSquare(waveform))
        {
            SetGain(1);
        }

        /* Set gain for sine and triangle */
        else
        {
            // SetFilterCutoff(15000);
            SetGain(range);
        }

        if (range < 0.05)
        {
            SetGain(0);
        }
    }

    void SetAmp(float amp)
    {
        for (size_t i = 0; i < max_polyphony; i++)
        {
            oscillators[i].SetAmp(amp);
        }
    }
    void SetGain(float targetGain) { gainLine.Start(gain, fclamp(targetGain, 0, 1), 0.2); }
};
