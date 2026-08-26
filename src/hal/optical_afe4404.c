// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 CareLoop Labs

/*
 * hal/optical.h for the TI AFE4404 (U7) driving the OSRAM SFH 7074 (LED1).
 *
 * There is no AFE4404 driver in Zephyr or NCS v3.4.0 - the sensor tree has no
 * optical front ends of this class at all - so the register access is here.
 * Written against SBAS689D (December 2016).
 *
 * Wiring, read out of CareLoop.kicad_pcb rather than the schematic sheet,
 * because two of the net names there are actively misleading:
 *
 *   TX1 -> LED_IR    -> SFH7074 pad 8      so IR    is the part's "LED1"
 *   TX2 -> LED_RED   -> SFH7074 pad 3      so red   is the part's "LED2"
 *   TX3 -> LED_GREEN -> SFH7074 pad 2      so green is the part's "LED3"
 *
 * The emitter cathodes sink into those pins; their common anode (pad 9) sits
 * on +5V together with TX_SUP, so nothing emits until the boost is enabled.
 *
 *   AFE_INT1 -> ADC_RDY -> P0.12
 *   AFE_INT2 -> RESETZ  -> P0.26
 *
 * The receive side is one photodiode, single-ended:
 *
 *   SFH7074 pad 5 (PD3 cathode) -> INP,  INM (A1) -> GND
 *
 * PD3 is the SFH7074's broadband diode. The part also carries two IR-cut
 * diodes, PD1 and PD2 on pads 1 and 7, and **neither is connected to
 * anything** - both are single-pad nets in CareLoop.kicad_pcb with no copper
 * on them. So there is exactly one detector, it is broadband, and it sees
 * green, red, IR and ambient alike. Nothing here can be cross-checked against
 * a second diode, and no amount of configuration recovers the IR-cut path;
 * that needs a board change.
 *
 * AFE_INT2 is not an interrupt. It is RESETZ, an output from us, and the name
 * is the single easiest thing to get wrong on this board: configure it as an
 * input the way the name suggests and the part is never released from reset,
 * which presents as an AFE that will not answer on I2C.
 *
 * Supplies, also from the netlist: RX_SUP is +3V3 (BUCK2, through jumper J3),
 * IO_SUP is +1V8 (BUCK1), TX_SUP is +5V from the MAX17222 boost (U8) enabled
 * by P1.09. The nPM1300's LDO1 is *not* in this path - its output reaches
 * jumper J4 and stops there, connected to nothing.
 *
 * Clocking: the CLK pin carries only R19, a 500k shunt to ground. SBAS689D
 * section 9.2 names that exact resistor as the internal-oscillator
 * configuration, and there is no other clock source anywhere on the net. That
 * makes OSC_ENABLE load-bearing rather than an optimisation: the part resets
 * into external-clock mode, so without it the timing engine has no clock, and
 * every write below succeeds and reads back correctly on a board that never
 * emits.
 */

#include <optical.h>

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(optical, LOG_LEVEL_INF);

#define AFE_NODE DT_NODELABEL(afe4404)

/*
 * The application also builds for the nRF52840 DK, which has no such node.
 * Everything still compiles there; optical_init() reports the part absent.
 */
#if DT_NODE_HAS_STATUS_OKAY(AFE_NODE)

/* Register addresses. Names are SBAS689D's. */
#define REG_CONTROL0 0x00U
#define REG_LED2STC 0x01U
#define REG_LED2ENDC 0x02U
#define REG_LED1LEDSTC 0x03U
#define REG_LED1LEDENDC 0x04U
#define REG_LED3STC 0x05U
#define REG_LED3ENDC 0x06U
#define REG_LED1STC 0x07U
#define REG_LED1ENDC 0x08U
#define REG_LED2LEDSTC 0x09U
#define REG_LED2LEDENDC 0x0AU
#define REG_ALED1STC 0x0BU
#define REG_ALED1ENDC 0x0CU
#define REG_LED2CONVST 0x0DU
#define REG_LED2CONVEND 0x0EU
#define REG_LED3CONVST 0x0FU
#define REG_LED3CONVEND 0x10U
#define REG_LED1CONVST 0x11U
#define REG_LED1CONVEND 0x12U
#define REG_ALED1CONVST 0x13U
#define REG_ALED1CONVEND 0x14U
#define REG_ADCRSTSTCT0 0x15U
#define REG_ADCRSTENDCT0 0x16U
#define REG_ADCRSTSTCT1 0x17U
#define REG_ADCRSTENDCT1 0x18U
#define REG_ADCRSTSTCT2 0x19U
#define REG_ADCRSTENDCT2 0x1AU
#define REG_ADCRSTSTCT3 0x1BU
#define REG_ADCRSTENDCT3 0x1CU
#define REG_PRPCT 0x1DU
#define REG_CONTROL1 0x1EU
#define REG_TIA_AMB_GAIN 0x21U
#define REG_LEDCNTRL 0x22U
#define REG_CONTROL2 0x23U
#define REG_CLKDIV_PRF 0x39U
#define REG_LED3LEDSTC 0x36U
#define REG_LED3LEDENDC 0x37U

/* ADC output registers. LED3VAL shares 0x2B with ALED2VAL; the LED3 meaning
 * is the one in force here, because all three emitters are used and that
 * replaces the second ambient phase. */
#define REG_LED2VAL 0x2AU  /* red */
#define REG_LED3VAL 0x2BU  /* green */
#define REG_LED1VAL 0x2CU  /* IR */
#define REG_ALED1VAL 0x2DU /* ambient */

/* CONTROL0 (0x00). */
#define CONTROL0_SW_RESET BIT(3)
#define CONTROL0_REG_READ BIT(0)

/* CONTROL1 (0x1E). NUMAV in bits 3:0 stays 0 - one sample per phase. */
#define CONTROL1_TIMEREN BIT(8)

/* CONTROL2 (0x23). */
#define CONTROL2_OSC_ENABLE BIT(9)

/*
 * LEDCNTRL (0x22) packs all three currents into one 24-bit word, so there is
 * no way to change one emitter without rewriting the other two. Hence the
 * shadow copy below.
 */
#define LEDCNTRL_ILED1_SHIFT 0U  /* IR */
#define LEDCNTRL_ILED2_SHIFT 6U  /* red */
#define LEDCNTRL_ILED3_SHIFT 12U /* green */
#define LEDCNTRL_ILED_MASK 0x3FU

/*
 * Current control is 6 bits over a 50 mA full scale (SBAS689D Table 53):
 * code 63 is 50 mA, so one code is 50000 / 63 uA. That does not divide
 * evenly, and the table gives the intended grain directly - 1 is 0.8 mA,
 * 2 is 1.6 mA - so the step is used as the definition and 63 * 800 uA
 * (50.4 mA) is clamped by the per-emitter ceilings below rather than being
 * reachable.
 */
#define ILED_STEP_UA 800U
#define ILED_CODE_MAX 63U

/*
 * Per-emitter ceilings, from the SFH 7074 datasheet's DC forward current
 * maxima (green 30 mA, red 40 mA, IR 60 mA), with IR further limited by the
 * AFE's own 50 mA full scale.
 *
 * The DC rating is the right one to enforce even though the emitters are
 * pulsed at about 2.5% duty by the timing below, where the far higher pulsed
 * ratings would apply. Enforcing the DC number keeps every value this HAL
 * accepts safe for the emitter whatever the duty cycle becomes later, so a
 * change to the timing constants cannot quietly turn a previously valid
 * current into one that cooks a diode against someone's skin.
 */
static const uint32_t emitter_max_ua[OPTICAL_EMITTER_COUNT] = {
	[OPTICAL_EMITTER_GREEN] = 30000U,
	[OPTICAL_EMITTER_RED] = 40000U,
	[OPTICAL_EMITTER_IR] = 50000U,
};

static const uint8_t emitter_shift[OPTICAL_EMITTER_COUNT] = {
	[OPTICAL_EMITTER_GREEN] = LEDCNTRL_ILED3_SHIFT,
	[OPTICAL_EMITTER_RED] = LEDCNTRL_ILED2_SHIFT,
	[OPTICAL_EMITTER_IR] = LEDCNTRL_ILED1_SHIFT,
};

static const uint8_t channel_reg[OPTICAL_CHANNEL_COUNT] = {
	[OPTICAL_CHANNEL_GREEN] = REG_LED3VAL,
	[OPTICAL_CHANNEL_RED] = REG_LED2VAL,
	[OPTICAL_CHANNEL_IR] = REG_LED1VAL,
	[OPTICAL_CHANNEL_AMBIENT] = REG_ALED1VAL,
};

/*
 * TIA feedback resistors, indexed by TIA_GAIN (SBAS689D Table 50). Note the
 * order is not monotonic - codes 6 and 7 are the two largest values, above
 * code 0 - so this must be searched, never compared against or interpolated.
 */
static const uint32_t tia_gain_ohms[8] = {
	500000U, 250000U, 100000U, 50000U, 25000U, 10000U, 1000000U, 2000000U,
};

#define TIA_GAIN_MASK 0x07U
#define TIA_GAIN_DEFAULT 0U /* 500 kohm */

/*
 * The converter is 22 bits inside a 24-bit twos-complement word, full scale
 * +-1.2 V spanning bits 21:0 (SBAS689D Table 3). So the largest magnitude a
 * valid code carries is 2^21 - 1.
 */
#define ADC_FULL_SCALE_CODE ((int32_t)((1 << 21) - 1))

/*
 * Bits 23:21 are sign extension of the 22-bit code while the input is inside
 * full scale: 000 for positive, 111 for negative (SBAS689D Table 4). Any
 * other combination - 001 or 110 - means the input ran past the end of the
 * range, and the code below it is a rail rather than a measurement. This is
 * the only saturation indication the part gives; there is no status bit.
 */
#define ADC_SIGN_BITS(raw) (((raw) >> 21) & 0x07U)

/*
 * Timing engine.
 *
 * The internal oscillator is 4 MHz and CLKDIV_PRF stays 0, so the timer
 * counts at 4 MHz and one count is 250 ns. PRPCT holds the top of the count,
 * so the period is PRPCT + 1.
 */
#define TIMER_CLK_HZ 4000000U

/*
 * 250 Hz, and the specific value is load-bearing - it is not a sample-rate
 * preference, it is ambient-light rejection.
 *
 * Mains lighting flickers at twice the mains frequency: 100 Hz in Europe,
 * 120 Hz in North America. Sampling at 100 Hz put the European flicker
 * exactly at the sequence rate, where it aliases to DC - and because the
 * timing runs off an internal RC oscillator with no better than percent-level
 * accuracy, "DC" is really a slow wandering beat that lands squarely in the
 * heart-rate band. Nothing downstream can remove it: once folded down it is
 * indistinguishable from the signal.
 *
 * Measured on hardware 2026-08-25, same board and same room, emitters dark,
 * undisturbed ambient channel over 2 s:
 *
 *     100 Hz PRF   range 295697 counts   (and the mean swung by ~1e6 and
 *                                         changed sign between runs)
 *     250 Hz PRF   range  20658 counts
 *
 * Anything above 240 Hz keeps both 100 and 120 Hz below Nyquist, so they stay
 * where they are instead of folding down, with enough margin that oscillator
 * error cannot drag them into the signal band. 250 Hz is that, rounded.
 */
#define PRF_HZ 250U
#define PERIOD_COUNTS (TIMER_CLK_HZ / PRF_HZ) /* 40000 = 10 ms */
#define PRPCT_VALUE (PERIOD_COUNTS - 1U)

/*
 * Four phases - red, green, IR and one ambient - each given a 500 us slot at
 * the head of the period, so they occupy 2 ms of the 4 ms period and the
 * rest is idle. The slot layout is deliberately independent of the period:
 * changing PRF_HZ moves only where the period ends, and the BUILD_ASSERT
 * below is what catches a PRF raised so far that the slots no longer fit.
 *
 * Within a slot the layout follows SBAS689D Table 7:
 *   t1 start of emitter to start of sampling  >= max(25 us, 20% of the pulse)
 *   t2 end of emitter to start of ADC reset   >= 2 counts
 *   t3 duration of ADC reset                  >= 6 counts
 *   t4 end of ADC reset to start of convert   == 2 counts
 *   t5 duration of convert  >= (NUMAV + 2) * 200 * t_ADC + 15 us, which with
 *      NUMAV = 0 and a 4 MHz ADC clock is 115 us, or 460 counts.
 *
 * The emitter pulse is 400 counts (100 us), so t1 is max(25 us, 20 us) =
 * 25 us = 100 counts. The convert phase runs 1490 counts (372 us), well
 * clear of its 460 count minimum.
 *
 * Nothing here is tuned for signal quality - the receive path is not in use
 * yet. It is a legal configuration that makes the emitters fire, and the
 * constants are derived rather than transcribed so that retuning the pulse
 * or the PRF later moves the whole layout consistently.
 */
#define SLOT_COUNTS 2000U   /* 500 us per phase */
#define LED_PULSE_COUNTS 400U /* 100 us */
#define SAMPLE_SKEW_COUNTS 100U /* t1 = 25 us */
#define RESET_GAP_COUNTS 2U  /* t2 */
#define RESET_LEN_COUNTS 6U  /* t3 */
#define CONVERT_GAP_COUNTS 2U /* t4 */
#define SLOT_TAIL_COUNTS 100U /* idle before the next slot */

#define SLOT_BASE(k) ((k) * SLOT_COUNTS)
#define LED_START(k) (SLOT_BASE(k))
#define LED_END(k) (SLOT_BASE(k) + LED_PULSE_COUNTS - 1U)
#define SAMPLE_START(k) (SLOT_BASE(k) + SAMPLE_SKEW_COUNTS)
#define SAMPLE_END(k) (LED_END(k))
#define RESET_START(k) (LED_END(k) + RESET_GAP_COUNTS)
#define RESET_END(k) (RESET_START(k) + RESET_LEN_COUNTS)
#define CONVERT_START(k) (RESET_END(k) + CONVERT_GAP_COUNTS)
#define CONVERT_END(k) (SLOT_BASE(k) + SLOT_COUNTS - SLOT_TAIL_COUNTS - 1U)

/* Slot order. Ambient last so the three emitter slots stay adjacent. */
#define SLOT_RED 0U
#define SLOT_GREEN 1U
#define SLOT_IR 2U
#define SLOT_AMBIENT 3U

BUILD_ASSERT(CONVERT_END(0) - CONVERT_START(0) >= 460U,
	     "convert phase shorter than SBAS689D Table 7 t5");
BUILD_ASSERT(CONVERT_END(SLOT_AMBIENT) < PERIOD_COUNTS,
	     "phase slots do not fit inside the pulse repetition period");
BUILD_ASSERT(PRPCT_VALUE <= 0xFFFFU, "PRPCT is a 16-bit counter");

struct afe_reg_value {
	uint8_t reg;
	uint32_t value;
};

/*
 * Written in order at init. Timing first, then the clock source, so the
 * engine only ever starts against a complete configuration.
 */
static const struct afe_reg_value afe_config[] = {
	/* Red, the part's LED2. */
	{ REG_LED2LEDSTC, LED_START(SLOT_RED) },
	{ REG_LED2LEDENDC, LED_END(SLOT_RED) },
	{ REG_LED2STC, SAMPLE_START(SLOT_RED) },
	{ REG_LED2ENDC, SAMPLE_END(SLOT_RED) },
	{ REG_ADCRSTSTCT0, RESET_START(SLOT_RED) },
	{ REG_ADCRSTENDCT0, RESET_END(SLOT_RED) },
	{ REG_LED2CONVST, CONVERT_START(SLOT_RED) },
	{ REG_LED2CONVEND, CONVERT_END(SLOT_RED) },

	/* Green, the part's LED3. */
	{ REG_LED3LEDSTC, LED_START(SLOT_GREEN) },
	{ REG_LED3LEDENDC, LED_END(SLOT_GREEN) },
	{ REG_LED3STC, SAMPLE_START(SLOT_GREEN) },
	{ REG_LED3ENDC, SAMPLE_END(SLOT_GREEN) },
	{ REG_ADCRSTSTCT1, RESET_START(SLOT_GREEN) },
	{ REG_ADCRSTENDCT1, RESET_END(SLOT_GREEN) },
	{ REG_LED3CONVST, CONVERT_START(SLOT_GREEN) },
	{ REG_LED3CONVEND, CONVERT_END(SLOT_GREEN) },

	/* IR, the part's LED1. */
	{ REG_LED1LEDSTC, LED_START(SLOT_IR) },
	{ REG_LED1LEDENDC, LED_END(SLOT_IR) },
	{ REG_LED1STC, SAMPLE_START(SLOT_IR) },
	{ REG_LED1ENDC, SAMPLE_END(SLOT_IR) },
	{ REG_ADCRSTSTCT2, RESET_START(SLOT_IR) },
	{ REG_ADCRSTENDCT2, RESET_END(SLOT_IR) },
	{ REG_LED1CONVST, CONVERT_START(SLOT_IR) },
	{ REG_LED1CONVEND, CONVERT_END(SLOT_IR) },

	/* Ambient, no emitter. */
	{ REG_ALED1STC, SAMPLE_START(SLOT_AMBIENT) },
	{ REG_ALED1ENDC, SAMPLE_END(SLOT_AMBIENT) },
	{ REG_ADCRSTSTCT3, RESET_START(SLOT_AMBIENT) },
	{ REG_ADCRSTENDCT3, RESET_END(SLOT_AMBIENT) },
	{ REG_ALED1CONVST, CONVERT_START(SLOT_AMBIENT) },
	{ REG_ALED1CONVEND, CONVERT_END(SLOT_AMBIENT) },

	{ REG_PRPCT, PRPCT_VALUE },
	{ REG_CLKDIV_PRF, 0U },

	/* Default TIA gain. Nothing reads the receiver yet. */
	{ REG_TIA_AMB_GAIN, 0U },

	/* Emitters dark until optical_set_current_ua() says otherwise. */
	{ REG_LEDCNTRL, 0U },

	/* Internal oscillator. See the clocking note at the top of this file. */
	{ REG_CONTROL2, CONTROL2_OSC_ENABLE },

	/* Timer stopped; optical_start() sets TIMEREN. */
	{ REG_CONTROL1, 0U },
};

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(AFE_NODE);
static const struct gpio_dt_spec reset_gpio = GPIO_DT_SPEC_GET(AFE_NODE, reset_gpios);

#if DT_NODE_HAS_PROP(AFE_NODE, tx_supply)
static const struct device *const tx_supply = DEVICE_DT_GET(DT_PHANDLE(AFE_NODE, tx_supply));
#else
static const struct device *const tx_supply = NULL;
#endif

static bool initialized;
static bool running;

/* Shadow of LEDCNTRL, because one write carries all three currents. */
static uint32_t ledcntrl;

/* Shadow of TIA_GAIN, so the gain can be reported without a register read. */
static uint8_t tia_gain = TIA_GAIN_DEFAULT;

static int afe_write(uint8_t reg, uint32_t value)
{
	const uint8_t buf[4] = {
		reg,
		(uint8_t)(value >> 16),
		(uint8_t)(value >> 8),
		(uint8_t)value,
	};

	int err = i2c_write_dt(&bus, buf, sizeof(buf));

	if (err) {
		LOG_ERR("write 0x%02x failed (%d)", reg, err);
	}

	return err;
}

/*
 * Reading anything outside the ADC output registers (0x2A-0x2F) needs
 * REG_READ set first and cleared afterwards, because while it is set the part
 * ignores writes to every other register. Leaving it set is therefore not a
 * missed optimisation but a wedged device: the next configuration write is
 * silently dropped.
 */
static int afe_read(uint8_t reg, uint32_t *value)
{
	uint8_t rx[3];
	int err;

	err = afe_write(REG_CONTROL0, CONTROL0_REG_READ);
	if (err) {
		return err;
	}

	err = i2c_write_read_dt(&bus, &reg, sizeof(reg), rx, sizeof(rx));

	/* Clear read mode whether or not the read itself worked. */
	int clear_err = afe_write(REG_CONTROL0, 0U);

	if (err) {
		LOG_ERR("read 0x%02x failed (%d)", reg, err);
		return err;
	}
	if (clear_err) {
		return clear_err;
	}

	*value = ((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2];

	return 0;
}

/*
 * The ADC output registers are the one group that reads without setting
 * REG_READ (SBAS689D section 8.3.5), so this path skips the CONTROL0
 * bracketing that afe_read() needs. That is worth a separate function rather
 * than a flag: bracketing every sample would put two extra register writes
 * into the middle of an acquisition, on the same bus and against the same
 * part that is converting.
 */
static int afe_read_adc(uint8_t reg, uint32_t *raw)
{
	uint8_t rx[3];
	int err = i2c_write_read_dt(&bus, &reg, sizeof(reg), rx, sizeof(rx));

	if (err) {
		LOG_ERR("adc read 0x%02x failed (%d)", reg, err);
		return err;
	}

	*raw = ((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2];

	return 0;
}

/* Sign-extend the 24-bit twos-complement word into int32_t. Done by hand
 * rather than by shifting a signed value, which is implementation-defined. */
static int32_t adc_to_code(uint32_t raw)
{
	if (raw & BIT(23)) {
		return (int32_t)(raw | 0xFF000000U);
	}

	return (int32_t)raw;
}

static bool adc_is_saturated(uint32_t raw)
{
	uint32_t sign = ADC_SIGN_BITS(raw);

	return (sign != 0x0U) && (sign != 0x7U);
}

/*
 * The AFE4404 has no identity register, so presence is established by writing
 * a value only this part would hold and reading it back. PRPCT is used
 * because it is 16 bits wide, so a stuck-high or stuck-low bus fails the
 * comparison rather than passing on an all-ones or all-zeros word, and
 * because the real configuration overwrites it moments later anyway.
 */
static int afe_probe(void)
{
	static const uint32_t pattern = 0xA5A5U;
	uint32_t readback = 0U;
	int err;

	err = afe_write(REG_PRPCT, pattern);
	if (err) {
		return -ERR_OPTICAL_COMM_FAILED;
	}

	err = afe_read(REG_PRPCT, &readback);
	if (err) {
		return -ERR_OPTICAL_COMM_FAILED;
	}

	if (readback != pattern) {
		LOG_ERR("readback 0x%06x, expected 0x%06x", readback, pattern);
		return -ERR_OPTICAL_NOT_RESPONDING;
	}

	return 0;
}

/*
 * SBAS689D Table 79: the reset pulse must be 25-50 us. Over 200 us is a
 * different mode entirely - hardware power-down - so this window is a mode
 * selector, not a minimum, and busy-waiting is the only way to hit it. A
 * k_sleep() here would overshoot into power-down on any tick rate this
 * project uses.
 *
 * Interrupts are locked across the pulse because k_busy_wait() guarantees
 * only a lower bound. An interrupt taken mid-pulse - the RTT console and the
 * system tick both fire in this window - stretches the high side of a 25-50 us
 * target that has only 20 us of headroom, and a stretched pulse resets
 * nothing. 30 us of latency at init costs nothing here.
 */
static void afe_reset_pulse(void)
{
	unsigned int key = irq_lock();

	gpio_pin_set_dt(&reset_gpio, 1); /* active low; 1 asserts RESETZ */
	k_busy_wait(30);
	gpio_pin_set_dt(&reset_gpio, 0);

	irq_unlock(key);
}

int optical_init(void)
{
	int err;

	initialized = false;
	running = false;
	ledcntrl = 0U;
	tia_gain = TIA_GAIN_DEFAULT;

	if (!i2c_is_ready_dt(&bus)) {
		LOG_ERR("i2c bus not ready");
		return -ERR_OPTICAL_NOT_READY;
	}

	if (!gpio_is_ready_dt(&reset_gpio)) {
		LOG_ERR("reset gpio not ready");
		return -ERR_OPTICAL_NOT_READY;
	}

	/* Inactive: RESETZ released. */
	err = gpio_pin_configure_dt(&reset_gpio, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("reset gpio configure failed (%d)", err);
		return err;
	}

	/*
	 * TX_SUP before the reset pulse, deliberately.
	 *
	 * Table 79 wants all supplies stable at least 10 ms before RESETZ goes
	 * low, and wants TX_SUP to follow RX_SUP closely. On this board it
	 * cannot: RX_SUP and IO_SUP are up from boot while TX_SUP waits on a
	 * boost that only firmware can enable. Enabling it here and only then
	 * resetting is what recovers the sequence - the part is reset once
	 * every rail it uses is up, which is the condition the table is
	 * actually protecting.
	 */
	if (tx_supply != NULL) {
		if (!device_is_ready(tx_supply)) {
			LOG_ERR("tx supply not ready");
			return -ERR_OPTICAL_NOT_READY;
		}

		err = regulator_enable(tx_supply);
		if (err) {
			LOG_ERR("tx supply enable failed (%d)", err);
			return err;
		}
	}

	k_msleep(10); /* t3 */
	afe_reset_pulse(); /* t4 */
	k_msleep(2); /* t5, > 1 ms before the first I2C command */

	err = afe_probe();
	if (err) {
		return err;
	}

	for (size_t i = 0; i < ARRAY_SIZE(afe_config); i++) {
		err = afe_write(afe_config[i].reg, afe_config[i].value);
		if (err) {
			return -ERR_OPTICAL_COMM_FAILED;
		}
	}

	initialized = true;

	LOG_INF("AFE4404 up, %u Hz PRF, emitters dark", PRF_HZ);

	return 0;
}

int optical_get_max_current_ua(enum optical_emitter emitter, uint32_t *max_ua)
{
	if (!initialized) {
		return -ERR_OPTICAL_NOT_INITIALIZED;
	}
	if ((unsigned int)emitter >= OPTICAL_EMITTER_COUNT || max_ua == NULL) {
		return -ERR_OPTICAL_INVALID_EMITTER;
	}

	*max_ua = emitter_max_ua[emitter];

	return 0;
}

int optical_get_current_step_ua(uint32_t *step_ua)
{
	if (!initialized) {
		return -ERR_OPTICAL_NOT_INITIALIZED;
	}
	if (step_ua == NULL) {
		return -EINVAL;
	}

	*step_ua = ILED_STEP_UA;

	return 0;
}

int optical_set_current_ua(enum optical_emitter emitter, uint32_t microamps)
{
	if (!initialized) {
		return -ERR_OPTICAL_NOT_INITIALIZED;
	}
	if ((unsigned int)emitter >= OPTICAL_EMITTER_COUNT) {
		return -ERR_OPTICAL_INVALID_EMITTER;
	}
	if (microamps > emitter_max_ua[emitter]) {
		LOG_ERR("emitter %u: %u uA over its %u uA rating", (unsigned int)emitter, microamps,
			emitter_max_ua[emitter]);
		return -ERR_OPTICAL_CURRENT_TOO_HIGH;
	}

	uint32_t code = microamps / ILED_STEP_UA;
	uint8_t shift = emitter_shift[emitter];

	if (code > ILED_CODE_MAX) {
		code = ILED_CODE_MAX;
	}

	uint32_t next = ledcntrl & ~((uint32_t)LEDCNTRL_ILED_MASK << shift);

	next |= code << shift;

	int err = afe_write(REG_LEDCNTRL, next);

	if (err) {
		return -ERR_OPTICAL_COMM_FAILED;
	}

	ledcntrl = next;

	return 0;
}

int optical_start(void)
{
	if (!initialized) {
		return -ERR_OPTICAL_NOT_INITIALIZED;
	}

	if (afe_write(REG_CONTROL1, CONTROL1_TIMEREN)) {
		return -ERR_OPTICAL_COMM_FAILED;
	}

	running = true;

	return 0;
}

int optical_stop(void)
{
	if (!initialized) {
		return -ERR_OPTICAL_NOT_INITIALIZED;
	}

	int err = afe_write(REG_CONTROL1, 0U);

	/*
	 * Zero the currents even if halting the timer failed - of the two,
	 * a running timer with dark emitters is the safer place to stop.
	 */
	int current_err = afe_write(REG_LEDCNTRL, 0U);

	if (current_err == 0) {
		ledcntrl = 0U;
	}

	if (err == 0) {
		running = false;
	}

	if (err || current_err) {
		return -ERR_OPTICAL_COMM_FAILED;
	}

	return 0;
}

int optical_read(struct optical_sample *sample)
{
	if (!initialized) {
		return -ERR_OPTICAL_NOT_INITIALIZED;
	}
	if (sample == NULL) {
		return -EINVAL;
	}
	if (!running) {
		return -ERR_OPTICAL_NOT_RUNNING;
	}

	struct optical_sample staged = { 0 };

	for (uint8_t i = 0; i < OPTICAL_CHANNEL_COUNT; i++) {
		uint32_t raw = 0U;

		if (afe_read_adc(channel_reg[i], &raw)) {
			return -ERR_OPTICAL_COMM_FAILED;
		}

		staged.code[i] = adc_to_code(raw);

		if (adc_is_saturated(raw)) {
			staged.saturated |= BIT(i);
		}
	}

	/* Staged, so a failure part way through leaves the caller's sample
	 * untouched rather than half updated with a mix of old and new. */
	*sample = staged;

	return 0;
}

int optical_get_full_scale_code(int32_t *code)
{
	if (!initialized) {
		return -ERR_OPTICAL_NOT_INITIALIZED;
	}
	if (code == NULL) {
		return -EINVAL;
	}

	*code = ADC_FULL_SCALE_CODE;

	return 0;
}

int optical_get_gain_ohms(uint32_t *ohms)
{
	if (!initialized) {
		return -ERR_OPTICAL_NOT_INITIALIZED;
	}
	if (ohms == NULL) {
		return -EINVAL;
	}

	*ohms = tia_gain_ohms[tia_gain];

	return 0;
}

int optical_set_gain_ohms(uint32_t ohms)
{
	if (!initialized) {
		return -ERR_OPTICAL_NOT_INITIALIZED;
	}

	/* Nearest supported value. The table is unordered, so this is a linear
	 * search on absolute difference rather than a bound. */
	uint8_t best = 0U;
	uint32_t best_delta = UINT32_MAX;

	for (uint8_t i = 0; i < ARRAY_SIZE(tia_gain_ohms); i++) {
		uint32_t delta = (ohms > tia_gain_ohms[i]) ? (ohms - tia_gain_ohms[i])
							   : (tia_gain_ohms[i] - ohms);

		if (delta < best_delta) {
			best_delta = delta;
			best = i;
		}
	}

	/* TIA_CF stays at its reset value; only the resistor is exposed. */
	if (afe_write(REG_TIA_AMB_GAIN, best & TIA_GAIN_MASK)) {
		return -ERR_OPTICAL_COMM_FAILED;
	}

	tia_gain = best;

	return 0;
}

#else /* !DT_NODE_HAS_STATUS_OKAY(AFE_NODE) */

int optical_init(void)
{
	LOG_ERR("no afe4404 node on this board");
	return -ERR_OPTICAL_NOT_READY;
}

int optical_get_max_current_ua(enum optical_emitter emitter, uint32_t *max_ua)
{
	ARG_UNUSED(emitter);
	ARG_UNUSED(max_ua);
	return -ERR_OPTICAL_NOT_INITIALIZED;
}

int optical_get_current_step_ua(uint32_t *step_ua)
{
	ARG_UNUSED(step_ua);
	return -ERR_OPTICAL_NOT_INITIALIZED;
}

int optical_set_current_ua(enum optical_emitter emitter, uint32_t microamps)
{
	ARG_UNUSED(emitter);
	ARG_UNUSED(microamps);
	return -ERR_OPTICAL_NOT_INITIALIZED;
}

int optical_start(void)
{
	return -ERR_OPTICAL_NOT_INITIALIZED;
}

int optical_stop(void)
{
	return -ERR_OPTICAL_NOT_INITIALIZED;
}

int optical_read(struct optical_sample *sample)
{
	ARG_UNUSED(sample);
	return -ERR_OPTICAL_NOT_INITIALIZED;
}

int optical_get_full_scale_code(int32_t *code)
{
	ARG_UNUSED(code);
	return -ERR_OPTICAL_NOT_INITIALIZED;
}

int optical_get_gain_ohms(uint32_t *ohms)
{
	ARG_UNUSED(ohms);
	return -ERR_OPTICAL_NOT_INITIALIZED;
}

int optical_set_gain_ohms(uint32_t ohms)
{
	ARG_UNUSED(ohms);
	return -ERR_OPTICAL_NOT_INITIALIZED;
}

#endif /* DT_NODE_HAS_STATUS_OKAY(AFE_NODE) */
