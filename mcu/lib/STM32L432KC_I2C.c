// STM32L432KC_I2C.c
// Source code for I2C1 controller (master) functions.

#include "STM32L432KC.h"
#include "STM32L432KC_I2C.h"
#include "STM32L432KC_GPIO.h"

// How many polling-loop iterations to wait on a flag before giving up.
// At 80 MHz this is on the order of 10 ms, far longer than one byte at 100 kHz (~90 us).
#define I2C_TIMEOUT_LOOPS 200000UL

// Busy-wait until (I2C1->ISR & flag) is set. Returns 1 if set, 0 on timeout.
static int waitFlag(uint32_t flag) {
    uint32_t t = I2C_TIMEOUT_LOOPS;
    while (!(I2C1->ISR & flag)) {
        if (--t == 0) return 0;
    }
    return 1;
}

// If something went wrong (stuck flag), toggling PE performs a software reset
// of the I2C state machine (RM0394 37.4.6). Configuration registers are kept.
static void i2cSoftReset(void) {
    I2C1->CR1 &= ~I2C_CR1_PE;
    while (I2C1->CR1 & I2C_CR1_PE);
    I2C1->CR1 |= I2C_CR1_PE;
}

// Wait for the STOP condition that AUTOEND generates, then clear the flags.
static int finishTransfer(void) {
    if (!waitFlag(I2C_ISR_STOPF)) { i2cSoftReset(); return I2C_TIMEOUT; }
    int nack = (I2C1->ISR & I2C_ISR_NACKF) ? 1 : 0;
    I2C1->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
    return nack ? I2C_NACK : I2C_OK;
}

// Load CR2 and generate the START condition.
//   SADD   : for 7-bit addressing the address goes in bits [7:1]
//   RD_WRN : 0 = write, 1 = read
//   NBYTES : number of bytes in this transfer (max 255)
//   AUTOEND: hardware sends STOP automatically after NBYTES bytes
static void startTransfer(uint8_t addr7, uint8_t len, int read) {
    I2C1->CR2 = ((uint32_t)addr7 << 1)
              | (read ? I2C_CR2_RD_WRN : 0)
              | ((uint32_t)len << I2C_CR2_NBYTES_Pos)
              | I2C_CR2_AUTOEND
              | I2C_CR2_START;
}

void initI2C(uint32_t timing) {
    // GPIO: PA9 = SCL, PA10 = SDA, alternate function 4, open-drain, pull-ups
    gpioEnable(GPIO_PORT_A);
    pinMode(I2C_SCL, GPIO_ALT);
    pinMode(I2C_SDA, GPIO_ALT);
    GPIOA->OTYPER  |= GPIO_OTYPER_OT9 | GPIO_OTYPER_OT10;          // open-drain (required for I2C)
    GPIOA->OSPEEDR |= GPIO_OSPEEDR_OSPEED9 | GPIO_OSPEEDR_OSPEED10; // fast edges
    pinResistor(I2C_SCL, GPIO_PULL_UP);
    pinResistor(I2C_SDA, GPIO_PULL_UP);
    GPIOA->AFR[1] &= ~(GPIO_AFRH_AFSEL9 | GPIO_AFRH_AFSEL10);
    GPIOA->AFR[1] |= _VAL2FLD(GPIO_AFRH_AFSEL9, 4) | _VAL2FLD(GPIO_AFRH_AFSEL10, 4);

    // Clock: use HSI16 as I2C1 kernel clock so the TIMINGR values do not
    //    depend on whether the core runs at 4 MHz (MSI) or 80 MHz (PLL).
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY));
    RCC->CCIPR &= ~RCC_CCIPR_I2C1SEL;
    RCC->CCIPR |= _VAL2FLD(RCC_CCIPR_I2C1SEL, 0b10);  // 10 = HSI16
    RCC->APB1ENR1 |= RCC_APB1ENR1_I2C1EN;

    // Peripheral: must be disabled while TIMINGR is written
    I2C1->CR1 &= ~I2C_CR1_PE;
    I2C1->TIMINGR = timing;
    I2C1->CR1 |= I2C_CR1_PE;
}

int i2cWrite(uint8_t addr7, const uint8_t *data, uint8_t len) {
    // Wait until any previous transaction has released the bus
    uint32_t t = I2C_TIMEOUT_LOOPS;
    while (I2C1->ISR & I2C_ISR_BUSY) { if (--t == 0) { i2cSoftReset(); return I2C_TIMEOUT; } }

    startTransfer(addr7, len, 0);

    for (uint8_t i = 0; i < len; i++) {
        // TXIS = transmit register empty, ready for next byte.
        // NACKF = the target did not acknowledge -> abort.
        if (!waitFlag(I2C_ISR_TXIS | I2C_ISR_NACKF)) { i2cSoftReset(); return I2C_TIMEOUT; }
        if (I2C1->ISR & I2C_ISR_NACKF) break;
        I2C1->TXDR = data[i];
    }
    return finishTransfer();
}

int i2cRead(uint8_t addr7, uint8_t *data, uint8_t len) {
    uint32_t t = I2C_TIMEOUT_LOOPS;
    while (I2C1->ISR & I2C_ISR_BUSY) { if (--t == 0) { i2cSoftReset(); return I2C_TIMEOUT; } }

    startTransfer(addr7, len, 1);

    for (uint8_t i = 0; i < len; i++) {
        // RXNE = a received byte is waiting in RXDR
        if (!waitFlag(I2C_ISR_RXNE | I2C_ISR_NACKF)) { i2cSoftReset(); return I2C_TIMEOUT; }
        if (I2C1->ISR & I2C_ISR_NACKF) break;   // address was not acknowledged
        data[i] = (uint8_t) I2C1->RXDR;
    }
    return finishTransfer();
}

int i2cProbe(uint8_t addr7) {
    // NBYTES = 0: START, address+W, STOP. ACK -> device present, NACK -> absent.
    return i2cWrite(addr7, 0, 0) == I2C_OK;
}

void i2cPinSelfTest(uint8_t addr7, uint32_t *sclLow, uint32_t *sdaLow) {
    // Start a 0-byte write (START, address, STOP) and, while the hardware
    // sends it, sample the actual pin levels through GPIOA->IDR. If the I2C
    // peripheral really drives the pins, both lines must be seen LOW many
    // times (SCL pulses 9 times; the address 0x32 contains 0 bits on SDA).
    *sclLow = 0;
    *sdaLow = 0;
    I2C1->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
    startTransfer(addr7, 0, 0);

    uint32_t t = I2C_TIMEOUT_LOOPS;
    while (!(I2C1->ISR & I2C_ISR_STOPF) && --t) {
        uint32_t idr = GPIOA->IDR;
        if (!(idr & (1u << gpioPinOffset(I2C_SCL)))) (*sclLow)++;
        if (!(idr & (1u << gpioPinOffset(I2C_SDA)))) (*sdaLow)++;
    }
    if (t == 0) i2cSoftReset();
    I2C1->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
}
