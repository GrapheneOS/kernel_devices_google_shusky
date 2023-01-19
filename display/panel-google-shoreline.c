// SPDX-License-Identifier: GPL-2.0-only
/*
 * MIPI-DSI based Google Shoreline panel driver.
 *
 * Copyright (c) 2022 Google LLC
 */

#include <drm/drm_vblank.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <video/mipi_display.h>

#include "samsung/panel/panel-samsung-drv.h"

static const struct drm_dsc_config pps_config = {
	.line_buf_depth = 9,
	.bits_per_component = 8,
	.convert_rgb = true,
	.slice_width = 540,
	.slice_height = 48,
	.simple_422 = false,
	.pic_width = 1080,
	.pic_height = 2400,
	.rc_tgt_offset_high = 3,
	.rc_tgt_offset_low = 3,
	.bits_per_pixel = 128,
	.rc_edge_factor = 6,
	.rc_quant_incr_limit1 = 11,
	.rc_quant_incr_limit0 = 11,
	.initial_xmit_delay = 512,
	.initial_dec_delay = 526,
	.block_pred_enable = true,
	.first_line_bpg_offset = 12,
	.initial_offset = 6144,
	.rc_buf_thresh = {
		14, 28, 42, 56,
		70, 84, 98, 105,
		112, 119, 121, 123,
		125, 126
	},
	.rc_range_params = {
		{.range_min_qp = 0, .range_max_qp = 4, .range_bpg_offset = 2},
		{.range_min_qp = 0, .range_max_qp = 4, .range_bpg_offset = 0},
		{.range_min_qp = 1, .range_max_qp = 5, .range_bpg_offset = 0},
		{.range_min_qp = 1, .range_max_qp = 6, .range_bpg_offset = 62},
		{.range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 60},
		{.range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 58},
		{.range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 56},
		{.range_min_qp = 3, .range_max_qp = 8, .range_bpg_offset = 56},
		{.range_min_qp = 3, .range_max_qp = 9, .range_bpg_offset = 56},
		{.range_min_qp = 3, .range_max_qp = 10, .range_bpg_offset = 54},
		{.range_min_qp = 5, .range_max_qp = 11, .range_bpg_offset = 54},
		{.range_min_qp = 5, .range_max_qp = 12, .range_bpg_offset = 52},
		{.range_min_qp = 5, .range_max_qp = 13, .range_bpg_offset = 52},
		{.range_min_qp = 7, .range_max_qp = 13, .range_bpg_offset = 52},
		{.range_min_qp = 13, .range_max_qp = 15, .range_bpg_offset = 52}
	},
	.rc_model_size = 8192,
	.flatness_min_qp = 3,
	.flatness_max_qp = 12,
	.initial_scale_value = 32,
	.scale_decrement_interval = 7,
	.scale_increment_interval = 1190,
	.nfl_bpg_offset = 523,
	.slice_bpg_offset = 543,
	.final_offset = 4336,
	.vbr_enable = false,
	.slice_chunk_size = 540,
	.dsc_version_minor = 1,
	.dsc_version_major = 1,
	.native_422 = false,
	.native_420 = false,
	.second_line_bpg_offset = 0,
	.nsl_bpg_offset = 0,
	.second_line_offset_adj = 0,
};

#define SHORELINE_WRCTRLD_DIMMING_BIT    0x08
#define SHORELINE_WRCTRLD_BCTRL_BIT      0x20
#define SHORELINE_WRCTRLD_HBM_BIT        0xC0
#define SHORELINE_WRCTRLD_LOCAL_HBM_BIT  0x10

#define SHORELINE_TE2_RISING_EDGE_60HZ  0x12D0
#define SHORELINE_TE2_RISING_EDGE_120HZ 0x960
#define SHORELINE_TE2_FALLING_EDGE      0x30

#define WIDTH_MM 64
#define HEIGHT_MM 143

static const u8 test_key_on_f0[] = { 0xF0, 0x5A, 0x5A };
static const u8 test_key_off_f0[] = { 0xF0, 0xA5, 0xA5 };
static const u8 freq_update[] = { 0xF7, 0x0F };

static const struct exynos_dsi_cmd shoreline_off_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_OFF),
	EXYNOS_DSI_CMD_SEQ_DELAY(120, MIPI_DCS_ENTER_SLEEP_MODE),
};
static DEFINE_EXYNOS_CMD_SET(shoreline_off);

static const struct exynos_dsi_cmd shoreline_lp_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_OFF),
	EXYNOS_DSI_CMD0(test_key_on_f0),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x10, 0xB9),
	EXYNOS_DSI_CMD_SEQ(0xB9, 0x00, 0x44, 0x00, 0x0C),
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x26, 0xB9),
	EXYNOS_DSI_CMD_SEQ(0xB9, 0x09, 0x60, 0x00, 0x40),
	EXYNOS_DSI_CMD0(test_key_off_f0),
};
static DEFINE_EXYNOS_CMD_SET(shoreline_lp);

static const struct exynos_dsi_cmd shoreline_lp_off_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_OFF)
};

static const struct exynos_dsi_cmd shoreline_lp_low_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY(34, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x25), /* AOD 10 nit */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_ON)
};

static const struct exynos_dsi_cmd shoreline_lp_high_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY(34, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24), /* AOD 50 nit */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_ON)
};

static const struct exynos_binned_lp shoreline_binned_lp[] = {
	BINNED_LP_MODE("off", 0, shoreline_lp_off_cmds),
	BINNED_LP_MODE_TIMING("low", 80, shoreline_lp_low_cmds,
			      SHORELINE_TE2_RISING_EDGE_60HZ, SHORELINE_TE2_FALLING_EDGE),
	BINNED_LP_MODE_TIMING("high", 2047, shoreline_lp_high_cmds,
			      SHORELINE_TE2_RISING_EDGE_60HZ, SHORELINE_TE2_FALLING_EDGE),
};

static const struct exynos_dsi_cmd shoreline_init_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY(120, MIPI_DCS_EXIT_SLEEP_MODE),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_TEAR_ON),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x5F),

	/* TE Settings */
	EXYNOS_DSI_CMD0(test_key_on_f0),
	EXYNOS_DSI_CMD_SEQ(0xB9, 0x31, 0x31), /* TE and TE2 Select for HS mode */

	/* LHBM Location */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x09, 0x6D), /* global para */
	EXYNOS_DSI_CMD_SEQ(0x6D, 0xC6, 0xE3, 0x65), /* Size and Location */

	/* FFC Settings (OSC: 180 MHz, MIPI: 756 Mbps) */
	EXYNOS_DSI_CMD_SEQ(0xFC, 0x5A, 0x5A), /* Test Key Enable */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x3E, 0xC5), /* Global Para 120HS */
	EXYNOS_DSI_CMD_SEQ(0xC5, 0x98, 0x62), /* OSC frequency Setting */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x46, 0xC5), /* Global Para 60HS */
	EXYNOS_DSI_CMD_SEQ(0xC5, 0x98, 0x62), /* OSC frequency Setting */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x36, 0xC5), /* Global Para */
	EXYNOS_DSI_CMD_SEQ(0xC5, 0x11, 0x10, 0x50, 0x05), /* FFC ON */
	EXYNOS_DSI_CMD_SEQ(0xFC, 0xA5, 0xA5), /* Test Key Disable */
	EXYNOS_DSI_CMD0(test_key_off_f0),
};
static DEFINE_EXYNOS_CMD_SET(shoreline_init);

#define LHBM_GAMMA_CMD_SIZE 6
/**
 * struct shoreline_panel - panel specific runtime info
 *
 * This struct maintains shoreline panel specific runtime info, any fixed details about panel
 * should most likely go into struct exynos_panel_desc
 */
struct shoreline_panel {
	/** @base: base panel struct */
	struct exynos_panel base;

	/** @local_hbm_gamma: lhbm gamma data */
	struct local_hbm_gamma {
		u8 hs_cmd[LHBM_GAMMA_CMD_SIZE];
		u8 ns_cmd[LHBM_GAMMA_CMD_SIZE];
	} local_hbm_gamma;
};

#define to_spanel(ctx) container_of(ctx, struct shoreline_panel, base)

static void shoreline_lhbm_gamma_read(struct exynos_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct shoreline_panel *spanel = to_spanel(ctx);
	int ret;
	u8 *hs_cmd = spanel->local_hbm_gamma.hs_cmd;
	u8 *ns_cmd = spanel->local_hbm_gamma.ns_cmd;

	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_on_f0);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x00, 0x22, 0xD8); /* global para */
	ret = mipi_dsi_dcs_read(dsi, 0xD8, hs_cmd + 1, LHBM_GAMMA_CMD_SIZE - 1);
	if (ret == (LHBM_GAMMA_CMD_SIZE - 1)) {
		/* fill in gamma write command 0x66 in offset 0 */
		hs_cmd[0] = 0x66;
		dev_info(ctx->dev, "hs_gamma: %*phN\n", LHBM_GAMMA_CMD_SIZE - 1, hs_cmd + 1);
	} else {
		dev_err(ctx->dev, "fail to read LHBM gamma for HS\n");
	}
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x00, 0x1D, 0xD8); /* global para */
	ret = mipi_dsi_dcs_read(dsi, 0xD8, ns_cmd + 1, LHBM_GAMMA_CMD_SIZE - 1);
	if (ret == (LHBM_GAMMA_CMD_SIZE - 1)) {
		/* fill in gamma write command 0x66 in offset 0 */
		ns_cmd[0] = 0x66;
		dev_info(ctx->dev, "ns_gamma: %*phN\n", LHBM_GAMMA_CMD_SIZE - 1, ns_cmd + 1);
	} else {
		dev_err(ctx->dev, "fail to read LHBM gamma for NS\n");
	}

	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_off_f0);
}

static void shoreline_lhbm_gamma_write(struct exynos_panel *ctx)
{
	struct shoreline_panel *spanel = to_spanel(ctx);
	const u8 *hs_cmd = spanel->local_hbm_gamma.hs_cmd;
	const u8 *ns_cmd = spanel->local_hbm_gamma.ns_cmd;

	if (!hs_cmd[0] && !ns_cmd[0]) {
		dev_err(ctx->dev, "%s: no lhbm gamma!\n", __func__);
		return;
	}

	dev_dbg(ctx->dev, "%s\n", __func__);
	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_on_f0);
	if (hs_cmd[0]) {
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x03, 0xD7, 0x66); /* global para */
		exynos_dcs_write(ctx, hs_cmd, LHBM_GAMMA_CMD_SIZE); /* write gamma */
	}
	if (ns_cmd[0]) {
		EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x03, 0xE6, 0x66); /* global para */
		exynos_dcs_write(ctx, ns_cmd, LHBM_GAMMA_CMD_SIZE); /* write gamma */
	}
	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_off_f0);
}

/**
 * TODO: Revise the calculation of TE2 timing
 *
 * Current definition of the B9h command parameter:
 * TE2 rising: start from next vsync falling and shift left
 *             min 0x1, max 0x96F for 120Hz
 *             min 0x1, max 0x12DF for 60Hz
 * TE2 falling: start from current vsync falling and shift right
 *              min 0x2, max 0x970 for 120Hz
 *              min 0x2, max 0x12E0 for 60Hz
 */
static void shoreline_update_te2(struct exynos_panel *ctx)
{
	struct exynos_panel_te2_timing timing = {
		.rising_edge = SHORELINE_TE2_RISING_EDGE_60HZ,
		.falling_edge = SHORELINE_TE2_FALLING_EDGE,
	};
	u32 rising, falling;
	int ret;

	if (!ctx)
		return;

	/* Not needed to update TE2 in LP mode */
	if (ctx->current_mode->exynos_mode.is_lp_mode)
		return;

	ret = exynos_panel_get_current_mode_te2(ctx, &timing);
	if (ret) {
		dev_dbg(ctx->dev, "failed to get TE2 timng\n");
		return;
	}
	rising = timing.rising_edge;
	falling = timing.falling_edge;

	dev_dbg(ctx->dev, "TE2 updated: rising=0x%X falling=0x%X\n", rising, falling);

	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_on_f0);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x00, 0x26, 0xB9); /* global para */
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB9, (rising >> 8) & 0xF, rising & 0xFF,
			     (falling >> 8) & 0xF, falling & 0xFF); /* TE2 Width */
	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_off_f0);
}

static void shoreline_change_frequency(struct exynos_panel *ctx,
				       const unsigned int vrefresh)
{
	static const u8 te_setting[2][5] = {
		{0xB9, 0x09, 0x74, 0x00, 0x0C}, /* HS 60Hz */
		{0xB9, 0x00, 0x44, 0x00, 0x0C}, /* HS 120Hz */
	};

	if (!ctx || (vrefresh != 60 && vrefresh != 120))
		return;

	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_on_f0);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0x60, (vrefresh == 120) ? 0x00 : 0x08, 0x00);
	EXYNOS_DCS_WRITE_TABLE(ctx, freq_update);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x00, 0x10, 0xB9); /* global para */
	EXYNOS_DCS_WRITE_TABLE(ctx, te_setting[(vrefresh == 60) ? 0 : 1]); /* TE Width */
	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_off_f0);

	dev_dbg(ctx->dev, "frequency changed to %uhz\n", vrefresh);
}

static void shoreline_update_wrctrld(struct exynos_panel *ctx)
{
	u8 val = SHORELINE_WRCTRLD_BCTRL_BIT;

	if (IS_HBM_ON(ctx->hbm_mode))
		val |= SHORELINE_WRCTRLD_HBM_BIT;

	if (ctx->hbm.local_hbm.enabled)
		val |= SHORELINE_WRCTRLD_LOCAL_HBM_BIT;

	if (ctx->dimming_on)
		val |= SHORELINE_WRCTRLD_DIMMING_BIT;

	dev_dbg(ctx->dev,
		"%s(wrctrld:%#x, hbm: %s, dimming: %s, local_hbm: %s)\n",
		__func__, val, IS_HBM_ON(ctx->hbm_mode) ? "on" : "off",
		ctx->dimming_on ? "on" : "off",
		ctx->hbm.local_hbm.enabled ? "on" : "off");

	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);

	/* TODO: need to perform gamma updates */
}

static void shoreline_set_nolp_mode(struct exynos_panel *ctx,
				    const struct exynos_panel_mode *pmode)
{
	unsigned int vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 delay_us = mult_frac(1000, 1020, vrefresh);

	if (!ctx->enabled)
		return;

	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_on_f0);
	/* backlight control and dimming */
	shoreline_update_wrctrld(ctx);
	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_off_f0);
	shoreline_change_frequency(ctx, vrefresh);
	usleep_range(delay_us, delay_us + 10);
	EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_SET_DISPLAY_ON);

	dev_info(ctx->dev, "exit LP mode\n");
}

static int shoreline_enable(struct drm_panel *panel)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	const struct drm_display_mode *mode;
	struct drm_dsc_picture_parameter_set pps_payload;

	if (!pmode) {
		dev_err(ctx->dev, "no current mode set\n");
		return -EINVAL;
	}
	mode = &pmode->mode;

	dev_dbg(ctx->dev, "%s\n", __func__);

	exynos_panel_reset(ctx);

	exynos_panel_send_cmd_set(ctx, &shoreline_init_cmd_set);

	shoreline_change_frequency(ctx, drm_mode_vrefresh(mode));

	shoreline_lhbm_gamma_write(ctx);

	/* DSC related configuration */
	drm_dsc_pps_payload_pack(&pps_payload, &pps_config);
	exynos_dcs_compression_mode(ctx, 0x1); /* DSC_DEC_ON */
	EXYNOS_PPS_WRITE_BUF(ctx, &pps_payload);

	shoreline_update_wrctrld(ctx); /* dimming and HBM */

	ctx->enabled = true;

	if (pmode->exynos_mode.is_lp_mode)
		exynos_panel_set_lp_mode(ctx, pmode);
	else
		EXYNOS_DCS_WRITE_SEQ(ctx, MIPI_DCS_SET_DISPLAY_ON); /* display on */

	return 0;
}

static void shoreline_set_hbm_mode(struct exynos_panel *exynos_panel,
				enum exynos_hbm_mode mode)
{
	const bool hbm_update =
		(IS_HBM_ON(exynos_panel->hbm_mode) != IS_HBM_ON(mode));
	const bool irc_update =
		(IS_HBM_ON_IRC_OFF(exynos_panel->hbm_mode) != IS_HBM_ON_IRC_OFF(mode));
	static const u8 cyc[2][6] = {
		{0xBD, 0x01, 0x01, 0x03, 0x03, 0x03}, /* Normal EM CYC */
		{0xBD, 0x01, 0x00, 0x01, 0x01, 0x01}, /* HBM EM CYC */
	};

	if (!hbm_update && !irc_update)
		return;

	exynos_panel->hbm_mode = mode;

	EXYNOS_DCS_WRITE_TABLE(exynos_panel, test_key_on_f0);

	if (hbm_update) {
		/* CYC Set */
		EXYNOS_DCS_WRITE_TABLE(exynos_panel, cyc[IS_HBM_ON(mode)]);
		/* Update Key */
		EXYNOS_DCS_WRITE_TABLE(exynos_panel, freq_update);
	}

	if (irc_update && IS_HBM_ON(mode)) {
		/* Global para */
		EXYNOS_DCS_WRITE_SEQ(exynos_panel, 0xB0, 0x00, 0x01, 0x6A);
		/* IRC Setting */
		EXYNOS_DCS_WRITE_SEQ(exynos_panel, 0x6A, IS_HBM_ON_IRC_OFF(mode) ? 0x01 : 0x21);
	}

	EXYNOS_DCS_WRITE_TABLE(exynos_panel, test_key_off_f0);
	shoreline_update_wrctrld(exynos_panel);

	dev_info(exynos_panel->dev, "hbm_on=%d hbm_ircoff=%d\n", IS_HBM_ON(exynos_panel->hbm_mode),
		 IS_HBM_ON_IRC_OFF(exynos_panel->hbm_mode));
}

static void shoreline_set_dimming_on(struct exynos_panel *exynos_panel,
				 bool dimming_on)
{
	const struct exynos_panel_mode *pmode = exynos_panel->current_mode;

	exynos_panel->dimming_on = dimming_on;
	if (pmode->exynos_mode.is_lp_mode) {
		dev_info(exynos_panel->dev, "in lp mode, skip to update\n");
		return;
	}

	shoreline_update_wrctrld(exynos_panel);
}

static void shoreline_set_local_hbm_mode(struct exynos_panel *exynos_panel,
				 bool local_hbm_en)
{
	shoreline_update_wrctrld(exynos_panel);
}

static void shoreline_mode_set(struct exynos_panel *ctx,
			       const struct exynos_panel_mode *pmode)
{
	shoreline_change_frequency(ctx, drm_mode_vrefresh(&pmode->mode));
}

static bool shoreline_is_mode_seamless(const struct exynos_panel *ctx,
				       const struct exynos_panel_mode *pmode)
{
	/* seamless mode switch is possible if only changing refresh rate */
	return drm_mode_equal_no_clocks(&ctx->current_mode->mode, &pmode->mode);
}

static void shoreline_panel_init(struct exynos_panel *ctx)
{
	struct dentry *csroot = ctx->debugfs_cmdset_entry;

	exynos_panel_debugfs_create_cmdset(ctx, csroot,
					   &shoreline_init_cmd_set, "init");
	shoreline_lhbm_gamma_read(ctx);
	shoreline_lhbm_gamma_write(ctx);

	/* LHBM Location */
	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_on_f0);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0xB0, 0x00, 0x09, 0x6D);
	EXYNOS_DCS_WRITE_SEQ(ctx, 0x6D, 0xC6, 0xE3, 0x65);
	EXYNOS_DCS_WRITE_TABLE(ctx, test_key_off_f0);
}

static int shoreline_read_id(struct exynos_panel *ctx)
{
	return exynos_panel_read_ddic_id(ctx);
}

static void shoreline_get_panel_rev(struct exynos_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	const u8 build_code = (id & 0xFF00) >> 8;
	const u8 main = (build_code & 0xE0) >> 3;
	const u8 sub = (build_code & 0x0C) >> 2;

	exynos_panel_get_panel_rev(ctx, main | sub);
}

static int shoreline_panel_probe(struct mipi_dsi_device *dsi)
{
	struct shoreline_panel *spanel;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	spanel->base.op_hz = 120;

	return exynos_panel_common_init(dsi, &spanel->base);
}

static const struct exynos_display_underrun_param underrun_param = {
	.te_idle_us = 280,
	.te_var = 1,
};

static const u32 shoreline_bl_range[] = {
	95, 205, 315, 400, 2047
};

#define GOOGLE_SHORELINE_DSC {\
	.enabled = true,\
	.dsc_count = 2,\
	.slice_count = 2,\
	.slice_height = 48,\
	.cfg = &pps_config,\
	}
static const struct exynos_panel_mode shoreline_modes[] = {
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
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 8356,
			.bpc = 8,
			.dsc = GOOGLE_SHORELINE_DSC,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = SHORELINE_TE2_RISING_EDGE_60HZ,
			.falling_edge = SHORELINE_TE2_FALLING_EDGE,
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
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 273,
			.bpc = 8,
			.dsc = GOOGLE_SHORELINE_DSC,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = SHORELINE_TE2_RISING_EDGE_120HZ,
			.falling_edge = SHORELINE_TE2_FALLING_EDGE,
		},
	},
};

static const struct exynos_panel_mode shoreline_lp_mode = {
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
		.width_mm = WIDTH_MM,
		.height_mm = HEIGHT_MM,
	},
	.exynos_mode = {
		.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
		.vblank_usec = 120,
		.bpc = 8,
		.dsc = GOOGLE_SHORELINE_DSC,
		.underrun_param = &underrun_param,
		.is_lp_mode = true,
	}
};

static const struct drm_panel_funcs shoreline_drm_funcs = {
	.disable = exynos_panel_disable,
	.unprepare = exynos_panel_unprepare,
	.prepare = exynos_panel_prepare,
	.enable = shoreline_enable,
	.get_modes = exynos_panel_get_modes,
};

static const struct exynos_panel_funcs shoreline_exynos_funcs = {
	.set_brightness = exynos_panel_set_brightness,
	.set_lp_mode = exynos_panel_set_lp_mode,
	.set_nolp_mode = shoreline_set_nolp_mode,
	.set_binned_lp = exynos_panel_set_binned_lp,
	.set_hbm_mode = shoreline_set_hbm_mode,
	.set_dimming_on = shoreline_set_dimming_on,
	.set_local_hbm_mode = shoreline_set_local_hbm_mode,
	.is_mode_seamless = shoreline_is_mode_seamless,
	.mode_set = shoreline_mode_set,
	.panel_init = shoreline_panel_init,
	.get_panel_rev = shoreline_get_panel_rev,
	.get_te2_edges = exynos_panel_get_te2_edges,
	.configure_te2_edges = exynos_panel_configure_te2_edges,
	.update_te2 = shoreline_update_te2,
	.read_id = shoreline_read_id,
};

static const struct brightness_capability shoreline_brightness_capability = {
	.normal = {
		.nits = {
			.min = 2,
			.max = 800,
		},
		.level = {
			.min = 209,
			.max = 3175,
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
			.min = 3176,
			.max = 4095,
		},
		.percentage = {
			.min = 57,
			.max = 100,
		},
	},
};

static const struct exynos_panel_desc google_shoreline = {
	.data_lane_cnt = 4,
	.max_brightness = 4095,
	.min_brightness = 209,
	.dft_brightness = 1023,
	.brt_capability = &shoreline_brightness_capability,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.max_luminance = 10000000,
	.max_avg_luminance = 1200000,
	.min_luminance = 5,
	.bl_range = shoreline_bl_range,
	.bl_num_ranges = ARRAY_SIZE(shoreline_bl_range),
	.modes = shoreline_modes,
	.num_modes = ARRAY_SIZE(shoreline_modes),
	.off_cmd_set = &shoreline_off_cmd_set,
	.lp_mode = &shoreline_lp_mode,
	.lp_cmd_set = &shoreline_lp_cmd_set,
	.binned_lp = shoreline_binned_lp,
	.num_binned_lp = ARRAY_SIZE(shoreline_binned_lp),
	.panel_func = &shoreline_drm_funcs,
	.exynos_panel_func = &shoreline_exynos_funcs,
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
	{ .compatible = "google,shoreline", .data = &google_shoreline },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos_panel_of_match);

static struct mipi_dsi_driver exynos_panel_driver = {
	.probe = shoreline_panel_probe,
	.remove = exynos_panel_remove,
	.driver = {
		.name = "panel-google-shoreline",
		.of_match_table = exynos_panel_of_match,
	},
};
module_mipi_dsi_driver(exynos_panel_driver);

MODULE_AUTHOR("Jeremy DeHaan <jdehaan@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google Shoreline panel driver");
MODULE_LICENSE("GPL");
