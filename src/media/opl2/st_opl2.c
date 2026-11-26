/*
    Copyright (c) 2026 Bitdancer (github.com/RealBitdancer).
    SPDX-License-Identifier: MIT

    YM3812-only adaptation of the MIT Opal core.
    Adaptation Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: MIT

    The inherited resampler follows public-domain Opal by Shayde/Reality.
    See UPSTREAM.md and UPSTREAM_THIRD_PARTY_LICENSES.md for provenance.
    New YM3812 WSE/CSM and native API additions are independently authored.
*/

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "io_error.h"
#include "st_opl2.h"

#define OPAL_CHANNELS_PER_BANK     9
#define OPAL_VOICE_STRIDE          3
#define OPAL_BASS_DRUM_CHANNEL     6
#define OPAL_RHYTHM_FIRST_SLOT     12
#define OPAL_HI_HAT_SLOT           13
#define OPAL_SNARE_SLOT            16
#define OPAL_CYMBAL_SLOT           17
#define OPAL_HI_HAT_PHASE_NOISY    0xD0u
#define OPAL_HI_HAT_PHASE_QUIET    0x34u
#define OPAL_CYMBAL_PHASE          0x80u
#define OPAL_RHYTHM_REGISTER       0xBDu
#define OPAL_WAVEFORM_MASK_OPL2    3u
#define OPAL_ENVELOPE_SILENT       511u
#define OPAL_ENVELOPE_OFF_ZONE     504u
#define OPAL_ENVELOPE_PERIOD       ((UINT64_C(1) << 36) - 1u)
#define OPAL_RATE_FAST             48u
#define OPAL_RATE_INSTANT          60u
#define OPAL_TREMOLO_STEPS         210u
#define OPAL_TREMOLO_PEAK          105u
#define OPAL_AUDIBLE_LEVEL_LIMIT   3072u
#define OPAL_EXPONENT_IMPLICIT_ONE 1024u

#define OPAL_ALGORITHM_FM          0u
#define OPAL_ALGORITHM_ADDITIVE    1u
#define OPAL_ALGORITHM_KICK_FM     2u
#define OPAL_ALGORITHM_KICK_DIRECT 3u
#define OPAL_ALGORITHM_PERCUSSION  4u
#define OPAL_ALGORITHM_COUNT       5u

typedef struct OpalEnvelopeTime {
	bool     odd;
	uint64_t count;
} OpalEnvelopeTime;

typedef struct OpalAlgorithm {
	uint8_t operatorCount;
	uint8_t chainMask;
	uint8_t carrierMask;
	uint8_t gain;
	bool    selfFeedback;
} OpalAlgorithm;

typedef struct OpalWave {
	uint16_t attenuation;
	bool     negative;
	bool     silent;
} OpalWave;

typedef struct OpalVibratoStep {
	uint8_t halves;
	bool    negative;
} OpalVibratoStep;

// clang-format off
static const uint16_t mulTimes2[16] =
{
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30,
};

static const uint8_t kslRom[16] =
{
    0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64
};

/* Decapped OPL2/OPL3 exponent ROM, raw ten-bit values in ascending order. */
static const uint16_t exponentRom[0x100] =
{
    0x000, 0x003, 0x006, 0x008, 0x00B, 0x00E, 0x011, 0x014,
    0x016, 0x019, 0x01C, 0x01F, 0x022, 0x025, 0x028, 0x02A,
    0x02D, 0x030, 0x033, 0x036, 0x039, 0x03C, 0x03F, 0x042,
    0x045, 0x048, 0x04B, 0x04E, 0x051, 0x054, 0x057, 0x05A,
    0x05D, 0x060, 0x063, 0x066, 0x069, 0x06C, 0x06F, 0x072,
    0x075, 0x078, 0x07B, 0x07E, 0x082, 0x085, 0x088, 0x08B,
    0x08E, 0x091, 0x094, 0x098, 0x09B, 0x09E, 0x0A1, 0x0A4,
    0x0A8, 0x0AB, 0x0AE, 0x0B1, 0x0B5, 0x0B8, 0x0BB, 0x0BE,
    0x0C2, 0x0C5, 0x0C8, 0x0CC, 0x0CF, 0x0D2, 0x0D6, 0x0D9,
    0x0DC, 0x0E0, 0x0E3, 0x0E7, 0x0EA, 0x0ED, 0x0F1, 0x0F4,
    0x0F8, 0x0FB, 0x0FF, 0x102, 0x106, 0x109, 0x10C, 0x110,
    0x114, 0x117, 0x11B, 0x11E, 0x122, 0x125, 0x129, 0x12C,
    0x130, 0x134, 0x137, 0x13B, 0x13E, 0x142, 0x146, 0x149,
    0x14D, 0x151, 0x154, 0x158, 0x15C, 0x160, 0x163, 0x167,
    0x16B, 0x16F, 0x172, 0x176, 0x17A, 0x17E, 0x181, 0x185,
    0x189, 0x18D, 0x191, 0x195, 0x199, 0x19C, 0x1A0, 0x1A4,
    0x1A8, 0x1AC, 0x1B0, 0x1B4, 0x1B8, 0x1BC, 0x1C0, 0x1C4,
    0x1C8, 0x1CC, 0x1D0, 0x1D4, 0x1D8, 0x1DC, 0x1E0, 0x1E4,
    0x1E8, 0x1EC, 0x1F0, 0x1F5, 0x1F9, 0x1FD, 0x201, 0x205,
    0x209, 0x20E, 0x212, 0x216, 0x21A, 0x21E, 0x223, 0x227,
    0x22B, 0x230, 0x234, 0x238, 0x23C, 0x241, 0x245, 0x249,
    0x24E, 0x252, 0x257, 0x25B, 0x25F, 0x264, 0x268, 0x26D,
    0x271, 0x276, 0x27A, 0x27F, 0x283, 0x288, 0x28C, 0x291,
    0x295, 0x29A, 0x29E, 0x2A3, 0x2A8, 0x2AC, 0x2B1, 0x2B5,
    0x2BA, 0x2BF, 0x2C4, 0x2C8, 0x2CD, 0x2D2, 0x2D6, 0x2DB,
    0x2E0, 0x2E5, 0x2E9, 0x2EE, 0x2F3, 0x2F8, 0x2FD, 0x302,
    0x306, 0x30B, 0x310, 0x315, 0x31A, 0x31F, 0x324, 0x329,
    0x32E, 0x333, 0x338, 0x33D, 0x342, 0x347, 0x34C, 0x351,
    0x356, 0x35B, 0x360, 0x365, 0x36A, 0x370, 0x375, 0x37A,
    0x37F, 0x384, 0x38A, 0x38F, 0x394, 0x399, 0x39F, 0x3A4,
    0x3A9, 0x3AE, 0x3B4, 0x3B9, 0x3BF, 0x3C4, 0x3C9, 0x3CF,
    0x3D4, 0x3DA, 0x3DF, 0x3E4, 0x3EA, 0x3EF, 0x3F5, 0x3FA,
};

static const uint16_t logSinTable[0x100] =
{
    2137, 1731, 1543, 1419, 1326, 1252, 1190, 1137, 1091, 1050, 1013,  979,  949,  920,  894,  869,
     846,  825,  804,  785,  767,  749,  732,  717,  701,  687,  672,  659,  646,  633,  621,  609,
     598,  587,  576,  566,  556,  546,  536,  527,  518,  509,  501,  492,  484,  476,  468,  461,
     453,  446,  439,  432,  425,  418,  411,  405,  399,  392,  386,  380,  375,  369,  363,  358,
     352,  347,  341,  336,  331,  326,  321,  316,  311,  307,  302,  297,  293,  289,  284,  280,
     276,  271,  267,  263,  259,  255,  251,  248,  244,  240,  236,  233,  229,  226,  222,  219,
     215,  212,  209,  205,  202,  199,  196,  193,  190,  187,  184,  181,  178,  175,  172,  169,
     167,  164,  161,  159,  156,  153,  151,  148,  146,  143,  141,  138,  136,  134,  131,  129,
     127,  125,  122,  120,  118,  116,  114,  112,  110,  108,  106,  104,  102,  100,   98,   96,
      94,   92,   91,   89,   87,   85,   83,   82,   80,   78,   77,   75,   74,   72,   70,   69,
      67,   66,   64,   63,   62,   60,   59,   57,   56,   55,   53,   52,   51,   49,   48,   47,
      46,   45,   43,   42,   41,   40,   39,   38,   37,   36,   35,   34,   33,   32,   31,   30,
      29,   28,   27,   26,   25,   24,   23,   23,   22,   21,   20,   20,   19,   18,   17,   17,
      16,   15,   15,   14,   13,   13,   12,   12,   11,   10,   10,    9,    9,    8,    8,    7,
       7,    7,    6,    6,    5,    5,    5,    4,    4,    4,    3,    3,    3,    2,    2,    2,
       2,    1,    1,    1,    1,    1,    1,    1,    0,    0,    0,    0,    0,    0,    0,    0,
};

static const uint8_t operatorOwner[ST_OPL2_OPERATORS] =
{
     0,  1,  2,  0,  1,  2,  3,  4,  5,  3,  4,  5,  6,  7,  8,  6,  7,  8,
};

static const uint8_t rhythmKeyBit[6] = { 4, 0, 2, 4, 3, 1 };

static const OpalVibratoStep vibratoSteps[8] =
{
    { 0, false }, { 1, false }, { 2, false }, { 1, false },
    { 0, false }, { 1, true  }, { 2, true  }, { 1, true  },
};

static const OpalAlgorithm opalAlgorithms[OPAL_ALGORITHM_COUNT] =
{
    { 2, 0x2, 0x2, 1, true  },
    { 2, 0x0, 0x3, 1, true  },
    { 2, 0x2, 0x2, 2, true  },
    { 2, 0x0, 0x2, 2, true  },
    { 2, 0x0, 0x3, 2, false },
};
// clang-format on

static int32_t
opalInterpolate(int32_t last, int32_t curr, int32_t fract)
{
	return (int32_t)(last + ((int64_t)fract * (curr - last)) / 65536);
}

static void
opalTickTimer(struct st_opl2 *self, int t)
{
	if (!(self->timerControl & (1u << t))) {
		return;
	}
	if (++self->timerCount[t] > 0xFF) {
		if (t == 0 && self->compositeSine) {
			self->csmPending = true;
		}
		uint8_t flag        = (uint8_t)(0x40 >> t);
		self->timerCount[t] = self->timer[t];
		if (!(self->timerControl & flag)) {
			self->status |= (uint8_t)(flag | 0x80u);
		}
	}
}

static int32_t
opalFloorShift(int32_t value, unsigned bits)
{
	if (value >= 0) {
		return value >> bits;
	}
	return ~(~value >> bits);
}

static bool
channelIsRhythm(const struct st_opl2 *self, unsigned ch)
{
	return self->rhythmMode && ch >= OPAL_BASS_DRUM_CHANNEL && ch < OPAL_CHANNELS_PER_BANK;
}

static unsigned
channelPickAlgorithm(const struct st_opl2 *self, unsigned ch)
{
	const OpalChannel *chan = &self->chan[ch];

	if (channelIsRhythm(self, ch)) {
		if (ch != OPAL_BASS_DRUM_CHANNEL) {
			return OPAL_ALGORITHM_PERCUSSION;
		}
		return chan->connection ? OPAL_ALGORITHM_KICK_DIRECT : OPAL_ALGORITHM_KICK_FM;
	}
	return chan->connection ? OPAL_ALGORITHM_ADDITIVE : OPAL_ALGORITHM_FM;
}

static const OpalAlgorithm *
channelAlgorithm(const OpalChannel *chan)
{
	return &opalAlgorithms[chan->algorithm];
}

static unsigned
channelVoiceBase(const struct st_opl2 *self, unsigned ch)
{
	return self->chan[ch].opSlot[0];
}

static bool
algorithmFeedsBack(const OpalAlgorithm *algorithm, unsigned position)
{
	return position == 0 && algorithm->selfFeedback;
}

static bool
algorithmChains(const OpalAlgorithm *algorithm, unsigned position)
{
	return ((algorithm->chainMask >> position) & 1u) != 0;
}

static bool
algorithmHears(const OpalAlgorithm *algorithm, unsigned position)
{
	return ((algorithm->carrierMask >> position) & 1u) != 0;
}

static unsigned
operatorVoicePosition(const struct st_opl2 *self, unsigned slot)
{
	return slot == self->chan[operatorOwner[slot]].opSlot[0] ? 0u : 1u;
}

static void
opalRefreshRouting(struct st_opl2 *self)
{
	for (unsigned ch = 0; ch < ST_OPL2_CHANNELS; ch++) {
		self->chan[ch].algorithm = (uint8_t)channelPickAlgorithm(self, ch);
	}
}

static void
channelKey(struct st_opl2 *self, const OpalChannel *chan, unsigned slotCount, unsigned key)
{
	for (unsigned i = 0; i < slotCount; i++) {
		OpalOperator *op = &self->op[chan->opSlot[i]];
		op->key          = (uint8_t)((op->key & 6u) | key);
	}
}

static void
channelLatchKeyScale(const struct st_opl2 *self, OpalChannel *chan)
{
	unsigned noteBit            = ((unsigned)chan->fNumber >> (self->noteSelect ? 8 : 9)) & 1u;
	chan->latchedKeyScaleNumber = (uint8_t)(2u * chan->block + noteBit);
}

static void
channelWriteFrequency(struct st_opl2 *self, unsigned ch, bool high, uint8_t val)
{
	OpalChannel *chan = &self->chan[ch];

	if (high) {
		chan->fNumber = (uint16_t)((chan->fNumber & 0xFFu) | ((val & 3u) << 8));
		chan->block   = (uint8_t)((val >> 2) & 7u);
	}
	else {
		chan->fNumber = (uint16_t)((chan->fNumber & 0x300u) | val);
	}

	channelLatchKeyScale(self, chan);
	if (high) {
		channelKey(self, chan, 2u, (val >> 5) & 1u);
	}
}

static void
channelWriteConnection(struct st_opl2 *self, unsigned ch, uint8_t val)
{
	OpalChannel *chan = &self->chan[ch];
	chan->feedback    = (uint8_t)((val >> 1) & 7u);
	chan->connection  = (uint8_t)(val & 1u);
	opalRefreshRouting(self);
}

static void
operatorWrite(const struct st_opl2 *self, OpalOperator *op, unsigned group, uint8_t val)
{
	switch (group) {
		case 0x20:
			op->tremolo      = (uint8_t)(val >> 7);
			op->vibrato      = (uint8_t)((val >> 6) & 1u);
			op->sustainHold  = (uint8_t)((val >> 5) & 1u);
			op->keyScaleRate = (uint8_t)((val >> 4) & 1u);
			op->multiple     = (uint8_t)(val & 15u);
			break;
		case 0x40:
			op->keyScaleLevel = (uint8_t)(val >> 6);
			op->totalLevel    = (uint8_t)(val & 63u);
			break;
		case 0x60:
			op->attackRate = (uint8_t)(val >> 4);
			op->decayRate  = (uint8_t)(val & 15u);
			break;
		case 0x80:
			op->sustainLevel = (uint8_t)(val >> 4);
			op->releaseRate  = (uint8_t)(val & 15u);
			break;
		default:
			/* YM3812 E0 writes are accepted only while WSE is set.
             * Clearing WSE retains the last accepted selector. See UPSTREAM.md. */
			if (self->waveSelectEnable) {
				op->waveform = (uint8_t)(val & OPAL_WAVEFORM_MASK_OPL2);
			}
			break;
	}
}

static void
opalWriteRhythm(struct st_opl2 *self, uint8_t val)
{
	self->tremoloDepth = (uint8_t)(val >> 7);
	self->vibratoDepth = (uint8_t)((val >> 6) & 1u);
	self->rhythmMode   = (uint8_t)((val >> 5) & 1u);

	for (unsigned i = 0; i < 6; i++) {
		OpalOperator *op  = &self->op[OPAL_RHYTHM_FIRST_SLOT + i];
		unsigned      key = self->rhythmMode ? (val >> rhythmKeyBit[i]) & 1u : 0u;
		op->key           = (uint8_t)((op->key & 5u) | (key << 1));
	}
	opalRefreshRouting(self);
}

static void
opalWriteTimerControl(struct st_opl2 *self, uint8_t val)
{
	if (val & 0x80u) {
		self->status = 0;
		return;
	}

	unsigned started = (unsigned)val & ~(unsigned)self->timerControl;
	if (started & 1u) {
		self->timerCount[0] = self->timer[0];
	}
	if (started & 2u) {
		self->timerCount[1] = self->timer[1];
	}
	self->timerControl = val;
	self->status       = (uint8_t)(self->status & ~(val & 0x60u));
	if ((self->status & 0x60u) == 0) {
		self->status = 0;
	}
}

static void
opalWriteControl(struct st_opl2 *self, unsigned reg, uint8_t val)
{
	switch (reg) {
		case 0x001:
			self->waveSelectEnable = (uint8_t)((val >> 5) & 1u);
			break;
		case 0x002:
			self->timer[0] = val;
			break;
		case 0x003:
			self->timer[1] = val;
			break;
		case 0x004:
			opalWriteTimerControl(self, val);
			break;
		case 0x008:
			self->noteSelect    = (uint8_t)((val >> 6) & 1u);
			self->compositeSine = (uint8_t)(val >> 7);
			break;
		default:
			break;
	}
}

static void
opalWrite(struct st_opl2 *self, uint16_t regNum, uint8_t val)
{
	unsigned reg  = regNum;
	unsigned addr = reg;

	if (addr < 0x20u) {
		opalWriteControl(self, reg, val);
	}
	else if (addr < 0xA0u || addr >= 0xE0u) {
		unsigned row    = (addr & 0x1Fu) >> 3;
		unsigned column = addr & 7u;
		if (row < 3 && column < 6) {
			OpalOperator *op = &self->op[row * 6 + column];
			operatorWrite(self, op, addr & 0xE0u, val);
		}
	}
	else if (reg == OPAL_RHYTHM_REGISTER) {
		opalWriteRhythm(self, val);
	}
	else if (addr < 0xD0u && (addr & 0x0Fu) < OPAL_CHANNELS_PER_BANK) {
		unsigned ch    = addr & 0x0Fu;
		unsigned group = addr & 0xF0u;
		if (group == 0xC0u) {
			channelWriteConnection(self, ch, val);
		}
		else {
			channelWriteFrequency(self, ch, group == 0xB0u, val);
		}
	}
}

static const OpalChannel *
operatorPitchChannel(const struct st_opl2 *self, unsigned slot)
{
	return &self->chan[operatorOwner[slot]];
}

static unsigned
channelKeyScaleAttenuation(const OpalChannel *chan)
{
	int attenuation = 4 * kslRom[chan->fNumber >> 6] - 32 * (8 - chan->block);
	return attenuation > 0 ? (unsigned)attenuation : 0u;
}

static unsigned
operatorKeyScaleLevel(const OpalOperator *op, const OpalChannel *pitch)
{
	unsigned full = channelKeyScaleAttenuation(pitch);

	switch (op->keyScaleLevel) {
		case 1:
			return full / 2u;
		case 2:
			return full / 4u;
		case 3:
			return full;
		default:
			return 0;
	}
}

static OpalEnvelopeTime
opalEnvelopeTime(const struct st_opl2 *self)
{
	OpalEnvelopeTime time;
	uint64_t         pairs = self->chipSamples >> 1;

	time.odd   = (self->chipSamples & 1u) != 0;
	time.count = pairs < 2u ? 0u : (pairs - 2u) % OPAL_ENVELOPE_PERIOD + 1u;
	return time;
}

static bool
envelopeSlowStepDue(uint64_t count, unsigned group, unsigned fraction)
{
	uint64_t period = UINT64_C(1) << (OPAL_RATE_FAST / 4u - group);

	if (count % period == period / 2u) {
		return true;
	}
	if ((fraction & 2u) && count % (2u * period) == period) {
		return true;
	}
	return (fraction & 1u) && count % (4u * period) == 2u * period;
}

static unsigned
operatorEnvelopeSpeed(unsigned registerRate, unsigned actualRate, OpalEnvelopeTime time)
{
	unsigned fraction = actualRate & 3u;

	if (registerRate == 0) {
		return 0;
	}
	if (actualRate >= OPAL_RATE_INSTANT) {
		return 4;
	}
	if (actualRate >= OPAL_RATE_FAST) {
		unsigned pairPosition = (unsigned)(time.count & 3u);
		unsigned spread       = ((pairPosition & 1u) << 1) | (pairPosition >> 1);
		unsigned pairUnits    = (spread < fraction ? 2u : 1u) << ((actualRate - OPAL_RATE_FAST) >> 2);
		return (pairUnits >> 1) | (time.odd ? pairUnits & 1u : 0u);
	}
	if (time.odd && envelopeSlowStepDue(time.count, actualRate >> 2, fraction)) {
		return 1;
	}
	return 0;
}

static bool
operatorEnvelope(OpalOperator *op, const OpalChannel *pitch, OpalEnvelopeTime time)
{
	bool     keyActive = op->key != 0;
	int      stage     = op->envelopeStage;
	bool     keyOn     = keyActive && stage == ST_OPL2_ENV_RELEASE;
	unsigned level     = op->envelope;
	unsigned rate      = op->releaseRate;

	if (keyOn || stage == ST_OPL2_ENV_ATTACK) {
		rate = op->attackRate;
	}
	else if (stage == ST_OPL2_ENV_DECAY) {
		rate = op->decayRate;
	}
	else if (stage == ST_OPL2_ENV_SUSTAIN && op->sustainHold) {
		rate = 0;
	}

	unsigned keyScale    = pitch->latchedKeyScaleNumber;
	unsigned rateOffset  = op->keyScaleRate ? keyScale : keyScale >> 2;
	unsigned actualRate  = 4u * rate + rateOffset;
	bool     instant     = actualRate >= OPAL_RATE_INSTANT;
	unsigned speed       = operatorEnvelopeSpeed(rate, actualRate, time);
	unsigned sustainBand = op->sustainLevel == 15 ? 31u : op->sustainLevel;
	bool     offZone     = level >= OPAL_ENVELOPE_OFF_ZONE;

	if (keyOn) {
		if (instant) {
			level = 0;
		}
		stage = ST_OPL2_ENV_ATTACK;
	}
	else if (stage == ST_OPL2_ENV_ATTACK) {
		if (level == 0) {
			stage = ST_OPL2_ENV_DECAY;
		}
		else if (keyActive && !instant) {
			level -= (speed * (level + 1u) + 7u) >> 3;
		}
	}
	else if (stage == ST_OPL2_ENV_DECAY && (level >> 4) == sustainBand) {
		if (offZone) {
			level = OPAL_ENVELOPE_SILENT;
		}
		stage = ST_OPL2_ENV_SUSTAIN;
	}
	else if (offZone) {
		level = OPAL_ENVELOPE_SILENT;
	}
	else {
		level += speed;
	}

	if (!keyActive) {
		stage = ST_OPL2_ENV_RELEASE;
	}

	op->envelope      = (uint16_t)level;
	op->envelopeStage = stage;
	return keyOn;
}

static uint32_t
operatorIncrement(const struct st_opl2 *self, const OpalOperator *op, const OpalChannel *pitch)
{
	int fNumber = pitch->fNumber;

	if (op->vibrato) {
		int                    fNumberHigh = fNumber >> 7;
		int                    depthShift  = self->vibratoDepth ? 0 : 1;
		unsigned               position    = ((unsigned)self->sampleCounter >> 10) & 7u;
		const OpalVibratoStep *step        = &vibratoSteps[position];
		int                    magnitude   = ((fNumberHigh * (int)step->halves) >> 1) >> depthShift;
		fNumber += step->negative ? -magnitude : magnitude;
	}

	uint32_t base = ((uint32_t)fNumber << pitch->block) >> 1;
	return (base * mulTimes2[op->multiple]) >> 1;
}

static unsigned
opalCymbalBit(const struct st_opl2 *self)
{
	unsigned hat    = self->hiHatPhase;
	unsigned cymbal = self->cymbalPhase;
	return (((hat >> 2) ^ (hat >> 7)) | ((hat >> 3) ^ (cymbal >> 5)) | ((cymbal >> 3) ^ (cymbal >> 5))) & 1u;
}

static unsigned
opalRhythmPhase(struct st_opl2 *self, unsigned slot, unsigned phase, unsigned noiseBit)
{
	if (slot == OPAL_HI_HAT_SLOT) {
		unsigned cymbalBit = opalCymbalBit(self);
		return (cymbalBit << 9)
		       | ((cymbalBit ^ noiseBit) ? OPAL_HI_HAT_PHASE_NOISY : OPAL_HI_HAT_PHASE_QUIET);
	}
	if (slot == OPAL_SNARE_SLOT) {
		unsigned hatBit = ((unsigned)self->hiHatPhase >> 8) & 1u;
		return (hatBit << 9) | ((hatBit ^ noiseBit) << 8);
	}
	if (slot == OPAL_CYMBAL_SLOT) {
		self->cymbalPhase = (uint16_t)phase;
		return (opalCymbalBit(self) << 9) | OPAL_CYMBAL_PHASE;
	}
	return phase;
}

static OpalWave
operatorWave(unsigned phase, unsigned waveform)
{
	bool     secondHalf      = (phase & 0x200u) != 0;
	bool     secondQuarter   = (phase & 0x100u) != 0;
	unsigned quarterPosition = phase & 0xFFu;
	uint16_t sine            = logSinTable[secondQuarter ? 0xFFu - quarterPosition : quarterPosition];
	OpalWave wave            = {0, false, false};

	switch (waveform) {
		case 0:
			wave.attenuation = sine;
			wave.negative    = secondHalf;
			break;
		case 1:
			wave.attenuation = sine;
			wave.silent      = secondHalf;
			break;
		case 2:
			wave.attenuation = sine;
			break;
		case 3:
			wave.attenuation = logSinTable[quarterPosition];
			wave.silent      = secondQuarter;
			break;
	}
	return wave;
}

static int16_t
operatorOutput(OpalWave wave, unsigned attenuation)
{
	if (wave.silent) {
		return 0;
	}

	unsigned level     = wave.attenuation + 8u * attenuation;
	int      magnitude = 0;
	if (level < OPAL_AUDIBLE_LEVEL_LIMIT) {
		unsigned mantissa = OPAL_EXPONENT_IMPLICIT_ONE + exponentRom[0xFFu - (level & 0xFFu)];
		magnitude         = (int)((2u * mantissa) >> (level >> 8));
	}
	return (int16_t)(wave.negative ? -magnitude - 1 : magnitude);
}

static int32_t
operatorModulation(const struct st_opl2 *self, unsigned slot)
{
	const OpalChannel   *owner     = &self->chan[operatorOwner[slot]];
	const OpalAlgorithm *algorithm = channelAlgorithm(owner);
	unsigned             position  = operatorVoicePosition(self, slot);

	if (algorithmFeedsBack(algorithm, position)) {
		const OpalOperator *op = &self->op[slot];
		if (owner->feedback == 0) {
			return 0;
		}
		return opalFloorShift((int32_t)op->out + op->prevOut, 9u - owner->feedback);
	}
	if (algorithmChains(algorithm, position)) {
		return self->op[slot - OPAL_VOICE_STRIDE].out;
	}
	return 0;
}

static void
operatorTurn(struct st_opl2 *self, unsigned slot, OpalEnvelopeTime time)
{
	OpalOperator      *op    = &self->op[slot];
	const OpalChannel *pitch = operatorPitchChannel(self, slot);

	unsigned attenuation = op->envelope + 4u * op->totalLevel + operatorKeyScaleLevel(op, pitch);
	if (op->tremolo) {
		attenuation += self->tremoloLevel;
	}
	op->egOut = (uint16_t)attenuation;

	bool restart = operatorEnvelope(op, pitch, time);

	unsigned phase     = (op->phase >> 9) & 0xFFFFu;
	uint32_t increment = operatorIncrement(self, op, pitch);
	op->phase          = restart ? increment : op->phase + increment;

	unsigned noiseBit = self->noise & 1u;
	self->noise       = (self->noise >> 1) | (((self->noise ^ (self->noise >> 14)) & 1u) << 22);

	if (slot == OPAL_HI_HAT_SLOT) {
		self->hiHatPhase = (uint16_t)phase;
	}
	if (self->rhythmMode) {
		phase = opalRhythmPhase(self, slot, phase, noiseBit);
	}

	int32_t  modulation = operatorModulation(self, slot);
	unsigned finalPhase = (unsigned)((int32_t)phase + modulation) & 0x3FFu;

	op->prevOut = op->out;
	op->out     = operatorOutput(operatorWave(finalPhase, op->waveform), attenuation);
}

static int32_t
channelVoiceSum(const struct st_opl2 *self, unsigned ch)
{
	const OpalChannel   *chan      = &self->chan[ch];
	const OpalAlgorithm *algorithm = channelAlgorithm(chan);
	unsigned             base      = channelVoiceBase(self, ch);
	int32_t              sum       = 0;

	for (unsigned position = 0; position < algorithm->operatorCount; position++) {
		if (algorithmHears(algorithm, position)) {
			sum += self->op[base + OPAL_VOICE_STRIDE * position].out;
		}
	}
	return sum * algorithm->gain;
}

static int32_t
opalMix(const struct st_opl2 *self)
{
	int32_t sum = 0;
	for (unsigned ch = 0; ch < ST_OPL2_CHANNELS; ch++) {
		sum += channelVoiceSum(self, ch);
	}
	return sum;
}

static void
opalCloseSample(struct st_opl2 *self)
{
	self->sampleCounter = (uint16_t)(self->sampleCounter + 1u);
	if ((self->sampleCounter & 63u) == 0) {
		self->tremoloPosition = (uint8_t)((self->tremoloPosition + 1u) % OPAL_TREMOLO_STEPS);
	}
	unsigned position  = self->tremoloPosition;
	unsigned triangle  = position < OPAL_TREMOLO_PEAK ? position : OPAL_TREMOLO_STEPS - position;
	self->tremoloLevel = (uint8_t)(triangle >> (self->tremoloDepth ? 2 : 4));

	if ((self->sampleCounter & 3u) == 0) {
		opalTickTimer(self, 0);
	}
	if ((self->sampleCounter & 15u) == 0) {
		opalTickTimer(self, 1);
	}

	self->chipSamples++;
}

static int32_t
opalOutput(struct st_opl2 *self)
{
	OpalEnvelopeTime time = opalEnvelopeTime(self);
	/* Newly authored CSM owner: a timer1 overflow requests one native sample
     * of independent key-on, then releases only that contribution. Melodic
     * and rhythm keys survive both the pulse and a later mode change. */
	bool csm         = self->csmPending;
	self->csmPending = false;
	for (unsigned slot = 0; slot < ST_OPL2_OPERATORS; slot++) {
		if (csm) {
			self->op[slot].key |= 4u;
		}
		operatorTurn(self, slot, time);
		self->op[slot].key &= 3u;
	}
	int32_t result = opalMix(self);
	opalCloseSample(self);
	return result;
}

static int32_t
opalSample(struct st_opl2 *self)
{
	while (self->sampleAccum >= self->sampleRate) {
		self->lastOutput = self->currOutput;
		self->currOutput = opalOutput(self);
		self->sampleAccum -= self->sampleRate;
	}
	int32_t fract  = (int32_t)(((int64_t)self->sampleAccum * 65536 + self->sampleRate / 2) / self->sampleRate);
	int32_t result = opalInterpolate(self->lastOutput, self->currOutput, fract);
	self->sampleAccum += ST_OPL2_NATIVE_RATE;
	return result;
}

/* New native API: the owner serializes writes/render and owns storage.
 * There is no bus, write-spacing queue, worker, device or allocation here. */
int
st_opl2_init(struct st_opl2 *self, unsigned rate)
{
	if (self == NULL || rate == 0 || rate > INT32_MAX - ST_OPL2_NATIVE_RATE)
		return IO_ERROR_INVALID_ARGUMENT;
	memset(self, 0, sizeof(*self));
	for (unsigned ch = 0; ch < ST_OPL2_CHANNELS; ch++) {
		self->chan[ch].opSlot[0] = (uint8_t)((ch / 3) * 6 + ch % 3);
		self->chan[ch].opSlot[1] = (uint8_t)(self->chan[ch].opSlot[0] + 3);
	}
	for (unsigned slot = 0; slot < ST_OPL2_OPERATORS; slot++) {
		self->op[slot].envelopeStage = ST_OPL2_ENV_RELEASE;
		self->op[slot].egOut         = OPAL_ENVELOPE_SILENT;
		self->op[slot].envelope      = OPAL_ENVELOPE_SILENT;
	}
	self->noise = 1;
	opalRefreshRouting(self);
	self->sampleRate = (int32_t)rate;
	return 0;
}

int
st_opl2_write(struct st_opl2 *self, unsigned reg, uint8_t value)
{
	if (self == NULL || self->sampleRate <= 0)
		return IO_ERROR_INVALID_ARGUMENT;
	if (reg > 255)
		return IO_ERROR_OUT_OF_RANGE;
	opalWrite(self, (uint16_t)reg, value);
	return 0;
}

int
st_opl2_status(const struct st_opl2 *self, uint8_t *value)
{
	if (self == NULL || value == NULL || self->sampleRate <= 0)
		return IO_ERROR_INVALID_ARGUMENT;
	/* Declared YM3812 status-port profile: unused low bits read as 06.
     * BIG detection observes only e0; no debug/serial bus model is claimed. */
	*value = (uint8_t)(self->status | 6u);
	return 0;
}

int
st_opl2_render(struct st_opl2 *self, int32_t *mono, size_t count)
{
	if (self == NULL || self->sampleRate <= 0 || (mono == NULL && count != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	if (count > SIZE_MAX / sizeof(*mono))
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < count; i++)
		mono[i] = opalSample(self);
	return 0;
}
