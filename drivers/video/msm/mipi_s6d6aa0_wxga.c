/* Copyright (c) 2008-2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#include <linux/gpio.h>
#include "msm_fb.h"
#include "mipi_dsi.h"
#include "mipi_s6d6aa0_wxga.h"

#define LCDC_DEBUG

#ifdef LCDC_DEBUG
#define DPRINT(x...)	printk("[Mipi_LCD] " x)
#else
#define DPRINT(x...)	
#endif

static struct msm_panel_common_pdata *mipi_s6d6aa0_wxga_pdata;

static struct dsi_buf s6d6aa0_wxga_tx_buf;
static struct dsi_buf s6d6aa0_wxga_rx_buf;


#define GPIO_BACKLIGHT_PWM

#ifdef GPIO_BACKLIGHT_PWM
#define GPIO_LCDC_ONEWIRE				70
#endif

#if 0
#ifdef TOSHIBA_CMDS_UNUSED
static char one_lane[3] = {0xEF, 0x60, 0x62};
static char dmode_wqvga[2] = {0xB3, 0x01};
static char intern_wr_clk1_wqvga[3] = {0xef, 0x2f, 0x22};
static char intern_wr_clk2_wqvga[3] = {0xef, 0x6e, 0x33};
static char hor_addr_2A_wqvga[5] = {0x2A, 0x00, 0x00, 0x00, 0xef};
static char hor_addr_2B_wqvga[5] = {0x2B, 0x00, 0x00, 0x01, 0xaa};
static char if_sel_cmd[2] = {0x53, 0x00};
#endif

static char mcap_off[2] = {0xb2, 0x00};
static char ena_test_reg[3] = {0xEF, 0x01, 0x01};
static char two_lane[3] = {0xEF, 0x60, 0x63};
static char non_burst_sync_pulse[3] = {0xef, 0x61, 0x09};
static char dmode_wvga[2] = {0xB3, 0x00};
static char intern_wr_clk1_wvga[3] = {0xef, 0x2f, 0xcc};
static char intern_wr_clk2_wvga[3] = {0xef, 0x6e, 0xdd};
static char hor_addr_2A_wvga[5] = {0x2A, 0x00, 0x00, 0x01, 0xdf};
static char hor_addr_2B_wvga[5] = {0x2B, 0x00, 0x00, 0x03, 0x55};
static char if_sel_video[2] = {0x53, 0x01};
static char exit_sleep[2] = {0x11, 0x00};
static char display_on[2] = {0x29, 0x00};
static char display_off[2] = {0x28, 0x00};
static char enter_sleep[2] = {0x10, 0x00};
#endif

static char ETC_COND_SET_1_PW1[3] = {0xF0, 0x5A, 0x5A};
static char ETC_COND_SET_1_PW2[3] = {0xF1, 0x5A, 0x5A};

static char ETC_COND_SET_2_1[2] = {0xb1, 0x00};
static char ETC_COND_SET_2_2[2] = {0xb2, 0x00};
static char ETC_COND_SET_2_3[2] = {0xb3, 0x00};
static char ETC_COND_SET_2_4[4] = {0xC0, 0x80, 0x80, 0x10};
static char ETC_COND_SET_2_5[2] = {0xc1, 0x01};
static char ETC_COND_SET_2_6[2] = {0xc2, 0x08};
static char ETC_COND_SET_2_7[4] = {0xC3, 0x00, 0x00, 0x20};
static char ETC_COND_SET_2_8[4] = {0xF2, 0x03, 0x33, 0x81};
static char ETC_COND_SET_2_9[13] = {0xF4, 0x0A, 0x0B, 0x33, 0x33, 0x20, 0x14, 0x0D, 0x0C, 0xB9, 0x00, 0x33, 0x33};

static char PANEL_COND_SET[14] = {0xF8, 0x28, 0x28, 0x08, 0x08, 0x40, 0xB0, 0x50, 0x90, 0x10, 0x30, 0x10, 0x00, 0x00};
static char PANEL_CONTROL_SET_2[20] = {0xF6, 0x00, 0x0D, 0x0C, 0x22, 0x09, 0x00, 0x0F, 0x1C, 0x18, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 
#if 0	
	0x08, 0x02, 0x30, 0x10};
#else
	0x0A, 0x0A, 0x0A, 0x0A};
#endif

static char PGAMMA_COND_SET[64] = {0xFA, 0x38, 0x3F, 0x28, 0x2E, 0x3F, 0x3F, 0x35, 0x30, 0x31, 
    0x2A, 0x25, 0x2A, 0x2D, 0x30, 0x34, 0x38, 0x3C, 0x3F, 0x3F,
    0x3F, 0x30, 0x38, 0x3F, 0x28, 0x2E, 0x3F, 0x3F, 0x35, 0x30,
    0x31, 0x2A, 0x25, 0x2A, 0x2D, 0x30, 0x34, 0x38, 0x3C, 0x3F, 
    0x3F, 0x3F, 0x30, 0x38, 0x3F, 0x28, 0x2E, 0x3F, 0x3F, 0x35,
    0x30, 0x31, 0x2A, 0x25, 0x2A, 0x2D, 0x30, 0x34, 0x38, 0x3C,
    0x3F, 0x3F, 0x3F, 0x30};

static char NGAMMA_COND_SET[64] = {0xFB,0x38,0x3F,0x28,0x2E,0x3F,0x3F,0x35,0x30,0x31,
    0x2A,0x25,0x2A,0x2D,0x30,0x34,0x38,0x3C,0x3F,0x3F,
    0x3F,0x30,0x38,0x3F,0x28,0x2E,0x3F,0x3F,0x35,0x30,
    0x31,0x2A,0x25,0x2A,0x2D,0x30,0x34,0x38,0x3C,0x3F,
    0x3F,0x3F,0x30,0x38,0x3F,0x28,0x2E,0x3F,0x3F,0x35,
    0x30,0x31,0x2A,0x25,0x2A,0x2D,0x30,0x34,0x38,0x3C,
    0x3F,0x3F,0x3F,0x30};

static char exit_sleep[2] = {0x11, 0x00};
static char display_on[2] = {0x29, 0x00};
static char display_off[2] = {0x28, 0x00};
static char enter_sleep[2] = {0x10, 0x00};
static char DISP_SCAN_CONTROL[2] = {0x36, 0x80};

#if 1 // jklee
static struct dsi_cmd_desc s6d6aa0_wxga_power_on_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 600, sizeof(exit_sleep), exit_sleep},	
	{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_on), display_on}		
};

static struct dsi_cmd_desc s6d6aa0_wxga_power_off_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 10, sizeof(display_off), display_off},
	{DTYPE_DCS_WRITE, 1, 0, 0, 120, sizeof(enter_sleep), enter_sleep}
};
#endif

static struct dsi_cmd_desc s6d6aa0_wxga_display_off_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 10, sizeof(display_off), display_off},
	{DTYPE_DCS_WRITE, 1, 0, 0, 120, sizeof(enter_sleep), enter_sleep}
};

static struct dsi_cmd_desc s6d6aa0_wxga_display_on_cmds[] = {
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_1_PW1), ETC_COND_SET_1_PW1},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_1_PW2), ETC_COND_SET_1_PW2},        
    {DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_1), ETC_COND_SET_2_1},
    {DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_2), ETC_COND_SET_2_2},
    {DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_3), ETC_COND_SET_2_3},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_4), ETC_COND_SET_2_4},        
    {DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_5), ETC_COND_SET_2_5},
    {DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_6), ETC_COND_SET_2_6},    
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_7), ETC_COND_SET_2_7},    
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_8), ETC_COND_SET_2_8},    
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_9), ETC_COND_SET_2_9},        
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(PANEL_CONTROL_SET_2), PANEL_CONTROL_SET_2},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(PGAMMA_COND_SET), PGAMMA_COND_SET},    
    {DTYPE_DCS_LWRITE, 1, 0, 0, 50, sizeof(NGAMMA_COND_SET), NGAMMA_COND_SET},
	{DTYPE_DCS_WRITE, 1, 0, 0, 600, sizeof(exit_sleep), exit_sleep},
    {DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(DISP_SCAN_CONTROL), DISP_SCAN_CONTROL},	
	{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_on), display_on}
    
//    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(PANEL_COND_SET), PANEL_COND_SET},

/*    
	{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(mcap_off), mcap_off},
	{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(ena_test_reg), ena_test_reg},
	{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(two_lane), two_lane},
	{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(non_burst_sync_pulse),
					non_burst_sync_pulse},
	{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(dmode_wvga), dmode_wvga},
	{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(intern_wr_clk1_wvga),
					intern_wr_clk1_wvga},
	{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(intern_wr_clk2_wvga),
					intern_wr_clk2_wvga},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(hor_addr_2A_wvga),
					hor_addr_2A_wvga},
	{DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(hor_addr_2B_wvga),
					hor_addr_2B_wvga},
	{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(if_sel_video), if_sel_video},
	{DTYPE_MAX_PKTSIZE, 1, 0, 0, 0, sizeof(max_pktsize), max_pktsize},
	{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(exit_sleep), exit_sleep},
	{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_on), display_on}
*/	
};

#if 1 // from samsung
char passwd1[3]     = {0xF0, 0x5A, 0x5A};
char passwd2[3]     = {0xF1, 0x5A, 0x5A};
char source_ctl[4]     = {0xF2, 0x03, 0x35, 0x81};
char power_ctl_1[13]     = {0xF4, 0x0A, 0x0B, 0x3E, 0x3E, 0x11, 0x39, 0x1C, 0x04, 0xD2, 0x00, 0x33, 0x33};
char power_ctl_2[11]     = {0xF8, 0x25, 0x35, 0x35, 0x97, 0x35, 0x10, 0x03, 0x01, 0x0A, 0x10};
char positive_gamma[64]     = {0xFA, 0x38, 0x3F, 0x24, 0x21, 0x31, 0x31, 0x29, 0x2C, 0x2D, 0x28, 
                                                           0x26, 0x26, 0x2A, 0x2D, 0x30, 0x33, 0x36, 0x3A, 0x3A, 0x38, 
                                                           0x32, 0x38, 0x3F, 0x21, 0x1E, 0x2E, 0x2F, 0x27, 0x2A, 0x2C, 
                                                           0x28, 0x25, 0x25, 0x27, 0x2B, 0x2B, 0x2F, 0x32, 0x35, 0x34, 
                                                           0x31, 0x16, 0x38, 0x3F, 0x1F, 0x1B, 0x2D, 0x2E, 0x27, 0x2B, 
                                                           0x2E, 0x27, 0x2B, 0x28, 0x2A, 0x2C, 0x30, 0x33, 0x35, 0x38, 0x3A, 0x37, 0x1B};
char negative_gamma[64]    = {0xFB, 0x38, 0x3F, 0x24, 0x21, 0x31, 0x31, 0x29, 0x2C, 0x2D, 0x28,
                                                            0x26, 0x26, 0x2A, 0x2D, 0x30, 0x33, 0x36, 0x3A, 0x34, 0x38, 
                                                            0x32, 0x38, 0x3F, 0x21, 0x1E, 0x2E, 0x2F, 0x27, 0x2A, 0x2C, 
                                                            0x28, 0x25, 0x25, 0x27, 0x2B, 0x2D, 0x2F, 0x32, 0x35, 0x34, 
                                                            0x31, 0x16, 0x38, 0x3F, 0x1F, 0x1B, 0x2D, 0x2E, 0x27, 0x2B, 
                                                            0x2E, 0x27, 0x2B, 0x28, 0x2A, 0x2C, 0x30, 0x33, 0x35, 0x38, 0x3A, 0x37, 0x1B};
char diplay_ctl[2]     = {0xEF, 0x20};

char mad_ctl[2]     = {0x36, 0x40};
char amp_type_1[3]     = {0xFC, 0x5A, 0x5A};
char amp_type_2[4]     = {0xF5, 0x5A, 0x55, 0x38};

char sleep_out[2]   = {0x11, 0x00};
char disp_on[2]     = {0x29, 0x00};
char sleep_in[2]    = {0x10, 0x00};
char disp_off[2]    = {0x28, 0x00};

static struct dsi_cmd_desc samsung_display_off_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(disp_off), disp_off},
	{DTYPE_DCS_WRITE, 1, 0, 0, 120, sizeof(sleep_in), sleep_in}
};

#if (1) 
static struct dsi_cmd_desc samsung_display_init_cmds[] = {
	{DTYPE_GEN_LWRITE, 1, 0, 0, 16, sizeof(mad_ctl), mad_ctl},
       {DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(amp_type_1), amp_type_1},
       {DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(amp_type_2), amp_type_2},
};
#else
static struct dsi_cmd_desc samsung_display_init_cmds[] = {
	{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(passwd1), passwd1},
       {DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(passwd2), passwd2},
       {DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(source_ctl), source_ctl},
       {DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(power_ctl_1), power_ctl_1},   
	{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(power_ctl_2), power_ctl_2},
	{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(positive_gamma), positive_gamma},
	{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(negative_gamma), negative_gamma},
	{DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(diplay_ctl), diplay_ctl},
       {DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(amp_type_1), amp_type_1},
       {DTYPE_GEN_LWRITE, 1, 0, 0, 0, sizeof(amp_type_2), amp_type_2},
};
#endif

static struct dsi_cmd_desc samsung_display_on_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 120, sizeof(sleep_out), sleep_out},
	{DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(disp_on), disp_on}
};

#endif

#if 1 // jklee
static char manufacture_id1[2] = {0xDA, 0x00}; /* DTYPE_DCS_READ */
static char manufacture_id2[2] = {0xDB, 0x00}; /* DTYPE_DCS_READ */
static char manufacture_id3[2] = {0xDC, 0x00}; /* DTYPE_DCS_READ */

static struct dsi_cmd_desc s6d6aa0_manufacture_id1_cmd = {
	DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(manufacture_id1), manufacture_id1};

static struct dsi_cmd_desc s6d6aa0_manufacture_id2_cmd = {
	DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(manufacture_id2), manufacture_id2};

static struct dsi_cmd_desc s6d6aa0_manufacture_id3_cmd = {
	DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(manufacture_id3), manufacture_id3};

static uint32 mipi_s6d6aa0_manufacture_ids(struct msm_fb_data_type *mfd)
{
	struct dsi_buf *rp, *tp;
	struct dsi_cmd_desc *cmd;
	uint32 *lp;

	tp = &s6d6aa0_wxga_tx_buf;
	rp = &s6d6aa0_wxga_rx_buf;
	mipi_dsi_buf_init(rp);
	mipi_dsi_buf_init(tp);

	cmd = &s6d6aa0_manufacture_id1_cmd;

	mutex_lock(&mfd->dma->ov_mutex);
	mipi_dsi_cmds_rx(mfd, tp, rp, cmd, 4);
	mutex_unlock(&mfd->dma->ov_mutex);
	
	lp = (uint32 *)rp->data;
	
	DPRINT("%s: manufacture_id1=0x%x\n", __func__, *lp);


	tp = &s6d6aa0_wxga_tx_buf;
	rp = &s6d6aa0_wxga_rx_buf;
	mipi_dsi_buf_init(rp);
	mipi_dsi_buf_init(tp);

	cmd = &s6d6aa0_manufacture_id2_cmd;

	mutex_lock(&mfd->dma->ov_mutex);
	mipi_dsi_cmds_rx(mfd, tp, rp, cmd, 4);
	mutex_unlock(&mfd->dma->ov_mutex);
	
	lp = (uint32 *)rp->data;
	
	DPRINT("%s: manufacture_id2=0x%x\n", __func__, *lp);


	tp = &s6d6aa0_wxga_tx_buf;
	rp = &s6d6aa0_wxga_rx_buf;
	mipi_dsi_buf_init(rp);
	mipi_dsi_buf_init(tp);

	cmd = &s6d6aa0_manufacture_id3_cmd;

	mutex_lock(&mfd->dma->ov_mutex);
	mipi_dsi_cmds_rx(mfd, tp, rp, cmd, 4);
	mutex_unlock(&mfd->dma->ov_mutex);
	
	lp = (uint32 *)rp->data;
	
	DPRINT("%s: manufacture_id3=0x%x\n", __func__, *lp);

	return *lp;
}



static char manufacture_id[2] = {0x04, 0x00}; /* DTYPE_DCS_READ */
static struct dsi_cmd_desc s6d6aa0_manufacture_id_cmd = {
	DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(manufacture_id), manufacture_id};

static uint32 mipi_s6d6aa0_manufacture_id(struct msm_fb_data_type *mfd)
{
	struct dsi_buf *rp, *tp;
	struct dsi_cmd_desc *cmd;
	uint32 *lp;

	tp = &s6d6aa0_wxga_tx_buf;
	rp = &s6d6aa0_wxga_rx_buf;
	mipi_dsi_buf_init(rp);
	mipi_dsi_buf_init(tp);

	cmd = &s6d6aa0_manufacture_id_cmd;

	mutex_lock(&mfd->dma->ov_mutex);
	mipi_dsi_cmds_rx(mfd, tp, rp, cmd, 4);
	mutex_unlock(&mfd->dma->ov_mutex);
	
	lp = (uint32 *)rp->data;
	
	DPRINT("%s: manufacture_id=0x%x\n", __func__, *lp);

	return *lp;
}

// Write Display Brightness
static char display_brightness[2] = {0x51, 0xFF}; /* DTYPE_DCS_WRITE1 */
static struct dsi_cmd_desc s6d6aa0_manufacture_display_brightness = {
	DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(display_brightness), display_brightness};

static char read_display_brightness[2] = {0xA1, 0x00}; /* DTYPE_DCS_READ1 */
static struct dsi_cmd_desc s6d6aa0_read_display_brightness = {
	DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(read_display_brightness), read_display_brightness};

static void mipi_s6d6aa0_display_brightness(struct msm_fb_data_type *mfd)
{
	struct dsi_buf *rp, *tp;
	struct dsi_cmd_desc *cmd;
	uint32 *lp;

	tp = &s6d6aa0_wxga_tx_buf;
	mipi_dsi_buf_init(tp);

	cmd = &s6d6aa0_manufacture_display_brightness;

	mutex_lock(&mfd->dma->ov_mutex);
	mipi_dsi_cmds_tx(mfd, tp, cmd, 1);
	mutex_unlock(&mfd->dma->ov_mutex);


	tp = &s6d6aa0_wxga_tx_buf;
	rp = &s6d6aa0_wxga_rx_buf;
	mipi_dsi_buf_init(rp);
	mipi_dsi_buf_init(tp);

	cmd = &s6d6aa0_read_display_brightness;

	mutex_lock(&mfd->dma->ov_mutex);
	mipi_dsi_cmds_rx(mfd, tp, rp, cmd, 4);
	mutex_unlock(&mfd->dma->ov_mutex);

	lp = (uint32 *)rp->data;

	DPRINT("%s: brightness = 0x%x\n", __func__, *lp);
}

// Write Control Display
static char control_display[2] = {0x53, 0x24}; /* DTYPE_DCS_WRITE1 */
static struct dsi_cmd_desc s6d6aa0_manufacture_control_display= {
	DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(control_display), control_display};

static void mipi_s6d6aa0_control_display(struct msm_fb_data_type *mfd)
{
	struct dsi_buf *tp;
	struct dsi_cmd_desc *cmd;

	tp = &s6d6aa0_wxga_tx_buf;
	mipi_dsi_buf_init(tp);

	cmd = &s6d6aa0_manufacture_control_display;

	mutex_lock(&mfd->dma->ov_mutex);
	mipi_dsi_cmds_tx(mfd, tp, cmd, 1);
	mutex_unlock(&mfd->dma->ov_mutex);
}
#endif

static int mipi_s6d6aa0_wxga_lcd_on(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	int rc=0;
    DPRINT("%s \n", __func__);
	mfd = platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;
	if (mfd->key != MFD_KEY)
		return -EINVAL;
#if 1
	mutex_lock(&mfd->dma->ov_mutex);
#if 1	
    // lcd_panel_init
	printk(KERN_ERR "mipi_s6d6aa0_wxga_lcd_on, before cmds tx\n");    
	mipi_dsi_cmds_tx(mfd, &s6d6aa0_wxga_tx_buf, s6d6aa0_wxga_display_on_cmds,
			ARRAY_SIZE(s6d6aa0_wxga_display_on_cmds));    
	mutex_unlock(&mfd->dma->ov_mutex);
#else // jklee
	// power on
	printk(KERN_ERR "mipi_s6d6aa0_wxga_lcd_on, before cmds tx\n");	  
	mipi_dsi_cmds_tx(mfd, &s6d6aa0_wxga_tx_buf, s6d6aa0_wxga_power_on_cmds,
			ARRAY_SIZE(s6d6aa0_wxga_power_on_cmds));	  
	mutex_unlock(&mfd->dma->ov_mutex);
#endif
#ifdef GPIO_BACKLIGHT_PWM
		rc = gpio_request(GPIO_LCDC_ONEWIRE, "lcd_on");
		if (rc) {
			pr_err("%s: unable to request gpio %d (%d)\n",
					__func__, GPIO_LCDC_ONEWIRE, rc);
		}

		rc = gpio_direction_output(GPIO_LCDC_ONEWIRE, 1);
		if (rc) {
			pr_err("%s: Unable to set direction\n", __func__);;
		}
#endif		
#if 1 // jklee
	DPRINT("%s: before manufacture_id\n", __func__);
	mipi_s6d6aa0_manufacture_id(mfd);
	DPRINT("%s: after manufacture_id\n", __func__);

	DPRINT("%s: before manufacture_ids\n", __func__);
	mipi_s6d6aa0_manufacture_ids(mfd);
	DPRINT("%s: after manufacture_ids\n", __func__);

	//mipi_s6d6aa0_control_display(mfd);
	
	mipi_s6d6aa0_display_brightness(mfd);
#endif
#else
//MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x14000000);    // Low Power Mode
   
	mutex_lock(&mfd->dma->ov_mutex);
	mipi_dsi_cmds_tx(mfd, &s6d6aa0_wxga_tx_buf, samsung_display_init_cmds,
	       ARRAY_SIZE(samsung_display_init_cmds));
	mutex_unlock(&mfd->dma->ov_mutex);

//MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x10000000);    // High Speed Mode
	   
	mutex_lock(&mfd->dma->ov_mutex);
	mipi_dsi_cmds_tx(mfd, &s6d6aa0_wxga_tx_buf, samsung_display_on_cmds,
		ARRAY_SIZE(samsung_display_on_cmds));
	mutex_unlock(&mfd->dma->ov_mutex);

#endif
	return 0;
}

static int mipi_s6d6aa0_wxga_lcd_off(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	int rc=0;
    DPRINT("%s \n", __func__);
	mfd = platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;
	if (mfd->key != MFD_KEY)
		return -EINVAL;
#ifdef GPIO_BACKLIGHT_PWM
		rc = gpio_request(GPIO_LCDC_ONEWIRE, "lcd_off");
		if (rc) {
			pr_err("%s: unable to request gpio %d (%d)\n",
					__func__, GPIO_LCDC_ONEWIRE, rc);
		}

		rc = gpio_direction_output(GPIO_LCDC_ONEWIRE, 0);
		if (rc) {
			pr_err("%s: Unable to set direction\n", __func__);;
		}
#endif			
#if 1
	mutex_lock(&mfd->dma->ov_mutex);
	mipi_dsi_cmds_tx(mfd, &s6d6aa0_wxga_tx_buf, s6d6aa0_wxga_display_off_cmds,
			ARRAY_SIZE(s6d6aa0_wxga_display_off_cmds));
	mutex_unlock(&mfd->dma->ov_mutex);
#else //from samsung
	mutex_lock(&mfd->dma->ov_mutex);
	mipi_dsi_cmds_tx(mfd, &s6d6aa0_wxga_tx_buf, samsung_display_off_cmds,
			ARRAY_SIZE(samsung_display_off_cmds));
	mutex_unlock(&mfd->dma->ov_mutex);
#endif
	return 0;
}

static int __devinit mipi_s6d6aa0_wxga_lcd_probe(struct platform_device *pdev)
{
    DPRINT("%s \n", __func__);
	if (pdev->id == 0) {
		mipi_s6d6aa0_wxga_pdata = pdev->dev.platform_data;
		return 0;
	}

	DPRINT("msm_fb_add_device START\n");
	msm_fb_add_device(pdev);
	DPRINT("msm_fb_add_device end\n");

	return 0;
}

static struct platform_driver this_driver = {
	.probe  = mipi_s6d6aa0_wxga_lcd_probe,
	.driver = {
		.name   = "mipi_s6d6aa0_wxga",
	},
};

static struct msm_fb_panel_data s6d6aa0_wxga_panel_data = {
	.on		= mipi_s6d6aa0_wxga_lcd_on,
	.off		= mipi_s6d6aa0_wxga_lcd_off,
};

static int ch_used[3];

int mipi_s6d6aa0_wxga_device_register(struct msm_panel_info *pinfo,
					u32 channel, u32 panel)
{
	struct platform_device *pdev = NULL;
	int ret;

	if ((channel >= 3) || ch_used[channel])
		return -ENODEV;

	ch_used[channel] = TRUE;

	pdev = platform_device_alloc("mipi_s6d6aa0_wxga", (panel << 8)|channel);
	if (!pdev)
		return -ENOMEM;

	s6d6aa0_wxga_panel_data.panel_info = *pinfo;

	ret = platform_device_add_data(pdev, &s6d6aa0_wxga_panel_data,
		sizeof(s6d6aa0_wxga_panel_data));
	if (ret) {
		printk(KERN_ERR
		  "%s: platform_device_add_data failed!\n", __func__);
		goto err_device_put;
	}

	ret = platform_device_add(pdev);
	if (ret) {
		printk(KERN_ERR
		  "%s: platform_device_register failed!\n", __func__);
		goto err_device_put;
	}

	return 0;

err_device_put:
	platform_device_put(pdev);
	return ret;
}

static int __init mipi_s6d6aa0_wxga_lcd_init(void)
{
    DPRINT("%s \n", __func__);
	mipi_dsi_buf_alloc(&s6d6aa0_wxga_tx_buf, DSI_BUF_SIZE);
	mipi_dsi_buf_alloc(&s6d6aa0_wxga_rx_buf, DSI_BUF_SIZE);

	return platform_driver_register(&this_driver);
}

module_init(mipi_s6d6aa0_wxga_lcd_init);
