# MCU (STM32L432KC) – HuskyLens over I2C

## Wiring

| HuskyLens (I2C cable) | STM32 / Nucleo-32 |
|---|---|
| T (SDA in I2C mode) | PA10 (D0) |
| R (SCL in I2C mode) | PA9 (D1)  |
| −                   | GND       |
| +                   | 5V        |

Go by the T/R/+/− labels printed on the HuskyLens, not wire colors.

Put a 4.7 kΩ pull-up resistor from SDA to 3.3 V and another from SCL to 3.3 V.
On the HuskyLens, set **General Settings → Protocol Type → I2C** and save.

## Switches

* `START_ALGORITHM` in `src/main.c`: currently `HUSKY_ALG_OBJECT_TRACKING`. Point the camera at the object and press the learning button so "ID1" appears; only the learned object is reported.
* For line tracking use `HUSKY_ALG_LINE_TRACKING` and `huskyRequestArrows`.

## Debugging

At startup the program checks the external pull-ups, checks that the STM32 really drives SCL/SDA (pin self-test), and scans the I2C bus.

| Terminal output | Likely cause |
|---|---|
| `Pull-up check ... BAD` | That pin has no external 4.7k pull-up, or the wire is on a different pin than you think |
| `Pin self-test ... BAD` | STM32 is not driving that pin: pin config, or the wire is not on PA9/PA10 |
| Self-test good, scan finds nothing | SDA/SCL swapped, no common GND, Protocol Type not I2C (re-save setting, power-cycle HuskyLens) |
| Scan finds 0x32, knock `NACK while READING` | HuskyLens not answering; update firmware with HUSKYLENSUploader |
| `TIMEOUT` | A line is held low (bus stuck); power-cycle both boards |
| `err -2` | HuskyLens answered I2C but sent no valid frame; update firmware with HUSKYLENSUploader |
| Knock works, `no object` | Object not learned yet (press the learning button until "ID1" shows), or object out of view |

A logic analyzer on PA9/PA10 should show address `0x32` writes of `55 AA 11 00 21 31` followed by 16-byte reads.
