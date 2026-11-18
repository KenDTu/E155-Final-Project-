// huskylens.c
// HuskyLens driver over I2C
//
// How a request works:
//   1. Write a request frame, e.g. 55 AA 11 00 21 31 (COMMAND_REQUEST_BLOCKS).
//   2. Read the reply in 16-byte I2C reads. Every reply frame the HuskyLens
//      sends for blocks/arrows/info is exactly 16 bytes:
//        55 AA 11 0A <cmd> <10 data bytes> <checksum>
//   3. The first reply is COMMAND_RETURN_INFO (how many results follow),
//      then one COMMAND_RETURN_BLOCK / COMMAND_RETURN_ARROW frame per result.
// Bytes are fed one at a time into a small state machine so that we can
// re-synchronise on the 0x55 0xAA header if we ever get out of step.

#include "huskylens.h"
#include "STM32L432KC_I2C.h"

#define HUSKY_HEADER0     0x55
#define HUSKY_HEADER1     0xAA
#define HUSKY_ADDRESS     0x11   // "address in protocol" field
#define HUSKY_CHUNK       16     // bytes per I2C read 
#define HUSKY_MAX_DATA    32     // largest data field we accept
#define HUSKY_MAX_READS   20     // give up after this many 16-byte reads

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[HUSKY_MAX_DATA];
} HuskyFrame_t;

// Receive buffer: holds the last 16-byte I2C read; rxIdx = next unread byte
static uint8_t rxChunk[HUSKY_CHUNK];
static uint8_t rxIdx = HUSKY_CHUNK;

// Parser state: pState = 0 means waiting for header, 1 = waiting for 2nd header, etc.
static uint8_t      pState = 0;
static uint8_t      pCount;
static uint8_t      pSum;
static HuskyFrame_t pFrame;

// Helper functions
static int16_t le16(const uint8_t *d) {
    return (int16_t)((uint16_t)d[0] | ((uint16_t)d[1] << 8));
}

static void flushRx(void) {
    rxIdx  = HUSKY_CHUNK;   // forget any unread bytes
    pState = 0;
}

// Diagnostics
int huskyLastI2C   = I2C_OK;
int huskyLastStage = HUSKY_STAGE_NONE;

// Short wait for a few microseconds
static void shortWait(void) {
    for (volatile uint32_t i = 0; i < 20000; i++);
}

// Reads one 16-byte chunk

static int i2cReadChunk(uint8_t *buf) {
    int r = I2C_OK;
    for (int tries = 0; tries < HUSKY_READ_RETRIES; tries++) {
        r = i2cRead(HUSKY_I2C_ADDR, buf, HUSKY_CHUNK);
        if (r == I2C_OK) return I2C_OK;
        shortWait();
    }
    huskyLastI2C   = r;
    huskyLastStage = HUSKY_STAGE_READ;
    return r;
}

//Feed one received byte to the frame parser

static int parseByte(uint8_t b) {
    switch (pState) {
        case 0:  // waiting for header 0x55
            if (b == HUSKY_HEADER0) { pSum = b; pState = 1; }
            break;
        case 1:  // waiting for header 0xAA
            if (b == HUSKY_HEADER1)      { pSum += b; pState = 2; }
            else if (b == HUSKY_HEADER0) { pSum = b; }            // could be a new start
            else                         { pState = 0; }
            break;
        case 2:  // address byte
            if (b == HUSKY_ADDRESS) { pSum += b; pState = 3; }
            else                    { pState = 0; }
            break;
        case 3:  // data length
            if (b > HUSKY_MAX_DATA) { pState = 0; break; }
            pFrame.len = b; pSum += b; pState = 4;
            break;
        case 4:  // command
            pFrame.cmd = b; pSum += b; pCount = 0;
            pState = (pFrame.len > 0) ? 5 : 6;
            break;
        case 5:  // data bytes
            pFrame.data[pCount++] = b; pSum += b;
            if (pCount == pFrame.len) pState = 6;
            break;
        case 6:  // checksum
            pState = 0;
            if (b == pSum) return 1;
            break;
    }
    return 0;
}

// Reads bytes (16 at a time over I2C) until one full frame is parsed. */
static int readFrame(HuskyFrame_t *out) {
    for (int reads = 0; reads < HUSKY_MAX_READS; ) {
        if (rxIdx >= HUSKY_CHUNK) {
            if (i2cReadChunk(rxChunk) != I2C_OK) return HUSKY_ERR_I2C;
            rxIdx = 0;
            reads++;
        }
        while (rxIdx < HUSKY_CHUNK) {
            if (parseByte(rxChunk[rxIdx++])) {
                *out = pFrame;
                return HUSKY_OK;
            }
        }
    }
    return HUSKY_ERR_NOREPLY;
}

// Builds and sends 55 AA 11 len cmd data... checksum
static int sendCommand(uint8_t cmd, const uint8_t *data, uint8_t len) {
    uint8_t buf[6 + HUSKY_MAX_DATA];
    uint8_t n = 0, sum = 0;

    buf[n++] = HUSKY_HEADER0;
    buf[n++] = HUSKY_HEADER1;
    buf[n++] = HUSKY_ADDRESS;
    buf[n++] = len;
    buf[n++] = cmd;
    for (uint8_t i = 0; i < len; i++) buf[n++] = data[i];
    for (uint8_t i = 0; i < n; i++)   sum += buf[i];
    buf[n++] = sum;

    flushRx();  // a new request means old unread bytes are stale
    int r = i2cWrite(HUSKY_I2C_ADDR, buf, n);
    if (r != I2C_OK) {
        huskyLastI2C   = r;
        huskyLastStage = HUSKY_STAGE_WRITE;
        return HUSKY_ERR_I2C;
    }
    return HUSKY_OK;
}

// Waits for COMMAND_RETURN_OK
static int waitForOk(void) {
    HuskyFrame_t f;
    for (int tries = 0; tries < 5; tries++) {
        int r = readFrame(&f);
        if (r != HUSKY_OK) return r;
        if (f.cmd == HUSKY_CMD_RETURN_OK)   return HUSKY_OK;
        if (f.cmd != HUSKY_CMD_RETURN_BUSY) return HUSKY_ERR_REPLY;
    }
    return HUSKY_ERR_NOREPLY;
}

// Sends a request command and collects the INFO frame + all result frames
static int request(uint8_t cmd, HuskyResult_t *res) {
    HuskyFrame_t f;
    int r;

    res->numBlocks = 0;
    res->numArrows = 0;

    if ((r = sendCommand(cmd, 0, 0)) != HUSKY_OK) return r;

    // INFO frame: [numResults][numLearned][frame][reserved]
    if ((r = readFrame(&f)) != HUSKY_OK) return r;
    if (f.cmd != HUSKY_CMD_RETURN_INFO || f.len < 6) return HUSKY_ERR_REPLY;
    res->numResults = (uint16_t) le16(&f.data[0]);
    res->numLearned = (uint16_t) le16(&f.data[2]);
    res->frame      = (uint16_t) le16(&f.data[4]);

    // One frame per result. Read all of them so the HuskyLens output queue is empty for the next request.
    for (uint16_t i = 0; i < res->numResults; i++) {
        if ((r = readFrame(&f)) != HUSKY_OK) return r;
        if (f.len < 10) return HUSKY_ERR_REPLY;

        if (f.cmd == HUSKY_CMD_RETURN_BLOCK) {
            if (res->numBlocks < HUSKY_MAX_BLOCKS) {
                HuskyBlock_t *b = &res->blocks[res->numBlocks++];
                b->xCenter = le16(&f.data[0]);
                b->yCenter = le16(&f.data[2]);
                b->width   = le16(&f.data[4]);
                b->height  = le16(&f.data[6]);
                b->id      = le16(&f.data[8]);
            }
        } else if (f.cmd == HUSKY_CMD_RETURN_ARROW) {
            if (res->numArrows < HUSKY_MAX_BLOCKS) {
                HuskyArrow_t *a = &res->arrows[res->numArrows++];
                a->xOrigin = le16(&f.data[0]);
                a->yOrigin = le16(&f.data[2]);
                a->xTarget = le16(&f.data[4]);
                a->yTarget = le16(&f.data[6]);
                a->id      = le16(&f.data[8]);
            }
        } else {
            return HUSKY_ERR_REPLY;
        }
    }
    return HUSKY_OK;
}


int huskyKnock(void) {
    int r = sendCommand(HUSKY_CMD_REQUEST_KNOCK, 0, 0);
    if (r != HUSKY_OK) return r;
    return waitForOk();
}

int huskySetAlgorithm(uint16_t algorithm) {
    uint8_t data[2] = { (uint8_t)(algorithm & 0xFF), (uint8_t)(algorithm >> 8) };
    int r = sendCommand(HUSKY_CMD_REQUEST_ALGORITHM, data, 2);
    if (r != HUSKY_OK) return r;
    return waitForOk();
}

int huskyRequestBlocks(HuskyResult_t *res) { return request(HUSKY_CMD_REQUEST_BLOCKS, res); }
int huskyRequestArrows(HuskyResult_t *res) { return request(HUSKY_CMD_REQUEST_ARROWS, res); }
int huskyRequestAll(HuskyResult_t *res)    { return request(HUSKY_CMD_REQUEST, res); }
