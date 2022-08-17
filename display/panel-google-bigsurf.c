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
#define BIGSURF_MIPI_STREAM_2C MIPI_DCS_WRITE_MEMORY_START


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
	/* enter AOD */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_ENTER_IDLE_MODE),
};
static DEFINE_EXYNOS_CMD_SET(bigsurf_lp);

static const struct exynos_dsi_cmd bigsurf_lp_off_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_OFF),
};

static const struct exynos_dsi_cmd bigsurf_lp_low_cmds[] = {
	/* 10 nit */
	EXYNOS_DSI_CMD_SEQ_DELAY(9, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
					0x00, 0x00, 0x00, 0x00, 0x03, 0x33),
	/* 2Ch needs to be sent twice in next 2 vsync */
	EXYNOS_DSI_CMD_SEQ_DELAY(9, BIGSURF_MIPI_STREAM_2C),
	EXYNOS_DSI_CMD_SEQ(BIGSURF_MIPI_STREAM_2C),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_ON),
};

static const struct exynos_dsi_cmd bigsurf_lp_high_cmds[] = {
	/* 50 nit */
	EXYNOS_DSI_CMD_SEQ_DELAY(9, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
					0x00, 0x00, 0x00, 0x00, 0x0F, 0xFE),
	/* 2Ch needs to be sent twice in next 2 vsync */
	EXYNOS_DSI_CMD_SEQ_DELAY(9, BIGSURF_MIPI_STREAM_2C),
	EXYNOS_DSI_CMD_SEQ(BIGSURF_MIPI_STREAM_2C),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_ON),
};

static const struct exynos_binned_lp bigsurf_binned_lp[] = {
	BINNED_LP_MODE("off", 0, bigsurf_lp_off_cmds),
	/* rising = 0, falling = 48 */
	BINNED_LP_MODE_TIMING("low", 648, bigsurf_lp_low_cmds, 0, 48),
	BINNED_LP_MODE_TIMING("high", 3789, bigsurf_lp_high_cmds, 0, 48),
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

	/* CMD2, Page1 */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01),
	EXYNOS_DSI_CMD_SEQ(0x6F, 0x05),
	EXYNOS_DSI_CMD_SEQ(0xC5, 0x15, 0x15, 0x15, 0xDD),

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

static void bigsurf_change_frequency(struct exynos_panel *ctx,
				    const struct exynos_panel_mode *pmode)
{
	int vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (!ctx || (vrefresh != 60 && vrefresh != 120))
		return;

	EXYNOS_DCS_WRITE_SEQ(ctx, 0x2F, (vrefresh == 120) ? 0x00 : 0x30);
	if (vrefresh == 60)
		EXYNOS_DCS_WRITE_SEQ(ctx, 0x6D, 0x00);

	dev_dbg(ctx->dev, "%s: change to %uhz\n", __func__, vrefresh);
}

static void bigsurf_set_nolp_mode(struct exynos_panel *ctx,
				  const struct exynos_panel_mode *pmode)
{
	if (!is_panel_active(ctx))
		return;

	/* exit AOD */
	EXYNOS_DCS_WRITE_SEQ_DELAY(ctx, 34, MIPI_DCS_EXIT_IDLE_MODE);

	bigsurf_change_frequency(ctx, pmode);

	/* 2Ch needs to be sent twice in next 2 vsync */
	EXYNOS_DCS_WRITE_SEQ_DELAY(ctx, 34, BIGSURF_MIPI_STREAM_2C);
	EXYNOS_DCS_WRITE_SEQ(ctx, BIGSURF_MIPI_STREAM_2C);
	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_SET_DISPLAY_ON);

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

static void bigsurf_set_hbm_mode(struct exynos_panel *ctx,
				 enum exynos_hbm_mode mode)
{
	if (ctx->hbm_mode == mode)
		return;

	/* TODO: implement IRC */

	ctx->hbm_mode = mode;
	dev_info(ctx->dev, "hbm_on=%d hbm_ircoff=%d\n", IS_HBM_ON(ctx->hbm_mode),
		 IS_HBM_ON_IRC_OFF(ctx->hbm_mode));
}

static void bigsurf_mode_set(struct exynos_panel *ctx,
			     const struct exynos_panel_mode *pmode)
{
	if (!is_panel_active(ctx))
		return;

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

static void bigsurf_set_dimming_on(struct exynos_panel *ctx,
				 bool dimming_on)
{
	ctx->dimming_on = dimming_on;
	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
					ctx->dimming_on ? 0x28 : 0x20);
	dev_dbg(ctx->dev, "%s dimming_on=%d\n", __func__, dimming_on);
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
		/* 1440x3120 @ 60Hz */
		.mode = {
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
			.bpc = 8,
			BIGSURF_DSC_CONFIG,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = 0,
			.falling_edge = 48,
		},
	},
	{
		/* 1440x3120 @ 120Hz */
		.mode = {
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
			.bpc = 8,
			BIGSURF_DSC_CONFIG,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = 0,
			.falling_edge = 48,
		},
	},
};

static const struct exynos_panel_mode bigsurf_lp_mode = {
	.mode = {
		/* 1080x2400 @ 30Hz */
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
	.set_brightness = exynos_panel_set_brightness,
	.set_lp_mode = exynos_panel_set_lp_mode,
	.set_nolp_mode = bigsurf_set_nolp_mode,
	.set_binned_lp = exynos_panel_set_binned_lp,
	.set_hbm_mode = bigsurf_set_hbm_mode,
	.set_dimming_on = bigsurf_set_dimming_on,
	.is_mode_seamless = bigsurf_is_mode_seamless,
	.mode_set = bigsurf_mode_set,
	.panel_init = bigsurf_panel_init,
	.get_panel_rev = bigsurf_get_panel_rev,
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
			.max = 60,
		},
	},
	.hbm = {
		.nits = {
			.min = 800,
			.max = 1000,
		},
		.level = {
			.min = 3790,
			.max = 4094,
		},
		.percentage = {
			.min = 60,
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
	.is_panel_idle_supported = true,
	.panel_func = &bigsurf_drm_funcs,
	.exynos_panel_func = &bigsurf_exynos_funcs,
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
