// huskylens.h
// HuskyLens driver over I2C for the STM32L432KC.
// Protocol reference: "HUSKYLENS Protocol.md" in
// https://github.com/HuskyLens/HUSKYLENSArduino
//
// Every message (both directions) is one frame:
//   0x55 0xAA 0x11 <len> <cmd> <data[0..len-1]> <checksum>
//   checksum = low byte of the sum of every byte before it.
// All 16-bit values are little-endian (low byte first).

#ifndef HUSKYLENS_H
#define HUSKYLENS_H

#include <stdint.h>

// Configuration

#define HUSKY_I2C_ADDR   0x32   // 7-bit I2C address of the HuskyLens
#define HUSKY_READ_RETRIES 100   // ~1 ms apart: wait up to ~100 ms for a reply
#define HUSKY_MAX_BLOCKS 4      // how many boxes/arrows we keep per request

// Camera frame is 320 x 240 pixels; (160,120) is the center.
#define HUSKY_FRAME_W 320
#define HUSKY_FRAME_H 240

// Protocol constants

#define HUSKY_CMD_REQUEST                0x20  // all blocks + arrows
#define HUSKY_CMD_REQUEST_BLOCKS         0x21
#define HUSKY_CMD_REQUEST_ARROWS         0x22
#define HUSKY_CMD_RETURN_INFO            0x29
#define HUSKY_CMD_RETURN_BLOCK           0x2A
#define HUSKY_CMD_RETURN_ARROW           0x2B
#define HUSKY_CMD_REQUEST_KNOCK          0x2C
#define HUSKY_CMD_REQUEST_ALGORITHM      0x2D
#define HUSKY_CMD_RETURN_OK              0x2E
#define HUSKY_CMD_RETURN_BUSY            0x3D

// Algorithm IDs for huskySetAlgorithm()
#define HUSKY_ALG_FACE_RECOGNITION       0
#define HUSKY_ALG_OBJECT_TRACKING        1
#define HUSKY_ALG_OBJECT_RECOGNITION     2
#define HUSKY_ALG_LINE_TRACKING          3
#define HUSKY_ALG_COLOR_RECOGNITION      4
#define HUSKY_ALG_TAG_RECOGNITION        5
#define HUSKY_ALG_OBJECT_CLASSIFICATION  6

// Driver return codes
#define HUSKY_OK          0
#define HUSKY_ERR_I2C    -1   // I2C NACK/timeout (wiring, address, protocol type setting)
#define HUSKY_ERR_NOREPLY -2  // no valid frame came back
#define HUSKY_ERR_REPLY  -3   // a frame came back but not the expected one

// Where the last I2C error happened (huskyLastStage)
#define HUSKY_STAGE_NONE  0
#define HUSKY_STAGE_WRITE 1   // sending the request frame
#define HUSKY_STAGE_READ  2   // reading the reply

// Set whenever a function returns HUSKY_ERR_I2C:
//   huskyLastI2C   = I2C_NACK (-1) or I2C_TIMEOUT (-2) from STM32L432KC_I2C.h
//   huskyLastStage = HUSKY_STAGE_WRITE or HUSKY_STAGE_READ
extern int huskyLastI2C;
extern int huskyLastStage;

// Data types

// A bounding box (COMMAND_RETURN_BLOCK). xCenter/yCenter are the box center.
typedef struct {
    int16_t xCenter;
    int16_t yCenter;
    int16_t width;
    int16_t height;
    int16_t id;       // 0 = detected but not learned, 1..n = learned ID
} HuskyBlock_t;

// An arrow (COMMAND_RETURN_ARROW), returned in line tracking mode.
typedef struct {
    int16_t xOrigin;
    int16_t yOrigin;
    int16_t xTarget;
    int16_t yTarget;
    int16_t id;
} HuskyArrow_t;

// Everything returned by one request.
typedef struct {
    uint16_t     numResults;  // number of blocks + arrows the HuskyLens reported
    uint16_t     numLearned;  // number of IDs the HuskyLens has learned
    uint16_t     frame;       // HuskyLens frame counter (changes every new camera frame)
    uint8_t      numBlocks;   // blocks actually stored below
    uint8_t      numArrows;   // arrows actually stored below
    HuskyBlock_t blocks[HUSKY_MAX_BLOCKS];
    HuskyArrow_t arrows[HUSKY_MAX_BLOCKS];
} HuskyResult_t;


// Function prototypes


// Sends COMMAND_REQUEST_KNOCK and waits for COMMAND_RETURN_OK. Use this to check that the HuskyLens is wired and in I2C mode.
int huskyKnock(void);

// Switches the running algorithm (e.g. HUSKY_ALG_FACE_RECOGNITION).
int huskySetAlgorithm(uint16_t algorithm);

// Requests all blocks (bounding boxes) and fills *res.
int huskyRequestBlocks(HuskyResult_t *res);

// Requests all arrows (line tracking) and fills *res.
int huskyRequestArrows(HuskyResult_t *res);

// Requests blocks and arrows together (whatever the current algorithm returns)
int huskyRequestAll(HuskyResult_t *res);

#endif
