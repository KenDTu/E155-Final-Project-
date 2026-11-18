// STM32L432KC_I2C.h
// Header for I2C1 (controller/master mode) functions.
//
// Pins (Nucleo-32 board labels in parentheses):
// PA9  -> I2C1_SCL (D1)   alternate function AF4
// PA10 -> I2C1_SDA (D0)   alternate function AF4
// I2C is open-drain: BOTH lines need pull-up resistors

#ifndef STM32L4_I2C_H
#define STM32L4_I2C_H

#include <stdint.h>
#include <stm32l432xx.h>


// Definitions

#define I2C_SCL PA9
#define I2C_SDA PA10

// Values for TIMINGR when the I2C kernel clock is HSI16 (16 MHz).
// From RM0394 Table "Examples of timing settings for fI2CCLK = 16 MHz".
#define I2C_TIMING_100KHZ 0x30420F13  // PRESC=3, SCLDEL=4, SDADEL=2, SCLH=0x0F, SCLL=0x13
#define I2C_TIMING_400KHZ 0x10320309  // PRESC=1, SCLDEL=3, SDADEL=2, SCLH=0x03, SCLL=0x09

// Return codes
#define I2C_OK       0
#define I2C_NACK    -1   // device did not acknowledge (wrong address / not connected)
#define I2C_TIMEOUT -2   // a flag never set (bus stuck, missing pull-ups, ...)


// Function prototypes

//Configures PA9/PA10 as I2C1, clocks I2C1 from HSI16 and enables it.
void initI2C(uint32_t timing);

// Blocking write of len bytes to a 7-bit address (START, addr+W, data..., STOP).
int i2cWrite(uint8_t addr7, const uint8_t *data, uint8_t len);

// Blocking read of len bytes from a 7-bit address (START, addr+R, data..., STOP).
int i2cRead(uint8_t addr7, uint8_t *data, uint8_t len);

// Returns 1 if a device ACKs its address (0-byte write). Useful for a bus scan.
int i2cProbe(uint8_t addr7);

// Diagnostic: sends START + address + STOP and counts how many times the
// SCL and SDA pins were read LOW meanwhile. Both counts should be > 0;
// a count of 0 means the STM32 is not driving that pin. */
void i2cPinSelfTest(uint8_t addr7, uint32_t *sclLow, uint32_t *sdaLow);

#endif
