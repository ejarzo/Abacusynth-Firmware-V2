/*!
 * @file VL6180X_Sensor.cpp
 *
 * @mainpage Adafruit VL6180X ToF sensor driver
 *
 * @section intro_sec Introduction
 *
 * This is the documentation for Adafruit's VL6180X driver for the
 * Arduino platform.  It is designed specifically to work with the
 * Adafruit VL6180X breakout: http://www.adafruit.com/products/3316
 *
 * These sensors use I2C to communicate, 2 pins (SCL+SDA) are required
 * to interface with the breakout.
 *
 * Adafruit invests time and resources providing this open source code,
 * please support Adafruit and open-source hardware by purchasing
 * products from Adafruit!
 *
 * @section author Author
 *
 * Written by ladyada for Adafruit Industries.
 *
 * @section license License
 *
 * BSD license, all text here must be included in any redistribution.
 *
 */

#include "./VL6180X_Sensor.h"

VL6180X_Sensor::~VL6180X_Sensor() {}
VL6180X_Sensor::VL6180X_Sensor() {}

/**************************************************************************/
/*!
    @brief  Initializes I2C interface, checks that VL6180X is found and resets
   chip.
    @param  theWire Optional pointer to I2C interface, &Wire is used by default
    @returns True if chip found and initialized, False otherwise
*/
/**************************************************************************/
bool VL6180X_Sensor::begin()
{
  uint8_t modelId = read8(VL6180X_REG_IDENTIFICATION_MODEL_ID);
  uint8_t freshOutOfReset = read8(VL6180X_REG_SYSTEM_FRESH_OUT_OF_RESET);

  // hw->PrintLine("ID: %x", modelId);
  // hw->PrintLine("Reset?: %x", freshOutOfReset);

  // check for expected model id
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

/**************************************************************************/
/*!
    @brief  Load the settings for proximity/distance ranging
*/
/**************************************************************************/

void VL6180X_Sensor::loadSettings(void)
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

/**************************************************************************/
/*!
    @brief  Single shot ranging. Be sure to check the return of {@link
   readRangeStatus} to before using the return value!
    @return Distance in millimeters if valid
*/
/**************************************************************************/

// uint8_t VL6180X_Sensor::readRange(void)
// {
//   // wait for device to be ready for range measurement
//   while (!(read8(VL6180X_REG_RESULT_RANGE_STATUS) & 0x01))
//     ;

//   // Start a range measurement
//   write8(VL6180X_REG_SYSRANGE_START, 0x01);

//   // Poll until bit 2 is set
//   while (!(read8(VL6180X_REG_RESULT_INTERRUPT_STATUS_GPIO) & 0x04))
//     ;

//   // read range in mm
//   uint8_t range = read8(VL6180X_REG_RESULT_RANGE_VAL);

//   // clear interrupt
//   write8(VL6180X_REG_SYSTEM_INTERRUPT_CLEAR, 0x07);

//   return range;
// }

/**************************************************************************/
/*!
    @brief  start Single shot ranging. The caller of this should have code
    that waits until the read completes, by either calling
    {@link waitRangeComplete} or calling {@link isRangeComplete} until it
    returns true.  And then the code should call {@link readRangeResult}
    to retrieve the range value and clear out the internal status.
    @return true if range completed.
*/
/**************************************************************************/

// bool VL6180X_Sensor::startRange(void)
// {
//   // wait for device to be ready for range measurement
//   while (!(read8(VL6180X_REG_RESULT_RANGE_STATUS) & 0x01))
//     ;

//   // Start a range measurement
//   write8(VL6180X_REG_SYSRANGE_START, 0x01);

//   return true;
// }

/**************************************************************************/
/*!
    @brief  Check to see if the range command completed.
    @return true if range completed.
*/
/**************************************************************************/

// bool VL6180X_Sensor::isRangeComplete(void)
// {

//   // Poll until bit 2 is set
//   if ((read8(VL6180X_REG_RESULT_INTERRUPT_STATUS_GPIO) & 0x04))
//     return true;

//   return false;
// }

/**************************************************************************/
/*!
    @brief  Wait until Range completed
    @return true if range completed.
*/
/**************************************************************************/

// bool VL6180X_Sensor::waitRangeComplete(void)
// {

//   // Poll until bit 2 is set
//   while (!(read8(VL6180X_REG_RESULT_INTERRUPT_STATUS_GPIO) & 0x04))
//     ;

//   return true;
// }

/**************************************************************************/
/*!
    @brief  Return results of read reqyest also clears out the interrupt
    Be sure to check the return of {@link readRangeStatus} to before using
    the return value!
    @return if range started.
*/
/**************************************************************************/

// uint8_t VL6180X_Sensor::readRangeResult(void)
// {

//   // read range in mm
//   uint8_t range = read8(VL6180X_REG_RESULT_RANGE_VAL);

//   // clear interrupt
//   write8(VL6180X_REG_SYSTEM_INTERRUPT_CLEAR, 0x07);

//   return range;
// }

/**************************************************************************/
/*!
    @brief  Start continuous ranging
    @param  period_ms Optional Period between ranges in ms.  Values will
    be rounded down to 10ms units with minimum of 10ms.  Default is 50
*/
/**************************************************************************/

void VL6180X_Sensor::startRangeContinuous(uint16_t period_ms)
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

/**************************************************************************/
/*!
    @brief stop continuous range operation.
*/
/**************************************************************************/

// void VL6180X_Sensor::stopRangeContinuous(void)
// {
//   // stop the continuous range operation, by setting the range register
//   // back to 1, Page 7 of appication notes
//   write8(VL6180X_REG_SYSRANGE_START, 0x01);
// }

/**************************************************************************/
/*!
    @brief  Request ranging success/error message (retreive after ranging)
    @returns One of possible VL6180X_ERROR_* values
*/
/**************************************************************************/

// uint8_t VL6180X_Sensor::readRangeStatus(void)
// {
//   return (read8(VL6180X_REG_RESULT_RANGE_STATUS) >> 4);
// }

/**************************************************************************/
/*!
    @brief  I2C low level interfacing
*/
/**************************************************************************/

// Read 1 byte from the VL6180X at 'address'
uint8_t VL6180X_Sensor::read8(uint16_t address)
{
  uint8_t buffer[2];
  buffer[0] = uint8_t(address >> 8);
  buffer[1] = uint8_t(address & 0xFF);

  _i2c->TransmitBlocking(VL6180X_DEFAULT_I2C_ADDR, buffer, 2, 500);
  // i2c_dev->write(buffer, 2);

  I2CHandle::Result i2cResult = _i2c->ReceiveBlocking(VL6180X_DEFAULT_I2C_ADDR, buffer, 1, 500);
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

// Read 2 byte from the VL6180X at 'address'
// uint16_t VL6180X_Sensor::read16(uint16_t address)
// {
//   uint8_t buffer[2];
//   buffer[0] = uint8_t(address >> 8);
//   buffer[1] = uint8_t(address & 0xFF);
//   i2c_dev->write(buffer, 2);
//   i2c_dev->read(buffer, 2);
//   return uint16_t(buffer[0]) << 8 | uint16_t(buffer[1]);
// }

// write 1 byte
void VL6180X_Sensor::write8(uint16_t address, uint8_t data)
{
  uint8_t buffer[3];
  buffer[0] = uint8_t(address >> 8);
  buffer[1] = uint8_t(address & 0xFF);
  buffer[2] = data;
  I2CHandle::Result i2cResult = _i2c->TransmitBlocking(VL6180X_DEFAULT_I2C_ADDR, buffer, 3, 500);
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

// write 2 bytes
// void VL6180X_Sensor::write16(uint16_t address, uint16_t data)
// {
//   uint8_t buffer[4];
//   buffer[0] = uint8_t(address >> 8);
//   buffer[1] = uint8_t(address & 0xFF);
//   buffer[2] = uint8_t(data >> 8);
//   buffer[3] = uint8_t(data & 0xFF);
//   i2c_dev->write(buffer, 4);
// }
