/* YM3812-only adaptation of MIT Opal, Copyright (c) 2026 Bitdancer.
 * Adaptation Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: MIT
 * See LICENSE.OPAL and UPSTREAM.md for source and authorship boundaries. */
#ifndef ST_OPL2_H_
#define ST_OPL2_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { ST_OPL2_NATIVE_RATE=49716, ST_OPL2_CHANNELS=9, ST_OPL2_OPERATORS=18 };
enum { ST_OPL2_ENV_ATTACK=0, ST_OPL2_ENV_DECAY, ST_OPL2_ENV_SUSTAIN, ST_OPL2_ENV_RELEASE };
typedef struct OpalOperator OpalOperator;
typedef struct OpalChannel OpalChannel;
struct OpalOperator
{
    int             envelopeStage;
    uint16_t        egOut;
    uint8_t         key;
    int16_t         out;
    int16_t         prevOut;
    uint32_t        phase;
    uint16_t        envelope;
    uint8_t         tremolo;
    uint8_t         vibrato;
    uint8_t         sustainHold;
    uint8_t         keyScaleRate;
    uint8_t         multiple;
    uint8_t         keyScaleLevel;
    uint8_t         totalLevel;
    uint8_t         attackRate;
    uint8_t         decayRate;
    uint8_t         sustainLevel;
    uint8_t         releaseRate;
    uint8_t         waveform;
};

struct OpalChannel
{
    uint8_t         opSlot[2];
    uint16_t        fNumber;
    uint8_t         block;
    uint8_t         latchedKeyScaleNumber;
    uint8_t         feedback;
    uint8_t         connection;
    uint8_t         algorithm;
};


/* Caller-owned value. No pointers/threads/devices/heap or foreign clocks.
 * All calls are serialized. Copying the initialized value saves full state. */
struct st_opl2 {
    int32_t sampleRate, sampleAccum;
    int32_t lastOutput, currOutput;
    OpalChannel chan[ST_OPL2_CHANNELS];
    OpalOperator op[ST_OPL2_OPERATORS];
    uint8_t timer[2], timerControl, status;
    uint16_t timerCount[2];
    uint8_t rhythmMode, noteSelect, tremoloDepth, vibratoDepth;
    uint8_t waveSelectEnable, compositeSine, tremoloPosition, tremoloLevel;
    uint16_t sampleCounter, hiHatPhase, cymbalPhase;
    uint32_t noise;
    uint64_t chipSamples;
    bool csmPending;
};
/* Rate must be positive and <= INT32_MAX-49716. Init clears the entire
 * chip, including resampling history. Errors preserve the owner/output. */
int st_opl2_init(struct st_opl2 *, unsigned rate);
/* Only byte-addressed YM3812 registers; no bank aliases/OPL3 controls. */
int st_opl2_write(struct st_opl2 *, unsigned reg, uint8_t value);
int st_opl2_status(const struct st_opl2 *, uint8_t *value);
/* Mono signed32 amplitude sum, deliberately unclipped before source gain.
 * One operator peaks near4096; caller owns gain/normalization and stereo mixing.
 * Empty output is valid; generation continues through silent voices. */
int st_opl2_render(struct st_opl2 *, int32_t *mono, size_t count);
#endif
