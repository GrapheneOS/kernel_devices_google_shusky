// SPDX-License-Identifier: GPL-2.0-only
/*
 * MIPI-DSI based HK3 AMOLED LCD panel driver.
 *
 * Copyright (c) 2022 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <drm/drm_vblank.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <video/mipi_display.h>

#include "include/trace/dpu_trace.h"
#include "samsung/panel/panel-samsung-drv.h"

/**
 * enum hk3_panel_feature - features supported by this panel
 * @FEAT_HBM: high brightness mode
 * @FEAT_IRC_OFF: IRC compensation off state
 * @FEAT_EARLY_EXIT: early exit from a long frame
 * @FEAT_OP_NS: normal speed (not high speed)
 * @FEAT_FRAME_AUTO: automatic (not manual) frame control
 * @FEAT_MAX: placeholder, counter for number of features
 *
 * The following features are correlated, if one or more of them change, the others need
 * to be updated unconditionally.
 */
enum hk3_panel_feature {
	FEAT_HBM = 0,
	FEAT_IRC_OFF,
	FEAT_EARLY_EXIT,
	FEAT_OP_NS,
	FEAT_FRAME_AUTO,
	FEAT_MAX,
};

/**
 * struct hk3_panel - panel specific runtime info
 *
 * This struct maintains hk3 panel specific runtime info, any fixed details about panel should
 * most likely go into struct exynos_panel_desc. The variables with the prefix hw_ keep track of the
 * features that were actually committed to hardware, and should be modified after sending cmds to panel,
 * i.e. updating hw state.
 */
struct hk3_panel {
	/** @base: base panel struct */
	struct exynos_panel base;
	/** @feat: software or working correlated features, not guaranteed to be effective in panel */
	DECLARE_BITMAP(feat, FEAT_MAX);
	/** @hw_feat: correlated states effective in panel */
	DECLARE_BITMAP(hw_feat, FEAT_MAX);
	/** @hw_vrefresh: vrefresh rate effective in panel */
	u32 hw_vrefresh;
	/** @hw_idle_vrefresh: idle vrefresh rate effective in panel */
	u32 hw_idle_vrefresh;
	/**
	 * @auto_mode_vrefresh: indicates current minimum refresh rate while in auto mode,
	 *			if 0 it means that auto mode is not enabled
	 */
	u32 auto_mode_vrefresh;
	/** @force_changeable_te: force changeable TE (instead of fixed) during early exit */
	bool force_changeable_te;
	/** @hw_acl_enabled: whether automatic current limiting is enabled */
	bool hw_acl_enabled;
	/** @hw_za_enabled: whether zonal attenuation is enabled */
	bool hw_za_enabled;
};

#define to_spanel(ctx) container_of(ctx, struct hk3_panel, base)

/* 1344x2992 */
static const unsigned char WQHD_PPS_SETTING[DSC_PPS_SIZE] = {
	0x11, 0x00, 0x00, 0x89, 0x30, 0x80, 0x0B, 0xB0,
	0x05, 0x40, 0x00, 0xBB, 0x02, 0xA0, 0x02, 0xA0,
	0x02, 0x00, 0x02, 0x50, 0x00, 0x20, 0x14, 0x39,
	0x00, 0x09, 0x00, 0x0C, 0x00, 0x85, 0x00, 0x70,
	0x18, 0x00, 0x10, 0xF0, 0x03, 0x0C, 0x20, 0x00,
	0x06, 0x0B, 0x0B, 0x33, 0x0E, 0x1C, 0x2A, 0x38,
	0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7B,
	0x7D, 0x7E, 0x01, 0x02, 0x01, 0x00, 0x09, 0x40,
	0x09, 0xBE, 0x19, 0xFC, 0x19, 0xFA, 0x19, 0xF8,
	0x1A, 0x38, 0x1A, 0x78, 0x1A, 0xB6, 0x2A, 0xF6,
	0x2B, 0x34, 0x2B, 0x74, 0x3B, 0x74, 0x6B, 0xF4,
	0x00
};

/* 1008x2244 */
static const unsigned char FHD_PPS_SETTING[DSC_PPS_SIZE] = {
	0x11, 0x00, 0x00, 0x89, 0x30, 0x80, 0x08, 0xC4,
	0x03, 0xF0, 0x00, 0xBB, 0x01, 0xF8, 0x01, 0xF8,
	0x02, 0x00, 0x01, 0xFC, 0x00, 0x20, 0x11, 0x82,
	0x00, 0x07, 0x00, 0x0C, 0x00, 0x85, 0x00, 0x96,
	0x18, 0x00, 0x10, 0xF0, 0x03, 0x0C, 0x20, 0x00,
	0x06, 0x0B, 0x0B, 0x33, 0x0E, 0x1C, 0x2A, 0x38,
	0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7B,
	0x7D, 0x7E, 0x01, 0x02, 0x01, 0x00, 0x09, 0x40,
	0x09, 0xBE, 0x19, 0xFC, 0x19, 0xFA, 0x19, 0xF8,
	0x1A, 0x38, 0x1A, 0x78, 0x1A, 0xB6, 0x2A, 0xF6,
	0x2B, 0x34, 0x2B, 0x74, 0x3B, 0x74, 0x6B, 0xF4,
	0x00
};

#define HK3_WRCTRLD_DIMMING_BIT    0x08
#define HK3_WRCTRLD_BCTRL_BIT      0x20
#define HK3_WRCTRLD_HBM_BIT        0xC0
#define HK3_WRCTRLD_LOCAL_HBM_BIT  0x10

#define HK3_TE2_CHANGEABLE 0x04
#define HK3_TE2_FIXED      0x51
#define HK3_TE2_RISING_EDGE_OFFSET 0x10
#define HK3_TE2_FALLING_EDGE_OFFSET 0x30
#define HK3_TE2_FALLING_EDGE_OFFSET_NS 0x25

static const u8 unlock_cmd_f0[] = { 0xF0, 0x5A, 0x5A };
static const u8 lock_cmd_f0[]   = { 0xF0, 0xA5, 0xA5 };
static const u8 freq_update[] = { 0xF7, 0x0F };

static const struct exynos_dsi_cmd hk3_lp_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_OFF),
	EXYNOS_DSI_CMD0(unlock_cmd_f0),
	/* Fixed TE: sync on */
	EXYNOS_DSI_CMD_SEQ(0xB9, 0x51),
	/* Set freq at 30 Hz */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x01, 0x60),
	EXYNOS_DSI_CMD_SEQ(0x60, 0x00),
	/* Set 10 Hz idle */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x18, 0xBD),
	EXYNOS_DSI_CMD_SEQ(0xBD, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0xBD, 0x25),
	EXYNOS_DSI_CMD0(freq_update),
	EXYNOS_DSI_CMD0(lock_cmd_f0),
};
static DEFINE_EXYNOS_CMD_SET(hk3_lp);

static const struct exynos_dsi_cmd hk3_lp_off_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_OFF),
};

static const struct exynos_dsi_cmd hk3_lp_low_cmds[] = {
	EXYNOS_DSI_CMD0(unlock_cmd_f0),
	/* AOD High Mode, 50nit */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x52, 0x94),
	/* AOD Low Mode, 10nit */
	EXYNOS_DSI_CMD_SEQ(0x94, 0x01, 0x07, 0x6A, 0x02),
	/* temporary solution to avoid black screen */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x00, 0x01),
	EXYNOS_DSI_CMD(lock_cmd_f0, 34),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_ON),
};

static const struct exynos_dsi_cmd hk3_lp_high_cmds[] = {
	EXYNOS_DSI_CMD0(unlock_cmd_f0),
	/* AOD High Mode, 50nit */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x52, 0x94),
	/* AOD High Mode, 50nit */
	EXYNOS_DSI_CMD_SEQ(0x94, 0x00, 0x07, 0x6A, 0x02),
	/* temporary solution to avoid black screen */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x00, 0x01),
	EXYNOS_DSI_CMD(lock_cmd_f0, 34),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_ON),
};

static const struct exynos_binned_lp hk3_binned_lp[] = {
	BINNED_LP_MODE("off", 0, hk3_lp_off_cmds),
	BINNED_LP_MODE_TIMING("low", 80, hk3_lp_low_cmds,
			      HK3_TE2_RISING_EDGE_OFFSET, HK3_TE2_FALLING_EDGE_OFFSET),
	BINNED_LP_MODE_TIMING("high", 2047, hk3_lp_high_cmds,
			      HK3_TE2_RISING_EDGE_OFFSET, HK3_TE2_FALLING_EDGE_OFFSET)
};

static u8 hk3_get_te2_option(struct exynos_panel *ctx)
{
	struct hk3_panel *spanel = to_spanel(ctx);

	if (!ctx || !ctx->current_mode)
		return HK3_TE2_CHANGEABLE;

	if (ctx->current_mode->exynos_mode.is_lp_mode ||
	    (test_bit(FEAT_EARLY_EXIT, spanel->feat) &&
		spanel->auto_mode_vrefresh < 30))
		return HK3_TE2_FIXED;

	return HK3_TE2_CHANGEABLE;
}

static void hk3_update_te2_internal(struct exynos_panel *ctx, bool lock)
{
	struct exynos_panel_te2_timing timing = {
		.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
		.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
	};
	u32 rising, falling;
	struct hk3_panel *spanel = to_spanel(ctx);
	u8 option = hk3_get_te2_option(ctx);
	u8 idx;
	int ret;

	if (!ctx)
		return;

	ret = exynos_panel_get_current_mode_te2(ctx, &timing);
	if (ret) {
		dev_dbg(ctx->dev, "failed to get TE2 timng\n");
		return;
	}
	rising = timing.rising_edge;
	falling = timing.falling_edge;

	if (option == HK3_TE2_CHANGEABLE && test_bit(FEAT_OP_NS, spanel->feat))
		falling = HK3_TE2_FALLING_EDGE_OFFSET_NS;

	ctx->te2.option = (option == HK3_TE2_FIXED) ? TE2_OPT_FIXED : TE2_OPT_CHANGEABLE;

	dev_dbg(ctx->dev,
		"TE2 updated: option %s, idle %s, rising=0x%X falling=0x%X\n",
		(option == HK3_TE2_CHANGEABLE) ? "changeable" : "fixed",
		ctx->panel_idle_vrefresh ? "active" : "inactive",
		rising, falling);

	if (lock)
		EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x42, 0xF2);
	EXYNOS_DCS_BUF_ADD(ctx, 0xF2, 0x0D);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x01, 0xB9);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB9, option);
	idx = option == HK3_TE2_FIXED ? 0x22 : 0x1E;
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, idx, 0xB9);
	if (option == HK3_TE2_FIXED) {
		EXYNOS_DCS_BUF_ADD(ctx, 0xB9, (rising >> 8) & 0xF, rising & 0xFF,
			(falling >> 8) & 0xF, falling & 0xFF,
			(rising >> 8) & 0xF, rising & 0xFF,
			(falling >> 8) & 0xF, falling & 0xFF);
	} else {
		EXYNOS_DCS_BUF_ADD(ctx, 0xB9, (rising >> 8) & 0xF, rising & 0xFF,
			(falling >> 8) & 0xF, falling & 0xFF);
	}
	if (lock)
		EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);
}

static void hk3_update_te2(struct exynos_panel *ctx)
{
	hk3_update_te2_internal(ctx, true);
}

static inline bool is_auto_mode_allowed(struct exynos_panel *ctx)
{
	/* don't want to enable auto mode/early exit during hbm or dimming on */
	if (IS_HBM_ON(ctx->hbm_mode) || ctx->dimming_on)
		return false;

	if (ctx->idle_delay_ms) {
		const unsigned int delta_ms = panel_get_idle_time_delta(ctx);

		if (delta_ms < ctx->idle_delay_ms)
			return false;
	}

	return ctx->panel_idle_enabled;
}

static u32 hk3_get_min_idle_vrefresh(struct exynos_panel *ctx,
				     const struct exynos_panel_mode *pmode)
{
	const int vrefresh = drm_mode_vrefresh(&pmode->mode);
	int min_idle_vrefresh = ctx->min_vrefresh;

	if ((min_idle_vrefresh < 0) || !is_auto_mode_allowed(ctx))
		return 0;

	if (min_idle_vrefresh <= 10)
		min_idle_vrefresh = 10;
	else if (min_idle_vrefresh <= 30)
		min_idle_vrefresh = 30;
	else if (min_idle_vrefresh <= 60)
		min_idle_vrefresh = 60;
	else
		return 0;

	if (min_idle_vrefresh >= vrefresh) {
		dev_dbg(ctx->dev, "min idle vrefresh (%d) higher than target (%d)\n",
				min_idle_vrefresh, vrefresh);
		return 0;
	}

	return min_idle_vrefresh;
}

static void hk3_update_panel_feat(struct exynos_panel *ctx,
	const struct exynos_panel_mode *pmode, bool enforce)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	u32 vrefresh, idle_vrefresh = spanel->auto_mode_vrefresh;
	u8 val;
	DECLARE_BITMAP(changed_feat, FEAT_MAX);

	if (pmode)
		vrefresh = drm_mode_vrefresh(&pmode->mode);
	else
		vrefresh = drm_mode_vrefresh(&ctx->current_mode->mode);

	if (enforce) {
		bitmap_fill(changed_feat, FEAT_MAX);
	} else {
		bitmap_xor(changed_feat, spanel->feat, spanel->hw_feat, FEAT_MAX);
		if (bitmap_empty(changed_feat, FEAT_MAX) &&
			vrefresh == spanel->hw_vrefresh &&
			idle_vrefresh == spanel->hw_idle_vrefresh)
			return;
	}

	spanel->hw_vrefresh = vrefresh;
	spanel->hw_idle_vrefresh = idle_vrefresh;
	bitmap_copy(spanel->hw_feat, spanel->feat, FEAT_MAX);
	dev_dbg(ctx->dev,
		"op=%s ee=%s hbm=%s irc=%s fi=%s fps=%u idle_fps=%u\n",
		test_bit(FEAT_OP_NS, spanel->feat) ? "ns" : "hs",
		test_bit(FEAT_EARLY_EXIT, spanel->feat) ? "on" : "off",
		test_bit(FEAT_HBM, spanel->feat) ? "on" : "off",
		test_bit(FEAT_IRC_OFF, spanel->feat) ? "off" : "on",
		test_bit(FEAT_FRAME_AUTO, spanel->feat) ? "auto" : "manual",
		vrefresh,
		idle_vrefresh);

	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);

	/* TE width setting */
	if (test_bit(FEAT_OP_NS, changed_feat)) {
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x04, 0xB9);
		if (test_bit(FEAT_OP_NS, spanel->feat))
			/* Changeable TE setting */
			EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x0B, 0xC9, 0x0B, 0xE8,
				/* Fixed TE setting */
				0x0B, 0xC9, 0x0B, 0xE8, 0x0B, 0xC9, 0x0B, 0xE8);
		else
			/* Changeable TE setting */
			EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x0B, 0xE0, 0x00, 0x2F,
				/* Fixed TE setting */
				0x0B, 0xE0, 0x00, 0x2F, 0x0B, 0xE0, 0x00, 0x2F);
	}
	/* TE setting */
	if (test_bit(FEAT_EARLY_EXIT, changed_feat) ||
		test_bit(FEAT_OP_NS, changed_feat)) {
		if (test_bit(FEAT_EARLY_EXIT, spanel->feat) && !spanel->force_changeable_te) {
			/* Fixed TE */
			EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x51);
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x02, 0xB9);
			val = test_bit(FEAT_OP_NS, spanel->feat) ? 0x01 : 0x00;
			EXYNOS_DCS_BUF_ADD(ctx, 0xB9, val);
		} else {
			/* Changeable TE */
			EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x04);
		}
	}

	/* TE2 setting */
	if (test_bit(FEAT_OP_NS, changed_feat))
		hk3_update_te2_internal(ctx, false);

	/* HBM IRC setting */
	if (test_bit(FEAT_IRC_OFF, changed_feat)) {
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x01, 0x9B, 0x92);
		val = test_bit(FEAT_IRC_OFF, spanel->feat) ? 0x07 : 0x27;
		EXYNOS_DCS_BUF_ADD(ctx, 0x92, val);
	}

	/*
	 * Operating Mode: NS or HS
	 *
	 * Description: the configs could possibly be overrided by frequency setting,
	 * depending on FI mode.
	 */
	if (test_bit(FEAT_OP_NS, changed_feat)) {
		/* mode set */
		EXYNOS_DCS_BUF_ADD(ctx, 0xF2, 0x01);
		val = test_bit(FEAT_OP_NS, spanel->feat) ? 0x18 : 0x00;
		EXYNOS_DCS_BUF_ADD(ctx, 0x60, val);
	}

	/*
	 * Note: the following command sequence should be sent as a whole if one of panel
	 * state defined by enum panel_state changes or at turning on panel, or unexpected
	 * behaviors will be seen, e.g. black screen, flicker.
	 */

	/*
	 * Early-exit: enable or disable
	 *
	 * Description: early-exit sequence overrides some configs HBM set.
	 */
	if (test_bit(FEAT_EARLY_EXIT, spanel->feat)) {
		if (test_bit(FEAT_HBM, spanel->feat))
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x21, 0x00, 0x83, 0x03, 0x01);
		else
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x21, 0x01, 0x83, 0x03, 0x03);
	} else {
		if (test_bit(FEAT_HBM, spanel->feat))
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x21, 0x80, 0x83, 0x03, 0x01);
		else
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x21, 0x81, 0x83, 0x03, 0x03);
	}
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x10, 0xBD);
	val = test_bit(FEAT_EARLY_EXIT, spanel->feat) ? 0x22 : 0x00;
	EXYNOS_DCS_BUF_ADD(ctx, 0xBD, val);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x82, 0xBD);
	EXYNOS_DCS_BUF_ADD(ctx, 0xBD, val, val, val, val);
	val = test_bit(FEAT_OP_NS, spanel->feat) ? 0x4E : 0x1E;
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, val, 0xBD);
	if (test_bit(FEAT_HBM, spanel->feat)) {
		if (test_bit(FEAT_OP_NS, spanel->feat))
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, 0x00, 0x02,
				0x00, 0x04, 0x00, 0x0A, 0x00, 0x16, 0x00, 0x76);
		else
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, 0x00, 0x01,
				0x00, 0x03, 0x00, 0x0B, 0x00, 0x17, 0x00, 0x77);
	} else {
		if (test_bit(FEAT_OP_NS, spanel->feat))
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, 0x00, 0x04,
				0x00, 0x08, 0x00, 0x14, 0x00, 0x2C, 0x00, 0xEC);
		else
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, 0x00, 0x02,
				0x00, 0x06, 0x00, 0x16, 0x00, 0x2E, 0x00, 0xEE);
	}

	/*
	 * Frequency setting: FI, frequency, idle frequency
	 *
	 * Description: this sequence possibly overrides some configs early-exit
	 * and operation set, depending on FI mode.
	 */
	if (test_bit(FEAT_FRAME_AUTO, spanel->feat)) {
		if (test_bit(FEAT_OP_NS, spanel->feat)) {
			/* threshold setting */
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x0C, 0xBD);
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00);
		} else {
			/* initial frequency */
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x92, 0xBD);
			if (vrefresh == 60)
				val = test_bit(FEAT_HBM, spanel->feat) ? 0x01 : 0x02;
			else
				val = 0x00;
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, val);
		}
		/* target frequency */
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x12, 0xBD);
		if (test_bit(FEAT_OP_NS, spanel->feat)) {
			if (idle_vrefresh == 10) {
				val = test_bit(FEAT_HBM, spanel->feat) ? 0x0A : 0x14;
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, val);
			} else {
				/* 30Hz */
				val = test_bit(FEAT_HBM, spanel->feat) ? 0x02 : 0x04;
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, val);
			}
		} else {
			if (idle_vrefresh == 10) {
				val = test_bit(FEAT_HBM, spanel->feat) ? 0x0B : 0x16;
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, val);
			} else if (idle_vrefresh == 30) {
				val = test_bit(FEAT_HBM, spanel->feat) ? 0x03 : 0x06;
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, val);
			} else {
				/* 60Hz */
				val = test_bit(FEAT_HBM, spanel->feat) ? 0x01 : 0x02;
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, val);
			}
		}
		/* step setting */
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x9E, 0xBD);
		if (test_bit(FEAT_OP_NS, spanel->feat)) {
			if (test_bit(FEAT_HBM, spanel->feat))
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x02, 0x00, 0x0A, 0x00, 0x00);
			else
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x04, 0x00, 0x14, 0x00, 0x00);
		} else {
			if (test_bit(FEAT_HBM, spanel->feat))
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x01, 0x00, 0x03, 0x00, 0x0B);
			else
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x02, 0x00, 0x06, 0x00, 0x16);
		}
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0xAE, 0xBD);
		if (test_bit(FEAT_OP_NS, spanel->feat)) {
			if (idle_vrefresh == 10)
				/* 60Hz -> 10Hz idle */
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x01, 0x00, 0x00);
			else
				/* 60Hz -> 30Hz idle */
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, 0x00);
		} else {
			if (vrefresh == 60) {
				if (idle_vrefresh == 10)
					/* 60Hz -> 10Hz idle */
					EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x01, 0x01, 0x00);
				else
					/* 60Hz -> 30Hz idle */
					EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x01, 0x00, 0x00);
			} else {
				if (idle_vrefresh == 10)
					/* 120Hz -> 10Hz idle */
					EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x03, 0x00);
				else
					/* 120Hz -> 60Hz/30Hz idle */
					EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x00, 0x00);
			}
		}
		EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0xA3);
	} else { /* manual */
		EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x21);
		if (test_bit(FEAT_OP_NS, spanel->feat)) {
			if (vrefresh == 1)
				val = 0x1F;
			else if (vrefresh == 5)
				val = 0x1E;
			else if (vrefresh == 10)
				val = 0x1B;
			else if (vrefresh == 30)
				val = 0x19;
			else
				/* 60Hz */
				val = 0x18;
		} else {
			if (vrefresh == 1)
				val = 0x07;
			else if (vrefresh == 5)
				val = 0x06;
			else if (vrefresh == 10)
				val = 0x03;
			else if (vrefresh == 30)
				val = 0x02;
			else if (vrefresh == 60)
				val = 0x01;
			else
				/* 120Hz */
				val = 0x00;
		}
		EXYNOS_DCS_BUF_ADD(ctx, 0x60, val);
	}

	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);;
}

static void hk3_update_refresh_mode(struct exynos_panel *ctx,
					const struct exynos_panel_mode *pmode,
					const u32 idle_vrefresh)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);

	dev_dbg(ctx->dev, "%s: mode: %s set idle_vrefresh: %u\n", __func__,
		pmode->mode.name, idle_vrefresh);

	if (idle_vrefresh)
		set_bit(FEAT_FRAME_AUTO, spanel->feat);
	else
		clear_bit(FEAT_FRAME_AUTO, spanel->feat);

	if (vrefresh == 120 || idle_vrefresh)
		set_bit(FEAT_EARLY_EXIT, spanel->feat);
	else
		clear_bit(FEAT_EARLY_EXIT, spanel->feat);

	spanel->auto_mode_vrefresh = idle_vrefresh;
	/*
	 * Note: when mode is explicitly set, panel performs early exit to get out
	 * of idle at next vsync, and will not back to idle until not seeing new
	 * frame traffic for a while. If idle_vrefresh != 0, try best to guess what
	 * panel_idle_vrefresh will be soon, and hk3_update_idle_state() in
	 * new frame commit will correct it if the guess is wrong.
	 */
	ctx->panel_idle_vrefresh = idle_vrefresh;
	hk3_update_panel_feat(ctx, pmode, false);
	te2_state_changed(ctx->bl);
	backlight_state_changed(ctx->bl);
}

static void hk3_change_frequency(struct exynos_panel *ctx,
				 const struct exynos_panel_mode *pmode)
{
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 idle_vrefresh = 0;

	if (vrefresh > ctx->op_hz) {
		dev_err(ctx->dev,
		"invalid freq setting: op_hz=%u, vrefresh=%u\n",
		ctx->op_hz, vrefresh);
		return;
	}

	if (pmode->idle_mode == IDLE_MODE_ON_INACTIVITY)
		idle_vrefresh = hk3_get_min_idle_vrefresh(ctx, pmode);

	hk3_update_refresh_mode(ctx, pmode, idle_vrefresh);

	dev_dbg(ctx->dev, "change to %u hz\n", vrefresh);
}

static void hk3_panel_idle_notification(struct exynos_panel *ctx,
		u32 display_id, u32 vrefresh, u32 idle_te_vrefresh)
{
	char event_string[64];
	char *envp[] = { event_string, NULL };
	struct drm_device *dev = ctx->bridge.dev;

	if (!dev) {
		dev_warn(ctx->dev, "%s: drm_device is null\n", __func__);
	} else {
		snprintf(event_string, sizeof(event_string),
			"PANEL_IDLE_ENTER=%u,%u,%u", display_id, vrefresh, idle_te_vrefresh);
		kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
	}
}

static bool hk3_set_self_refresh(struct exynos_panel *ctx, bool enable)
{
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	struct hk3_panel *spanel = to_spanel(ctx);
	u32 idle_vrefresh;

	if (unlikely(!pmode))
		return false;

	/* self refresh is not supported in lp mode since that always makes use of early exit */
	if (pmode->exynos_mode.is_lp_mode)
		return false;

	idle_vrefresh = hk3_get_min_idle_vrefresh(ctx, pmode);

	if (pmode->idle_mode != IDLE_MODE_ON_SELF_REFRESH) {
		/*
		 * if idle mode is on inactivity, may need to update the target fps for auto mode,
		 * or switch to manual mode if idle should be disabled (idle_vrefresh=0)
		 */
		if ((pmode->idle_mode == IDLE_MODE_ON_INACTIVITY) &&
			(spanel->auto_mode_vrefresh != idle_vrefresh)) {
			hk3_update_refresh_mode(ctx, pmode, idle_vrefresh);
			return true;
		}
		return false;
	}

	if (!enable)
		idle_vrefresh = 0;

	/* if there's no change in idle state then skip cmds */
	if (ctx->panel_idle_vrefresh == idle_vrefresh)
		return false;

	DPU_ATRACE_BEGIN(__func__);
	hk3_update_refresh_mode(ctx, pmode, idle_vrefresh);

	if (idle_vrefresh) {
		const int vrefresh = drm_mode_vrefresh(&pmode->mode);

		hk3_panel_idle_notification(ctx, 0, vrefresh, 120);
	} else if (ctx->panel_need_handle_idle_exit) {
		struct drm_crtc *crtc = NULL;

		if (ctx->exynos_connector.base.state)
			crtc = ctx->exynos_connector.base.state->crtc;

		/*
		 * after exit idle mode with fixed TE at non-120hz, TE may still keep at 120hz.
		 * If any layer that already be assigned to DPU that can't be handled at 120hz,
		 * panel_need_handle_idle_exit will be set then we need to wait one vblank to
		 * avoid underrun issue.
		 */
		dev_dbg(ctx->dev, "wait one vblank after exit idle\n");
		DPU_ATRACE_BEGIN("wait_one_vblank");
		if (crtc) {
			int ret = drm_crtc_vblank_get(crtc);

			if (!ret) {
				drm_crtc_wait_one_vblank(crtc);
				drm_crtc_vblank_put(crtc);
			} else {
				usleep_range(8350, 8500);
			}
		} else {
			usleep_range(8350, 8500);
		}
		DPU_ATRACE_END("wait_one_vblank");
	}

	DPU_ATRACE_END(__func__);

	return true;
}

static int hk3_atomic_check(struct exynos_panel *ctx, struct drm_atomic_state *state)
{
	struct drm_connector *conn = &ctx->exynos_connector.base;
	struct drm_connector_state *new_conn_state = drm_atomic_get_new_connector_state(state, conn);
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct hk3_panel *spanel = to_spanel(ctx);

	if (!ctx->current_mode || drm_mode_vrefresh(&ctx->current_mode->mode) == 120 ||
	    !new_conn_state || !new_conn_state->crtc)
		return 0;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);
	old_crtc_state = drm_atomic_get_old_crtc_state(state, new_conn_state->crtc);
	if (!old_crtc_state || !new_crtc_state || !new_crtc_state->active)
		return 0;

	if ((spanel->auto_mode_vrefresh && old_crtc_state->self_refresh_active) ||
	    !drm_atomic_crtc_effectively_active(old_crtc_state)) {
		struct drm_display_mode *mode = &new_crtc_state->adjusted_mode;

		/* set clock to max refresh rate on self refresh exit or resume due to early exit */
		mode->clock = mode->htotal * mode->vtotal * 120 / 1000;

		if (mode->clock != new_crtc_state->mode.clock) {
			new_crtc_state->mode_changed = true;
			dev_dbg(ctx->dev, "raise mode (%s) clock to 120hz on %s\n",
				mode->name,
				old_crtc_state->self_refresh_active ? "self refresh exit" : "resume");
		}
	} else if (old_crtc_state->active_changed &&
		   (old_crtc_state->adjusted_mode.clock != old_crtc_state->mode.clock)) {
		/* clock hacked in last commit due to self refresh exit or resume, undo that */
		new_crtc_state->mode_changed = true;
		new_crtc_state->adjusted_mode.clock = new_crtc_state->mode.clock;
		dev_dbg(ctx->dev, "restore mode (%s) clock after self refresh exit or resume\n",
			new_crtc_state->mode.name);
	}

	return 0;
}

static void hk3_write_display_mode(struct exynos_panel *ctx,
				   const struct drm_display_mode *mode)
{
	u8 val = HK3_WRCTRLD_BCTRL_BIT;

	if (IS_HBM_ON(ctx->hbm_mode))
		val |= HK3_WRCTRLD_HBM_BIT;

	if (ctx->hbm.local_hbm.enabled)
		val |= HK3_WRCTRLD_LOCAL_HBM_BIT;

	if (ctx->dimming_on)
		val |= HK3_WRCTRLD_DIMMING_BIT;

	dev_dbg(ctx->dev,
		"%s(wrctrld:0x%x, hbm: %s, dimming: %s local_hbm: %s)\n",
		__func__, val, IS_HBM_ON(ctx->hbm_mode) ? "on" : "off",
		ctx->dimming_on ? "on" : "off",
		ctx->hbm.local_hbm.enabled ? "on" : "off");

	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);
}

#define HK3_OPR_VAL_LEN 2
#define HK3_MAX_OPR_VAL 0x3FF
/* Get OPR (on pixel ratio), the unit is percent */
static int hk3_get_opr(struct exynos_panel *ctx, u8 *opr)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	u8 buf[HK3_OPR_VAL_LEN] = {0};
	u16 val;
	int ret;

	DPU_ATRACE_BEGIN(__func__);
	EXYNOS_DCS_WRITE_TABLE(ctx, unlock_cmd_f0);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x00, 0xE7, 0x91);
	ret = mipi_dsi_dcs_read(dsi, 0x91, buf, HK3_OPR_VAL_LEN);
	EXYNOS_DCS_WRITE_TABLE(ctx, lock_cmd_f0);
	DPU_ATRACE_END(__func__);

	if (ret != HK3_OPR_VAL_LEN) {
		dev_warn(ctx->dev, "Failed to read OPR (%d)\n", ret);
		return ret;
	}

	val = (buf[0] << 8) | buf[1];
	*opr = DIV_ROUND_CLOSEST(val * 100, HK3_MAX_OPR_VAL);
	dev_dbg(ctx->dev, "%s: %u (0x%X)\n", __func__, *opr, val);

	return 0;
}

#define HK3_ZA_THRESHOLD_OPR 80
static void hk3_update_za(struct exynos_panel *ctx)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	bool enable_za = false;
	u8 opr;

	if (spanel->hw_acl_enabled) {
		if (!hk3_get_opr(ctx, &opr)) {
			enable_za = (opr > HK3_ZA_THRESHOLD_OPR);
		} else {
			dev_warn(ctx->dev, "Unable to update za\n");
			return;
		}
	}

	if (spanel->hw_za_enabled != enable_za) {
		EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x01, 0x6C, 0x92);
		/* LP setting - 0x21: 7.5%, 0x00: off */
		EXYNOS_DCS_BUF_ADD(ctx, 0x92, enable_za ? 0x21 : 0x00);
		EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);

		spanel->hw_za_enabled = enable_za;
		dev_info(ctx->dev, "%s: %s\n", __func__, enable_za ? "on" : "off");
	}
}

#define HK3_ACL_ZA_THRESHOLD_DBV 3917
static int hk3_set_brightness(struct exynos_panel *ctx, u16 br)
{
	int ret;
	u16 brightness;

	if (ctx->current_mode->exynos_mode.is_lp_mode) {
		const struct exynos_panel_funcs *funcs;

		funcs = ctx->desc->exynos_panel_func;
		if (funcs && funcs->set_binned_lp)
			funcs->set_binned_lp(ctx, br);
		return 0;
	}

	brightness = (br & 0xff) << 8 | br >> 8;
	ret = exynos_dcs_set_brightness(ctx, brightness);
	if (!ret) {
		struct hk3_panel *spanel = to_spanel(ctx);
		bool enable_acl = (br >= HK3_ACL_ZA_THRESHOLD_DBV && IS_HBM_ON(ctx->hbm_mode));

		if (spanel->hw_acl_enabled != enable_acl) {
			/* ACL setting - 0x01: 5%, 0x00: off */
			EXYNOS_DCS_WRITE_SEQ(ctx, 0x55, enable_acl ? 0x01 : 0x00);
			spanel->hw_acl_enabled = enable_acl;
			dev_info(ctx->dev, "%s: acl: %s\n", __func__, enable_acl ? "on" : "off");

			hk3_update_za(ctx);
		}
	}

	return ret;
}

static void hk3_set_nolp_mode(struct exynos_panel *ctx,
			      const struct exynos_panel_mode *pmode)
{
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 delay_us = mult_frac(1000, 1020, vrefresh);

	/* clear the brightness level (temporary solution) */
	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x00, 0x00);

	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	/* AOD low mode setting off */
	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x52, 0x94);
	EXYNOS_DCS_BUF_ADD(ctx, 0x94, 0x00);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);

	hk3_update_panel_feat(ctx, pmode, true);
	/* backlight control and dimming */
	hk3_write_display_mode(ctx, &pmode->mode);
	hk3_change_frequency(ctx, pmode);

	usleep_range(delay_us, delay_us + 10);
	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_SET_DISPLAY_ON);

	dev_info(ctx->dev, "exit LP mode\n");
}

static const struct exynos_dsi_cmd hk3_init_cmds[] = {
	/* Enable TE*/
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_TEAR_ON),
	EXYNOS_DSI_CMD0(unlock_cmd_f0),

	/* TSP SYNC Enable (Auto Set) */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x3C, 0xB9),
	EXYNOS_DSI_CMD_SEQ(0xB9, 0x19, 0x09),

	/* FFC: 165MHz, MIPI Speed 1346 Mbps */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x36, 0xC5),
	EXYNOS_DSI_CMD_SEQ(0xC5, 0x11, 0x10, 0x50, 0x05, 0x4E, 0x74),

	EXYNOS_DSI_CMD0(freq_update),
	EXYNOS_DSI_CMD0(lock_cmd_f0),
	/* CASET: 1343 */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x05, 0x3F),
	/* PASET: 2991 */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x0B, 0xAF),
};
static DEFINE_EXYNOS_CMD_SET(hk3_init);

static void hk3_lhbm_luminance_opr_setting(struct exynos_panel *ctx)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	bool is_ns_mode = test_bit(FEAT_OP_NS, spanel->feat);

	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x02, 0xF9, 0x95);
	/* DBV setting */
	EXYNOS_DCS_BUF_ADD(ctx, 0x95, 0x00, 0x40, 0x0C, 0x01, 0x90, 0x33, 0x06, 0x60,
				0xCC, 0x11, 0x92, 0x7F);
	EXYNOS_DCS_BUF_ADD(ctx, 0x71, 0xC6, 0x00, 0x00, 0x19);
	/* 120Hz base (HS) offset */
	EXYNOS_DCS_BUF_ADD(ctx, 0x6C, 0x9C, 0x9F, 0x59, 0x58, 0x50, 0x2F, 0x2B, 0x2E);
	EXYNOS_DCS_BUF_ADD(ctx, 0x71, 0xC6, 0x00, 0x00, 0x6A);
	/* 60Hz base (NS) offset */
	EXYNOS_DCS_BUF_ADD(ctx, 0x6C, 0xA0, 0xA7, 0x57, 0x5C, 0x52, 0x37, 0x37, 0x40);

	/* Target frequency */
	EXYNOS_DCS_BUF_ADD(ctx, 0x60, is_ns_mode ? 0x18 : 0x00);
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	/* Opposite setting of target frequency */
	EXYNOS_DCS_BUF_ADD(ctx, 0x60, is_ns_mode ? 0x00 : 0x18);
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	/* Target frequency */
	EXYNOS_DCS_BUF_ADD(ctx, 0x60, is_ns_mode ? 0x18 : 0x00);
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);
}

static int hk3_enable(struct drm_panel *panel)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	const struct drm_display_mode *mode;
	const bool needs_reset = !is_panel_enabled(ctx);
	bool is_fhd;

	if (!pmode) {
		dev_err(ctx->dev, "no current mode set\n");
		return -EINVAL;
	}
	mode = &pmode->mode;
	is_fhd = mode->hdisplay == 1008;

	dev_info(ctx->dev, "%s\n", __func__);

	if (needs_reset)
		exynos_panel_reset(ctx);

	/* DSC related configuration */
	EXYNOS_DCS_WRITE_SEQ(ctx, 0x9D, 0x01);
	EXYNOS_PPS_WRITE_BUF(ctx, is_fhd ? FHD_PPS_SETTING : WQHD_PPS_SETTING);

	if (needs_reset) {
		EXYNOS_DCS_WRITE_SEQ_DELAY(ctx, 120, MIPI_DCS_EXIT_SLEEP_MODE);
		exynos_panel_send_cmd_set(ctx, &hk3_init_cmd_set);
		if (ctx->panel_rev == PANEL_REV_PROTO1)
			hk3_lhbm_luminance_opr_setting(ctx);
	}

	EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xC3, is_fhd ? 0x0D : 0x0C);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);

	hk3_update_panel_feat(ctx, pmode, true);
	hk3_write_display_mode(ctx, mode); /* dimming and HBM */
	hk3_change_frequency(ctx, pmode);

	if (pmode->exynos_mode.is_lp_mode)
		exynos_panel_set_lp_mode(ctx, pmode);
	else if (needs_reset || (ctx->panel_state == PANEL_STATE_BLANK))
		EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_SET_DISPLAY_ON);

	return 0;
}

static int hk3_disable(struct drm_panel *panel)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	struct hk3_panel *spanel = to_spanel(ctx);
	int ret;

	/* skip disable sequence if going through modeset */
	if (ctx->panel_state == PANEL_STATE_MODESET)
		return 0;

	ret = exynos_panel_disable(panel);
	if (ret)
		return ret;

	/* panel register state gets reset after disabling hardware */
	bitmap_clear(spanel->hw_feat, 0, FEAT_MAX);
	spanel->hw_vrefresh = 60;
	spanel->hw_idle_vrefresh = 0;
	spanel->hw_acl_enabled = false;
	spanel->hw_za_enabled = false;

	EXYNOS_DCS_WRITE_SEQ_DELAY(ctx, 20, MIPI_DCS_SET_DISPLAY_OFF);

	if (ctx->panel_state == PANEL_STATE_OFF)
		EXYNOS_DCS_WRITE_SEQ_DELAY(ctx, 100, MIPI_DCS_ENTER_SLEEP_MODE);

	return 0;
}

/*
 * 120hz auto mode takes at least 2 frames to start lowering refresh rate in addition to
 * time to next vblank. Use just over 2 frames time to consider worst case scenario
 */
#define EARLY_EXIT_THRESHOLD_US 17000

/**
 * hk3_update_idle_state - update panel auto frame insertion state
 * @ctx: panel struct
 *
 * - update timestamp of switching to manual mode in case its been a while since the
 *   last frame update and auto mode may have started to lower refresh rate.
 * - disable auto refresh mode if there is switching delay requirement
 * - trigger early exit by command if it's changeable TE, which could result in
 *   fast 120 Hz boost and seeing 120 Hz TE earlier
 */
static void hk3_update_idle_state(struct exynos_panel *ctx)
{
	s64 delta_us;
	struct hk3_panel *spanel = to_spanel(ctx);

	ctx->panel_idle_vrefresh = 0;
	if (!test_bit(FEAT_FRAME_AUTO, spanel->feat))
		return;

	delta_us = ktime_us_delta(ktime_get(), ctx->last_commit_ts);
	if (delta_us < EARLY_EXIT_THRESHOLD_US) {
		dev_dbg(ctx->dev, "skip early exit. %lldus since last commit\n",
			delta_us);
		return;
	}

	/* triggering early exit causes a switch to 120hz */
	ctx->last_mode_set_ts = ktime_get();

	DPU_ATRACE_BEGIN(__func__);
	/*
	 * If there is delay limitation requirement, turn off auto mode to prevent panel
	 * from lowering frequency too fast if not seeing new frame.
	 */
	if (ctx->idle_delay_ms) {
		const struct exynos_panel_mode *pmode = ctx->current_mode;
		hk3_update_refresh_mode(ctx, pmode, 0);
	} else if (spanel->force_changeable_te) {
		dev_dbg(ctx->dev, "sending early exit out cmd\n");
		EXYNOS_DCS_BUF_ADD_SET(ctx, unlock_cmd_f0);
		EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
		EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, lock_cmd_f0);
	}

	DPU_ATRACE_END(__func__);
}

static void hk3_commit_done(struct exynos_panel *ctx)
{
	if (!ctx->current_mode)
		return;

	hk3_update_idle_state(ctx);

	hk3_update_za(ctx);
}

static void hk3_set_hbm_mode(struct exynos_panel *ctx,
			     enum exynos_hbm_mode mode)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	const struct exynos_panel_mode *pmode = ctx->current_mode;

	if (mode == ctx->hbm_mode)
		return;

	if (unlikely(!pmode))
		return;

	ctx->hbm_mode = mode;

	if (IS_HBM_ON(mode)) {
		set_bit(FEAT_HBM, spanel->feat);
		/* enforce IRC on for factory builds */
#ifndef DPU_FACTORY_BUILD
		if (mode == HBM_ON_IRC_ON)
			clear_bit(FEAT_IRC_OFF, spanel->feat);
		else
			set_bit(FEAT_IRC_OFF, spanel->feat);
#endif
		hk3_update_panel_feat(ctx, NULL, false);
		hk3_write_display_mode(ctx, &pmode->mode);
	} else {
		clear_bit(FEAT_HBM, spanel->feat);
		clear_bit(FEAT_IRC_OFF, spanel->feat);
		hk3_write_display_mode(ctx, &pmode->mode);
		hk3_update_panel_feat(ctx, NULL, false);
	}
}

static void hk3_set_dimming_on(struct exynos_panel *ctx,
			       bool dimming_on)
{
	const struct exynos_panel_mode *pmode = ctx->current_mode;

	ctx->dimming_on = dimming_on;
	if (pmode->exynos_mode.is_lp_mode) {
		dev_info(ctx->dev,"in lp mode, skip to update");
		return;
	}
	hk3_write_display_mode(ctx, &pmode->mode);
}

static void hk3_set_local_hbm_mode(struct exynos_panel *ctx,
				 bool local_hbm_en)
{
	const struct exynos_panel_mode *pmode;

	if (ctx->hbm.local_hbm.enabled == local_hbm_en)
		return;

	pmode = ctx->current_mode;
	if (unlikely(pmode == NULL)) {
		dev_err(ctx->dev, "%s: unknown current mode\n", __func__);
		return;
	}

	if (local_hbm_en) {
		const int vrefresh = drm_mode_vrefresh(&pmode->mode);
		/* Add check to turn on LHBM @ 120hz only to comply with HW requirement */
		if (vrefresh != 120) {
			dev_err(ctx->dev, "unexpected mode `%s` while enabling LHBM, give up\n",
				pmode->mode.name);
			return;
		}
	}

	ctx->hbm.local_hbm.enabled = local_hbm_en;
	/* TODO: LHBM Position & Size */
	hk3_write_display_mode(ctx, &pmode->mode);
}

static void hk3_mode_set(struct exynos_panel *ctx,
			 const struct exynos_panel_mode *pmode)
{
	if (!is_panel_active(ctx))
		return;

	if (ctx->hbm.local_hbm.enabled == true)
		dev_warn(ctx->dev, "do mode change (`%s`) unexpectedly when LHBM is ON\n",
			pmode->mode.name);

	hk3_change_frequency(ctx, pmode);
}

static bool hk3_is_mode_seamless(const struct exynos_panel *ctx,
				     const struct exynos_panel_mode *pmode)
{
	const struct drm_display_mode *c = &ctx->current_mode->mode;
	const struct drm_display_mode *n = &pmode->mode;

	/* seamless mode set can happen if active region resolution is same */
	return (c->vdisplay == n->vdisplay) && (c->hdisplay == n->hdisplay) &&
	       (c->flags == n->flags);
}

static int hk3_set_op_hz(struct exynos_panel *ctx, unsigned int hz)
{
	struct hk3_panel *spanel = to_spanel(ctx);
	u32 vrefresh = drm_mode_vrefresh(&ctx->current_mode->mode);

	if (vrefresh > hz || (hz != 60 && hz != 120)) {
		dev_err(ctx->dev, "invalid op_hz=%d for vrefresh=%d\n",
			hz, vrefresh);
		return -EINVAL;
	}

	ctx->op_hz = hz;
	if (hz == 60)
		set_bit(FEAT_OP_NS, spanel->feat);
	else
		clear_bit(FEAT_OP_NS, spanel->feat);

	if (is_panel_active(ctx))
		hk3_update_panel_feat(ctx, NULL, false);
	dev_info(ctx->dev, "%s op_hz at %d\n",
		is_panel_active(ctx) ? "set" : "cache", hz);

	return 0;
}

static int hk3_read_id(struct exynos_panel *ctx)
{
	return exynos_panel_read_ddic_id(ctx);
}

static void hk3_get_panel_rev(struct exynos_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	u8 build_code = (id & 0xFF00) >> 8;
	u8 rev = ((build_code & 0xE0) >> 3) | ((build_code & 0x0C) >> 2);

	exynos_panel_get_panel_rev(ctx, rev);
}

static const struct exynos_display_underrun_param underrun_param = {
	.te_idle_us = 350,
	.te_var = 1,
};

static const u32 hk3_bl_range[] = {
	94, 180, 270, 360, 2047
};

static const struct exynos_panel_mode hk3_modes[] = {
#ifdef PANEL_FACTORY_BUILD
	{
		/* 1344x2992 @ 1Hz */
		.mode = {
			.name = "1344x2992x1",
			.clock = 4485,
			.hdisplay = 1344,
			.hsync_start = 1344 + 80, // add hfp
			.hsync_end = 1344 + 80 + 24, // add hsa
			.htotal = 1344 + 80 + 24 + 36, // add hbp
			.vdisplay = 2992,
			.vsync_start = 2992 + 12, // add vfp
			.vsync_end = 2992 + 12 + 4, // add vsa
			.vtotal = 2992 + 12 + 4 + 14, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = {
				.enabled = true,
				.dsc_count = 2,
				.slice_count = 2,
				.slice_height = 187,
			},
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_UNSUPPORTED,
	},
	{
		/* 1344x2992 @ 5Hz */
		.mode = {
			.name = "1344x2992x5",
			.clock = 22423,
			.hdisplay = 1344,
			.hsync_start = 1344 + 80, // add hfp
			.hsync_end = 1344 + 80 + 24, // add hsa
			.htotal = 1344 + 80 + 24 + 36, // add hbp
			.vdisplay = 2992,
			.vsync_start = 2992 + 12, // add vfp
			.vsync_end = 2992 + 12 + 4, // add vsa
			.vtotal = 2992 + 12 + 4 + 14, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = {
				.enabled = true,
				.dsc_count = 2,
				.slice_count = 2,
				.slice_height = 187,
			},
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_UNSUPPORTED,
	},
	{
		/* 1344x2992 @ 10Hz */
		.mode = {
			.name = "1344x2992x10",
			.clock = 44846,
			.hdisplay = 1344,
			.hsync_start = 1344 + 80, // add hfp
			.hsync_end = 1344 + 80 + 24, // add hsa
			.htotal = 1344 + 80 + 24 + 36, // add hbp
			.vdisplay = 2992,
			.vsync_start = 2992 + 12, // add vfp
			.vsync_end = 2992 + 12 + 4, // add vsa
			.vtotal = 2992 + 12 + 4 + 14, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = {
				.enabled = true,
				.dsc_count = 2,
				.slice_count = 2,
				.slice_height = 187,
			},
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_UNSUPPORTED,
	},
	{
		/* 1344x2992 @ 30Hz */
		.mode = {
			.name = "1344x2992x30",
			.clock = 134539,
			.hdisplay = 1344,
			.hsync_start = 1344 + 80, // add hfp
			/* change hsa and hbp to avoid conflicting to LP mode 30Hz */
			.hsync_end = 1344 + 80 + 22, // add hsa
			.htotal = 1344 + 80 + 24 + 38, // add hbp
			.vdisplay = 2992,
			.vsync_start = 2992 + 12, // add vfp
			.vsync_end = 2992 + 12 + 4, // add vsa
			.vtotal = 2992 + 12 + 4 + 14, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = {
				.enabled = true,
				.dsc_count = 2,
				.slice_count = 2,
				.slice_height = 187,
			},
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_UNSUPPORTED,
	},
#endif
	{
		/* 1344x2992 @ 60Hz */
		.mode = {
			.name = "1344x2992x60",
			.clock = 269079,
			.hdisplay = 1344,
			.hsync_start = 1344 + 80, // add hfp
			.hsync_end = 1344 + 80 + 24, // add hsa
			.htotal = 1344 + 80 + 24 + 36, // add hbp
			.vdisplay = 2992,
			.vsync_start = 2992 + 12, // add vfp
			.vsync_end = 2992 + 12 + 4, // add vsa
			.vtotal = 2992 + 12 + 4 + 14, // add vbp
			.flags = 0,
			.type = DRM_MODE_TYPE_PREFERRED,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = {
				.enabled = true,
				.dsc_count = 2,
				.slice_count = 2,
				.slice_height = 187,
			},
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_UNSUPPORTED, //TODO
	},
	{
		/* 1344x2992 @ 120Hz */
		.mode = {
			.name = "1344x2992x120",
			.clock = 538158,
			.hdisplay = 1344,
			.hsync_start = 1344 + 80, // add hfp
			.hsync_end = 1344 + 80 + 24, // add hsa
			.htotal = 1344 + 80 + 24 + 36, // add hbp
			.vdisplay = 2992,
			.vsync_start = 2992 + 12, // add vfp
			.vsync_end = 2992 + 12 + 4, // add vsa
			.vtotal = 2992 + 12 + 4 + 14, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 150, //TODO
			.bpc = 8,
			.dsc = {
				.enabled = true,
				.dsc_count = 2,
				.slice_count = 2,
				.slice_height = 187,
			},
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_UNSUPPORTED, //TODO
	},
	{
		/* 1008x2244 @ 60Hz */
		.mode = {
			.name = "1008x2244x60",
			.clock = 156633,
			.hdisplay = 1008,
			.hsync_start = 1008 + 80, // add hfp
			.hsync_end = 1008 + 80 + 24, // add hsa
			.htotal = 1008 + 80 + 24 + 36, // add hbp
			.vdisplay = 2244,
			.vsync_start = 2244 + 12, // add vfp
			.vsync_end = 2244 + 12 + 4, // add vsa
			.vtotal = 2244 + 12 + 4 + 14, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = {
				.enabled = true,
				.dsc_count = 2,
				.slice_count = 2,
				.slice_height = 187,
			},
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_UNSUPPORTED, //TODO
	},
	{
		/* 1008x2244 @ 120Hz */
		.mode = {
			.name = "1008x2244x120",
			.clock = 313266,
			.hdisplay = 1008,
			.hsync_start = 1008 + 80, // add hfp
			.hsync_end = 1008 + 80 + 24, // add hsa
			.htotal = 1008 + 80 + 24 + 36, // add hbp
			.vdisplay = 2244,
			.vsync_start = 2244 + 12, // add vfp
			.vsync_end = 2244 + 12 + 4, // add vsa
			.vtotal = 2244 + 12 + 4 + 14, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 150, //TODO
			.bpc = 8,
			.dsc = {
				.enabled = true,
				.dsc_count = 2,
				.slice_count = 2,
				.slice_height = 187,
			},
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = HK3_TE2_RISING_EDGE_OFFSET,
			.falling_edge = HK3_TE2_FALLING_EDGE_OFFSET,
		},
		.idle_mode = IDLE_MODE_UNSUPPORTED, //TODO
	},
};

static const struct exynos_panel_mode hk3_lp_modes[] = {
	{
		.mode = {
			/* 1344x2992 @ 30Hz */
			.name = "1344x2992x30",
			.clock = 134539,
			.hdisplay = 1344,
			.hsync_start = 1344 + 80, // add hfp
			.hsync_end = 1344 + 80 + 24, // add hsa
			.htotal = 1344 + 80 + 24 + 36, // add hbp
			.vdisplay = 2992,
			.vsync_start = 2992 + 12, // add vfp
			.vsync_end = 2992 + 12 + 4, // add vsa
			.vtotal = 2992 + 12 + 4 + 14, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 25300, //TODO
			.bpc = 8,
			.dsc = {
				.enabled = true,
				.dsc_count = 2,
				.slice_count = 2,
				.slice_height = 187,
			},
			.underrun_param = &underrun_param,
			.is_lp_mode = true,
		},
	},
	{
		.mode = {
			/* 1008x2244 @ 30Hz */
			.name = "1008x2244x30",
			.clock = 78317,
			.hdisplay = 1008,
			.hsync_start = 1008 + 80, // add hfp
			.hsync_end = 1008 + 80 + 24, // add hsa
			.htotal = 1008 + 80 + 24 + 36, // add hbp
			.vdisplay = 2244,
			.vsync_start = 2244 + 12, // add vfp
			.vsync_end = 2244 + 12 + 4, // add vsa
			.vtotal = 2244 + 12 + 4 + 14, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 155,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 25300, //TODO
			.bpc = 8,
			.dsc = {
				.enabled = true,
				.dsc_count = 2,
				.slice_count = 2,
				.slice_height = 187,
			},
			.underrun_param = &underrun_param,
			.is_lp_mode = true,
		},
	},
};

static void hk3_panel_init(struct exynos_panel *ctx)
{
	struct dentry *csroot = ctx->debugfs_cmdset_entry;
	struct hk3_panel *spanel = to_spanel(ctx);

	exynos_panel_debugfs_create_cmdset(ctx, csroot, &hk3_init_cmd_set, "init");
	debugfs_create_bool("force_changeable_te", 0644, ctx->debugfs_entry,
				&spanel->force_changeable_te);

	if (ctx->panel_rev == PANEL_REV_PROTO1)
		hk3_lhbm_luminance_opr_setting(ctx);
}

static int hk3_panel_probe(struct mipi_dsi_device *dsi)
{
	struct hk3_panel *spanel;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	spanel->base.op_hz = 120;
	spanel->hw_vrefresh = 60;
	spanel->hw_acl_enabled = false;
	spanel->hw_za_enabled = false;
	return exynos_panel_common_init(dsi, &spanel->base);
}

static const struct drm_panel_funcs hk3_drm_funcs = {
	.disable = hk3_disable,
	.unprepare = exynos_panel_unprepare,
	.prepare = exynos_panel_prepare,
	.enable = hk3_enable,
	.get_modes = exynos_panel_get_modes,
};

static const struct exynos_panel_funcs hk3_exynos_funcs = {
	.set_brightness = hk3_set_brightness,
	.set_lp_mode = exynos_panel_set_lp_mode,
	.set_nolp_mode = hk3_set_nolp_mode,
	.set_binned_lp = exynos_panel_set_binned_lp,
	.set_hbm_mode = hk3_set_hbm_mode,
	.set_dimming_on = hk3_set_dimming_on,
	.set_local_hbm_mode = hk3_set_local_hbm_mode,
	.is_mode_seamless = hk3_is_mode_seamless,
	.mode_set = hk3_mode_set,
	.panel_init = hk3_panel_init,
	.get_panel_rev = hk3_get_panel_rev,
	.get_te2_edges = exynos_panel_get_te2_edges,
	.configure_te2_edges = exynos_panel_configure_te2_edges,
	.update_te2 = hk3_update_te2,
	.commit_done = hk3_commit_done,
	.atomic_check = hk3_atomic_check,
	.set_self_refresh = hk3_set_self_refresh,
	.set_op_hz = hk3_set_op_hz,
	.read_id = hk3_read_id,
};

const struct brightness_capability hk3_brightness_capability = {
	.normal = {
		.nits = {
			.min = 2,
			.max = 800,
		},
		.level = {
			.min = 4,
			.max = 2047,
		},
		.percentage = {
			.min = 0,
			.max = 57,
		},
	},
	.hbm = {
		.nits = {
			.min = 800,
			.max = 1400,
		},
		.level = {
			.min = 2048,
			.max = 4095,
		},
		.percentage = {
			.min = 57,
			.max = 100,
		},
	},
};

const struct exynos_panel_desc google_hk3 = {
	.data_lane_cnt = 4,
	.max_brightness = 4095,
	.dft_brightness = 1023,
	.brt_capability = &hk3_brightness_capability,
	.dbv_extra_frame = true,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.max_luminance = 10000000,
	.max_avg_luminance = 1200000,
	.min_luminance = 5,
	.bl_range = hk3_bl_range,
	.bl_num_ranges = ARRAY_SIZE(hk3_bl_range),
	.modes = hk3_modes,
	.num_modes = ARRAY_SIZE(hk3_modes),
	.lp_mode = hk3_lp_modes,
	.lp_mode_count = ARRAY_SIZE(hk3_lp_modes),
	.lp_cmd_set = &hk3_lp_cmd_set,
	.binned_lp = hk3_binned_lp,
	.num_binned_lp = ARRAY_SIZE(hk3_binned_lp),
	.is_panel_idle_supported = true,
	.panel_func = &hk3_drm_funcs,
	.exynos_panel_func = &hk3_exynos_funcs,
	.reset_timing_ms = {1, 1, 5},
	.reg_ctrl_enable = {
		{PANEL_REG_ID_VDDI, 1},
		{PANEL_REG_ID_VCI, 10},
	},
	.reg_ctrl_post_enable = {
		{PANEL_REG_ID_VDDD, 1},
	},
	.reg_ctrl_pre_disable = {
		{PANEL_REG_ID_VDDD, 1},
	},
	.reg_ctrl_disable = {
		{PANEL_REG_ID_VCI, 1},
		{PANEL_REG_ID_VDDI, 1},
	},
};

static const struct of_device_id exynos_panel_of_match[] = {
	{ .compatible = "google,hk3", .data = &google_hk3 },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos_panel_of_match);

static struct mipi_dsi_driver exynos_panel_driver = {
	.probe = hk3_panel_probe,
	.remove = exynos_panel_remove,
	.driver = {
		.name = "panel-google-hk3",
		.of_match_table = exynos_panel_of_match,
	},
};
module_mipi_dsi_driver(exynos_panel_driver);

MODULE_AUTHOR("Chris Lu <luchris@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google HK3 panel driver");
MODULE_LICENSE("GPL");
