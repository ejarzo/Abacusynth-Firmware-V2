#include "daisy_seed.h"
#include "daisysp.h"

#include "./vl6180x/VL6180X_Sensor.h"

using namespace daisy;
using namespace daisy::seed;
using namespace daisysp;

#define TCAADDR 0x70

#define NUM_SENSORS 4

I2CHandle _i2c;

static constexpr I2CHandle::Config _i2c_config = {
    I2CHandle::Config::Peripheral::I2C_1,
    {
        {DSY_GPIOB, 8}, // SCL
        {DSY_GPIOB, 9}  // SDA
    },
    I2CHandle::Config::Speed::I2C_1MHZ};

class DistanceSensorManager
{
private:
    VL6180X_Sensor vl[NUM_SENSORS];
    DaisySeed *hw;
    bool isActive = false;
    uint8_t range = 0;

    void tcaselect(uint8_t i)
    {
        if (i > 7)
            return;

        uint8_t data = 1 << i;
        I2CHandle::Result i2cResult = _i2c.TransmitBlocking(TCAADDR, &data, 1, 500);

        // if (i2cResult == I2CHandle::Result::OK)
        // {
        //     hw->PrintLine("Changed to address %d", data);
        // }
        // else
        // {
        //     hw->PrintLine("tcaselect error");
        // }
    }

    void PrintAddresses()
    {
        hw->PrintLine("Scanning...");
        for (size_t i = 0; i < 8; i++)
        {
            tcaselect(i);
            int nDevices = 0;
            for (unsigned char address = 1; address < 127; address++)
            {
                if (address == TCAADDR)
                    continue;
                uint8_t testData = 0;
                I2CHandle::Result i2cResult = _i2c.TransmitBlocking(address, &testData, 1, 500);

                if (i2cResult == I2CHandle::Result::OK)
                {
                    int prAddress = (address < 16) ? 0 : address;
                    hw->PrintLine("I2C device found at address %x", prAddress);
                    nDevices++;
                }
            }
            // if (nDevices == 0)
            //     hw->PrintLine("No I2C devices found");
            // else
            //     hw->PrintLine("done");
        }
    }

public:
    DistanceSensorManager(){};
    ~DistanceSensorManager(){};
    void Init(DaisySeed *_hw)
    {
        hw = _hw;
        _i2c.Init(_i2c_config);
        // PrintAddresses();

        for (size_t i = 0; i < NUM_SENSORS; i++)
        {
            tcaselect(i);
            hw->PrintLine("Init sensor %d", i);
            vl[i].Init(_hw, &_i2c);
            bool res = vl[i].Begin();
            if (!res)
            {
                hw->PrintLine("Error setting up");
            }
        }
    };
    uint8_t GetRange(int idx)
    {
        // tcaselect(idx);
        return vl[idx].GetRange();
    }
    void UpdateRanges()
    {
        for (size_t i = 0; i < NUM_SENSORS; i++)
        {
            tcaselect(i);
            vl[i].UpdateRange();
        }
    }
};
