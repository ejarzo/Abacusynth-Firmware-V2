/*
  I2C Interface for the VL6180X ToF Sensor
  Ported from https://github.com/adafruit/Adafruit_VL6180X/blob/master/Adafruit_VL6180X.h
*/

/*
  TODO: Split into header and cpp files
*/
#include "daisy_seed.h"
#include "daisysp.h"

#define VL6180X_DEFAULT_I2C_ADDR 0x29 ///< The fixed I2C addres

///! Device model identification number
#define VL6180X_REG_IDENTIFICATION_MODEL_ID 0x000
///! Interrupt configuration
#define VL6180X_REG_SYSTEM_INTERRUPT_CONFIG 0x014
///! Interrupt clear bits
#define VL6180X_REG_SYSTEM_INTERRUPT_CLEAR 0x015
///! Fresh out of reset bit
#define VL6180X_REG_SYSTEM_FRESH_OUT_OF_RESET 0x016
///! Trigger Ranging
#define VL6180X_REG_SYSRANGE_START 0x018
///! Trigger Lux Reading
#define VL6180X_REG_SYSALS_START 0x038
///! Lux reading gain
#define VL6180X_REG_SYSALS_ANALOGUE_GAIN 0x03F
///! Integration period for ALS mode, high byte
#define VL6180X_REG_SYSALS_INTEGRATION_PERIOD_HI 0x040
///! Integration period for ALS mode, low byte
#define VL6180X_REG_SYSALS_INTEGRATION_PERIOD_LO 0x041
///! Specific error codes
#define VL6180X_REG_RESULT_RANGE_STATUS 0x04d
///! Interrupt status
#define VL6180X_REG_RESULT_INTERRUPT_STATUS_GPIO 0x04f
///! Light reading value
#define VL6180X_REG_RESULT_ALS_VAL 0x050
///! Ranging reading value
#define VL6180X_REG_RESULT_RANGE_VAL 0x062
///! I2C Slave Device Address
#define VL6180X_REG_SLAVE_DEVICE_ADDRESS 0x212

#define VL6180X_ALS_GAIN_1 0x06    ///< 1x gain
#define VL6180X_ALS_GAIN_1_25 0x05 ///< 1.25x gain
#define VL6180X_ALS_GAIN_1_67 0x04 ///< 1.67x gain
#define VL6180X_ALS_GAIN_2_5 0x03  ///< 2.5x gain
#define VL6180X_ALS_GAIN_5 0x02    ///< 5x gain
#define VL6180X_ALS_GAIN_10 0x01   ///< 10x gain
#define VL6180X_ALS_GAIN_20 0x00   ///< 20x gain
#define VL6180X_ALS_GAIN_40 0x07   ///< 40x gain

#define VL6180X_ERROR_NONE 0        ///< Success!
#define VL6180X_ERROR_SYSERR_1 1    ///< System error
#define VL6180X_ERROR_SYSERR_5 5    ///< Sysem error
#define VL6180X_ERROR_ECEFAIL 6     ///< Early convergence estimate fail
#define VL6180X_ERROR_NOCONVERGE 7  ///< No target detected
#define VL6180X_ERROR_RANGEIGNORE 8 ///< Ignore threshold check failed
#define VL6180X_ERROR_SNR 11        ///< Ambient conditions too high
#define VL6180X_ERROR_RAWUFLOW 12   ///< Raw range algo underflow
#define VL6180X_ERROR_RAWOFLOW 13   ///< Raw range algo overflow
#define VL6180X_ERROR_RANGEUFLOW 14 ///< Raw range algo underflow
#define VL6180X_ERROR_RANGEOFLOW 15 ///< Raw range algo overflow

#define SYSRANGE__INTERMEASUREMENT_PERIOD 0x001b // P19 application notes

using namespace daisy;
using namespace daisy::seed;
using namespace daisysp;

class VL6180X_Sensor
{
private:
  DaisySeed *hw;
  I2CHandle *_i2c;
  bool isActive = false;
  uint8_t range = 0;
  int numReadings = 10;
  int currIndex = 0;
  uint8_t ranges[10];

  float normalizedRange = 0.f;

  uint8_t read8(uint16_t address)
  {
    uint8_t buffer[2];
    buffer[0] = uint8_t(address >> 8);
    buffer[1] = uint8_t(address & 0xFF);

    _i2c->TransmitBlocking(VL6180X_DEFAULT_I2C_ADDR, buffer, 2, 500);
    _i2c->ReceiveBlocking(VL6180X_DEFAULT_I2C_ADDR, buffer, 1, 500);
    // if (i2cResult == I2CHandle::Result::OK)
    // {
    //     hw->PrintLine("read8 success");
    // }
    // else
    // {
    //     hw->PrintLine("read8 error");
    // }
    // i2c_dev->read(buffer, 1);

    return buffer[0];
  }
  void write8(uint16_t address, uint8_t data)
  {
    uint8_t buffer[3];
    buffer[0] = uint8_t(address >> 8);
    buffer[1] = uint8_t(address & 0xFF);
    buffer[2] = data;
    _i2c->TransmitBlocking(VL6180X_DEFAULT_I2C_ADDR, buffer, 3, 500);
    // if (i2cResult == I2CHandle::Result::OK)
    // {
    //     hw->PrintLine("write8 success");
    // }
    // else
    // {
    //     hw->PrintLine("write8 error");
    // }
    // i2c_dev->write(buffer, 3);
  }
  void startRangeContinuous(uint16_t period_ms)
  {
    uint8_t period_reg = 0;
    if (period_ms > 10)
    {
      if (period_ms < 2550)
        period_reg = (period_ms / 10) - 1;
      else
        period_reg = 254;
    }

    // Set  ranging inter-measurement
    write8(SYSRANGE__INTERMEASUREMENT_PERIOD, period_reg);

    // Start a continuous range measurement
    write8(VL6180X_REG_SYSRANGE_START, 0x03);
  }
  uint8_t readRange(void)
  {
    // wait for device to be ready for range measurement
    // while (!(read8(VL6180X_REG_RESULT_RANGE_STATUS) & 0x01))
    //   ;

    // Start a range measurement
    write8(VL6180X_REG_SYSRANGE_START, 0x01);

    // Poll until bit 2 is set
    while (!(read8(VL6180X_REG_RESULT_INTERRUPT_STATUS_GPIO) & 0x04))
      ;

    // read range in mm
    uint8_t newRange = read8(VL6180X_REG_RESULT_RANGE_VAL);

    // clear interrupt
    write8(VL6180X_REG_SYSTEM_INTERRUPT_CLEAR, 0x07);

    return newRange;
  }
  void loadSettings(void)
  {
    // load settings!

    // private settings from page 24 of app note
    write8(0x0207, 0x01);
    write8(0x0208, 0x01);
    write8(0x0096, 0x00);
    write8(0x0097, 0xfd);
    write8(0x00e3, 0x00);
    write8(0x00e4, 0x04);
    write8(0x00e5, 0x02);
    write8(0x00e6, 0x01);
    write8(0x00e7, 0x03);
    write8(0x00f5, 0x02);
    write8(0x00d9, 0x05);
    write8(0x00db, 0xce);
    write8(0x00dc, 0x03);
    write8(0x00dd, 0xf8);
    write8(0x009f, 0x00);
    write8(0x00a3, 0x3c);
    write8(0x00b7, 0x00);
    write8(0x00bb, 0x3c);
    write8(0x00b2, 0x09);
    write8(0x00ca, 0x09);
    write8(0x0198, 0x01);
    write8(0x01b0, 0x17);
    write8(0x01ad, 0x00);
    write8(0x00ff, 0x05);
    write8(0x0100, 0x05);
    write8(0x0199, 0x05);
    write8(0x01a6, 0x1b);
    write8(0x01ac, 0x3e);
    write8(0x01a7, 0x1f);
    write8(0x0030, 0x00);

    // Recommended : Public registers - See data sheet for more detail
    write8(0x0011, 0x10); // Enables polling for 'New Sample ready'
                          // when measurement completes
    write8(0x010a, 0x30); // Set the averaging sample period
                          // (compromise between lower noise and
                          // increased execution time)
    write8(0x003f, 0x46); // Sets the light and dark gain (upper
                          // nibble). Dark gain should not be
                          // changed.
    write8(0x0031, 0xFF); // sets the # of range measurements after
                          // which auto calibration of system is
                          // performed
    write8(0x0041, 0x63); // Set ALS integration time to 100ms
    write8(0x002e, 0x01); // perform a single temperature calibration
                          // of the ranging sensor

    // Optional: Public registers - See data sheet for more detail
    write8(SYSRANGE__INTERMEASUREMENT_PERIOD,
           0x09);         // Set default ranging inter-measurement
                          // period to 100ms
    write8(0x003e, 0x31); // Set default ALS inter-measurement period
                          // to 500ms
    write8(0x0014, 0x24); // Configures interrupt on 'New Sample
                          // Ready threshold event'
  }

  uint8_t readRangeStatus(void)
  {
    return (read8(VL6180X_REG_RESULT_RANGE_STATUS) >> 4);
  }
  bool begin()
  {
    uint8_t modelId = read8(VL6180X_REG_IDENTIFICATION_MODEL_ID);
    uint8_t freshOutOfReset = read8(VL6180X_REG_SYSTEM_FRESH_OUT_OF_RESET);

    // check for expected model id
    if (DEBUG)
    {
      hw->PrintLine("ID: %x", modelId);
      hw->PrintLine("Reset?: %x", freshOutOfReset);
    }

    if (modelId != 0xB4)
    {
      return false;
    }

    // fresh out of reset?
    if (freshOutOfReset & 0x01)
    {
      loadSettings();
      write8(VL6180X_REG_SYSTEM_FRESH_OUT_OF_RESET, 0x00);
    }

    return true;
  }

public:
  VL6180X_Sensor(){};
  ~VL6180X_Sensor(){};
  void Init(DaisySeed *_hw, I2CHandle *i2c)
  {
    hw = _hw;
    _i2c = i2c;
    for (size_t i = 0; i < numReadings; i++)
    {
      ranges[i] = 0;
    }
  };
  bool Begin()
  {
    bool res = begin();
    if (!res)
    {
      if (DEBUG)
      {
        hw->PrintLine("Error setting up");
      }
      return false;
    }
    // startRangeContinuous(0);
    if (DEBUG)
    {
      hw->PrintLine("setup done");
    }
    isActive = true;
    return true;
  };
  uint8_t GetRange()
  {
    return range;
  }
  float GetNormalizedRange()
  {
    int sum = 0;
    for (size_t i = 0; i < numReadings; i++)
    {
      sum += ranges[i];
    }

    float avg = sum / float(numReadings);

    float normalizedRange = constrain(avg, MIN_RANGE, MAX_RANGE);
    return mapf(normalizedRange, MIN_RANGE, MAX_RANGE, 0.f, 1.f);
  }
  void UpdateRange()
  {
    // uint8_t status = readRangeStatus();
    // hw->PrintLine("Status: %d", status);
    if (isActive)
    {
      // range = read8(VL6180X_REG_RESULT_RANGE_VAL);
      // range = readRange();

      ranges[currIndex] = readRange();
      currIndex++;
      currIndex = currIndex % numReadings;
    }
  }
};
