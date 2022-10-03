// SPDX-License-Identifier: GPL-2.0-only
/*
 * MIPI-DSI based bigsurf AMOLED LCD panel driver.
 *
 * Copyright (c) 2022 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <video/mipi_display.h>

#include "samsung/panel/panel-samsung-drv.h"

#define BIGSURF_DDIC_ID_LEN 8

/**
 * struct bigsurf_panel - panel specific runtime info
 *
 * This struct maintains bigsurf panel specific runtime info, any fixed details about panel
 * should most likely go into struct exynos_panel_desc.
 */
struct bigsurf_panel {
	/** @base: base panel struct */
	struct exynos_panel base;
};

#define to_spanel(ctx) container_of(ctx, struct bigsurf_panel, base)

static const struct exynos_dsi_cmd bigsurf_lp_cmds[] = {
	/* disable dimming */
	EXYNOS_DSI_CMD_SEQ(0x53, 0x20),
	/* enter AOD */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_ENTER_IDLE_MODE),
	EXYNOS_DSI_CMD_SEQ(0x5A, 0x00),
};
static DEFINE_EXYNOS_CMD_SET(bigsurf_lp);

static const struct exynos_dsi_cmd bigsurf_lp_off_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
};

static const struct exynos_dsi_cmd bigsurf_lp_low_cmds[] = {
	/* 10 nit */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
					0x00, 0x00, 0x00, 0x00, 0x03, 0x33),
};

static const struct exynos_dsi_cmd bigsurf_lp_high_cmds[] = {
	/* 50 nit */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
					0x00, 0x00, 0x00, 0x00, 0x0F, 0xFE),
};

static const struct exynos_binned_lp bigsurf_binned_lp[] = {
	BINNED_LP_MODE("off", 0, bigsurf_lp_off_cmds),
	/* rising = 0, falling = 32 */
	BINNED_LP_MODE_TIMING("low", 648, bigsurf_lp_low_cmds, 0, 32),
	BINNED_LP_MODE_TIMING("high", 3789, bigsurf_lp_high_cmds, 0, 32),
};

static const struct exynos_dsi_cmd bigsurf_off_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY(100, MIPI_DCS_SET_DISPLAY_OFF),
	EXYNOS_DSI_CMD_SEQ_DELAY(120, MIPI_DCS_ENTER_SLEEP_MODE),
};
static DEFINE_EXYNOS_CMD_SET(bigsurf_off);

static const struct exynos_dsi_cmd bigsurf_init_cmds[] = {
	/* CMD2, Page0 */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x1B),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x18),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x1C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x2C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
				0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x3C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x01, 0x01, 0x01, 0x01, 0x03, 0x03, 0x03, 0x03,
				0x03, 0x03, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x4C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x01, 0x01, 0x01, 0x01, 0x0B, 0x0B, 0x0B, 0x0B,
				0x0B, 0x0B, 0x0B, 0x0B, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x5C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
				0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x6C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
				0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x7C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
				0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x8C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x9C),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x11, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0xA4),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0xA8),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0xB0),
	EXYNOS_DSI_CMD_SEQ(0xBA, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),

	EXYNOS_DSI_CMD_SEQ(0x6F, 0x08),
	EXYNOS_DSI_CMD_SEQ(0xBB, 0x01, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x18),
	EXYNOS_DSI_CMD_SEQ(0xBB, 0x01, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x1C),
	EXYNOS_DSI_CMD_SEQ(0xBB, 0x01, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x01),
	EXYNOS_DSI_CMD_SEQ(0xBE, 0x47),

	/* Disable the Black insertion in AoD */
	EXYNOS_DSI_CMD_SEQ(0xC0, 0x44),

	/* CMD2, Page1 */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x05),
	EXYNOS_DSI_CMD_SEQ(0xC5, 0x15, 0x15, 0x15, 0xDD),

	/* CMD2, Page7 */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x07),
	/* Disable round corner and punch hole */
	EXYNOS_DSI_CMD_SEQ(0xC9, 0x00),
	EXYNOS_DSI_CMD_SEQ(0xCA, 0x00),
	EXYNOS_DSI_CMD_SEQ(0xCB, 0x00),
	EXYNOS_DSI_CMD_SEQ(0xCC, 0x00),

	/* CMD3, Page0 */
	EXYNOS_DSI_CMD_SEQ(0xFF, 0xAA, 0x55, 0xA5, 0x80),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x19),
	EXYNOS_DSI_CMD_SEQ(0xF2, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x1A),
	EXYNOS_DSI_CMD_SEQ(0xF4, 0x55),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x2D),
	EXYNOS_DSI_CMD_SEQ(0xFC, 0x44),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x11),
	EXYNOS_DSI_CMD_SEQ(0xF8, 0x01, 0x7B),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x2D),
	EXYNOS_DSI_CMD_SEQ(0xF8, 0x01, 0x1D),

	/* CMD3, Page1 */
	EXYNOS_DSI_CMD_SEQ(0xFF, 0xAA, 0x55, 0xA5, 0x81),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x05),
	EXYNOS_DSI_CMD_SEQ(0xFE, 0x3C),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x02),
	EXYNOS_DSI_CMD_SEQ(0xF9, 0x04),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x1E),
	EXYNOS_DSI_CMD_SEQ(0xFB, 0x0F),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x0D),
	EXYNOS_DSI_CMD_SEQ(0xFB, 0x80),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x0F),
	EXYNOS_DSI_CMD_SEQ(0xF5, 0x20),
	/* CMD3, Page2 */
	EXYNOS_DSI_CMD_SEQ(0xFF, 0xAA, 0x55, 0xA5, 0x82),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x09),
	EXYNOS_DSI_CMD_SEQ(0xF2, 0x55),
	/* CMD3, Page3 */
	EXYNOS_DSI_CMD_SEQ(0xFF, 0xAA, 0x55, 0xA5, 0x83),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x12),
	EXYNOS_DSI_CMD_SEQ(0xFE, 0x41),

	/* CMD, Disable */
	EXYNOS_DSI_CMD_SEQ(0xFF, 0xAA, 0x55, 0xA5, 0x00),

	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_TEAR_SCANLINE, 0x00, 0x00),
	/* b/241726710, long write 0x35 as a WA */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_TEAR_ON, 0x00, 0x20),
	EXYNOS_DSI_CMD_SEQ(0x5A, 0x04),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x5F),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_GAMMA_CURVE, 0x00),
	EXYNOS_DSI_CMD_SEQ(0x81, 0x01, 0x19),
	EXYNOS_DSI_CMD_SEQ(0x88, 0x81, 0x02, 0x1C, 0x06, 0xE2, 0x00, 0x00, 0x00, 0x00),
	// 8bpc PPS
	EXYNOS_DSI_CMD_SEQ(0x03, 0x01),
	EXYNOS_DSI_CMD_SEQ(0x90, 0x03, 0x03),
	EXYNOS_DSI_CMD_SEQ(0x91, 0x89, 0x28, 0x00, 0x1E, 0xD2, 0x00, 0x02, 0x25, 0x02,
				0xC5, 0x00, 0x07, 0x03, 0x97, 0x03, 0x64, 0x10, 0xF0),

	EXYNOS_DSI_CMD_SEQ_DELAY(120, MIPI_DCS_EXIT_SLEEP_MODE)
};
static DEFINE_EXYNOS_CMD_SET(bigsurf_init);

static void bigsurf_update_te2(struct exynos_panel *ctx)
{
	struct exynos_panel_te2_timing timing;
	u8 width = 0x20; /* default width */
	u32 rising = 0, falling;
	int ret;

	if (!ctx)
		return;

	ret = exynos_panel_get_current_mode_te2(ctx, &timing);
	if (!ret) {
		falling = timing.falling_edge;
		if (falling >= timing.rising_edge) {
			rising = timing.rising_edge;
			width = falling - rising;
		} else {
			dev_warn(ctx->dev, "invalid timing, use default setting\n");
		}
	} else if (ret == -EAGAIN) {
		dev_dbg(ctx->dev, "Panel is not ready, use default setting\n");
	} else {
		return;
	}

	dev_dbg(ctx->dev, "TE2 updated: rising= 0x%x, width= 0x%x", rising, width);

	EXYNOS_DCS_BUF_ADD(ctx, MIPI_DCS_SET_TEAR_SCANLINE, 0x00, rising);
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, MIPI_DCS_SET_TEAR_ON, 0x00, width);
}

static void bigsurf_update_irc(struct exynos_panel *ctx,
				const enum exynos_hbm_mode hbm_mode,
				const int vrefresh)
{
	if (!IS_HBM_ON(hbm_mode)) {
		dev_info(ctx->dev, "hbm is off, skip update irc\n");
		return;
	}

	if (IS_HBM_ON_IRC_OFF(hbm_mode)) {
		EXYNOS_DCS_BUF_ADD(ctx, 0x5F, 0x01);
		if (vrefresh == 120) {
			if (ctx->hbm.local_hbm.enabled) {
				EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
				EXYNOS_DCS_BUF_ADD(ctx, 0x6F, 0x04);
				EXYNOS_DCS_BUF_ADD(ctx, 0xC0, 0x76);
			}
			EXYNOS_DCS_BUF_ADD(ctx, 0x2F, 0x00);
			EXYNOS_DCS_BUF_ADD(ctx, MIPI_DCS_SET_GAMMA_CURVE, 0x02);
		} else {
			EXYNOS_DCS_BUF_ADD(ctx, 0x2F, 0x30);
			EXYNOS_DCS_BUF_ADD(ctx, 0x6D, 0x01, 0x00);
		}
	} else {
		EXYNOS_DCS_BUF_ADD(ctx, 0x5F, 0x00);
		if (vrefresh == 120) {
			if (ctx->hbm.local_hbm.enabled) {
				EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
				EXYNOS_DCS_BUF_ADD(ctx, 0x6F, 0x04);
				EXYNOS_DCS_BUF_ADD(ctx, 0xC0, 0x75);
			}
			EXYNOS_DCS_BUF_ADD(ctx, 0x2F, 0x00);
		} else {
			EXYNOS_DCS_BUF_ADD(ctx, 0x2F, 0x30);
			EXYNOS_DCS_BUF_ADD(ctx, 0x6D, 0x00, 0x00);
		}
	}
	EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x02);
	EXYNOS_DCS_BUF_ADD(ctx, 0xCC, 0x30);
	EXYNOS_DCS_BUF_ADD(ctx, 0xCE, 0x01);
	EXYNOS_DCS_BUF_ADD(ctx, 0xCC, 0x00);
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0xCE, 0x00);
}

static void bigsurf_change_frequency(struct exynos_panel *ctx,
				    const struct exynos_panel_mode *pmode)
{
	int vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (!ctx || (vrefresh != 60 && vrefresh != 120))
		return;

	if (!IS_HBM_ON(ctx->hbm_mode)) {
		EXYNOS_DCS_WRITE_SEQ(ctx, 0x2F, (vrefresh == 120) ? 0x00 : 0x30);
		if (vrefresh == 60)
			EXYNOS_DCS_WRITE_SEQ(ctx, 0x6D, 0x00, 0x00);
	} else {
		bigsurf_update_irc(ctx, ctx->hbm_mode, vrefresh);
	}

	dev_dbg(ctx->dev, "%s: change to %uhz\n", __func__, vrefresh);
}

static void bigsurf_set_dimming_on(struct exynos_panel *ctx,
				 bool dimming_on)
{
	const struct exynos_panel_mode *pmode = ctx->current_mode;

	if (pmode->exynos_mode.is_lp_mode) {
		dev_warn(ctx->dev, "in lp mode, skip to update\n");
		return;
	}

	ctx->dimming_on = dimming_on;
	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
					ctx->dimming_on ? 0x28 : 0x20);
	dev_dbg(ctx->dev, "%s dimming_on=%d\n", __func__, dimming_on);
}

static void bigsurf_set_nolp_mode(struct exynos_panel *ctx,
				  const struct exynos_panel_mode *pmode)
{
	if (!is_panel_active(ctx))
		return;

	/* exit AOD */
	EXYNOS_DCS_BUF_ADD(ctx, MIPI_DCS_EXIT_IDLE_MODE);
	EXYNOS_DCS_BUF_ADD(ctx, 0x5A, 0x04);
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
					ctx->dimming_on ? 0x28 : 0x20);

	bigsurf_change_frequency(ctx, pmode);

	dev_info(ctx->dev, "exit LP mode\n");
}

static int bigsurf_enable(struct drm_panel *panel)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	const struct exynos_panel_mode *pmode = ctx->current_mode;

	if (!pmode) {
		dev_err(ctx->dev, "no current mode set\n");
		return -EINVAL;
	}

	dev_dbg(ctx->dev, "%s\n", __func__);

	exynos_panel_reset(ctx);
	exynos_panel_send_cmd_set(ctx, &bigsurf_init_cmd_set);
	bigsurf_change_frequency(ctx, pmode);

	if (!pmode->exynos_mode.is_lp_mode)
		EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_SET_DISPLAY_ON);
	else
		exynos_panel_set_lp_mode(ctx, pmode);

	return 0;
}

static int bigsurf_set_brightness(struct exynos_panel *ctx, u16 br)
{
	u16 brightness;

	if (ctx->current_mode->exynos_mode.is_lp_mode) {
		const struct exynos_panel_funcs *funcs;

		funcs = ctx->desc->exynos_panel_func;
		if (funcs && funcs->set_binned_lp)
			funcs->set_binned_lp(ctx, br);
		return 0;
	}

	if (!br) {
		// turn off panel and set brightness directly.
		return exynos_dcs_set_brightness(ctx, 0);
	}

	if (ctx->hbm.local_hbm.enabled) {
		u16 level = br * 4;
		u8 val1 = level >> 8;
		u8 val2 = level & 0xff;

		/* LHBM DBV value write */
		EXYNOS_DCS_BUF_ADD(ctx, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
		EXYNOS_DCS_BUF_ADD(ctx, 0x6F, 0x4C);
		EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0xDF, val1, val2, val1, val2, val1, val2);
	}

	brightness = (br & 0xff) << 8 | br >> 8;

	return exynos_dcs_set_brightness(ctx, brightness);
}

static void bigsurf_set_hbm_mode(struct exynos_panel *ctx,
				 enum exynos_hbm_mode hbm_mode)
{
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	int vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (ctx->hbm_mode == hbm_mode)
		return;

	bigsurf_update_irc(ctx, hbm_mode, vrefresh);

	ctx->hbm_mode = hbm_mode;
	dev_info(ctx->dev, "hbm_on=%d hbm_ircoff=%d\n", IS_HBM_ON(ctx->hbm_mode),
		 IS_HBM_ON_IRC_OFF(ctx->hbm_mode));
}

static void bigsurf_set_local_hbm_mode(struct exynos_panel *ctx,
				       bool local_hbm_en)
{
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	int vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (local_hbm_en) {
		if (IS_HBM_ON(ctx->hbm_mode))
			bigsurf_update_irc(ctx, ctx->hbm_mode, vrefresh);
		EXYNOS_DCS_WRITE_SEQ(ctx, 0x87, 0x05);
	} else {
		EXYNOS_DCS_WRITE_SEQ(ctx, 0x87, 0x00);
		EXYNOS_DCS_WRITE_SEQ(ctx, 0x2F, 0x00);
	}
}

static void bigsurf_mode_set(struct exynos_panel *ctx,
			     const struct exynos_panel_mode *pmode)
{
	bigsurf_change_frequency(ctx, pmode);
}

static bool bigsurf_is_mode_seamless(const struct exynos_panel *ctx,
				     const struct exynos_panel_mode *pmode)
{
	const struct drm_display_mode *c = &ctx->current_mode->mode;
	const struct drm_display_mode *n = &pmode->mode;

	/* seamless mode set can happen if active region resolution is same */
	return (c->vdisplay == n->vdisplay) && (c->hdisplay == n->hdisplay) &&
	       (c->flags == n->flags);
}

static void bigsurf_get_panel_rev(struct exynos_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	const u8 build_code = (id & 0xFF00) >> 8;
	const u8 main = (build_code & 0xE0) >> 3;
	const u8 sub = (build_code & 0x0C) >> 2;

	exynos_panel_get_panel_rev(ctx, main | sub);
}

static int bigsurf_read_id(struct exynos_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	char buf[BIGSURF_DDIC_ID_LEN] = {0};
	int ret;

	EXYNOS_DCS_WRITE_SEQ(ctx, 0xFF, 0xAA, 0x55, 0xA5, 0x81);
	ret = mipi_dsi_dcs_read(dsi, 0xF2, buf, BIGSURF_DDIC_ID_LEN);
	if (ret != BIGSURF_DDIC_ID_LEN) {
		dev_warn(ctx->dev, "Unable to read DDIC id (%d)\n", ret);
		goto done;
	}

	exynos_bin2hex(buf, BIGSURF_DDIC_ID_LEN,
		ctx->panel_id, sizeof(ctx->panel_id));
done:
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xFF, 0xAA, 0x55, 0xA5, 0x00);
	return ret;
}

static const struct exynos_display_underrun_param underrun_param = {
	.te_idle_us = 350,
	.te_var = 1,
};

/* Truncate 8-bit signed value to 6-bit signed value */
#define TO_6BIT_SIGNED(v) ((v) & 0x3F)

static const struct drm_dsc_config bigsurf_dsc_cfg = {
	.first_line_bpg_offset = 13,
	.rc_range_params = {
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{0, 0, 0},
		{4, 10, TO_6BIT_SIGNED(-10)},
		{5, 10, TO_6BIT_SIGNED(-10)},
		{5, 11, TO_6BIT_SIGNED(-10)},
		{5, 11, TO_6BIT_SIGNED(-12)},
		{8, 12, TO_6BIT_SIGNED(-12)},
		{12, 13, TO_6BIT_SIGNED(-12)},
	},
};

#define BIGSURF_DSC_CONFIG \
	.dsc = { \
		.enabled = true, \
		.dsc_count = 2, \
		.slice_count = 2, \
		.slice_height = 30, \
		.cfg = &bigsurf_dsc_cfg, \
	}

static const struct exynos_panel_mode bigsurf_modes[] = {
	{
		.mode = {
			.name = "1080x2400x60",
			.clock = 168498,
			.hdisplay = 1080,
			.hsync_start = 1080 + 32, // add hfp
			.hsync_end = 1080 + 32 + 12, // add hsa
			.htotal = 1080 + 32 + 12 + 26, // add hbp
			.vdisplay = 2400,
			.vsync_start = 2400 + 12, // add vfp
			.vsync_end = 2400 + 12 + 4, // add vsa
			.vtotal = 2400 + 12 + 4 + 26, // add vbp
			.flags = 0,
			.width_mm = 64,
			.height_mm = 134,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 8545,
			.bpc = 8,
			BIGSURF_DSC_CONFIG,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = 0,
			.falling_edge = 32,
		},
	},
	{
		.mode = {
			.name = "1080x2400x120",
			.clock = 336996,
			.hdisplay = 1080,
			.hsync_start = 1080 + 32, // add hfp
			.hsync_end = 1080 + 32 + 12, // add hsa
			.htotal = 1080 + 32 + 12 + 26, // add hbp
			.vdisplay = 2400,
			.vsync_start = 2400 + 12, // add vfp
			.vsync_end = 2400 + 12 + 4, // add vsa
			.vtotal = 2400 + 12 + 4 + 26, // add vbp
			.flags = 0,
			.width_mm = 64,
			.height_mm = 134,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 202,
			.bpc = 8,
			BIGSURF_DSC_CONFIG,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = 0,
			.falling_edge = 32,
		},
	},
};

static const struct exynos_panel_mode bigsurf_lp_mode = {
	.mode = {
		.name = "1080x2400x30",
		.clock = 84249,
		.hdisplay = 1080,
		.hsync_start = 1080 + 32, // add hfp
		.hsync_end = 1080 + 32 + 12, // add hsa
		.htotal = 1080 + 32 + 12 + 26, // add hbp
		.vdisplay = 2400,
		.vsync_start = 2400 + 12, // add vfp
		.vsync_end = 2400 + 12 + 4, // add vsa
		.vtotal = 2400 + 12 + 4 + 26, // add vbp
		.flags = 0,
		.type = DRM_MODE_TYPE_DRIVER,
		.width_mm = 64,
		.height_mm = 134,
	},
	.exynos_mode = {
		.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
		.vblank_usec = 120,
		.bpc = 8,
		BIGSURF_DSC_CONFIG,
		.underrun_param = &underrun_param,
		.is_lp_mode = true,
	}
};

static void bigsurf_panel_init(struct exynos_panel *ctx)
{
	struct dentry *csroot = ctx->debugfs_cmdset_entry;

	exynos_panel_debugfs_create_cmdset(ctx, csroot, &bigsurf_init_cmd_set, "init");
}

static int bigsurf_panel_probe(struct mipi_dsi_device *dsi)
{
	struct bigsurf_panel *spanel;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	return exynos_panel_common_init(dsi, &spanel->base);
}

static const struct drm_panel_funcs bigsurf_drm_funcs = {
	.disable = exynos_panel_disable,
	.unprepare = exynos_panel_unprepare,
	.prepare = exynos_panel_prepare,
	.enable = bigsurf_enable,
	.get_modes = exynos_panel_get_modes,
};

static const struct exynos_panel_funcs bigsurf_exynos_funcs = {
	.set_brightness = bigsurf_set_brightness,
	.set_lp_mode = exynos_panel_set_lp_mode,
	.set_nolp_mode = bigsurf_set_nolp_mode,
	.set_binned_lp = exynos_panel_set_binned_lp,
	.set_hbm_mode = bigsurf_set_hbm_mode,
	.set_local_hbm_mode = bigsurf_set_local_hbm_mode,
	.set_dimming_on = bigsurf_set_dimming_on,
	.is_mode_seamless = bigsurf_is_mode_seamless,
	.mode_set = bigsurf_mode_set,
	.panel_init = bigsurf_panel_init,
	.get_panel_rev = bigsurf_get_panel_rev,
	.get_te2_edges = exynos_panel_get_te2_edges,
	.configure_te2_edges = exynos_panel_configure_te2_edges,
	.update_te2 = bigsurf_update_te2,
	.read_id = bigsurf_read_id,
};

const struct brightness_capability bigsurf_brightness_capability = {
	.normal = {
		.nits = {
			.min = 2,
			.max = 800,
		},
		.level = {
			.min = 290,
			.max = 3789,
		},
		.percentage = {
			.min = 0,
			.max = 67,
		},
	},
	.hbm = {
		.nits = {
			.min = 800,
			.max = 1200,
		},
		.level = {
			.min = 3790,
			.max = 4094,
		},
		.percentage = {
			.min = 67,
			.max = 100,
		},
	},
};

const struct exynos_panel_desc google_bigsurf = {
	.data_lane_cnt = 4,
	.max_brightness = 4094,
	.min_brightness = 290,
	.dft_brightness = 1448,
	.brt_capability = &bigsurf_brightness_capability,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.max_luminance = 10000000,
	.max_avg_luminance = 1200000,
	.min_luminance = 5,
	.modes = bigsurf_modes,
	.num_modes = ARRAY_SIZE(bigsurf_modes),
	.off_cmd_set = &bigsurf_off_cmd_set,
	.lp_mode = &bigsurf_lp_mode,
	.lp_cmd_set = &bigsurf_lp_cmd_set,
	.binned_lp = bigsurf_binned_lp,
	.num_binned_lp = ARRAY_SIZE(bigsurf_binned_lp),
	.panel_func = &bigsurf_drm_funcs,
	.exynos_panel_func = &bigsurf_exynos_funcs,
	.reset_timing_ms = {1, 1, 20},
	.reg_ctrl_enable = {
		{PANEL_REG_ID_VDDI, 0},
		{PANEL_REG_ID_VCI, 0},
		{PANEL_REG_ID_VDDD, 10},
	},
	.reg_ctrl_disable = {
		{PANEL_REG_ID_VDDD, 0},
		{PANEL_REG_ID_VCI, 0},
		{PANEL_REG_ID_VDDI, 0},
	},
};

static const struct of_device_id exynos_panel_of_match[] = {
	{ .compatible = "google,bigsurf", .data = &google_bigsurf },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos_panel_of_match);

static struct mipi_dsi_driver exynos_panel_driver = {
	.probe = bigsurf_panel_probe,
	.remove = exynos_panel_remove,
	.driver = {
		.name = "panel-google-bigsurf",
		.of_match_table = exynos_panel_of_match,
	},
};
module_mipi_dsi_driver(exynos_panel_driver);

MODULE_AUTHOR("Ken Huang <kenbshuang@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google bigsurf panel driver");
MODULE_LICENSE("GPL");
