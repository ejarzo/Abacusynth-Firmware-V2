// #include "daisy_seed.h"
#include "daisysp.h"
#include <math.h>

// #include "Adafruit_VL6180X.h"
using namespace daisy;
using namespace daisy::seed;
using namespace daisysp;

class RodSensors
{
private:
    // int tcaIndex;
    dsy_gpio_pin pinBreakBeam_;
    dsy_gpio_pin pinEnc1_;
    dsy_gpio_pin pinEnc2_;
    dsy_gpio_pin pinEncBtn_;
    // Adafruit_VL6180X vl = Adafruit_VL6180X();

    const uint8_t PulsesPerRevolution = 3;
    const unsigned long ZeroTimeout = 500000;
    const uint8_t numReadings = 10;

    Encoder rodEncoder;
    Switch breakBeamSwitch;

    unsigned long LastTimeWeMeasured;
    unsigned long PeriodBetweenPulses = ZeroTimeout + 10;
    unsigned long PeriodAverage = ZeroTimeout + 10;
    unsigned long FrequencyRaw;
    unsigned long FrequencyReal;
    unsigned long RPM;
    unsigned int PulseCounter = 1;
    unsigned long PeriodSum;

    unsigned long LastTimeCycleMeasure = LastTimeWeMeasured;
    unsigned long CurrentMicros = (System::GetNow() * 1000);
    unsigned int AmountOfReadings = 1;
    unsigned int ZeroDebouncingExtra;
    unsigned long readings[10];
    unsigned long readIndex;
    unsigned long total;
    unsigned int average;
    unsigned int photoCellVal;

    int encoderVal = 0;
    int waveformIndex = 0;

    int prevLongPress = 0;
    int longPress = 0;
    int longPressRisingEdge = 0;

    float lightThreshold = 0.14;

    int pulse = 0;
    int PERIOD = 1;

    float k = 0.0;
    float oldk = 0.0;

    bool canUpdateWaveform = false;
    void updateVelocityAverage()
    {
        LastTimeCycleMeasure = LastTimeWeMeasured;
        CurrentMicros = (System::GetNow() * 1000);
        if (CurrentMicros < LastTimeCycleMeasure)
        {
            LastTimeCycleMeasure = CurrentMicros;
        }
        FrequencyRaw = 10000000000 / PeriodAverage;
        if (PeriodBetweenPulses > ZeroTimeout - ZeroDebouncingExtra || CurrentMicros - LastTimeCycleMeasure > ZeroTimeout - ZeroDebouncingExtra)
        {
            FrequencyRaw = 0; // Set frequency as 0.
            ZeroDebouncingExtra = 2000;
        }
        else
        {
            ZeroDebouncingExtra = 0;
        }
        FrequencyReal = FrequencyRaw / 10000;

        RPM = FrequencyRaw / PulsesPerRevolution * 60;
        RPM = RPM / 10000;
        total = total - readings[readIndex];
        readings[readIndex] = RPM;
        total = total + readings[readIndex];
        readIndex = readIndex + 1;

        if (readIndex >= numReadings)
        {
            readIndex = 0;
        }
        average = total / numReadings;
    }
    void Pulse_Event()
    {
        PeriodBetweenPulses = (System::GetNow() * 1000) - LastTimeWeMeasured;
        LastTimeWeMeasured = (System::GetNow() * 1000);

        if (PulseCounter >= AmountOfReadings)
        {
            PeriodAverage = PeriodSum / AmountOfReadings;
            PulseCounter = 1;
            PeriodSum = PeriodBetweenPulses;

            int RemapedAmountOfReadings = map(PeriodBetweenPulses, 40000, 5000, 1, 10);
            RemapedAmountOfReadings = constrain(RemapedAmountOfReadings, 1, 10);
            AmountOfReadings = RemapedAmountOfReadings;
        }
        else
        {
            PulseCounter++;
            PeriodSum = PeriodSum + PeriodBetweenPulses;
        }
    }

public:
    RodSensors(){};
    ~RodSensors(){};
    void Init(int initialHarmonic, dsy_gpio_pin pinBreakBeam, dsy_gpio_pin pinEnc1, dsy_gpio_pin pinEnc2, dsy_gpio_pin pinEncBtn)
    {
        encoderVal = initialHarmonic;
        rodEncoder.Init(pinEnc1, pinEnc2, pinEncBtn);
        breakBeamSwitch.Init(pinBreakBeam);
    };
    float GetDistance();
    float GetRotationSpeed()
    {
        return average / 60.0;
    };

    int GetPulse()
    {
        return breakBeamSwitch.Pressed();
    };

    int GetEncoderVal()
    {
        return encoderVal;
    };

    int GetWaveformIndex()
    {
        return waveformIndex;
    };

    void SetVal(float val)
    {
        k = val;
    }

    int GetLongPress()
    {
        return longPressRisingEdge;
    }

    float GetPressTime()
    {
        return rodEncoder.TimeHeldMs();
    }

    void Process()
    {
        rodEncoder.Debounce();
        breakBeamSwitch.Debounce();

        if (rodEncoder.RisingEdge())
        {
            canUpdateWaveform = true;
        }

        longPress = rodEncoder.TimeHeldMs() > LONG_PRESS_THRESHOLD;

        if (!prevLongPress && longPress)
        {
            longPressRisingEdge = true;
        }
        else
        {
            longPressRisingEdge = false;
        }

        if (longPress)
        {
            canUpdateWaveform = false;
        }
        // Serial.println(prevLongPress);
        if (canUpdateWaveform)
        {
            waveformIndex += rodEncoder.FallingEdge();
            waveformIndex = (waveformIndex % NUM_WAVEFORMS + NUM_WAVEFORMS) % NUM_WAVEFORMS;
        }

        prevLongPress = longPress;

        encoderVal -= rodEncoder.Increment();
        encoderVal = (encoderVal % 10 + 10) % 10;

        updateVelocityAverage();

        if (breakBeamSwitch.RisingEdge() || breakBeamSwitch.FallingEdge())
        {
            Pulse_Event();
        }
    }
};

float RodSensors::GetDistance()
{
    // tcaselect(tcaIndex);
    /* Todo: readStatus? */
    // Wire.beginTransmission(TCAADDR);
    // Wire.write(1 << 0);
    // Wire.endTransmission();
    // uint8_t range = vl.readRange();
    // float normal = range / 255.0;
    // if (!vl.readRangeStatus())
    // {
    //   uint8_t range = vl.readRange();
    //   float normal = range / 255.0;
    //   return normal;
    // }
    return 1.0;
}
