// SPDX-License-Identifier: GPL-2.0-only
/*
 * es8156.c -- es8156 ALSA SoC audio driver
 * Copyright Everest Semiconductor Co.,Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Ported from the Zhihe A210 SDK.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/limits.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "es8156.h"

#define ES8156_DEF_VOL			0xBF
#define ES8156_VOL_MAX			0xBF

static const struct es8156_gpio es8156_gpio[] = {
	{ ES8156_GPIO_3V3_PWREN, "audio-3v3-pwren" },
	{ ES8156_GPIO_1V8_PWREN, "audio-1v8-pwren" },
	{ ES8156_GPIO_HP_CTL, "audio-hp-ctl" },
};

static const struct reg_default es8156_reg_defaults[] = {
	{0x00, 0x1c}, {0x01, 0x20}, {0x02, 0x00}, {0x03, 0x01},
	{0x04, 0x00}, {0x05, 0x04}, {0x06, 0x11}, {0x07, 0x00},
	{0x08, 0x06}, {0x09, 0x00}, {0x0a, 0x50}, {0x0b, 0x50},
	{0x0c, 0x00}, {0x0d, 0x10}, {0x10, 0x40},
	{0x11, 0x00}, {0x12, 0x04}, {0x13, 0x11}, {0x14, 0xbf},
	{0x15, 0x00}, {0x16, 0x00}, {0x17, 0xf7}, {0x18, 0x00},
	{0x19, 0x20}, {0x1a, 0x00}, {0x20, 0x16}, {0x21, 0x7f},
	{0x22, 0x00}, {0x23, 0x86}, {0x24, 0x00}, {0x25, 0x07},
	{0xfc, 0x00}, {0xfd, 0x81}, {0xfe, 0x55}, {0xff, 0x10},
};

/* Registers saved on system suspend and restored on resume, in order. */
static const unsigned int es8156_suspend_regs[] = {
	ES8156_RESET_REG00,
	ES8156_MAINCLOCK_CTL_REG01,
	ES8156_SCLK_MODE_REG02,
	ES8156_LRCLK_DIV_H_REG03,
	ES8156_LRCLK_DIV_L_REG04,
	ES8156_SCLK_DIV_REG05,
	ES8156_NFS_CONFIG_REG06,
	ES8156_MISC_CONTROL1_REG07,
	ES8156_CLOCK_ON_OFF_REG08,
	ES8156_MISC_CONTROL2_REG09,
	ES8156_TIME_CONTROL1_REG0A,
	ES8156_TIME_CONTROL2_REG0B,
	ES8156_CHIP_STATUS_REG0C,
	ES8156_P2S_CONTROL_REG0D,
	ES8156_DAC_OSR_COUNTER_REG10,
	ES8156_DAC_SDP_REG11,
	ES8156_AUTOMUTE_SET_REG12,
	ES8156_DAC_MUTE_REG13,
	ES8156_VOLUME_CONTROL_REG14,
	ES8156_ALC_CONFIG1_REG15,
	ES8156_ALC_CONFIG2_REG16,
	ES8156_ALC_CONFIG3_REG17,
	ES8156_MISC_CONTROL3_REG18,
	ES8156_EQ_CONTROL1_REG19,
	ES8156_EQ_CONTROL2_REG1A,
	ES8156_ANALOG_SYS1_REG20,
	ES8156_ANALOG_SYS2_REG21,
	ES8156_ANALOG_SYS3_REG22,
	ES8156_ANALOG_SYS4_REG23,
	ES8156_ANALOG_LP_REG24,
	ES8156_ANALOG_SYS5_REG25,
};

/* codec private data */
struct es8156_priv {
	struct regmap *regmap;
	struct snd_soc_component *component;
	struct regulator *avdd;
	struct regulator *dvdd;
	struct regulator *pvdd;
	struct gpio_desc *audio_pw_desc[ES8156_GPIO_MAX];

	u32 mclk_sclk_ratio;
	u32 mclk_src;

	u32 suspend_regs[ARRAY_SIZE(es8156_suspend_regs)];
};

/*
 * es8156_reset
 */
static int es8156_reset(struct snd_soc_component *codec)
{
	snd_soc_component_write(codec, ES8156_RESET_REG00, 0x1c);
	usleep_range(5000, 5500);
	return snd_soc_component_write(codec, ES8156_RESET_REG00, 0x03);
}

static const char * const es8156_DAC_SRC[] = {
	"Left to Left, Right to Right",
	"Right to both Left and Right",
	"Left to both Left & Right",
	"Left to Right, Right to Left"
};

static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -9550, 50, 1);
static const DECLARE_TLV_DB_SCALE(alc_gain_tlv, -2800, 400, 1);
static SOC_ENUM_SINGLE_DECL(es8165_dac_enum, ES8156_MISC_CONTROL3_REG18, 4, es8156_DAC_SRC);

static const struct snd_kcontrol_new es8156_DAC_MUX =
	SOC_DAPM_ENUM("Route", es8165_dac_enum);

static const struct snd_kcontrol_new es8156_snd_controls[] = {
	SOC_SINGLE("Timer 1", ES8156_TIME_CONTROL1_REG0A, 0, 63, 0),
	SOC_SINGLE("Timer 2", ES8156_TIME_CONTROL2_REG0B, 0, 63, 0),
	SOC_SINGLE("DAC Automute Gate", ES8156_AUTOMUTE_SET_REG12, 4, 15, 0),
	SOC_SINGLE_TLV("ALC Gain", ES8156_ALC_CONFIG1_REG15, 1, 7, 1, alc_gain_tlv),
	SOC_SINGLE("ALC Ramp Rate", ES8156_ALC_CONFIG2_REG16, 4, 15, 0),
	SOC_SINGLE("ALC Window Size", ES8156_ALC_CONFIG2_REG16, 0, 15, 0),
	SOC_DOUBLE("ALC Maximum Minimum Volume", ES8156_ALC_CONFIG3_REG17,
		   4, 0, 15, 0),
	/* DAC Digital controls */
	SOC_SINGLE_TLV("Master Playback Volume", ES8156_VOLUME_CONTROL_REG14, 0,
		       ES8156_VOL_MAX, 0, dac_vol_tlv),
	SOC_SINGLE("HP Switch", ES8156_ANALOG_SYS3_REG22, 3, 1, 0),
};

static const struct snd_soc_dapm_widget es8156_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_OUT("AIFSDOUT", "I2S Capture", 0,
			     ES8156_P2S_CONTROL_REG0D, 2, 0),

	SND_SOC_DAPM_AIF_IN("SDIN", "I2S Playback", 0,
			    SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_MUX("Channel Select Mux", SND_SOC_NOPM, 0, 0,
			 &es8156_DAC_MUX),

	SND_SOC_DAPM_DAC("DACL", "Left Playback", ES8156_DAC_MUTE_REG13, 1, 1),
	SND_SOC_DAPM_DAC("DACR", "Right Playback", ES8156_DAC_MUTE_REG13, 2, 1),

	SND_SOC_DAPM_PGA("SDOUT TRISTATE", ES8156_P2S_CONTROL_REG0D,
			 0, 1, NULL, 0),

	SND_SOC_DAPM_OUTPUT("SDOUT"),
	SND_SOC_DAPM_OUTPUT("LOUT"),
	SND_SOC_DAPM_OUTPUT("ROUT"),
};

/*************** parameter define ***************/
#define STATEconfirm		0x0C
#define NORMAL_I2S		0x00
#define NORMAL_LJ		0x01
#define NORMAL_DSPA		0x03
#define NORMAL_DSPB		0x07
#define Format_Len24		0x00
#define Format_Len20		0x01
#define Format_Len18		0x02
#define Format_Len16		0x03
#define Format_Len32		0x04

#define VDDA_3V3		0x00
#define VDDA_1V8		0x01
#define MCLK_PIN		0x00
#define SCLK_PIN		0x01

/**************************************************/
#define MSMode_MasterSelOn	0		// SlaveMode:0, MasterMode:1
#define Ratio			64		// Ratio = MCLK/LRCK on board
#define Format			NORMAL_I2S
#define Format_Len		Format_Len16	// data format
#define SCLK_DIV		8		// SCLK_DIV = MCLK/SCLK
#define SCLK_INV		0
#define EQ7bandOn		0
#define VDDA_VOLTAGE		VDDA_3V3
#define DAC_Volume		191		// DAC digital gain
#define DACHPModeOn		0		// disable:0, enable:1

/**************************************************/

static int es8156_init_regs(struct snd_soc_component *codec)
{
	struct es8156_priv *priv = snd_soc_component_get_drvdata(codec);
	unsigned int ratio = priv->mclk_sclk_ratio * Ratio;
	unsigned int config_val = (priv->mclk_src << 7) + (SCLK_INV << 4) +
				  (EQ7bandOn << 3) + 0x04 + MSMode_MasterSelOn;

	dev_dbg(codec->dev, "%s\n", __func__);

	snd_soc_component_write(codec, 0x02, config_val);
	snd_soc_component_write(codec, 0x19, 0x20);

	if (DACHPModeOn == 0) { // output from PA
		snd_soc_component_write(codec, 0x20, 0x2A);
		snd_soc_component_write(codec, 0x21, 0x3C);
		snd_soc_component_write(codec, 0x22, 0x02);
		snd_soc_component_write(codec, 0x24, 0x07);
		snd_soc_component_write(codec, 0x23, 0x40 + (0x30 * VDDA_VOLTAGE));
	} else if (DACHPModeOn == 1) { // output from headphone
		snd_soc_component_write(codec, 0x20, 0x16);
		snd_soc_component_write(codec, 0x21, 0x3F);
		snd_soc_component_write(codec, 0x22, 0x0A);
		snd_soc_component_write(codec, 0x24, 0x01);
		snd_soc_component_write(codec, 0x23, 0xCA + (0x30 * VDDA_VOLTAGE));
	}
	snd_soc_component_write(codec, 0x0A, 0x01);
	snd_soc_component_write(codec, 0x0B, 0x01);

	switch (ratio) {
	case 1536: // Ratio=MCLK/LRCK=1536; 12M288/8K; 24M576/16K
		snd_soc_component_write(codec, 0x01, 0x26 - (0x03 * EQ7bandOn));
		snd_soc_component_write(codec, 0x09, 0x00);
		snd_soc_component_write(codec, 0x03, 0x06); // LRCK H
		snd_soc_component_write(codec, 0x04, 0x00); // LRCK=MCLK/1536
		snd_soc_component_write(codec, 0x05, SCLK_DIV);
		break;
	case 1024: // Ratio=MCLK/LRCK=1024; 12M288/12K; 24M576/24K
		snd_soc_component_write(codec, 0x01, 0x24 - (0x02 * EQ7bandOn));
		snd_soc_component_write(codec, 0x09, 0x00);
		snd_soc_component_write(codec, 0x03, 0x04); // LRCK H
		snd_soc_component_write(codec, 0x04, 0x00); // LRCK=MCLK/1024
		snd_soc_component_write(codec, 0x05, SCLK_DIV);
		break;
	case 768: // Ratio=MCLK/LRCK=768; 12M288/16K; 24M576/32K
		snd_soc_component_write(codec, 0x01, 0x23 + (0x40 * EQ7bandOn));
		snd_soc_component_write(codec, 0x09, 0x00);
		snd_soc_component_write(codec, 0x03, 0x03); // LRCK H
		snd_soc_component_write(codec, 0x04, 0x00); // LRCK=MCLK/768
		snd_soc_component_write(codec, 0x05, SCLK_DIV);
		break;
	case 512: // Ratio=MCLK/LRCK=512; 12M288/24K; 24M576/48K
		snd_soc_component_write(codec, 0x01, 0xC0 + 0x22 - (0x01 * EQ7bandOn));
		snd_soc_component_write(codec, 0x09, 0x00);
		snd_soc_component_write(codec, 0x03, 0x02); // LRCK H
		snd_soc_component_write(codec, 0x04, 0x00); // LRCK=MCLK/512
		snd_soc_component_write(codec, 0x05, SCLK_DIV);
		break;
	case 400: // Ratio=MCLK/LRCK=400; 19M2/48K
		// DVDD must be 3.3V
		snd_soc_component_write(codec, 0x01, 0x21 + (0x40 * EQ7bandOn));
		snd_soc_component_write(codec, 0x09, 0x00);
		snd_soc_component_write(codec, 0x10, 0x64); // 400 OSR
		snd_soc_component_write(codec, 0x03, 0x01); // LRCK H
		snd_soc_component_write(codec, 0x04, 0x90); // LRCK=MCLK/400
		snd_soc_component_write(codec, 0x05, SCLK_DIV);
		break;
	case 384: // Ratio=MCLK/LRCK=384; 12M288/32K; 6M144/16K
		snd_soc_component_write(codec, 0x01, 0x63 + (0x40 * EQ7bandOn));
		snd_soc_component_write(codec, 0x09, 0x00);
		snd_soc_component_write(codec, 0x03, 0x01); // LRCK H
		snd_soc_component_write(codec, 0x04, 0x80); // LRCK=MCLK/384
		snd_soc_component_write(codec, 0x05, SCLK_DIV);
		break;
	case 256: // Ratio=MCLK/LRCK=256; 12M288/48K
		snd_soc_component_write(codec, 0x01, 0x21 + (0x40 * EQ7bandOn));
		snd_soc_component_write(codec, 0x09, 0x00);
		snd_soc_component_write(codec, 0x03, 0x01); // LRCK H
		snd_soc_component_write(codec, 0x04, 0x00); // LRCK=MCLK/256
		snd_soc_component_write(codec, 0x05, SCLK_DIV);
		break;
	case 128: // Ratio=MCLK/LRCK=128; 6M144/48K
		snd_soc_component_write(codec, 0x01, 0x61 + (0x40 * EQ7bandOn));
		snd_soc_component_write(codec, 0x09, 0x00);
		snd_soc_component_write(codec, 0x03, 0x00); // LRCK H
		snd_soc_component_write(codec, 0x04, 0x80); // LRCK=MCLK/128
		snd_soc_component_write(codec, 0x05, SCLK_DIV);
		break;
	case 64: // Ratio=MCLK/LRCK=64; 3M072/48K
		snd_soc_component_write(codec, 0x01, 0xE1);
		snd_soc_component_write(codec, 0x09, 0x02);
		snd_soc_component_write(codec, 0x03, 0x00); // LRCK H
		snd_soc_component_write(codec, 0x04, 0x40); // LRCK=MCLK/64
		snd_soc_component_write(codec, 0x05, SCLK_DIV);
		break;
	default:
		break;
	}

	snd_soc_component_write(codec, 0x0D, 0x14);
	snd_soc_component_write(codec, 0x18, 0x00);
	snd_soc_component_write(codec, 0x08, 0x3F);
	snd_soc_component_write(codec, 0x00, 0x02);
	snd_soc_component_write(codec, 0x00, 0x03);
	snd_soc_component_write(codec, 0x25, 0x20);

	return 0;
}

static int es8156_init_sequence(struct snd_soc_component *codec)
{
	es8156_init_regs(codec);
	snd_soc_component_write(codec, ES8156_VOLUME_CONTROL_REG14, DAC_Volume);

	return 0;
}

static const struct snd_soc_dapm_route es8156_dapm_routes[] = {
	{"SDOUT TRISTATE", NULL, "SDIN"},
	{"SDOUT", NULL, "SDOUT TRISTATE"},

	{"Channel Select Mux", "Left to Left, Right to Right", "SDIN"},
	{"Channel Select Mux", "Right to both Left and Right", "SDIN"},
	{"Channel Select Mux", "Left to both Left & Right", "SDIN"},
	{"Channel Select Mux", "Left to Right, Right to Left", "SDIN"},

	{"DACL", NULL, "Channel Select Mux"},
	{"DACR", NULL, "Channel Select Mux"},

	{ "LOUT", NULL, "DACL" },
	{ "ROUT", NULL, "DACR" },
};

static int es8156_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_component *codec = codec_dai->component;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP: /* es8156 clock provider (master) */
		snd_soc_component_update_bits(codec, ES8156_SCLK_MODE_REG02, 0x01, 0x01);
		break;
	case SND_SOC_DAIFMT_CBC_CFC: /* es8156 clock consumer (slave) */
		snd_soc_component_update_bits(codec, ES8156_SCLK_MODE_REG02, 0x01, 0x00);
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		snd_soc_component_update_bits(codec, ES8156_DAC_SDP_REG11, 0x07, 0x00);
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		return -EINVAL;
	case SND_SOC_DAIFMT_LEFT_J:
		snd_soc_component_update_bits(codec, ES8156_DAC_SDP_REG11, 0x07, 0x01);
		break;
	case SND_SOC_DAIFMT_DSP_A:
		snd_soc_component_update_bits(codec, ES8156_DAC_SDP_REG11, 0x07, 0x03);
		break;
	case SND_SOC_DAIFMT_DSP_B:
		snd_soc_component_update_bits(codec, ES8156_DAC_SDP_REG11, 0x07, 0x07);
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		snd_soc_component_update_bits(codec, ES8156_SCLK_MODE_REG02, 0x01, 0x00);
		break;
	case SND_SOC_DAIFMT_IB_IF:
		snd_soc_component_update_bits(codec, ES8156_SCLK_MODE_REG02, 0x01, 0x01);
		break;
	case SND_SOC_DAIFMT_IB_NF:
		snd_soc_component_update_bits(codec, ES8156_SCLK_MODE_REG02, 0x01, 0x01);
		break;
	case SND_SOC_DAIFMT_NB_IF:
		snd_soc_component_update_bits(codec, ES8156_SCLK_MODE_REG02, 0x01, 0x00);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int es8156_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *codec = dai->component;
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				     4096, UINT_MAX);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		snd_soc_component_update_bits(codec, ES8156_DAC_SDP_REG11, 0x70, 0x30);
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		snd_soc_component_update_bits(codec, ES8156_DAC_SDP_REG11, 0x70, 0x10);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		snd_soc_component_update_bits(codec, ES8156_DAC_SDP_REG11, 0x70, 0x00);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		snd_soc_component_update_bits(codec, ES8156_DAC_SDP_REG11, 0x70, 0x40);
		break;
	default:
		break;
	}

	return 0;
}

static int es8156_set_bias_level(struct snd_soc_component *codec,
				 enum snd_soc_bias_level level)
{
	/* Nothing to do: the vendor driver had all bias handling disabled. */
	return 0;
}

#define ES8156_RATES SNDRV_PCM_RATE_8000_96000

#define ES8156_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops es8156_ops = {
	.hw_params = es8156_pcm_hw_params,
	.set_fmt = es8156_set_dai_fmt,
};

static struct snd_soc_dai_driver es8156_dai = {
	.name = "ES8156 HiFi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = ES8156_RATES,
		.formats = ES8156_FORMATS,
	},
	.ops = &es8156_ops,
};

static int es8156_suspend(struct snd_soc_component *codec)
{
	struct es8156_priv *priv = snd_soc_component_get_drvdata(codec);
	int i;

	dev_dbg(codec->dev, "Entered: %s\n", __func__);

	es8156_set_bias_level(codec, SND_SOC_BIAS_OFF);
	for (i = 0; i < ARRAY_SIZE(es8156_suspend_regs); i++)
		priv->suspend_regs[i] =
			snd_soc_component_read(codec, es8156_suspend_regs[i]);

	return 0;
}

static int es8156_resume(struct snd_soc_component *codec)
{
	struct es8156_priv *priv = snd_soc_component_get_drvdata(codec);
	int i;

	dev_dbg(codec->dev, "Entered: %s\n", __func__);

	for (i = 0; i < ARRAY_SIZE(es8156_suspend_regs); i++)
		snd_soc_component_write(codec, es8156_suspend_regs[i],
					priv->suspend_regs[i]);

	return 0;
}

static int es8156_probe(struct snd_soc_component *codec)
{
	struct es8156_priv *es8156 = snd_soc_component_get_drvdata(codec);
	int ret;

	es8156->component = codec;

	/* power up the controller */
	if (es8156->avdd) {
		ret = regulator_enable(es8156->avdd);
		if (ret)
			goto err_avdd;
	}
	if (es8156->dvdd) {
		ret = regulator_enable(es8156->dvdd);
		if (ret)
			goto err_dvdd;
	}
	if (es8156->pvdd) {
		ret = regulator_enable(es8156->pvdd);
		if (ret)
			goto err_pvdd;
	}

	es8156_reset(codec);
	es8156_init_sequence(codec);

	return 0;

err_pvdd:
	if (es8156->dvdd)
		regulator_disable(es8156->dvdd);
err_dvdd:
	if (es8156->avdd)
		regulator_disable(es8156->avdd);
err_avdd:
	dev_err(codec->dev, "Failed to enable VDD regulator: %d\n", ret);
	es8156->component = NULL;
	return ret;
}

static void es8156_remove(struct snd_soc_component *codec)
{
	struct es8156_priv *es8156 = snd_soc_component_get_drvdata(codec);

	es8156_set_bias_level(codec, SND_SOC_BIAS_OFF);
	/* power down the controller */
	if (es8156->pvdd)
		regulator_disable(es8156->pvdd);
	if (es8156->dvdd)
		regulator_disable(es8156->dvdd);
	if (es8156->avdd)
		regulator_disable(es8156->avdd);

	es8156->component = NULL;
}

static const struct regmap_config es8156_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= 0xff,
	.cache_type	= REGCACHE_MAPLE,
	.reg_defaults	= es8156_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(es8156_reg_defaults),
};

static const struct snd_soc_component_driver soc_codec_dev_es8156 = {
	.name			= "es8156",
	.probe			= es8156_probe,
	.remove			= es8156_remove,
	.suspend		= pm_sleep_ptr(es8156_suspend),
	.resume			= pm_sleep_ptr(es8156_resume),
	.set_bias_level		= es8156_set_bias_level,
	.controls		= es8156_snd_controls,
	.num_controls		= ARRAY_SIZE(es8156_snd_controls),
	.dapm_widgets		= es8156_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(es8156_dapm_widgets),
	.dapm_routes		= es8156_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(es8156_dapm_routes),
};

static struct regulator *es8156_get_supply(struct device *dev, const char *id)
{
	struct regulator *reg;

	reg = devm_regulator_get(dev, id);
	if (IS_ERR(reg)) {
		if (PTR_ERR(reg) == -EPROBE_DEFER)
			return reg;
		dev_warn(dev, "Failed to get %s regulator: %ld\n",
			 id, PTR_ERR(reg));
		return NULL;
	}

	return reg;
}

/*
 * es8156 7bit i2c address:CE pin:0 0x08 / CE pin:1 0x09
 */
static int es8156_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct device_node *np = dev->of_node;
	struct es8156_priv *es8156;
	int ret;
	int i;

	es8156 = devm_kzalloc(dev, sizeof(*es8156), GFP_KERNEL);
	if (!es8156)
		return -ENOMEM;

	es8156->regmap = devm_regmap_init_i2c(i2c, &es8156_regmap_config);
	if (IS_ERR(es8156->regmap))
		return dev_err_probe(dev, PTR_ERR(es8156->regmap),
				     "Failed to init regmap\n");

	es8156->avdd = es8156_get_supply(dev, "AVDD");
	if (IS_ERR(es8156->avdd))
		return PTR_ERR(es8156->avdd);
	es8156->dvdd = es8156_get_supply(dev, "DVDD");
	if (IS_ERR(es8156->dvdd))
		return PTR_ERR(es8156->dvdd);
	es8156->pvdd = es8156_get_supply(dev, "PVDD");
	if (IS_ERR(es8156->pvdd))
		return PTR_ERR(es8156->pvdd);

	if (of_property_read_u32(np, "mclk-sclk-ratio", &es8156->mclk_sclk_ratio))
		es8156->mclk_sclk_ratio = 1;

	if (es8156->mclk_sclk_ratio == 1)
		es8156->mclk_src = SCLK_PIN;
	else
		es8156->mclk_src = MCLK_PIN;

	i2c_set_clientdata(i2c, es8156);

	/* Optional power/headphone control GPIOs, driven active on probe. */
	for (i = 0; i < ARRAY_SIZE(es8156_gpio); i++) {
		es8156->audio_pw_desc[i] =
			devm_gpiod_get_optional(dev, es8156_gpio[i].name,
						GPIOD_OUT_HIGH);
		if (IS_ERR(es8156->audio_pw_desc[i]))
			return dev_err_probe(dev, PTR_ERR(es8156->audio_pw_desc[i]),
					     "Failed to get %s gpio\n",
					     es8156_gpio[i].name);
	}

	/*
	 * The power-enable GPIOs may have just switched the codec supplies
	 * on: give the rails and the chip time to come up before the first
	 * I2C access, otherwise it NACKs (-EREMOTEIO) the reset write.
	 */
	if (es8156->audio_pw_desc[ES8156_GPIO_3V3_PWREN] ||
	    es8156->audio_pw_desc[ES8156_GPIO_1V8_PWREN])
		msleep(20);

	ret = devm_snd_soc_register_component(dev, &soc_codec_dev_es8156,
					      &es8156_dai, 1);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register component\n");

	return 0;
}

static void es8156_i2c_shutdown(struct i2c_client *client)
{
	struct es8156_priv *es8156 = i2c_get_clientdata(client);

	if (es8156 && es8156->component) {
		msleep(20);
		es8156_set_bias_level(es8156->component, SND_SOC_BIAS_OFF);
	}
}

static const struct i2c_device_id es8156_i2c_id[] = {
	{ "es8156" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, es8156_i2c_id);

static const struct of_device_id es8156_of_match[] = {
	{ .compatible = "everest,es8156", },
	{ }
};
MODULE_DEVICE_TABLE(of, es8156_of_match);

static struct i2c_driver es8156_i2c_driver = {
	.driver = {
		.name		= "es8156",
		.of_match_table = es8156_of_match,
	},
	.probe    = es8156_i2c_probe,
	.shutdown = es8156_i2c_shutdown,
	.id_table = es8156_i2c_id,
};
module_i2c_driver(es8156_i2c_driver);

MODULE_DESCRIPTION("ASoC es8156 driver");
MODULE_LICENSE("GPL");
