#include "daisy_seed.h"
#include "daisysp.h"
#include <stdio.h>
#include <string.h>

#define DEBUG false

#define MAX_POLYPHONY 5

#define NUM_WAVEFORMS 4
#define NUM_LFO_TARGETS 2
#define LONG_PRESS_THRESHOLD 700
#define NUM_RODS 4

#define MIN_RANGE 10.f
#define MAX_RANGE 120.f

#include "./utils.h"
#include "./RodOscillators.h"
#include "./RodSensors.h"
#include "./VoiceManager.h"
#include "./DistanceSensorManager.h"

using namespace daisy;
using namespace daisy::seed;
using namespace daisysp;

/* Breakbeams */
#define PIN_BREAKBEAM_IN_1 15
#define PIN_BREAKBEAM_IN_2 16
#define PIN_BREAKBEAM_IN_3 17
#define PIN_BREAKBEAM_IN_4 18

/* Pots */
#define PIN_POT_GAIN 19

#define PIN_POT_ATTACK 20
#define PIN_POT_DECAY 21
#define PIN_POT_SUSTAIN 22
#define PIN_POT_RELEASE 23

/* Distance Sensors */
#define TCA_IDX_1 0
#define TCA_IDX_2 1
#define TCA_IDX_3 3
#define TCA_IDX_4 2

/* Encoders */
#define PIN_ENC_1_A 3
#define PIN_ENC_1_B 4
#define PIN_ENC_1_BTN 5

#define PIN_ENC_2_A 6
#define PIN_ENC_2_B 7
#define PIN_ENC_2_BTN 8

#define PIN_ENC_3_A 25
#define PIN_ENC_3_B 26
#define PIN_ENC_3_BTN 27

#define PIN_ENC_4_A 0
#define PIN_ENC_4_B 1
#define PIN_ENC_4_BTN 2

/* Which multiplexer input maps to which rod */
uint8_t tcaIndexMap[NUM_RODS] = {
    TCA_IDX_1,
    TCA_IDX_2,
    TCA_IDX_3,
    TCA_IDX_4,
};

/* Available waveforms */
uint8_t waveforms[NUM_WAVEFORMS] = {
    Oscillator::WAVE_SIN,
    Oscillator::WAVE_POLYBLEP_TRI,
    Oscillator::WAVE_POLYBLEP_SAW,
    Oscillator::WAVE_POLYBLEP_SQUARE,
};

DaisySeed hw;
MidiUartHandler midi;
MidiUartHandler::Config midi_config;

/* Test filter */
Svf filt;

/* Polyphony voices */
static VoiceManager<MAX_POLYPHONY> voiceHandler;
Voice *voices = voiceHandler.GetVoices();

/* ADSR amplitude envelopes for each voice */
float amps[MAX_POLYPHONY];

/* DSP for each rod */
static RodOscillators<MAX_POLYPHONY> rodOscillators[NUM_RODS];

/* Sensor parsing for each rod */
static RodSensors rodSensors[NUM_RODS];

/* Managing the I2C multiplexer for the distance sensors */
DistanceSensorManager distanceSensorManager;

/* Gain */
AnalogControl gainPot;
float gain = 1.f;

float attackPotVal, decayPotVal, sustainPotVal, releasePotVal = 1.f;

int adsrMode = 1;

size_t currentPolyphony = MAX_POLYPHONY;

/* ============================================================================ */
void SetPolyphony(size_t newPolyphony)
{
    if (currentPolyphony == newPolyphony)
        return;

    currentPolyphony = newPolyphony;
    voiceHandler.SetCurrentPolyphony(newPolyphony);
    for (size_t j = 0; j < NUM_RODS; j++)
    {
        rodOscillators[j].SetCurrentPolyphony(newPolyphony);
    }
}

void NextSamples(float &sig)
{
    float result = 0.0;

    /* Get amplitude envelopes from voices */
    for (size_t i = 0; i < currentPolyphony; i++)
    {
        Voice *v = &voices[i];
        amps[i] = v->Process();
    }

    /* Pass amps to each rod */
    for (size_t i = 0; i < NUM_RODS; i++)
    {
        result += rodOscillators[i].Process(amps);
    }

    sig = result / NUM_RODS;
}

void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t size)
{
    for (size_t i = 0; i < NUM_RODS; i++)
    {
        rodSensors[i].Process();
        float rotationSpeed = rodSensors[i].GetRotationSpeed();
        int harmonic = rodSensors[i].GetEncoderVal();
        int waveform = rodSensors[i].GetWaveformIndex();

        rodOscillators[i].SetHarmonic(harmonic);
        rodOscillators[i].SetLfoFreq(rotationSpeed);
        rodOscillators[i].SetOscWaveform(waveforms[waveform]);

        /* TODO clean up */
        int addrIdx = i;
        if (i == 2)
            addrIdx = 3;
        if (i == 3)
            addrIdx = 2;

        rodOscillators[i].SetRange(distanceSensorManager.GetNormalizedRange(addrIdx));
        if (rodSensors[i].GetLongPress())
        {
            rodOscillators[i].IncrementLfoTarget();
        }
    }

    float sig;
    sig = 0.0f;
    for (size_t i = 0; i < size; i += 2)
    {
        NextSamples(sig);
        // filt.Process(sig);
        // sig = filt.Low()
        out[i] = out[i + 1] = sig * gain;
    }
}

void HandleMidiMessage(MidiEvent m)
{
    switch (m.type)
    {
    case PitchBend:
    {
        PitchBendEvent p = m.AsPitchBend();
        float divider = p.value > 0 ? 8191.f : 8192.f;
        float semiTones = 1.f;
        float fqPerSemiTone = semiTones / 12.f;
        float percent = p.value / divider;
        float fqMultiplier = pow(2.f, percent * fqPerSemiTone);
        for (size_t j = 0; j < NUM_RODS; j++)
        {
            rodOscillators[j].SetPitchBend(fqMultiplier);
        }
        break;
    }
    case NoteOn:
    {

        NoteOnEvent p = m.AsNoteOn();
        if (DEBUG)
        {
            hw.PrintLine("Note ON:\t%d\t%d\t%d\r\n", p.channel, p.note, p.velocity);
        }

        /* zero indexed */
        int channel = p.channel + 1;

        /* TODO: move to voice manager, get polyphony from voice manager */

        /* "Hack" to set ADSR based on MIDI channel */
        if (adsrMode != channel)
        {
            adsrMode = channel;
            switch (adsrMode)
            {
            case 2:
                voiceHandler.setADSR(3.f, 2.f, 0.3f, 3.f);
                SetPolyphony(MAX_POLYPHONY);
                break;
            case 3:
                voiceHandler.setADSR(0.005f, 9.f, 0.1f, 2.f);
                SetPolyphony(MAX_POLYPHONY);
                break;
            case 4:
                voiceHandler.setADSR(0.001f, 0.1f, 0.4f, 0.4f);
                SetPolyphony(1);
                break;
            case 5:
                voiceHandler.setADSR(0.003f, 0.3f, 0.1f, 0.5f);
                SetPolyphony(MAX_POLYPHONY);
                break;

            default:
                voiceHandler.setADSR(0.06f, 0.1f, 0.6f, 0.2f);
                SetPolyphony(MAX_POLYPHONY);
                break;
            }
        }

        /* Note on could have 0 velocity? */
        if (m.data[1] == 0)
            return;

        /* TODO: better way of doing this? */

        /* Get voice */
        Voice *freeVoice = voiceHandler.FindFreeVoice(p.note);
        if (freeVoice == NULL)
            return;

        /* Set note but don't trigger */
        freeVoice->OnNoteOn(p.note, p.velocity);

        /* Update oscillators with all notes (do we need to do all?) */
        /* Would need freeVoice index */
        for (size_t i = 0; i < currentPolyphony; i++)
        {
            Voice *v = &voices[i];
            float note = mtof(v->GetNote());
            for (size_t j = 0; j < NUM_RODS; j++)
            {
                rodOscillators[j].SetFundamentalFreq(note, i);
            }
        }

        /* Trigger ADSR */
        freeVoice->TriggerNote();
        break;
    }
    case NoteOff:
    {
        if (DEBUG)
        {
            hw.PrintLine("Note OFF:\t%d\t%d\t%d\r\n", m.channel, m.data[0], m.data[1]);
        }
        NoteOffEvent p = m.AsNoteOff();
        voiceHandler.OnNoteOff(p.note, p.velocity);
        break;
    }
    case ControlChange:
    {
        /* TODO: update with pitch bend */
        ControlChangeEvent p = m.AsControlChange();
        float normal = ((float)p.value / 127.0f);

        switch (p.control_number)
        {
        case 1:
            // CC 1 for cutoff.
            // filt.SetFreq(mtof((float)p.value));
            voiceHandler.SetAttack(normal * 5.f + 0.002f);
            break;
        case 2:
            voiceHandler.SetDecay(normal * 5.f + 0.05f);
            break;
        case 3:
            voiceHandler.SetSustain(normal);
            break;
        case 4:
            voiceHandler.SetRelease(normal * 5.f + 0.002f);
            break;
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}

/* =============================================================================== */

int main(void)
{
    float sample_rate;
    int count = 0;

    hw.Init();
    hw.SetAudioBlockSize(4);

    /* Serial log */
    if (DEBUG)
    {
        hw.StartLog(true);
    }
    System::Delay(200);

    sample_rate = hw.AudioSampleRate();

    /* Polyphony Voices */
    voiceHandler.Init(sample_rate);

    /* Distance sensors */
    distanceSensorManager.Init(&hw);

    /* Init Rod Oscillators */
    for (size_t i = 0; i < NUM_RODS; i++)
    {
        rodOscillators[i].Init(sample_rate);
    }

    /* Rod Sensors */
    rodSensors[0].Init(1, hw.GetPin(PIN_BREAKBEAM_IN_1), hw.GetPin(PIN_ENC_1_A), hw.GetPin(PIN_ENC_1_B), hw.GetPin(PIN_ENC_1_BTN));
    rodSensors[1].Init(2, hw.GetPin(PIN_BREAKBEAM_IN_2), hw.GetPin(PIN_ENC_2_A), hw.GetPin(PIN_ENC_2_B), hw.GetPin(PIN_ENC_2_BTN));
    rodSensors[2].Init(3, hw.GetPin(PIN_BREAKBEAM_IN_3), hw.GetPin(PIN_ENC_3_A), hw.GetPin(PIN_ENC_3_B), hw.GetPin(PIN_ENC_3_BTN));
    rodSensors[3].Init(4, hw.GetPin(PIN_BREAKBEAM_IN_4), hw.GetPin(PIN_ENC_4_A), hw.GetPin(PIN_ENC_4_B), hw.GetPin(PIN_ENC_4_BTN));

    filt.Init(sample_rate);

    /* ADC Setup */
    const int numAdcChannels = 5;
    AdcChannelConfig adcConfig[numAdcChannels];

    adcConfig[0].InitSingle(hw.GetPin(PIN_POT_GAIN));

    adcConfig[1].InitSingle(hw.GetPin(PIN_POT_ATTACK));
    adcConfig[2].InitSingle(hw.GetPin(PIN_POT_DECAY));
    adcConfig[3].InitSingle(hw.GetPin(PIN_POT_SUSTAIN));
    adcConfig[4].InitSingle(hw.GetPin(PIN_POT_RELEASE));

    hw.adc.Init(adcConfig, numAdcChannels);

    hw.adc.Start();

    /* MIDI */
    midi.Init(midi_config);

    /* Start */
    hw.StartAudio(AudioCallback);
    midi.StartReceive();

    for (;;)
    {
        /* MIDI */
        midi.Listen();
        if (midi.HasEvents())
        {
            HandleMidiMessage(midi.PopEvent());
        }

        // Set the onboard LED
        // hw.SetLed(rodSensors[0].GetPulse());

        /* Distance Sensors are slow */
        if (count >= 8000)
        {
            distanceSensorManager.UpdateRanges();
            // if (DEBUG)
            // {
            //     for (size_t i = 0; i < 4; i++)
            //     {
            //         float range = distanceSensorManager.GetNormalizedRange(i);
            //         hw.PrintLine("%d", int(range * 100.f));
            //     }
            // }
            count = 0;
        }
        count++;

        /* TODO change to AnalogControl? */
        gain = 1.f - hw.adc.GetFloat(0);
        if (gain < 0.04f)
            gain = 0.0f;

        float newAttackVal = hw.adc.GetFloat(1);
        if (abs(newAttackVal - attackPotVal) > 0.03f)
        {
            attackPotVal = newAttackVal;
            voiceHandler.SetAttack(newAttackVal * 5.f);
        }
        float newDecayVal = hw.adc.GetFloat(2);
        if (abs(newDecayVal - decayPotVal) > 0.03f)
        {
            decayPotVal = newDecayVal;
            voiceHandler.SetDecay(newDecayVal * 5.f);
        }
        float newSustainVal = hw.adc.GetFloat(3);
        if (abs(newSustainVal - sustainPotVal) > 0.03f)
        {
            sustainPotVal = newSustainVal;
            voiceHandler.SetSustain(newSustainVal);
        }
        float newReleaseVal = hw.adc.GetFloat(4);
        if (abs(newReleaseVal - releasePotVal) > 0.03f)
        {
            releasePotVal = newReleaseVal;
            voiceHandler.SetRelease(newReleaseVal * 5.f);
        }
    }
}