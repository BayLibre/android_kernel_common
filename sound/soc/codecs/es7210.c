// SPDX-License-Identifier: GPL-2.0-only
/*
 * ALSA SoC ES7210 adc driver
 *
 * Copyright:   (C) 2018 Everest Semiconductor Co Ltd.,
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Notes:
 *  ES7210 is a 4-ch ADC of Everest
 *
 * Ported from the Zhihe A210 SDK.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "es7210.h"

#define DOUBLESPEED 0

#define ENABLE      1
#define DISABLE     0

#define MIC_CHN_16  16
#define MIC_CHN_14  14
#define MIC_CHN_12  12
#define MIC_CHN_10  10
#define MIC_CHN_8   8
#define MIC_CHN_6   6
#define MIC_CHN_4   4
#define MIC_CHN_2   2

#define ES7210_TDM_ENABLE   ENABLE

/*
 * Number of cascaded ES7210 chips and total number of channels, from the
 * "channels-max" DT property. Shared by all instances (vendor design: the
 * chips of a microphone array are driven together).
 */
static int ADC_DEV_MAXNUM = 1;
static int ES7210_CHANNELS_MAX = 1;

#define ES7210_TDM_1LRCK_DSPA                 0
#define ES7210_TDM_1LRCK_DSPB                 1
#define ES7210_TDM_1LRCK_I2S                  2
#define ES7210_TDM_1LRCK_LJ                   3
#define ES7210_TDM_NLRCK_DSPA                 4
#define ES7210_TDM_NLRCK_DSPB                 5
#define ES7210_TDM_NLRCK_I2S                  6
#define ES7210_TDM_NLRCK_LJ                   7
#define ES7210_NORMAL_I2S                     8

#define ES7210_WORK_MODE    ES7210_TDM_1LRCK_DSPB

#define ES7210_I2C_BUS_NUM          1
#define ES7210_CODEC_RW_TEST_EN     0
#define ES7210_IDLE_RESET_EN        1   //reset ES7210 when in idle time

#define ES7210_MIC_GAIN 0x1c  // need check hw design and channel
#define ES7210_AEC_GAIN 0x13  // need check hw design and channel

/*  to set internal mclk and adclrclk ratio   */

#define RATIO_768  0xC3
#define RATIO_384  0x03
#define RATIO_256  0xC1
#define RATIO_192  0x43
#define RATIO_128  0x01
#define RATIO_64  0x41 /* mclk from bclk pin */

/*
 * mclk=sclk. mclk/lrck = sclk / lrck = 16bit*4slot=RATIO_64.
 * 32bit*4slot=16bit*8slot=RATIO_128  32bit*8slot=RATIO_256
 */
#define ES7210_MCLK_LRCK_RATIO   RATIO_256

/* I2C clients of the cascaded chips, indexed by MicArray_N index */
static struct i2c_client *i2c_clt1[ES7210_MAX_CHIPS];

static int es7210_init_reg;
static int es7210_hw_param_reg;

struct es7210_reg_config {
	unsigned char reg_addr;
	unsigned char reg_v;
};

static const struct es7210_reg_config es7210_tdm_reg_common_cfg1[] = {
	{ 0x00, 0xFF },
	{ 0x00, 0x32 },
	{ 0x09, 0x30 },
	{ 0x0A, 0x30 },
	{ 0x23, 0x2a },
	{ 0x22, 0x0a },
	{ 0x21, 0x2a },
	{ 0x20, 0x0a },
};

static const struct es7210_reg_config es7210_tdm_reg_common_cfg2[] = {
	{ 0x40, 0xC3 },
	{ 0x41, 0x70 },
	{ 0x42, 0x70 },
	{ 0x43, 0x1E },
	{ 0x44, 0x1E },
	{ 0x45, 0x1E },
	{ 0x46, 0x1E },
	{ 0x47, 0x08 },
	{ 0x48, 0x08 },
	{ 0x49, 0x08 },
	{ 0x4A, 0x08 },
	{ 0x07, 0x20 },
};

static const struct es7210_reg_config es7210_tdm_reg_common_cfg3[] = {
	{ 0x06, 0x00 },
	{ 0x4B, 0x0F },
	{ 0x4C, 0x0F },
	{ 0x00, 0x71 },
	{ 0x00, 0x41 },
};

/* Registers saved on system suspend and restored on resume, in order. */
static const u8 es7210_suspend_regs[ES7210_NUM_SUSPEND_REGS] = {
	ES7210_RESET_CTL_REG00,
	ES7210_CLK_ON_OFF_REG01,
	ES7210_MCLK_CTL_REG02,
	ES7210_MST_CLK_CTL_REG03,
	ES7210_MST_LRCDIVH_REG04,
	ES7210_MST_LRCDIVL_REG05,
	ES7210_DIGITAL_PDN_REG06,
	ES7210_ADC_OSR_REG07,
	ES7210_MODE_CFG_REG08,
	ES7210_TCT0_CHPINI_REG09,
	ES7210_TCT1_CHPINI_REG0A,
	ES7210_CHIP_STA_REG0B,
	ES7210_IRQ_CTL_REG0C,
	ES7210_MISC_CTL_REG0D,
	ES7210_DMIC_CTL_REG10,
	ES7210_SDP_CFG1_REG11,
	ES7210_SDP_CFG2_REG12,
	ES7210_ADC_AUTOMUTE_REG13,
	ES7210_ADC34_MUTE_REG14,
	ES7210_ADC12_MUTE_REG15,
	ES7210_ALC_SEL_REG16,
	ES7210_ALC_COM_CFG1_REG17,
	ES7210_ALC34_LVL_REG18,
	ES7210_ALC12_LVL_REG19,
	ES7210_ALC_COM_CFG2_REG1A,
	ES7210_ALC4_MAX_GAIN_REG1B,
	ES7210_ALC3_MAX_GAIN_REG1C,
	ES7210_ALC2_MAX_GAIN_REG1D,
	ES7210_ALC1_MAX_GAIN_REG1E,
	ES7210_ADC34_HPF2_REG20,
	ES7210_ADC34_HPF1_REG21,
	ES7210_ADC12_HPF2_REG22,
	ES7210_ADC12_HPF1_REG23,
	ES7210_ANALOG_SYS_REG40,
	ES7210_MICBIAS12_REG41,
	ES7210_MICBIAS34_REG42,
	ES7210_MIC1_GAIN_REG43,
	ES7210_MIC2_GAIN_REG44,
	ES7210_MIC3_GAIN_REG45,
	ES7210_MIC4_GAIN_REG46,
	ES7210_MIC1_LP_REG47,
	ES7210_MIC2_LP_REG48,
	ES7210_MIC3_LP_REG49,
	ES7210_MIC4_LP_REG4A,
	ES7210_MIC12_PDN_REG4B,
	ES7210_MIC34_PDN_REG4C,
};

static int es7210_read(u8 reg, u8 *rt_value, struct i2c_client *client)
{
	int ret;
	u8 read_cmd[3] = {0};
	u8 cmd_len = 0;

	*rt_value = 0;

	if (!client)
		return -ENODEV;

	read_cmd[0] = reg;
	cmd_len = 1;

	ret = i2c_master_send(client, read_cmd, cmd_len);
	if (ret != cmd_len)
		return -EIO;

	ret = i2c_master_recv(client, rt_value, 1);
	if (ret != 1)
		return -EIO;

	return 0;
}

static int es7210_write(u8 reg, unsigned char value, struct i2c_client *client)
{
	int ret;
	u8 write_cmd[2] = {0};

	if (!client)
		return -ENODEV;

	write_cmd[0] = reg;
	write_cmd[1] = value;

	ret = i2c_master_send(client, write_cmd, 2);
	if (ret != 2)
		return -EIO;

	return 0;
}

static int es7210_update_bits(u8 reg, u8 mask, u8 value, struct i2c_client *client)
{
	u8 val_old, val_new;
	int ret;

	ret = es7210_read(reg, &val_old, client);
	if (ret)
		return ret;

	val_new = (val_old & ~mask) | (value & mask);
	if (val_new != val_old)
		return es7210_write(reg, val_new, client);

	return 0;
}

static int es7210_multi_chips_write(u8 reg, unsigned char value)
{
	u8 i;

	for (i = 0; i < ADC_DEV_MAXNUM; i++) {
		es7210_write(reg, value, i2c_clt1[i]);
	}

	return 0;
}

static int es7210_multi_chips_update_bits(u8 reg, u8 mask, u8 value)
{
	u8 i;

	for (i = 0; i < ADC_DEV_MAXNUM; i++) {
		es7210_update_bits(reg, mask, value, i2c_clt1[i]);
	}

	return 0;
}

/*
* Note that this should be called from init rather than from hw_params.
*/
static int es7210_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	return 0;
}

static int es7210_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	return 0;
}
static void es7210_tdm_init_ratio(struct es7210_priv *priv)
{
	int cnt, channel, width = 32;

	channel = ES7210_CHANNELS_MAX;

	switch(priv->tdm_mode) {
		case ES7210_TDM_1LRCK_DSPB:
			switch(priv->pcm_format) {
				case SNDRV_PCM_FORMAT_S16_LE:
					width = 16;
					break;
				case SNDRV_PCM_FORMAT_S24_LE:
				case SNDRV_PCM_FORMAT_S32_LE:
					width = 32;
					break;
				default:
					break;
			}
			break;
		case ES7210_NORMAL_I2S:
			width = 32;
			if (channel > 2) // i2s 8ch has 2 channels in actual.
				channel = 2;
			break;
		default:
			break;
	}

	priv->sclk_lrck_ratio = channel * width ;

	switch(priv->sclk_lrck_ratio * priv->mclk_sclk_ratio) {
		case 64:
			priv->mclk_lrck_ratio = RATIO_64;
			break;
		case 128:
			priv->mclk_lrck_ratio = RATIO_128;
			break;
		case 192:
			priv->mclk_lrck_ratio = RATIO_192;
			break;
		case 256:
			priv->mclk_lrck_ratio = RATIO_256;
			break;
		case 384:
			priv->mclk_lrck_ratio = RATIO_384;
			break;
		case 768:
			priv->mclk_lrck_ratio = RATIO_768;
			break;
		default:
			pr_err("%s Unable to calculate proper mclk_lrck_ratio with sclk_lrck_ratio=%d!\n",
			       __func__, priv->sclk_lrck_ratio);
			break;
	}

	switch (priv->tdm_mode) {
	case ES7210_TDM_1LRCK_DSPA:
	case ES7210_TDM_1LRCK_DSPB:
	case ES7210_TDM_1LRCK_I2S:
	case ES7210_TDM_1LRCK_LJ:
	case ES7210_NORMAL_I2S:
		/*
		* to set internal mclk
		* here, we assume that cpu/soc always provides
		* 256FS i2s clock
		* to es7210.
		* dll bypassed, use clock doubler to get double
		* frequency for
		* internal modem which need
		* 512FS clock. the clk divider ratio is 1.
		* user must modify the setting of register0x02
		* according to FS
		* ratio provided by CPU/SOC.
		*/

		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_MCLK_CTL_REG02,
				     priv->mclk_lrck_ratio, i2c_clt1[cnt]);
		}
		// es7210_multi_chips_write(ES7210_MCLK_CTL_REG02,
		// 0xc1);
		break;
	case ES7210_TDM_NLRCK_DSPA:
	case ES7210_TDM_NLRCK_DSPB:
	case ES7210_TDM_NLRCK_I2S:
	case ES7210_TDM_NLRCK_LJ:
		/*
		* Here to set TDM format for DSP-A with
		* multiple LRCK TDM mode
		*/
		channel = ES7210_CHANNELS_MAX;
		/*
		* Set the microphone numbers in array
		*/
		switch (channel) {
		case 2:
			/* ES7210_CHANNELS_MAX=2 */
			es7210_multi_chips_write(
				ES7210_MODE_CFG_REG08, 0x10);
			break;
		case 4:
			/* ES7210_CHANNELS_MAX=4 */
			es7210_multi_chips_write(
				ES7210_MODE_CFG_REG08, 0x20);
			break;
		case 6:
			/* ES7210_CHANNELS_MAX=6 */
			es7210_multi_chips_write(
				ES7210_MODE_CFG_REG08, 0x30);
			break;
		case 8:
			/* ES7210_CHANNELS_MAX=8 */
			es7210_multi_chips_write(
				ES7210_MODE_CFG_REG08, 0x40);
			break;
		case 10:
			/* ES7210_CHANNELS_MAX=10 */
			es7210_multi_chips_write(
				ES7210_MODE_CFG_REG08, 0x50);
			break;
		case 12:
			/* ES7210_CHANNELS_MAX=12 */
			es7210_multi_chips_write(
				ES7210_MODE_CFG_REG08, 0x60);
			break;
		case 14:
			/* ES7210_CHANNELS_MAX=14 */
			es7210_multi_chips_write(
				ES7210_MODE_CFG_REG08, 0x70);
			break;
		case 16:
			/* ES7210_CHANNELS_MAX=16 */
			es7210_multi_chips_write(
				ES7210_MODE_CFG_REG08, 0x80);
			break;
		default:
			break;
		}
		/*
		* to set internal mclk
		* here, we assume that cpu/soc always provides
		* 256FS i2s clock
		* to es7210 and there is four
		    * es7210 devices in tdm link. so the
		* internal FS in es7210
		* is only FS/4;
		* dll bypassed, clock doubler bypassed. the clk
		* divider ratio is
		* 2. so the clock of internal
		    * modem equals to (256FS / (FS/4) / 2) * FS
		* = 512FS
		* user must modify the setting of register0x02
		* according to FS
		* ratio provided by CPU/SOC.
		*/

		es7210_multi_chips_write(ES7210_MCLK_CTL_REG02,
					 priv->mclk_lrck_ratio);// NFS MODE:RATIO_768 ,12.288M/48K(16K 6 CH),12.288M/64K(16K 8 CH)
		break;
	default:
		/*
		* to set internal mclk for normal mode
		* here, we assume that cpu/soc always provides
		* 256FS i2s clock
		* to es7210.
		* dll bypassed, use clock doubler to get double
		* frequency for
		* internal modem which need
		* 512FS clock. the clk divider ratio is 1.
		* user must modify the setting of register0x02
		* according to FS
		* ratio provided by CPU/SOC.
		*/
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_MCLK_CTL_REG02,
				     priv->mclk_lrck_ratio, i2c_clt1[cnt]);
		}

		break;
	}
	return;
}

/*
* to initialize es7210 for tdm mode
*/
static void es7210_tdm_init_codec(u8 mode)
{
	int cnt, i;

	for (cnt = 0;
	     cnt < sizeof(es7210_tdm_reg_common_cfg1) /
	     sizeof(es7210_tdm_reg_common_cfg1[0]);
	     cnt++) {
		es7210_multi_chips_write(
			es7210_tdm_reg_common_cfg1[cnt].reg_addr,
			es7210_tdm_reg_common_cfg1[cnt].reg_v);
	}
	if(DOUBLESPEED)
		es7210_multi_chips_update_bits(ES7210_MODE_CFG_REG08, 0x02, 0x02);

	switch (mode) {
	case ES7210_TDM_1LRCK_DSPA:
		/*
		* Here to set TDM format for DSP-A mode
		*/
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_SDP_CFG1_REG11,
				     0x63, i2c_clt1[cnt]);
			es7210_write(ES7210_SDP_CFG2_REG12,
				     0x01, i2c_clt1[cnt]);
		}
		break;
	case ES7210_TDM_1LRCK_DSPB:
		/*
		* Here to set TDM format for DSP-B mode
		*/
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_SDP_CFG1_REG11,
				     0x73, i2c_clt1[cnt]);
			es7210_write(ES7210_SDP_CFG2_REG12,
				     0x01, i2c_clt1[cnt]);
		}
		break;
	case ES7210_TDM_1LRCK_I2S:
		/*
		* Here to set TDM format for I2S mode
		*/
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_SDP_CFG1_REG11,
				     0x60, i2c_clt1[cnt]);
		}
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_SDP_CFG2_REG12,
				     0x02, i2c_clt1[cnt]);
		}
		break;
	case ES7210_TDM_1LRCK_LJ:
		/*
		* Here to set TDM format for Left Justified mode
		*/
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_SDP_CFG1_REG11,
				     0x61, i2c_clt1[cnt]);
		}
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_SDP_CFG2_REG12,
				     0x02, i2c_clt1[cnt]);
		}
		break;
	case ES7210_TDM_NLRCK_DSPA:
		/*
		* set format, dsp-a with multiple LRCK tdm mode
		*/
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_SDP_CFG1_REG11,
				     0x63, i2c_clt1[cnt]);
		}
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			if (cnt == 0) {
				/*
				* set tdm flag in the interface
				* chip
				*/
				es7210_write(
					ES7210_SDP_CFG2_REG12, 0x07,
					i2c_clt1[cnt]);
			} else {
				es7210_write(
					ES7210_SDP_CFG2_REG12, 0x03,
					i2c_clt1[cnt]);
			}
		}
		break;
	case ES7210_TDM_NLRCK_DSPB:
		/*
		* set format, dsp-b with multiple LRCK tdm mode
		*/
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_SDP_CFG1_REG11,
				     0x73, i2c_clt1[cnt]);
		}
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			if (cnt == 0) {
				/*
				* set tdm flag in the interface
				* chip
				*/
				es7210_write(
					ES7210_SDP_CFG2_REG12, 0x07,
					i2c_clt1[cnt]);
			} else {
				es7210_write(
					ES7210_SDP_CFG2_REG12, 0x03,
					i2c_clt1[cnt]);
			}
		}

		break;
	case ES7210_TDM_NLRCK_I2S:
		/*
		* set format, I2S with multiple LRCK tdm mode
		*/
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_SDP_CFG1_REG11,
				     0x60, i2c_clt1[cnt]);
		}
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			if (cnt == 0) {
				/*
				* set tdm flag in the interface
				* chip
				*/
				es7210_write(
					ES7210_SDP_CFG2_REG12, 0x07,
					i2c_clt1[cnt]);
			} else {
				es7210_write(
					ES7210_SDP_CFG2_REG12, 0x03,
					i2c_clt1[cnt]);
			}
		}

		break;
	case ES7210_TDM_NLRCK_LJ:
		/*
		* set format, left justified with multiple LRCK
		* tdm mode
		*/
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_SDP_CFG1_REG11,
				     0x61, i2c_clt1[cnt]);
		}
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			if (cnt == 0) {
				/*
				* set tdm flag in the interface
				* chip
				*/
				es7210_write(
					ES7210_SDP_CFG2_REG12, 0x07,
					i2c_clt1[cnt]);
			} else {
				es7210_write(
					ES7210_SDP_CFG2_REG12, 0x03,
					i2c_clt1[cnt]);
			}
		}

		break;
	case ES7210_NORMAL_I2S:
		 /*
		* Here to set Normal  i2s mode format for SD1/SD2 output
		*/
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_SDP_CFG1_REG11,
				     0x60, i2c_clt1[cnt]);
		}
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_SDP_CFG2_REG12,
				     0x00, i2c_clt1[cnt]);
		}

		break;

	default:
		/*
		* here to disable tdm and set i2s-16bit for
		* normal mode
		*/
		for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
			es7210_write(ES7210_SDP_CFG1_REG11,
				     0x60, i2c_clt1[cnt]);
			es7210_write(ES7210_SDP_CFG2_REG12,
				     0x00, i2c_clt1[cnt]);
		}
		break;
	}
	for (cnt = 0;
	     cnt < sizeof(es7210_tdm_reg_common_cfg2) /
	     sizeof(es7210_tdm_reg_common_cfg2[0]);
	     cnt++) {
		es7210_multi_chips_write(
			es7210_tdm_reg_common_cfg2[cnt].reg_addr,
			es7210_tdm_reg_common_cfg2[cnt].reg_v);
	}

	for (cnt = 0;
	     cnt < sizeof(es7210_tdm_reg_common_cfg3) /
	     sizeof(es7210_tdm_reg_common_cfg3[0]);
	     cnt++) {
		es7210_multi_chips_write(
			es7210_tdm_reg_common_cfg3[cnt].reg_addr,
			es7210_tdm_reg_common_cfg3[cnt].reg_v);
	}
	/*
	* Mute All ADC
	*/
	for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
		es7210_write(ES7210_ADC34_MUTE_REG14, 0x03,
			     i2c_clt1[cnt]);
		es7210_write(ES7210_ADC12_MUTE_REG15, 0x03,
			     i2c_clt1[cnt]);
	}

	for (i = 0; i < ADC_DEV_MAXNUM; i++) {
		if (i == 0) {
			/* set first es7210 PGA GAIN */
			es7210_write(ES7210_MIC1_GAIN_REG43, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
			es7210_write(ES7210_MIC2_GAIN_REG44, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
			es7210_write(ES7210_MIC3_GAIN_REG45, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
			es7210_write(ES7210_MIC4_GAIN_REG46, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
		}
		if (i == 1) {
			/* set second es7210 PGA GAIN */
			es7210_write(ES7210_MIC1_GAIN_REG43, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
			es7210_write(ES7210_MIC2_GAIN_REG44, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
			es7210_write(ES7210_MIC3_GAIN_REG45, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
			es7210_write(ES7210_MIC4_GAIN_REG46, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
		}
		if (i == 2) {
			/* set third es7210 PGA GAIN */
			es7210_write(ES7210_MIC1_GAIN_REG43, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
			es7210_write(ES7210_MIC2_GAIN_REG44, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
			es7210_write(ES7210_MIC3_GAIN_REG45, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
			es7210_write(ES7210_MIC4_GAIN_REG46, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
		}

		if (i == 3) {
			/* set third es7210 PGA GAIN */
			es7210_write(ES7210_MIC1_GAIN_REG43, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
			es7210_write(ES7210_MIC2_GAIN_REG44, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
			es7210_write(ES7210_MIC3_GAIN_REG45, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
			es7210_write(ES7210_MIC4_GAIN_REG46, ES7210_MIC_GAIN,
				     i2c_clt1[i]);
		}
	}
}
static void es7210_unmute(void)
{
	es7210_multi_chips_update_bits(ES7210_ADC34_MUTE_REG14, 0x03, 0x00);
	es7210_multi_chips_update_bits(ES7210_ADC12_MUTE_REG15, 0x03, 0x00);
	es7210_multi_chips_update_bits(ES7210_MIC1_GAIN_REG43, 0x1f, ES7210_MIC_GAIN);
	es7210_multi_chips_update_bits(ES7210_MIC2_GAIN_REG44, 0x1f, ES7210_MIC_GAIN);
	es7210_multi_chips_update_bits(ES7210_MIC3_GAIN_REG45, 0x1f, ES7210_MIC_GAIN);
	es7210_multi_chips_update_bits(ES7210_MIC4_GAIN_REG46, 0x1f, ES7210_MIC_GAIN);
}

static void pcm_pop_work_events(struct work_struct *work)
{
	es7210_unmute();
}

static int es7210_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es7210_priv *es7210 = snd_soc_component_get_drvdata(component);

	if (es7210_init_reg == 0) {
		es7210_init_reg = 1;
		schedule_delayed_work(&es7210->pcm_pop_work, msecs_to_jiffies(100));
	}
	return 0;
}
static int es7210_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es7210_priv *priv = snd_soc_component_get_drvdata(component);
	int i;

	if (es7210_hw_param_reg == 1 && params_format(params) == priv->pcm_format) {
		return 0;
	}

	es7210_hw_param_reg = 1;
	priv->pcm_format = params_format(params);
	es7210_tdm_init_ratio(priv);

	es7210_multi_chips_update_bits(ES7210_RESET_CTL_REG00, 0x30, 0x30);
	for (i = 0; i < ADC_DEV_MAXNUM; i++) {
		/* set es7210 bit size */
		switch (priv->pcm_format) {
		case SNDRV_PCM_FORMAT_S16_LE:
			es7210_update_bits(0x11, 0xE0, 0x60,
					   i2c_clt1[i]);
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			es7210_update_bits(0x11, 0xE0, 0x20,
					   i2c_clt1[i]);
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			if (priv->tdm_mode == ES7210_TDM_1LRCK_DSPB)
				es7210_update_bits(0x11, 0xE0, 0x80, i2c_clt1[i]);
			else if (priv->tdm_mode == ES7210_NORMAL_I2S)
				es7210_update_bits(0x11, 0xE0, 0x00, i2c_clt1[i]);
			break;
		case SNDRV_PCM_FORMAT_S24_3LE:
			es7210_update_bits(0x11, 0xE0, 0x00,
					   i2c_clt1[i]);
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			es7210_update_bits(0x11, 0xE0, 0x80,
					   i2c_clt1[i]);
			break;
		}
	}
	es7210_multi_chips_update_bits(ES7210_RESET_CTL_REG00, 0x30, 0x00);
	switch (params_rate(params)) {
	case 16000:
		break;
	case 32000:
		break;
	case 48000:
		break;
	case 64000:
		break;
	default:
		break;
	}

	return 0;
}

#define es7210_RATES (SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_64000 | SNDRV_PCM_RATE_88200)

#define es7210_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_3LE |\
    SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops es7210_ops = {
	.startup = es7210_pcm_startup,
	.hw_params = es7210_pcm_hw_params,
	.set_fmt = es7210_set_dai_fmt,
	.set_sysclk = es7210_set_dai_sysclk,
};

static struct snd_soc_dai_driver es7210_dai0 = {
	.name = "ES7210 ADC 0",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = es7210_RATES,
		.formats = es7210_FORMATS,
	},
	.ops = &es7210_ops,
};

static struct snd_soc_dai_driver es7210_dai1 = {
	.name = "ES7210 4CH ADC 1",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 4,
		.rates = es7210_RATES,
		.formats = es7210_FORMATS,
	},
	.ops = &es7210_ops,
};

static struct snd_soc_dai_driver es7210_dai2 = {
	.name = "ES7210 4CH ADC 2",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 4,
		.rates = es7210_RATES,
		.formats = es7210_FORMATS,
	},
	.ops = &es7210_ops,
};

static struct snd_soc_dai_driver es7210_dai3 = {
	.name = "ES7210 4CH ADC 3",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 4,
		.rates = es7210_RATES,
		.formats = es7210_FORMATS,
	},
	.ops = &es7210_ops,
};

static struct snd_soc_dai_driver *es7210_dai[] = {
	&es7210_dai0,
	&es7210_dai1,
	&es7210_dai2,
	&es7210_dai3,
};

static int es7210_suspend(struct snd_soc_component *component)
{
	struct es7210_priv *priv = snd_soc_component_get_drvdata(component);
	int i, r;

	for (i = 0; i < ADC_DEV_MAXNUM; i++)
		for (r = 0; r < ES7210_NUM_SUSPEND_REGS; r++)
			es7210_read(es7210_suspend_regs[r],
				    &priv->suspend_regs[i][r], i2c_clt1[i]);

	es7210_init_reg = 0;

	/* power down the controller */
	if (priv->pvdd)
		regulator_disable(priv->pvdd);
	if (priv->dvdd)
		regulator_disable(priv->dvdd);
	if (priv->avdd)
		regulator_disable(priv->avdd);
	if (priv->mvdd)
		regulator_disable(priv->mvdd);

	return 0;
}

static int es7210_resume(struct snd_soc_component *component)
{
	struct es7210_priv *priv = snd_soc_component_get_drvdata(component);
	int ret = 0, i, r;

	/* power up the controller */
	if (priv->mvdd)
		ret |= regulator_enable(priv->mvdd);
	if (priv->avdd)
		ret |= regulator_enable(priv->avdd);
	if (priv->dvdd)
		ret |= regulator_enable(priv->dvdd);
	if (priv->pvdd)
		ret |= regulator_enable(priv->pvdd);
	if (ret) {
		dev_err(component->dev, "Failed to enable VDD regulator: %d\n", ret);
		return ret;
	}
	mdelay(10); // es7210 need 10ms setup time after power up.

	for (i = 0; i < ADC_DEV_MAXNUM; i++)
		for (r = 0; r < ES7210_NUM_SUSPEND_REGS; r++)
			es7210_write(es7210_suspend_regs[r],
				     priv->suspend_regs[i][r], i2c_clt1[i]);

	return 0;
}

static int es7210_probe(struct snd_soc_component *component)
{
	struct es7210_priv *es7210 = snd_soc_component_get_drvdata(component);
	u8 val, val1, cnt;
	int ret = 0;

	/* power up the controller */
	if (es7210->mvdd)
		ret |= regulator_enable(es7210->mvdd);
	if (es7210->avdd)
		ret |= regulator_enable(es7210->avdd);
	if (es7210->dvdd)
		ret |= regulator_enable(es7210->dvdd);
	if (es7210->pvdd)
		ret |= regulator_enable(es7210->pvdd);
	if (ret) {
		dev_err(component->dev, "Failed to enable VDD regulator: %d\n", ret);
		return ret;
	}

	INIT_DELAYED_WORK(&es7210->pcm_pop_work, pcm_pop_work_events);

	for (cnt = 0; cnt < ADC_DEV_MAXNUM; cnt++) {
		/* es7210 chip id */
		ret = es7210_read(ES7210_CHP_ID1_REG3D, &val, i2c_clt1[cnt]);
		ret |= es7210_read(ES7210_CHP_ID0_REG3E, &val1, i2c_clt1[cnt]);
		if (ret < 0) {
			dev_err(component->dev, "%s: read chipid failed %d\n",
				__func__, ret);
			return 0;
		}
		dev_dbg(component->dev, "chip %u id 0x%02x%02x\n", cnt, val, val1);
	}

	es7210_tdm_init_codec(es7210->tdm_mode);

	return 0;
}

static void es7210_remove(struct snd_soc_component *component)
{
	struct es7210_priv *es7210 = snd_soc_component_get_drvdata(component);

	cancel_delayed_work_sync(&es7210->pcm_pop_work);

	/* power down the controller */
	if (es7210->pvdd)
		regulator_disable(es7210->pvdd);
	if (es7210->dvdd)
		regulator_disable(es7210->dvdd);
	if (es7210->avdd)
		regulator_disable(es7210->avdd);
	if (es7210->mvdd)
		regulator_disable(es7210->mvdd);
}

static int es7210_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		break;

	case SND_SOC_BIAS_OFF:
		break;
	}
	return 0;
}

static const DECLARE_TLV_DB_RANGE(mic_boost_tlv,
	0, 11, TLV_DB_SCALE_ITEM(0, 300, 0),	// 0db ~ 33db
	12, 14, TLV_DB_SCALE_ITEM(3450, 150, 0)	// 34.5db ~ 37.5db
);

static int es7210_micboost1_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7210_update_bits(0x43, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[0]);

	return 0;
}

static int es7210_micboost1_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7210_read(0x43, &val, i2c_clt1[0]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_micboost2_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7210_update_bits(0x44, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[0]);

	return 0;
}

static int es7210_micboost2_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7210_read(0x44, &val, i2c_clt1[0]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_micboost3_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7210_update_bits(0x45, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[0]);

	return 0;
}

static int es7210_micboost3_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7210_read(0x45, &val, i2c_clt1[0]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_micboost4_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	es7210_update_bits(0x46, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[0]);

	return 0;
}

static int es7210_micboost4_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7210_read(0x46, &val, i2c_clt1[0]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_adc1_set(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC12_MUTE_REG15, 0x01, val & 0x01,
			   i2c_clt1[0]);

	return 0;
}

static int es7210_adc1_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7210_read(ES7210_ADC12_MUTE_REG15, &val, i2c_clt1[0]);
	ucontrol->value.integer.value[0] = !(val & 0x01);

	return 0;
}

static int es7210_adc2_set(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC12_MUTE_REG15, 0x02, (val & 0x01) << 1,
			   i2c_clt1[0]);

	return 0;
}

static int es7210_adc2_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7210_read(ES7210_ADC12_MUTE_REG15, &val, i2c_clt1[0]);
	ucontrol->value.integer.value[0] = !((val & 0x02) >> 1);

	return 0;
}

static int es7210_adc3_set(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC34_MUTE_REG14, 0x01, val & 0x01,
			   i2c_clt1[0]);

	return 0;
}

static int es7210_adc3_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7210_read(ES7210_ADC34_MUTE_REG14, &val, i2c_clt1[0]);
	ucontrol->value.integer.value[0] = !(val & 0x01);

	return 0;
}

static int es7210_adc4_set(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC34_MUTE_REG14, 0x02, (val & 0x01) << 1,
			   i2c_clt1[0]);

	return 0;
}

static int es7210_adc4_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	es7210_read(ES7210_ADC34_MUTE_REG14, &val, i2c_clt1[0]);
	ucontrol->value.integer.value[0] = !((val & 0x02) >> 1);

	return 0;
}

static int es7210_micboost5_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	if (i2c_clt1[1] == NULL)
		return 0;
	es7210_update_bits(0x43, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[1]);

	return 0;
}

static int es7210_micboost5_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[1] == NULL)
		return 0;
	es7210_read(0x43, &val, i2c_clt1[1]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_micboost6_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	if (i2c_clt1[1] == NULL)
		return 0;
	es7210_update_bits(0x44, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[1]);

	return 0;
}

static int es7210_micboost6_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[1] == NULL)
		return 0;
	es7210_read(0x44, &val, i2c_clt1[1]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}
static int es7210_micboost7_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	if (i2c_clt1[1] == NULL)
		return 0;
	es7210_update_bits(0x45, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[1]);

	return 0;
}

static int es7210_micboost7_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[1] == NULL)
		return 0;
	es7210_read(0x45, &val, i2c_clt1[1]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_micboost8_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	if (i2c_clt1[1] == NULL)
		return 0;
	es7210_update_bits(0x46, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[1]);

	return 0;
}

static int es7210_micboost8_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[1] == NULL)
		return 0;
	es7210_read(0x46, &val, i2c_clt1[1]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_adc5_set(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[1] == NULL)
		return 0;
	val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC12_MUTE_REG15, 0x01, val & 0x01,
			   i2c_clt1[1]);

	return 0;
}

static int es7210_adc5_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[1] == NULL)
		return 0;
	es7210_read(ES7210_ADC12_MUTE_REG15, &val, i2c_clt1[1]);
	ucontrol->value.integer.value[0] = !(val & 0x01);

	return 0;
}

static int es7210_adc6_set(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[1] == NULL)
		return 0;
	val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC12_MUTE_REG15, 0x02, (val & 0x01) << 1,
			   i2c_clt1[1]);

	return 0;
}

static int es7210_adc6_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[1] == NULL)
		return 0;
	es7210_read(ES7210_ADC12_MUTE_REG15, &val, i2c_clt1[1]);
	ucontrol->value.integer.value[0] = !((val & 0x02) >> 1);

	return 0;
}

static int es7210_adc7_set(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[1] == NULL)
		return 0;
	val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC34_MUTE_REG14, 0x01, val & 0x01,
			   i2c_clt1[1]);

	return 0;
}

static int es7210_adc7_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[1] == NULL)
		return 0;
	es7210_read(ES7210_ADC34_MUTE_REG14, &val, i2c_clt1[1]);
	ucontrol->value.integer.value[0] = !(val & 0x01);

	return 0;
}

static int es7210_adc8_set(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[1] == NULL)
		return 0;
	val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC34_MUTE_REG14, 0x02, (val & 0x01) << 1,
			   i2c_clt1[1]);

	return 0;
}

static int es7210_adc8_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[1] == NULL)
		return 0;
	es7210_read(ES7210_ADC34_MUTE_REG14, &val, i2c_clt1[1]);
	ucontrol->value.integer.value[0] = !((val & 0x02) >> 1);

	return 0;
}

static int es7210_micboost9_setting_set(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	if (i2c_clt1[2] == NULL)
		return 0;
	es7210_update_bits(0x43, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[2]);

	return 0;
}

static int es7210_micboost9_setting_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[2] == NULL)
		return 0;
	es7210_read(0x43, &val, i2c_clt1[2]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_micboost10_setting_set(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	if (i2c_clt1[2] == NULL)
		return 0;
	es7210_update_bits(0x44, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[2]);

	return 0;
}

static int es7210_micboost10_setting_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[2] == NULL)
		return 0;
	es7210_read(0x44, &val, i2c_clt1[2]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_micboost11_setting_set(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	if (i2c_clt1[2] == NULL)
		return 0;
	es7210_update_bits(0x45, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[2]);

	return 0;
}

static int es7210_micboost11_setting_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[2] == NULL)
		return 0;
	es7210_read(0x45, &val, i2c_clt1[2]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_micboost12_setting_set(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	if (i2c_clt1[2] == NULL)
		return 0;
	es7210_update_bits(0x46, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[2]);

	return 0;
}

static int es7210_micboost12_setting_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[2] == NULL)
		return 0;
	es7210_read(0x46, &val, i2c_clt1[2]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_adc9_set(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[2] == NULL)
		return 0;
	val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC12_MUTE_REG15, 0x01, val & 0x01,
			   i2c_clt1[2]);

	return 0;
}

static int es7210_adc9_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[2] == NULL)
		return 0;
	es7210_read(ES7210_ADC12_MUTE_REG15, &val, i2c_clt1[2]);
	ucontrol->value.integer.value[0] = !(val & 0x01);

	return 0;
}

static int es7210_adc10_set(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[2] == NULL)
		return 0;
	val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC12_MUTE_REG15, 0x02, (val & 0x01) << 1,
			   i2c_clt1[2]);

	return 0;
}

static int es7210_adc10_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[2] == NULL)
		return 0;
	es7210_read(ES7210_ADC12_MUTE_REG15, &val, i2c_clt1[2]);
	ucontrol->value.integer.value[0] = !((val & 0x02) >> 1);

	return 0;
}

static int es7210_adc11_set(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[2] == NULL)
		return 0;
	val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC34_MUTE_REG14, 0x01, val & 0x01,
			   i2c_clt1[2]);

	return 0;
}

static int es7210_adc11_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[2] == NULL)
		return 0;
	es7210_read(ES7210_ADC34_MUTE_REG14, &val, i2c_clt1[2]);
	ucontrol->value.integer.value[0] = !(val & 0x01);

	return 0;
}

static int es7210_adc12_set(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[2] == NULL)
		return 0;
	val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC34_MUTE_REG14, 0x02, (val & 0x01) << 1,
			   i2c_clt1[2]);

	return 0;
}

static int es7210_adc12_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[2] == NULL)
		return 0;
	es7210_read(ES7210_ADC34_MUTE_REG14, &val, i2c_clt1[2]);
	ucontrol->value.integer.value[0] = !((val & 0x02) >> 1);

	return 0;
}

static int es7210_micboost13_setting_set(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	if (i2c_clt1[3] == NULL)
		return 0;
	es7210_update_bits(0x43, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[3]);

	return 0;
}

static int es7210_micboost13_setting_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[3] == NULL)
		return 0;
	es7210_read(0x43, &val, i2c_clt1[3]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_micboost14_setting_set(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	if (i2c_clt1[3] == NULL)
		return 0;
	es7210_update_bits(0x44, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[3]);

	return 0;
}

static int es7210_micboost14_setting_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[3] == NULL)
		return 0;
	es7210_read(0x44, &val, i2c_clt1[3]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_micboost15_setting_set(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	if (i2c_clt1[3] == NULL)
		return 0;
	es7210_update_bits(0x45, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[3]);

	return 0;
}

static int es7210_micboost15_setting_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[3] == NULL)
		return 0;
	es7210_read(0x45, &val, i2c_clt1[3]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_micboost16_setting_set(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	if (i2c_clt1[3] == NULL)
		return 0;
	es7210_update_bits(0x46, 0x0F, ucontrol->value.integer.value[0],
			   i2c_clt1[3]);

	return 0;
}

static int es7210_micboost16_setting_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[3] == NULL)
		return 0;
	es7210_read(0x46, &val, i2c_clt1[3]);
	ucontrol->value.integer.value[0] = val & 0x0F;

	return 0;
}

static int es7210_adc13_set(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[3] == NULL)
		return 0;
	val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC12_MUTE_REG15, 0x01, val & 0x01,
			   i2c_clt1[3]);

	return 0;
}

static int es7210_adc13_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[3] == NULL)
		return 0;
	es7210_read(ES7210_ADC12_MUTE_REG15, &val, i2c_clt1[3]);
	ucontrol->value.integer.value[0] = !(val & 0x01);

	return 0;
}

static int es7210_adc14_set(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[3] == NULL)
		return 0;
	val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC12_MUTE_REG15, 0x02, (val & 0x01) << 1,
			   i2c_clt1[3]);

	return 0;
}

static int es7210_adc14_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[3] == NULL)
		return 0;
	es7210_read(ES7210_ADC12_MUTE_REG15, &val, i2c_clt1[3]);
	ucontrol->value.integer.value[0] = !((val & 0x02) >> 1);

	return 0;
}

static int es7210_adc15_set(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[3] == NULL)
		return 0;
	val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC34_MUTE_REG14, 0x01, val & 0x01,
			   i2c_clt1[3]);

	return 0;
}

static int es7210_adc15_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[3] == NULL)
		return 0;
	es7210_read(ES7210_ADC34_MUTE_REG14, &val, i2c_clt1[3]);
	ucontrol->value.integer.value[0] = !(val & 0x01);

	return 0;
}

static int es7210_adc16_set(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[3] == NULL)
		return 0;
	val = !ucontrol->value.integer.value[0];
	es7210_update_bits(ES7210_ADC34_MUTE_REG14, 0x02, (val & 0x01) << 1,
			   i2c_clt1[3]);

	return 0;
}

static int es7210_adc16_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	u8 val;
	if (i2c_clt1[3] == NULL)
		return 0;
	es7210_read(ES7210_ADC34_MUTE_REG14, &val, i2c_clt1[3]);
	ucontrol->value.integer.value[0] = !((val & 0x02) >> 1);

	return 0;
}

static const struct snd_kcontrol_new es7210_snd_controls[] = {
	/* CHIP 1 */
	SOC_SINGLE_EXT_TLV("PGA_01 Capture Volume", 0x43, 0, 0x0E, 0,
			   es7210_micboost1_setting_get,
			   es7210_micboost1_setting_set, mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA_02 Capture Volume", 0x44, 0, 0x0E, 0,
			   es7210_micboost2_setting_get,
			   es7210_micboost2_setting_set, mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA_03 Capture Volume", 0x45, 0, 0x0E, 0,
			   es7210_micboost3_setting_get,
			   es7210_micboost3_setting_set, mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA_04 Capture Volume", 0x46, 0, 0x0E, 0,
			   es7210_micboost4_setting_get,
			   es7210_micboost4_setting_set, mic_boost_tlv),
	SOC_SINGLE_BOOL_EXT("ADC_01 Capture Switch", 0, es7210_adc1_get,
			    es7210_adc1_set),
	SOC_SINGLE_BOOL_EXT("ADC_02 Capture Switch", 0, es7210_adc2_get,
			    es7210_adc2_set),
	SOC_SINGLE_BOOL_EXT("ADC_03 Capture Switch", 0, es7210_adc3_get,
			    es7210_adc3_set),
	SOC_SINGLE_BOOL_EXT("ADC_04 Capture Switch", 0, es7210_adc4_get,
			    es7210_adc4_set),
	/* CHIP 2 */
	SOC_SINGLE_EXT_TLV("PGA_05 Capture Volume", 0x43, 0, 0x0E, 0,
			   es7210_micboost5_setting_get,
			   es7210_micboost5_setting_set, mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA_06 Capture Volume", 0x44, 0, 0x0E, 0,
			   es7210_micboost6_setting_get,
			   es7210_micboost6_setting_set, mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA_07 Capture Volume", 0x45, 0, 0x0E, 0,
			   es7210_micboost7_setting_get,
			   es7210_micboost7_setting_set, mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA_08 Capture Volume", 0x46, 0, 0x0E, 0,
			   es7210_micboost8_setting_get,
			   es7210_micboost8_setting_set, mic_boost_tlv),
	SOC_SINGLE_BOOL_EXT("ADC_05 Capture Switch", 0, es7210_adc5_get,
			    es7210_adc5_set),
	SOC_SINGLE_BOOL_EXT("ADC_06 Capture Switch", 0, es7210_adc6_get,
			    es7210_adc6_set),
	SOC_SINGLE_BOOL_EXT("ADC_07 Capture Switch", 0, es7210_adc7_get,
			    es7210_adc7_set),
	SOC_SINGLE_BOOL_EXT("ADC_08 Capture Switch", 0, es7210_adc8_get,
			    es7210_adc8_set),
	/* CHIP 3 */
	SOC_SINGLE_EXT_TLV("PGA_09 Capture Volume", 0x43, 0, 0x0E, 0,
			   es7210_micboost9_setting_get,
			   es7210_micboost9_setting_set, mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA_10 Capture Volume", 0x44, 0, 0x0E, 0,
			   es7210_micboost10_setting_get,
			   es7210_micboost10_setting_set, mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA_11 Capture Volume", 0x45, 0, 0x0E, 0,
			   es7210_micboost11_setting_get,
			   es7210_micboost11_setting_set, mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA_12 Capture Volume", 0x46, 0, 0x0E, 0,
			   es7210_micboost12_setting_get,
			   es7210_micboost12_setting_set, mic_boost_tlv),
	SOC_SINGLE_BOOL_EXT("ADC_09 Capture Switch", 0, es7210_adc9_get,
			    es7210_adc9_set),
	SOC_SINGLE_BOOL_EXT("ADC_10 Capture Switch", 0, es7210_adc10_get,
			    es7210_adc10_set),
	SOC_SINGLE_BOOL_EXT("ADC_11 Capture Switch", 0, es7210_adc11_get,
			    es7210_adc11_set),
	SOC_SINGLE_BOOL_EXT("ADC_12 Capture Switch", 0, es7210_adc12_get,
			    es7210_adc12_set),
	/* CHIP 4 */
	SOC_SINGLE_EXT_TLV("PGA_13 Capture Volume", 0x43, 0, 0x0E, 0,
			   es7210_micboost13_setting_get,
			   es7210_micboost13_setting_set, mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA_14 Capture Volume", 0x44, 0, 0x0E, 0,
			   es7210_micboost14_setting_get,
			   es7210_micboost14_setting_set, mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA_15 Capture Volume", 0x45, 0, 0x0E, 0,
			   es7210_micboost15_setting_get,
			   es7210_micboost15_setting_set, mic_boost_tlv),
	SOC_SINGLE_EXT_TLV("PGA_16 Capture Volume", 0x46, 0, 0x0E, 0,
			   es7210_micboost16_setting_get,
			   es7210_micboost16_setting_set, mic_boost_tlv),
	SOC_SINGLE_BOOL_EXT("ADC_13 Capture Switch", 0, es7210_adc13_get,
			    es7210_adc13_set),
	SOC_SINGLE_BOOL_EXT("ADC_14 Capture Switch", 0, es7210_adc14_get,
			    es7210_adc14_set),
	SOC_SINGLE_BOOL_EXT("ADC_15 Capture Switch", 0, es7210_adc15_get,
			    es7210_adc15_set),
	SOC_SINGLE_BOOL_EXT("ADC_16 Capture Switch", 0, es7210_adc16_get,
			    es7210_adc16_set),
};

static const struct snd_soc_component_driver soc_codec_dev_es7210 = {
	.name = "es7210",
	.probe = es7210_probe,
	.remove = es7210_remove,
	.suspend = pm_sleep_ptr(es7210_suspend),
	.resume = pm_sleep_ptr(es7210_resume),
	.set_bias_level = es7210_set_bias_level,
	.controls = es7210_snd_controls,
	.num_controls = ARRAY_SIZE(es7210_snd_controls),
};

static struct regulator *es7210_get_supply(struct device *dev, const char *id)
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

static int es7210_i2c_probe(struct i2c_client *i2c_client)
{
	struct device *dev = &i2c_client->dev;
	struct device_node *np = dev->of_node;
	struct es7210_priv *es7210;
	const char *property;
	u32 channels_max;
	int ret;

	es7210 = devm_kzalloc(dev, sizeof(struct es7210_priv), GFP_KERNEL);
	if (!es7210)
		return -ENOMEM;
	es7210->i2c_client = i2c_client;

	/*
	 * Chip index in the microphone array: taken from the OF match data
	 * ("MicArray_N" / "everest,es7210" == MicArray_0) or from the
	 * i2c_device_id driver_data.
	 */
	es7210->chip_index = (uintptr_t)i2c_get_match_data(i2c_client);
	if (es7210->chip_index >= ES7210_MAX_CHIPS)
		return -EINVAL;

	if (!of_property_read_string(np, "work-mode", &property)) {
		if (!strcmp(property, "ES7210_TDM_1LRCK_DSPB")) {
			es7210->tdm_mode = ES7210_TDM_1LRCK_DSPB;
		} else if (!strcmp(property, "ES7210_NORMAL_I2S")) {
			es7210->tdm_mode = ES7210_NORMAL_I2S;
		} else {
			dev_err(dev, "unsupported work mode %s\n", property);
			return -EINVAL;
		}
	} else {
		dev_warn(dev, "work-mode not defined, using ES7210_NORMAL_I2S by default\n");
		es7210->tdm_mode = ES7210_NORMAL_I2S;
	}

	if (!of_property_read_u32(np, "channels-max", &channels_max)) {
		ES7210_CHANNELS_MAX = channels_max;
		if (ES7210_CHANNELS_MAX == MIC_CHN_2 || ES7210_CHANNELS_MAX == MIC_CHN_4) {
			ADC_DEV_MAXNUM = 1;
		} else if (ES7210_CHANNELS_MAX == MIC_CHN_6 || ES7210_CHANNELS_MAX == MIC_CHN_8) {
			ADC_DEV_MAXNUM = 2;
		} else if (ES7210_CHANNELS_MAX == MIC_CHN_10 || ES7210_CHANNELS_MAX == MIC_CHN_12) {
			ADC_DEV_MAXNUM = 3;
		} else if (ES7210_CHANNELS_MAX == MIC_CHN_14 || ES7210_CHANNELS_MAX == MIC_CHN_16) {
			ADC_DEV_MAXNUM = 4;
		} else {
			dev_err(dev, "unsupported channels-max %u\n", channels_max);
			return -EINVAL;
		}
	} else {
		dev_warn(dev, "channels-max not defined, using MIC_CHN_2 by default\n");
		ES7210_CHANNELS_MAX = MIC_CHN_2;
		ADC_DEV_MAXNUM = 1;
	}
	dev_dbg(dev, "%s tdm_mode=%d channels-max=%d ADC_DEV_MAXNUM=%d index=%u\n",
		__func__, es7210->tdm_mode, ES7210_CHANNELS_MAX, ADC_DEV_MAXNUM,
		es7210->chip_index);

	es7210->mvdd = es7210_get_supply(dev, "MVDD");
	if (IS_ERR(es7210->mvdd))
		return PTR_ERR(es7210->mvdd);
	es7210->avdd = es7210_get_supply(dev, "AVDD");
	if (IS_ERR(es7210->avdd))
		return PTR_ERR(es7210->avdd);
	es7210->dvdd = es7210_get_supply(dev, "DVDD");
	if (IS_ERR(es7210->dvdd))
		return PTR_ERR(es7210->dvdd);
	es7210->pvdd = es7210_get_supply(dev, "PVDD");
	if (IS_ERR(es7210->pvdd))
		return PTR_ERR(es7210->pvdd);

	if (of_property_read_u32(np, "mclk-sclk-ratio", &es7210->mclk_sclk_ratio))
		es7210->mclk_sclk_ratio = 1;

	i2c_set_clientdata(i2c_client, es7210);

	if (es7210->chip_index < ADC_DEV_MAXNUM) {
		if (i2c_clt1[es7210->chip_index])
			dev_warn(dev, "MicArray_%u already registered, overriding\n",
				 es7210->chip_index);
		i2c_clt1[es7210->chip_index] = i2c_client;
		ret = devm_snd_soc_register_component(dev, &soc_codec_dev_es7210,
						      es7210_dai[es7210->chip_index], 1);
		if (ret < 0) {
			i2c_clt1[es7210->chip_index] = NULL;
			return dev_err_probe(dev, ret, "Failed to register component\n");
		}
	}

	return 0;
}

static void es7210_i2c_remove(struct i2c_client *i2c_client)
{
	struct es7210_priv *es7210 = i2c_get_clientdata(i2c_client);

	if (es7210 && es7210->chip_index < ES7210_MAX_CHIPS &&
	    i2c_clt1[es7210->chip_index] == i2c_client)
		i2c_clt1[es7210->chip_index] = NULL;
}

static const struct i2c_device_id es7210_i2c_id[] = {
	{ "MicArray_0", 0 },	//es7210_0
	{ "MicArray_1", 1 },	//es7210_1
	{ "MicArray_2", 2 },	//es7210_2
	{ "MicArray_3", 3 },	//es7210_3
	{ "es7210", 0 },	//es7210_0
	{ }
};
MODULE_DEVICE_TABLE(i2c, es7210_i2c_id);

static const struct of_device_id es7210_dt_ids[] = {
	{ .compatible = "MicArray_0", .data = (void *)0 },	//es7210_0
	{ .compatible = "MicArray_1", .data = (void *)1 },	//es7210_1
	{ .compatible = "MicArray_2", .data = (void *)2 },	//es7210_2
	{ .compatible = "MicArray_3", .data = (void *)3 },	//es7210_3
	{ .compatible = "everest,es7210", .data = (void *)0 },	//es7210_0
	{ }
};
MODULE_DEVICE_TABLE(of, es7210_dt_ids);

static struct i2c_driver es7210_i2c_driver = {
	.driver = {
		.name = "es7210",
		.of_match_table = es7210_dt_ids,
	},
	.probe = es7210_i2c_probe,
	.remove = es7210_i2c_remove,
	.id_table = es7210_i2c_id,
};
module_i2c_driver(es7210_i2c_driver);

MODULE_DESCRIPTION("ASoC ES7210 audio adc driver");
MODULE_LICENSE("GPL");
