// main.c
// HuskyLens -> STM32L432KC over I2C (polled), Object Tracking mode.
// Ivan Jimenez Pineda and Ken Tu, E155 Final Project
//
// Wiring:
//   HuskyLens T (= SDA in I2C mode) -> PA10
//   HuskyLens R  (= SCL in I2C mode) -> PA9  
//   HuskyLens -  -> GND
//   HuskyLens +  -> 5V
// On the HuskyLens: General Settings -> Protocol Type -> I2C, then save.
//
// Object Tracking: point the HuskyLens at the object, press the "learning"
// button (top-right button) so a box with "ID1" appears. Only learned objects
// are tracked, so you get at most one box with id = 1.

#include <stdio.h>
#include "STM32L432KC.h"
#include "STM32L432KC_I2C.h"
#include "huskylens.h"

#define START_ALGORITHM HUSKY_ALG_OBJECT_TRACKING
#define LOOP_PERIOD_MS  50      // 20 Hz polling (spec: at least 10 Hz)
#define DELAY_TIM       TIM15


HuskyResult_t         husky;          // full result of the latest request
volatile HuskyBlock_t target;         // tracked bounding box this frame
volatile int          targetFound;    // 1 if a box was seen
volatile int16_t      targetErrorX;   // + = target is right of center
volatile int16_t      targetErrorY;   // + = target is below center
volatile uint32_t     goodReads;      // successful requests
volatile uint32_t     badReads;       // failed requests
volatile int          lastError;      // last HUSKY_ERR_* code

// Pick the largest box 
static int pickLargest(const HuskyResult_t *r) {
    int best = -1;
    int32_t bestArea = -1;
    for (int i = 0; i < r->numBlocks; i++) {
        int32_t area = (int32_t) r->blocks[i].width * r->blocks[i].height;
        if (area > bestArea) { bestArea = area; best = i; }
    }
    return best;
}

// Explain a HUSKY_ERR_I2C in words.
static void printI2CError(void) {
    printf("  I2C %s while %s\n",
           (huskyLastI2C == I2C_NACK) ? "NACK (no device answered at 0x32)"
                                      : "TIMEOUT (bus stuck low / no pull-ups)",
           (huskyLastStage == HUSKY_STAGE_WRITE) ? "WRITING the request"
                                                 : "READING the reply");
}

// Startup checks: idle line levels and an I2C bus scan.
static void i2cDiagnostics(void) {
    //   reads 1 -> a 4.7k pull-up (or the powered HuskyLens)
    //   reads 0 -> this pin is not connected to anything pulled up
    pinResistor(I2C_SCL, GPIO_PULL_DOWN);
    pinResistor(I2C_SDA, GPIO_PULL_DOWN);
    delay_millis(DELAY_TIM, 1);
    int scl = digitalRead(I2C_SCL);
    int sda = digitalRead(I2C_SDA);
    pinResistor(I2C_SCL, GPIO_PULL_UP);
    pinResistor(I2C_SDA, GPIO_PULL_UP);
    printf("Pull-up check: SCL(PA9)=%d SDA(PA10)=%d  %s\n", scl, sda,
           (scl && sda) ? "(good: both pins see an external pull-up)"
                        : "(BAD: a 0 means that pin has no external pull-up)");

    // Does the STM32 actually drive the pins?
    uint32_t sclLow, sdaLow;
    i2cPinSelfTest(HUSKY_I2C_ADDR, &sclLow, &sdaLow);
    printf("Pin self-test: SCL low %lu times, SDA low %lu times  %s\n",
           (unsigned long) sclLow, (unsigned long) sdaLow,
           (sclLow && sdaLow) ? "(good: STM32 side works, problem is the HuskyLens side)"
                              : "(BAD: STM32 is not driving the pin(s) that read 0)");

    // Try every 7-bit address and print the ones that ACK.
    printf("Scanning I2C bus...\n");
    int found = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (i2cProbe(a)) {
            printf("  device at 0x%02X%s\n", a, (a == HUSKY_I2C_ADDR) ? "  <- HuskyLens" : "");
            found++;
        }
    }
    if (found == 0) {
        printf("  nothing found: SDA/SCL swapped? no common GND? Protocol Type not I2C?\n");
    }
}

int main(void) {
    // System setup: 80 MHz clock, timer for delays
    configureFlash();                          // 4 wait states needed at 80 MHz
    configureClock();                          // PLL -> 80 MHz
    RCC->APB2ENR |= RCC_APB2ENR_TIM15EN;
    initTIM(DELAY_TIM);

    // I2C setup (HuskyLens is specified for 100 kHz)
    initI2C(I2C_TIMING_100KHZ);
    delay_millis(DELAY_TIM, 1000);             // give the HuskyLens time to boot

    i2cDiagnostics();

    // Make sure the HuskyLens answers 
    printf("Knocking on HuskyLens at 0x%02X...\n", HUSKY_I2C_ADDR);
    while ((lastError = huskyKnock()) != HUSKY_OK) {
        printf("  no answer (err %d)\n", lastError);
        if (lastError == HUSKY_ERR_I2C) printI2CError();
        delay_millis(DELAY_TIM, 1000);
    }
    printf("HuskyLens connected!\n");

    // Select Object Tracking from the MCU
    lastError = huskySetAlgorithm(START_ALGORITHM);
    printf("Set algorithm %d (object tracking): %s\n", START_ALGORITHM,
           (lastError == HUSKY_OK) ? "OK" : "failed");

    // Main loop
    // if (!huskylens.request())  -> "Fail to request data"
    // else if (!huskylens.isLearned()) -> "Nothing learned"
    // else if (!huskylens.available()) -> "No block or arrow"
    // else print every block / arrow
    while (1) {
        int r = huskyRequestAll(&husky);          // COMMAND_REQUEST (0x20)

        if (r != HUSKY_OK) {
            badReads++;
            lastError = r;
            printf("Fail to request data from HUSKYLENS, recheck the connection! (err %d)\n", r);
            if (r == HUSKY_ERR_I2C) printI2CError();
        } else if (husky.numLearned == 0) {
            goodReads++;
            targetFound = 0;
            printf("Nothing learned, press learn button on HUSKYLENS to learn one!\n");
        } else if (husky.numBlocks == 0 && husky.numArrows == 0) {
            goodReads++;
            targetFound = 0;
            printf("No block or arrow appears on the screen!\n");
        } else {
            goodReads++;
            for (int i = 0; i < husky.numBlocks; i++) {
                HuskyBlock_t *b = &husky.blocks[i];
                printf("Block:xCenter=%d,yCenter=%d,width=%d,height=%d,ID=%d\n",
                       b->xCenter, b->yCenter, b->width, b->height, b->id);
            }
            for (int i = 0; i < husky.numArrows; i++) {
                HuskyArrow_t *a = &husky.arrows[i];
                printf("Arrow:xOrigin=%d,yOrigin=%d,xTarget=%d,yTarget=%d,ID=%d\n",
                       a->xOrigin, a->yOrigin, a->xTarget, a->yTarget, a->id);
            }

            // Tracking target for the robot: largest box, error from frame center
            int i = pickLargest(&husky);
            targetFound = (i >= 0);
            if (targetFound) {
                target       = husky.blocks[i];
                targetErrorX = target.xCenter - HUSKY_FRAME_W / 2;
                targetErrorY = target.yCenter - HUSKY_FRAME_H / 2;
            }
        }

        delay_millis(DELAY_TIM, LOOP_PERIOD_MS);
    }
}
