#include "daisy_pod.h"
#include "daisysp.h"
#include <math.h>

using namespace daisy;
using namespace daisysp;

inline float constrain(float in, int outMin, int outMax)
{
    if (in < outMin)
        return outMin;
    if (in > outMax)
        return outMax;
    return in;
}

inline float rangeToFilterFreq(int range)
{
    float exp = range * 11 / 128.0 + 5;
    exp = constrain(exp, 5, 14);
    return pow(2, exp);
}

inline bool isSaw(int wf)
{
    return wf == Oscillator::WAVE_POLYBLEP_SAW;
}

inline bool isSquare(int wf)
{
    return wf == Oscillator::WAVE_POLYBLEP_SQUARE;
}

inline long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

inline float mapf(float x, float in_min, float in_max, float out_min, float out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}