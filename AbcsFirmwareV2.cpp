#include "daisy_seed.h"
#include "daisysp.h"
#include <stdio.h>
#include <string.h>

#define NUM_WAVEFORMS 4
#define NUM_LFO_TARGETS 2
#define NUM_POLY_VOICES 4
#define LONG_PRESS_THRESHOLD 700
#define NUM_RODS 4

#include "./RodOscillators.h"
#include "./RodSensors.h"
#include "./VoiceManager.h"

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

uint8_t waveforms[NUM_WAVEFORMS] = {
    // Oscillator::WAVE_SQUARE,
    Oscillator::WAVE_SIN,
    Oscillator::WAVE_POLYBLEP_TRI,
    Oscillator::WAVE_POLYBLEP_SAW,
    Oscillator::WAVE_POLYBLEP_SQUARE,
    // Oscillator::WAVE_SQUARE,
};

DaisySeed hw;
MidiUartHandler midi;

float amps[NUM_POLY_VOICES];

Svf filt;

static VoiceManager<NUM_POLY_VOICES> voice_handler;
Voice *voices = voice_handler.GetVoices();

RodOscillators synthVoices[NUM_RODS];
AbcsRod abcsRods[NUM_RODS];

AnalogControl gainPot;
// Line gainLine;

float gain = 1.f;
float prevGain = gain;
float newGain = 0.0f;
// uint8_t gainLineFinished;

void InitMidi()
{
    MidiUartHandler::Config midi_config;
    midi.Init(midi_config);
}

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
        result += synthVoices[i].Process(amps);
    }

    sig = result / NUM_RODS;
}

void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t size)
{
    for (size_t i = 0; i < NUM_RODS; i++)
    {
        abcsRods[i].Process();
        float rotationSpeed = abcsRods[i].GetRotationSpeed();
        int harmonic = abcsRods[i].GetEncoderVal();
        int waveform = abcsRods[i].GetWaveformIndex();
        // hw.PrintLine("Speed %f", rotationSpeed);
        synthVoices[i].SetHarmonic(harmonic);
        synthVoices[i].SetLfoFreq(rotationSpeed);
        synthVoices[i].SetOscWaveform(waveforms[waveform]);

        /* TODO verify */
        if (abcsRods[i].GetLongPress())
        {
            synthVoices[i].IncrementLfoTarget();
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
        // This is to avoid Max/MSP Note outs for now..
        // if (m.data[1] != 0)
        // {
        //     p = m.AsNoteOn();
        //     for (size_t i = 0; i < NUM_OSCS; i++)
        //     {
        //         /* code */
        //         oscillators[i].SetFreq(mtof(p.note + i * 5));
        //         oscillators[i].SetAmp((p.velocity / 127.0f));
        //     }
        // }
        Voice *v = voice_handler.FindFreeVoice(p.note);
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
                synthVoices[j].SetFundamentalFreq(mtof(v->GetNote()), i);
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
        voice_handler.OnNoteOff(p.note, p.velocity);
        break;
    }
    case ControlChange:
    {
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

// Main -- Init, and Midi Handling
int main(void)
{
    // Init
    float sample_rate;
    hw.Init();
    hw.SetAudioBlockSize(4);

    // hw.usb_handle.Init(UsbHandle::FS_INTERNAL);

    hw.StartLog(true);
    System::Delay(250);

    sample_rate = hw.AudioSampleRate();
    // callback_rate = hw.AudioCallbackRate();
    voice_handler.Init(sample_rate);

    /* Gain */
    // gainLine.Init(sample_rate);

    /* Init Rod Oscillators */
    for (size_t i = 0; i < NUM_RODS; i++)
    {
        synthVoices[i].Init(sample_rate);
        synthVoices[i].SetOscWaveform(Oscillator::WAVE_POLYBLEP_SQUARE);
        // synthVoices[i].SetOscWaveform(0);
        synthVoices[i].SetHarmonic(i + 1);
    }

    abcsRods[0].Init(hw.GetPin(PIN_BREAKBEAM_IN_1), hw.GetPin(PIN_ENC_1_A), hw.GetPin(PIN_ENC_1_B), hw.GetPin(PIN_ENC_1_BTN));
    abcsRods[1].Init(hw.GetPin(PIN_BREAKBEAM_IN_2), hw.GetPin(PIN_ENC_2_A), hw.GetPin(PIN_ENC_2_B), hw.GetPin(PIN_ENC_2_BTN));
    abcsRods[2].Init(hw.GetPin(PIN_BREAKBEAM_IN_3), hw.GetPin(PIN_ENC_3_A), hw.GetPin(PIN_ENC_3_B), hw.GetPin(PIN_ENC_3_BTN));
    abcsRods[3].Init(hw.GetPin(PIN_BREAKBEAM_IN_4), hw.GetPin(PIN_ENC_4_A), hw.GetPin(PIN_ENC_4_B), hw.GetPin(PIN_ENC_4_BTN));

    filt.Init(sample_rate);

    /* ADC Setup */
    AdcChannelConfig adcConfig;
    adcConfig.InitSingle(hw.GetPin(PIN_POT_GAIN));

    hw.adc.Init(&adcConfig, 1);
    hw.adc.Start();

    InitMidi();

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
        hw.SetLed(abcsRods[0].GetPulse());

        /* TODO change to AnalogControl */
        gain = 1.f - hw.adc.GetFloat(0);
        if (gain < 0.04f)
            gain = 0.0f;
    }
}