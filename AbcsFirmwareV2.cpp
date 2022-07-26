#include "daisy_seed.h"
#include "daisysp.h"
#include <stdio.h>
#include <string.h>

#define NUM_POLY_VOICES 5

#define NUM_WAVEFORMS 4
#define NUM_LFO_TARGETS 2
#define LONG_PRESS_THRESHOLD 700
#define NUM_RODS 4

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
static VoiceManager<NUM_POLY_VOICES> voiceHandler;
Voice *voices = voiceHandler.GetVoices();

/* ADSR amplitude envelopes for each voice */
float amps[NUM_POLY_VOICES];

/* DSP for each rod */
RodOscillators rodOscillators[NUM_RODS];

/* Sensor parsing for each rod */
RodSensors rodSensors[NUM_RODS];

/* Managing the I2C multiplexer for the distance sensors */
DistanceSensorManager distanceSensorManager;

/* Gain */
AnalogControl gainPot;
float gain = 1.f;

/* ============================================================================ */

void NextSamples(float &sig)
{
    float result = 0.0;

    /* Get amplitude envelopes from voices */
    for (size_t i = 0; i < NUM_POLY_VOICES; i++)
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
        rodOscillators[i].SetRange(distanceSensorManager.GetRange(addrIdx));

        /* TODO verify */
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

// Typical Switch case for Message Type.
void HandleMidiMessage(MidiEvent m)
{

    switch (m.type)
    {
    case NoteOn:
    {
        hw.PrintLine("Note ON:\t%d\t%d\t%d\r\n", m.channel, m.data[0], m.data[1]);
        NoteOnEvent p = m.AsNoteOn();

        /* Note on could have 0 velocity? */
        if (m.data[1] == 0)
            return;

        /* Get voice */
        Voice *v = voiceHandler.FindFreeVoice(p.note);
        if (v == NULL)
            return;

        /* Set note but don't trigger */
        v->OnNoteOn(p.note, p.velocity);

        /* Update oscillators with new note */
        for (size_t i = 0; i < NUM_POLY_VOICES; i++)
        {
            Voice *v = &voices[i];
            for (size_t j = 0; j < NUM_RODS; j++)
            {
                rodOscillators[j].SetFundamentalFreq(mtof(v->GetNote()), i);
            }
        }

        /* Trigger ADSR */
        v->TriggerNote();
        break;
    }
    case NoteOff:
    {
        hw.PrintLine("Note OFF:\t%d\t%d\t%d\r\n", m.channel, m.data[0], m.data[1]);
        NoteOffEvent p = m.AsNoteOff();
        voiceHandler.OnNoteOff(p.note, p.velocity);
        break;
    }
    case ControlChange:
    {
        /* TODO: update with pitch bend */
        ControlChangeEvent p = m.AsControlChange();
        switch (p.control_number)
        {
        case 1:
            // CC 1 for cutoff.
            filt.SetFreq(mtof((float)p.value));
            break;
        case 2:
            // CC 2 for res.
            filt.SetRes(((float)p.value / 127.0f));
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
    hw.StartLog(true);
    System::Delay(250);

    sample_rate = hw.AudioSampleRate();
    // callback_rate = hw.AudioCallbackRate();

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
    AdcChannelConfig adcConfig;
    adcConfig.InitSingle(hw.GetPin(PIN_POT_GAIN));

    hw.adc.Init(&adcConfig, 1);
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
        if (count >= 10000)
        {
            distanceSensorManager.UpdateRanges();
            // for (size_t i = 0; i < 4; i++)
            // {
            //     int range = distanceSensorManager.GetRange(i);
            //     hw.PrintLine("range %d %d", i, range);
            // }
            count = 0;
        }
        count++;

        /* TODO change to AnalogControl? */
        gain = 1.f - hw.adc.GetFloat(0);
        if (gain < 0.04f)
            gain = 0.0f;
    }
}