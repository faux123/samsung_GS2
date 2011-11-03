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

#include "msm_fb.h"
#include "mipi_dsi.h"
#include "mipi_s6e8aa0_hd720.h"
#include "mipi_s6e8aa0_hd720_seq.h"
#include "mdp4_video_enhance.h"
#include "smart_dimming.h"

#include <linux/gpio.h>

#define LCDC_DEBUG

//#define MIPI_SINGLE_WRITE

#ifdef LCDC_DEBUG
#define DPRINT(x...)	printk("[Mipi_LCD] " x)
#else
#define DPRINT(x...)	
#endif

#define MUTEX_LOCK(a) do { DPRINT("MUTEX LOCK : Line=%d, %x\n", __LINE__, a); mutex_lock(a);} while(0)
#define MUTEX_UNLOCK(a) do { DPRINT("MUTEX unlock : Line=%d, %x\n", __LINE__, a); mutex_unlock(a);} while(0)

#define GPIO_S6E8AA0_RESET (28)

#define LCD_OFF_GAMMA_VALUE (0)
#define DEFAULT_CANDELA_VALUE (160)

#define ACL_OFF_GAMMA_VALUE (60)

enum lcd_id_value {
lcd_id_unknown = -1,
lcd_id_a1_m3_line = 1,
lcd_id_a1_sm2_line,
lcd_id_a2_line,
lcd_id_temp_7500k,
lcd_id_temp_8500k,
lcd_id_elvss_normal,
lcd_id_elvss_each,
};


struct lcd_state_type{
	boolean initialized;
	boolean display_on;
	boolean powered_up;
};

#define LCD_ID_BUFFER_SIZE (8)

struct lcd_setting {
	struct device			*dev;
	struct spi_device		*spi;
	unsigned int			power;
	unsigned int			gamma_mode;
	unsigned int			lcd_gamma; // lcd has this value
	unsigned int			stored_gamma; // driver has this value
	unsigned int			gamma_table_count;
	unsigned int			lcd_elvss; // lcd has this value
	unsigned int			stored_elvss; // driver has this value
	unsigned int			beforepower;
	unsigned int			ldi_enable;
	unsigned int 			enabled_acl; // lcd has this value
	unsigned int 			stored_acl; // driver has this value
	unsigned int 			lcd_acl; // lcd has this value
	struct mutex	lock;
	struct lcd_device		*ld;
	struct backlight_device		*bd;
	struct lcd_platform_data	*lcd_pd;
	struct early_suspend    early_suspend;
	struct lcd_state_type lcd_state;
	char	lcd_id_buffer[LCD_ID_BUFFER_SIZE];
	enum lcd_id_value	factory_id_line;
	enum lcd_id_value	factory_id_colorTemp;
	enum lcd_id_value	factory_id_elvssType;
	struct dsi_cmd_desc_LCD *lcd_brightness_table;
	struct dsi_cmd_desc_LCD *lcd_acl_table;
	int	eachElvss_value;
	int	eachElvss_vector;

	boolean	isSmartDimming;
	boolean	isSmartDimming_loaded;
	struct str_smart_dim smart;
};

static struct lcd_setting s6e8aa0_lcd;
//



static void lcd_set_brightness(struct msm_fb_data_type *mfd, int level);
static void lcd_gamma_apply( struct msm_fb_data_type *mfd, int srcGamma);
static void lcd_gamma_smartDimming_apply( struct msm_fb_data_type *mfd, int srcGamma);
static void lcd_apply_elvss(struct msm_fb_data_type *mfd, int srcElvss, struct lcd_setting *lcd);
static void lcd_apply_acl(struct msm_fb_data_type *mfd, unsigned int srcAcl, boolean nowOffNeedOn);
static void lcd_set_acl(struct msm_fb_data_type *mfd, struct lcd_setting *lcd);
static void lcd_set_elvss(struct msm_fb_data_type *mfd, struct lcd_setting *lcd);
static void lcd_gamma_ctl(struct msm_fb_data_type *mfd, struct lcd_setting *lcd);

static struct msm_panel_common_pdata *pdata;
static struct msm_fb_data_type *pMFD;

#undef  SmartDimming_16bitBugFix
#define SmartDimming_useSampleValue
#define SmartDimming_CANDELA_UPPER_LIMIT (299)
#define MTP_DATA_SIZE (24)
#define MTP_REGISTER	(0xD3)
unsigned char lcd_mtp_data[MTP_DATA_SIZE+16] = {0,};
#ifdef SmartDimming_useSampleValue
boolean	isFAIL_mtp_load = FALSE;
const unsigned char lcd_mtp_data_default [MTP_DATA_SIZE] = {
0x00, 0x00, 0x00 ,0x04, 0x02, 0x05, 
0x06, 0x05, 0x08, 0x03, 0x02, 0x03, 
0x02, 0x01, 0x01, 0xfa, 0xfe, 0xfa, 
0x00, 0x14, 0x00, 0x0e, 0x00, 0x0d,
};
#endif // SmartDimming_useSampleValue

static struct dsi_buf s6e8aa0_tx_buf;
static struct dsi_buf s6e8aa0_rx_buf;

static char ETC_COND_SET_1[3] = {0xF0, 0x5A, 0x5A};
static char ETC_COND_SET_2[3] = {0xF1, 0x5A, 0x5A};
static char ETC_COND_SET_2_OFF[3] = {0xF1, 0x5A, 0x5A};

static char ETC_COND_SET_2_1[4] = {0xF6, 0x00, 0x02, 0x00};
static char ETC_COND_SET_2_2[10] = {0xB6, 0x0C, 0x02, 0x03, 0x32, 0xFF, 0x44, 0x44, 0xC0, 0x00};
static char ETC_COND_SET_2_3[15] = {0xD9, 0x14, 0x40, 0x0C, 0xCB, 0xCE, 0x6E, 0xC4, 0x07, 0x40, 0x41, 0xD0, 0x00, 0x60, 0x19};
static char ETC_COND_SET_2_4[6] = {0xE1, 0x10, 0x1C, 0x17, 0x08, 0x1D};
static char ETC_COND_SET_2_5[7] = {0xE2, 0xED, 0x07, 0xC3, 0x13, 0x0D, 0x03};
static char ETC_COND_SET_2_6[2] = {0xE3, 0x40};
static char ETC_COND_SET_2_7[8] = {0xE4, 0x00, 0x00, 0x14, 0x80, 0x00, 0x00, 0x00};
static char ETC_COND_SET_2_8[8] = {0xF4, 0xCF, 0x0A, 0x12, 0x10, 0x19, 0x33, 0x02};

static char PANEL_COND_SET_7500K_500MBPS[39] = {
	0xF8 ,/*0x3C*/0x3d, 0x35, 0x00, 0x00, 0x00, 0x93, 0x00, 0x3C, 0x7D, 
	0x08, 0x27, 0x7D, 0x3F, 0x00, 0x00, 0x00, 0x20, 0x04, 0x08, 
	0x6E, 0x00, 0x00, 0x00, 0x02, 0x08, 0x08, 0x23, 0x66, 0xC0, 
	0xC8, 0x08, 0x48, 0xC1, 0x00, 0xC1, 0xFF, 0xFF, 0xC8
};
	
static char PANEL_COND_SET_7500K_480MBPS[39] = {
	0xF8 ,/*0x3C*/0x3d, 0x35, 0x00, 0x00, 0x00, 0x8D, 0x00, 0x3A, 0x78, 
	0x08, 0x26, 0x78, 0x3C, 0x00, 0x00, 0x00, 0x20, 0x04, 0x08, 
	0x69, 0x00, 0x00, 0x00, 0x02, 0x07, 0x07, 0x21, 0x21, 0xC0, 
	0xC8, 0x08, 0x48, 0xC1, 0x00, 0xC1, 0xFF, 0xFF, 0xC8
};

static char PANEL_COND_SET_8500K_500MBPS[39] = {
	0xF8 ,/*0x3C*/0x3d, 0x35, 0x00, 0x00, 0x00, 0x8D, 0x00, 0x4C, 0x6E, 
	0x10, 0x27, 0x7D, 0x3F, 0x10, 0x00, 0x00, 0x20, 0x04, 0x08, 
	0x6E, 0x00, 0x00, 0x00, 0x02, 0x08, 0x08, 0x23, 0x23, 0xC0, 
	0xC8, 0x08, 0x48, 0xC1, 0x00, 0xC1, 0xFF, 0xFF, 0xC8
};


static char DISPLAY_COND_SET[4] = {0xF2, 0x80, 0x03, 0x0D};

static char GAMMA_COND_SET[] = // 160 cd
{ 0xFA, 0x01, 
0x0F, 	0x0F, 	0x0F, 	
0xCA, 	0x6E, 	0xDE, 	
0xC2, 	0xB6, 	0xC4, 	
0xD2, 	0xCD, 	0xD2, 	
0xAE, 	0xAD, 	0xAD, 	
0xC2, 	0xC5, 	0xC0, 	
0x0, 	0x6E, 	0x0, 	0x62, 	0x0, 	0x98};


static char exit_sleep[2] = {0x11, 0x00};
static char display_on[2] = {0x29, 0x00};
static char display_off[2] = {0x28, 0x00};
static char enter_sleep[2] = {0x10, 0x00};
static char tear_off[2] = {0x34, 0x00};
static char tear_on [2] = {0x35, 0x00};
static char gamma_update_cmd[2] = {0xF7, /*0x01*/0x03};
static char ltps_update_cmd[2] = {0xF7, 0x02};
static char all_pixel_off_cmd[2] = {0x22, 0x00};
static char all_pixel_on_cmd[2] = {0x23, 0x00};
static char acl_off_cmd[2] = {0xC0, 0x00};
static char acl_on_cmd[2] = {0xC0, 0x01};
static char reset_cmd[2] = {0x01, 0x00};

static struct dsi_cmd_desc s6e8aa0_display_off_cmds[] = {
	{DTYPE_DCS_WRITE, 1, 0, 0, 10, sizeof(display_off), display_off},
	{DTYPE_DCS_WRITE, 1, 0, 0, 120, sizeof(enter_sleep), enter_sleep}
};

static struct dsi_cmd_desc s6e8aa0_gamma_update_cmd = {
	DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(gamma_update_cmd), gamma_update_cmd};

static struct dsi_cmd_desc s6e8aa0_F1Key_On_cmd = {
	DTYPE_DCS_WRITE1, 1, 0, 0, 10, sizeof(ETC_COND_SET_2), ETC_COND_SET_2};

static struct dsi_cmd_desc s6e8aa0_F1Key_Off_cmd = {
	DTYPE_DCS_WRITE1, 1, 0, 0, 10, sizeof(ETC_COND_SET_2_OFF), ETC_COND_SET_2_OFF};

static struct dsi_cmd_desc s6e8aa0_acl_off_cmd = {
	DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(acl_off_cmd), acl_off_cmd};
static struct dsi_cmd_desc s6e8aa0_acl_on_cmd = {
	DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(acl_on_cmd), acl_on_cmd};


static struct dsi_cmd_desc s6e8aa0_display_on_cmds[] = {
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_1), ETC_COND_SET_1},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2), ETC_COND_SET_2},
    {DTYPE_DCS_WRITE, 1, 0, 0, 120, sizeof(exit_sleep), exit_sleep},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(PANEL_COND_SET_7500K_500MBPS), PANEL_COND_SET_7500K_500MBPS},    
    //{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(ltps_update_cmd), ltps_update_cmd},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(DISPLAY_COND_SET), DISPLAY_COND_SET},    
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(GAMMA_COND_SET), GAMMA_COND_SET},    
    {DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(gamma_update_cmd), gamma_update_cmd},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_1), ETC_COND_SET_2_1},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_2), ETC_COND_SET_2_2},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_3), ETC_COND_SET_2_3},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_4), ETC_COND_SET_2_4},        
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_5), ETC_COND_SET_2_5},
    {DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_6), ETC_COND_SET_2_6},    
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_7), ETC_COND_SET_2_7},    
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_8), ETC_COND_SET_2_8},    
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ELVSS_COND_SET_160), ELVSS_COND_SET_160},    
    {DTYPE_DCS_LWRITE, 1, 0, 0, 120, sizeof(ACL_COND_SET_40), ACL_COND_SET_40},    
    {DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_on), display_on},
};

static struct dsi_cmd_desc s6e8aa0_display_on_before_read_id[] = {
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_1), ETC_COND_SET_1},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2), ETC_COND_SET_2},
    {DTYPE_DCS_WRITE, 1, 0, 0, 120, sizeof(exit_sleep), exit_sleep},
};

static struct dsi_cmd_desc s6e8aa0_display_on_before_gamma_cmds[] = {
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(PANEL_COND_SET_7500K_500MBPS), PANEL_COND_SET_7500K_500MBPS},    
    //{DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(ltps_update_cmd), ltps_update_cmd},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(DISPLAY_COND_SET), DISPLAY_COND_SET},    
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_1), ETC_COND_SET_2_1},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_2), ETC_COND_SET_2_2},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_3), ETC_COND_SET_2_3},
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_4), ETC_COND_SET_2_4},        
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_5), ETC_COND_SET_2_5},
    {DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_6), ETC_COND_SET_2_6},    
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_7), ETC_COND_SET_2_7},    
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ETC_COND_SET_2_8), ETC_COND_SET_2_8},    
    {DTYPE_DCS_LWRITE, 1, 0, 0, 0, sizeof(ELVSS_COND_SET_160), ELVSS_COND_SET_160},    
    {DTYPE_DCS_LWRITE, 1, 0, 0, 120, sizeof(ACL_COND_SET_40), ACL_COND_SET_40},    
};

static struct dsi_cmd_desc s6e8aa0_display_on_after_gamma_cmds[] = {
    {DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(gamma_update_cmd), gamma_update_cmd},
    {DTYPE_DCS_WRITE, 1, 0, 0, 0, sizeof(display_on), display_on},
};

static inline boolean isPANEL_COND_SET( char* src )
{
	if( src == PANEL_COND_SET_7500K_500MBPS 
		|| src == PANEL_COND_SET_7500K_480MBPS
		|| src == PANEL_COND_SET_7500K_500MBPS
		|| src == PANEL_COND_SET_8500K_500MBPS )
	return TRUE;
	else
	return FALSE;
}

char* get_s6e8aa0_id_buffer( void )
{
	return s6e8aa0_lcd.lcd_id_buffer;
}

static void update_LCD_SEQ_by_id(struct lcd_setting *destLCD)
{
	int	PANEL_COND_SET_dlen_NEW = 0;
	char* PANEL_COND_SET_NEW = NULL;
	int updateCnt = 0;
	int i;
	char pBuffer[128] = {0,};

	updateCnt = 0;
	switch( destLCD->factory_id_colorTemp )
	{
		case lcd_id_temp_7500k:
		case lcd_id_unknown:
		default:	// if Error-case, Not Change 
		destLCD->lcd_brightness_table = lcd_brightness_7500k_table;
		if( MIPI_MBPS == 500 )
		{
			PANEL_COND_SET_dlen_NEW = sizeof(PANEL_COND_SET_7500K_500MBPS);
			PANEL_COND_SET_NEW = PANEL_COND_SET_7500K_500MBPS;
			sprintf( pBuffer +strlen(pBuffer), " 500Mbps" );
		} else if( MIPI_MBPS == 480 ) {
			PANEL_COND_SET_dlen_NEW = sizeof(PANEL_COND_SET_7500K_480MBPS);
			PANEL_COND_SET_NEW = PANEL_COND_SET_7500K_480MBPS;
			sprintf( pBuffer +strlen(pBuffer), " 480Mbps" );
		} else {
			PANEL_COND_SET_dlen_NEW = sizeof(PANEL_COND_SET_7500K_500MBPS);
			PANEL_COND_SET_NEW = PANEL_COND_SET_7500K_500MBPS;
			sprintf( pBuffer +strlen(pBuffer), " 500(%d)Mbps", MIPI_MBPS );
		}
		
		sprintf( pBuffer +strlen(pBuffer), "update_LCD_SEQ_by_id : 7500K" );
		updateCnt++;
		break;

		case lcd_id_temp_8500k:
		destLCD->lcd_brightness_table = lcd_brightness_8500k_table;
		updateCnt++;
		PANEL_COND_SET_dlen_NEW = sizeof(PANEL_COND_SET_8500K_500MBPS);
		PANEL_COND_SET_NEW = PANEL_COND_SET_8500K_500MBPS;
		sprintf( pBuffer +strlen(pBuffer), "update_LCD_SEQ_by_id : 8500K" );
		
		sprintf( pBuffer +strlen(pBuffer), " (%dMbps)", MIPI_MBPS );
		break;
	}

	for( i = 0; i < ARRAY_SIZE( s6e8aa0_display_on_cmds ); i++ )
	{
		if( isPANEL_COND_SET(s6e8aa0_display_on_cmds[i].payload))
		{
			s6e8aa0_display_on_cmds[i].dlen	= PANEL_COND_SET_dlen_NEW;
			s6e8aa0_display_on_cmds[i].payload	= PANEL_COND_SET_NEW;
			updateCnt++;
			sprintf( pBuffer +strlen(pBuffer), " c%d", i );
		}
	}

	for( i = 0; i < ARRAY_SIZE( s6e8aa0_display_on_before_gamma_cmds ); i++ )
	{
		if( isPANEL_COND_SET(s6e8aa0_display_on_before_gamma_cmds[i].payload))
		{
			s6e8aa0_display_on_before_gamma_cmds[i].dlen	= PANEL_COND_SET_dlen_NEW;
			s6e8aa0_display_on_before_gamma_cmds[i].payload	= PANEL_COND_SET_NEW;
			updateCnt++;
			sprintf( pBuffer +strlen(pBuffer), " b%d", i );
		}
	}

	// smartDimming data initialiazing
	if( destLCD->isSmartDimming && s6e8aa0_lcd.isSmartDimming_loaded )
	{
		init_table_info( &(s6e8aa0_lcd.smart), GAMMA_SmartDimming_VALUE_SET_300cd );
		calc_voltage_table(&(s6e8aa0_lcd.smart), lcd_mtp_data);
		sprintf( pBuffer +strlen(pBuffer), " i" );
	}

	sprintf( pBuffer +strlen(pBuffer), "\n" );
	printk( pBuffer );
}


void update_id( char* srcBuffer, struct lcd_setting *destLCD )
{
	char pBuffer[96] = {0,};
	unsigned char checkValue;
	boolean elvss_Minus10;
	int	cntBuf;
	int	i;

	cntBuf = 0;
	cntBuf += sprintf( pBuffer +cntBuf, "Update LCD ID:" );
	
	// make read-data valid
	if( srcBuffer[0] == 0x00 )
	{
		for( i = 0; i < LCD_ID_BUFFER_SIZE-2; i ++ ) srcBuffer[i] = srcBuffer[i+2];
	}
	destLCD->isSmartDimming = FALSE;
	
	checkValue = srcBuffer[0];
	switch( checkValue )
	{
		case 0xA1:
			destLCD->factory_id_line = lcd_id_a1_m3_line;
			cntBuf += sprintf( pBuffer +cntBuf, " A1M3-Line" );
		break;
		case 0x12:
			destLCD->factory_id_line = lcd_id_a1_sm2_line;
			destLCD->isSmartDimming = TRUE;
			cntBuf += sprintf( pBuffer +cntBuf, " A1SM2-Line" );
		break;
		case 0xA2:
			destLCD->factory_id_line = lcd_id_a2_line;
			cntBuf += sprintf( pBuffer +cntBuf, " A2-Line" );
		break;
		default :
			destLCD->factory_id_line = lcd_id_unknown;
			cntBuf += sprintf( pBuffer +cntBuf, " A?-Line" );
		break;				
	} // end switch
	cntBuf += sprintf( pBuffer +cntBuf, "(%X)", checkValue );

	checkValue = srcBuffer[1];
	switch( checkValue )
	{
		case 0x25 ... 0x6F:
			destLCD->factory_id_colorTemp = lcd_id_temp_7500k;
			cntBuf += sprintf( pBuffer +cntBuf, " 7500K" );
		break;
		case 0x23:
		case 0x80:
			destLCD->factory_id_colorTemp = lcd_id_temp_8500k;
			cntBuf += sprintf( pBuffer +cntBuf, " 8500K" );
		break;
		default :
			destLCD->factory_id_colorTemp = lcd_id_unknown;
			cntBuf += sprintf( pBuffer +cntBuf, " 7500?K" );
		break;				
	} // end switch
	cntBuf += sprintf( pBuffer +cntBuf, "(%X)", checkValue );
	elvss_Minus10 = (checkValue == 0x4E);

	checkValue = srcBuffer[2];
	if( destLCD->factory_id_colorTemp == lcd_id_temp_7500k && checkValue > 0 )
	{
		destLCD->factory_id_elvssType = lcd_id_elvss_each;
		destLCD->eachElvss_value = (checkValue & 0x3F);

		if( elvss_Minus10 )	destLCD->eachElvss_vector = -10;
		else destLCD->eachElvss_vector = 0;
		cntBuf += sprintf( pBuffer +cntBuf, " Elvss:%x_%d", destLCD->eachElvss_value, destLCD->eachElvss_vector );
	}
	else
	{
		destLCD->factory_id_elvssType = lcd_id_elvss_normal;
		cntBuf += sprintf( pBuffer +cntBuf, " DElvss" );
	} // end switch
	cntBuf += sprintf( pBuffer +cntBuf, "(%X)", checkValue);

	DPRINT( "%s\n", pBuffer );
}



void read_reg( char srcReg, int srcLength, char* destBuffer, const int isUseMutex )
{
	const int	one_read_size = 4;
	const int	loop_limit = 16;

	static char read_reg[2] = {0xFF, 0x00}; // first byte = read-register
	static struct dsi_cmd_desc s6e8aa0_read_reg_cmd = 
	{ DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(read_reg), read_reg};

	static char packet_size[] = { 0x04, 0 }; // first byte is size of Register
	static struct dsi_cmd_desc s6e8aa0_packet_size_cmd = 
	{ DTYPE_MAX_PKTSIZE, 1, 0, 0, 0, sizeof(packet_size), packet_size};

	static char reg_read_pos[] = { 0xB0, 0x00 }; // second byte is Read-position
	static struct dsi_cmd_desc s6e8aa0_read_pos_cmd = 
	{ DTYPE_DCS_WRITE1, 1, 0, 0, 0, sizeof(reg_read_pos), reg_read_pos};

	int read_pos;
	int readed_size;
	int show_cnt;

	int i,j;
	char	show_buffer[256];
	int	show_buffer_pos = 0;

	read_reg[0] = srcReg;

	show_buffer_pos += sprintf( show_buffer, "read_reg : %X[%d] : ", srcReg, srcLength );

	read_pos = 0;
	readed_size = 0;
	if( isUseMutex ) mutex_lock(&(s6e8aa0_lcd.lock));

//	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x14000000); // lp

	packet_size[0] = (char) srcLength;
	mipi_dsi_cmds_tx(pMFD, &s6e8aa0_tx_buf, &(s6e8aa0_packet_size_cmd),	1);

	show_cnt = 0;
	for( j = 0; j < loop_limit; j ++ )
	{
		reg_read_pos[1] = read_pos;
		if( mipi_dsi_cmds_tx(pMFD, &s6e8aa0_tx_buf, &(s6e8aa0_read_pos_cmd), 1) < 1 ) {
			show_buffer_pos += sprintf( show_buffer +show_buffer_pos, "Tx command FAILED" );
			break;
		}

		mipi_dsi_buf_init(&s6e8aa0_rx_buf);
		readed_size = mipi_dsi_cmds_rx(pMFD, &s6e8aa0_tx_buf, &s6e8aa0_rx_buf, &s6e8aa0_read_reg_cmd, one_read_size);
		for( i=0; i < readed_size; i++, show_cnt++) 
		{
			show_buffer_pos += sprintf( show_buffer +show_buffer_pos, "%02x ", s6e8aa0_rx_buf.data[i] );
			if( destBuffer != NULL && show_cnt < srcLength ) {
				destBuffer[show_cnt] = s6e8aa0_rx_buf.data[i];
			} // end if
		} // end for
		show_buffer_pos += sprintf( show_buffer +show_buffer_pos, "." );
		read_pos += readed_size;
		if( read_pos > srcLength ) break;
	} // end for
//	MIPI_OUTP(MIPI_DSI_BASE + 0x38, 0x10000000); // hs
	if( isUseMutex ) mutex_unlock(&(s6e8aa0_lcd.lock));
	if( j == loop_limit ) show_buffer_pos += sprintf( show_buffer +show_buffer_pos, "Overrun" );

	DPRINT( "%s\n", show_buffer );
	
}


static int lcd_on_seq(struct msm_fb_data_type *mfd)
{
	int	txCnt;
	int	txAmount;
	int	ret = 0;
	volatile int	isFailed_TX = FALSE;
	unsigned int	acl_level;

	int i, j;
	char pBuffer[256];
	
#if 1
	// in result of test, gamma update can update before gammaTableCommand of LCD-ON-sequence.
	// if update gamma after gammaTableCommand cannot be update to lcd
	// if update gamma after displayOnCommand cannot be update lcd.
	// so gammaTableCommand is moved to ahead of displayOnCommand.

	mutex_lock(&(s6e8aa0_lcd.lock));

	if( s6e8aa0_lcd.lcd_state.initialized )	{
		DPRINT("%s : Already ON -> return 1\n", __func__ );
		ret = 1;
	}
	else {
		DPRINT("%s +\n", __func__);

		txAmount = ARRAY_SIZE(s6e8aa0_display_on_before_read_id);
		txCnt = mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, s6e8aa0_display_on_before_read_id,
				txAmount );    
		if( txAmount != txCnt )	
		{
			isFailed_TX = TRUE;
			goto failed_tx_lcd_on_seq;
		}

		if( !s6e8aa0_lcd.isSmartDimming_loaded )
		{
			read_reg( MTP_REGISTER, MTP_DATA_SIZE, lcd_mtp_data, FALSE );
			for( i=0, j=0; i < MTP_DATA_SIZE; i++ ) if(lcd_mtp_data[i]==0) j++;
			if( j > MTP_DATA_SIZE/2 )
			{
				DPRINT( "MTP Load FAIL\n" );
				s6e8aa0_lcd.isSmartDimming_loaded = FALSE;
				isFAIL_mtp_load = TRUE;

#ifdef SmartDimming_useSampleValue
				for( i = 0; i < MTP_DATA_SIZE; i++ )	 lcd_mtp_data[i] = lcd_mtp_data_default[i];
				s6e8aa0_lcd.isSmartDimming_loaded = TRUE;
#endif 				
			} 
			else {
				sprintf( pBuffer, "mtp :" );
				for( i = 0; i < MTP_DATA_SIZE; i++ )	sprintf( pBuffer+strlen(pBuffer), " %02x", lcd_mtp_data[i] );
				DPRINT( "%s\n", pBuffer );

				s6e8aa0_lcd.isSmartDimming_loaded = TRUE;
			}
		}

		if( s6e8aa0_lcd.factory_id_line == lcd_id_unknown )
		{
			read_reg( 0xD1, LCD_ID_BUFFER_SIZE, s6e8aa0_lcd.lcd_id_buffer, FALSE );
			update_id( s6e8aa0_lcd.lcd_id_buffer, &s6e8aa0_lcd );
			update_LCD_SEQ_by_id( &s6e8aa0_lcd );
		}

		txAmount = ARRAY_SIZE(s6e8aa0_display_on_before_gamma_cmds);
		txCnt = mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, s6e8aa0_display_on_before_gamma_cmds,
				txAmount );    

		if( txAmount != txCnt )	
		{
			isFailed_TX = TRUE;
			goto failed_tx_lcd_on_seq;
		}

		// update stored-gamma value to gammaTable

		if( s6e8aa0_lcd.isSmartDimming )
		{
			lcd_gamma_smartDimming_apply( mfd, s6e8aa0_lcd.stored_gamma );
		} 
		else {
			lcd_gamma_apply( mfd, s6e8aa0_lcd.stored_gamma );
		}
		s6e8aa0_lcd.lcd_gamma = s6e8aa0_lcd.stored_gamma;
		
		// set elvss
		lcd_apply_elvss(mfd, s6e8aa0_lcd.stored_elvss, &s6e8aa0_lcd);
		s6e8aa0_lcd.lcd_elvss = s6e8aa0_lcd.stored_elvss;

		// set acl 
		if( s6e8aa0_lcd.enabled_acl ) acl_level	= s6e8aa0_lcd.stored_acl;
		else acl_level = 0;
		lcd_apply_acl( mfd, acl_level, TRUE );
		s6e8aa0_lcd.lcd_acl = acl_level; 

		mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, s6e8aa0_display_on_after_gamma_cmds,
				ARRAY_SIZE(s6e8aa0_display_on_after_gamma_cmds));    

		s6e8aa0_lcd.lcd_state.initialized = TRUE;
		s6e8aa0_lcd.lcd_state.display_on = TRUE;
		s6e8aa0_lcd.lcd_state.powered_up = TRUE;

		DPRINT("%s - : gamma=%d, ACl=%d, Elvss=%d\n", __func__, s6e8aa0_lcd.lcd_gamma, s6e8aa0_lcd.lcd_acl, s6e8aa0_lcd.lcd_elvss );

failed_tx_lcd_on_seq:
		if( isFailed_TX ) {
			s6e8aa0_lcd.factory_id_line = lcd_id_unknown;
			DPRINT("%s : tx FAILED\n", __func__ );
		}
	}
	mutex_unlock(&(s6e8aa0_lcd.lock));
#else // regal process
	mutex_lock(&(s6e8aa0_lcd.lock));
    // lcd_panel_init
	mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, s6e8aa0_display_on_cmds,
			ARRAY_SIZE(s6e8aa0_display_on_cmds));    
	s6e8aa0_lcd.lcd_state.initialized = TRUE;
	s6e8aa0_lcd.lcd_state.display_on = TRUE;
	s6e8aa0_lcd.lcd_state.powered_up = TRUE;
	mutex_unlock(&(s6e8aa0_lcd.lock));
#endif 

	return ret;
}

static int lcd_on(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;

	DPRINT("%s\n", __func__);

	mfd = platform_get_drvdata(pdev);
	if (!mfd)
		return -ENODEV;
	if (mfd->key != MFD_KEY)
		return -EINVAL;
	pMFD = mfd;

	lcd_on_seq( mfd );
//	readCmd(mfd, DTYPE_GEN_READ, 0x05, 0x00, 8, 1 );
//	readCmd(mfd, DTYPE_GEN_READ, 0x0A, 0x00, 8, 1 );
//	readCmd(mfd, DTYPE_GEN_READ, 0x0D, 0x00, 8, 1 );
//	readCmd(mfd, DTYPE_GEN_READ, 0x0E, 0x00, 8, 1 );
	return 0;
}


static int lcd_off_seq(struct msm_fb_data_type *mfd)
{
	if( !s6e8aa0_lcd.lcd_state.initialized )
	{
		DPRINT("%s : Already OFF -> return 1\n", __func__ );
		return 1;
	}

	DPRINT("%s\n", __func__);

	mutex_lock(&(s6e8aa0_lcd.lock));
	mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, s6e8aa0_display_off_cmds,
			ARRAY_SIZE(s6e8aa0_display_off_cmds));
	s6e8aa0_lcd.lcd_state.display_on = FALSE;
	s6e8aa0_lcd.lcd_state.initialized = FALSE;
	s6e8aa0_lcd.lcd_state.powered_up = FALSE;
	mutex_unlock(&(s6e8aa0_lcd.lock));

	//gpio_set_value(GPIO_S6E8AA0_RESET, 0);
	//msleep(120);

	return 0;
}


static int lcd_off(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;

#ifdef CONFIG_HAS_EARLYSUSPEND
	DPRINT("%s : to be early_suspend. return.\n", __func__);
	return 0;
#endif 

    DPRINT("%s +\n", __func__);

	mfd = platform_get_drvdata(pdev);
	if (!mfd)
		return -ENODEV;
	if (mfd->key != MFD_KEY)
		return -EINVAL;
	pMFD = mfd;

	lcd_off_seq(mfd);
	DPRINT("%s -\n", __func__);
	return 0;
}

static void lcd_gamma_apply( struct msm_fb_data_type *mfd, int srcGamma)
{
	mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, s6e8aa0_lcd.lcd_brightness_table[srcGamma].cmd,	1);
	mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, &s6e8aa0_gamma_update_cmd, 1);
}

static void lcd_gamma_smartDimming_apply( struct msm_fb_data_type *mfd, int srcGamma)
{
	int gamma_lux;
	char pBuffer[256];
	int i;

	gamma_lux = s6e8aa0_lcd.lcd_brightness_table[srcGamma].lux;
	if( gamma_lux > SmartDimming_CANDELA_UPPER_LIMIT ) gamma_lux = SmartDimming_CANDELA_UPPER_LIMIT;

	for( i = SmartDimming_GammaUpdate_Pos; i < sizeof(GAMMA_SmartDimming_COND_SET); i++ ) 	GAMMA_SmartDimming_COND_SET[i] = 0;
	calc_gamma_table(&(s6e8aa0_lcd.smart), gamma_lux, GAMMA_SmartDimming_COND_SET +SmartDimming_GammaUpdate_Pos);

#ifdef SmartDimming_16bitBugFix
	if( gamma_lux > SmartDimming_CANDELA_UPPER_LIMIT /2 )
	{
		if( (int)GAMMA_SmartDimming_COND_SET[21] < 0x70 && GAMMA_SmartDimming_COND_SET[20] == 0 ) GAMMA_SmartDimming_COND_SET[20] =1;
		if( (int)GAMMA_SmartDimming_COND_SET[23] < 0x70 && GAMMA_SmartDimming_COND_SET[22] == 0 ) GAMMA_SmartDimming_COND_SET[22] =1;
		if( (int)GAMMA_SmartDimming_COND_SET[25] < 0x70 && GAMMA_SmartDimming_COND_SET[24] == 0 ) GAMMA_SmartDimming_COND_SET[24] =1;
	}
	pBuffer[0] = 0;
	for( i =0; i < sizeof(GAMMA_SmartDimming_COND_SET); i++ )
	{
		sprintf( pBuffer+ strlen(pBuffer), " %02x", GAMMA_SmartDimming_COND_SET[i] );
	}
	DPRINT( "SD: %03d %s\n", gamma_lux, pBuffer );
#endif 	

	mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, &DSI_CMD_SmartDimming_GAMMA,	1);
	DPRINT( "SmartDimming : gamma %d = lux %d\n", srcGamma, gamma_lux );
}


static void lcd_gamma_ctl(struct msm_fb_data_type *mfd, struct lcd_setting *lcd)
{
	int gamma_level;

	gamma_level = s6e8aa0_lcd.stored_gamma;
	if( s6e8aa0_lcd.lcd_brightness_table[gamma_level].lux != s6e8aa0_lcd.lcd_brightness_table[s6e8aa0_lcd.lcd_gamma].lux )
	{

		if( s6e8aa0_lcd.isSmartDimming )
		{
			mutex_lock(&(s6e8aa0_lcd.lock));
			lcd_gamma_smartDimming_apply( mfd, gamma_level );
			mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, &s6e8aa0_gamma_update_cmd, 1);
			mutex_unlock(&(s6e8aa0_lcd.lock));
		} 
		else {
			mutex_lock(&(s6e8aa0_lcd.lock));
			lcd_gamma_apply( mfd, gamma_level );
			mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, &s6e8aa0_gamma_update_cmd, 1);
			mutex_unlock(&(s6e8aa0_lcd.lock));
		}

		s6e8aa0_lcd.lcd_gamma = gamma_level;
		DPRINT("lcd_gamma_ctl %d -> %d(lcd) (%s,%dcd)\n", gamma_level, s6e8aa0_lcd.lcd_gamma, 
			s6e8aa0_lcd.lcd_brightness_table[s6e8aa0_lcd.lcd_gamma].strID,
			s6e8aa0_lcd.lcd_brightness_table[s6e8aa0_lcd.lcd_gamma].lux );
    	}
    	else
    	{
		s6e8aa0_lcd.lcd_gamma = gamma_level;
//		DPRINT("lcd_gamma_ctl %d -> %d(lcd)\n", gamma_level, s6e8aa0_lcd.lcd_gamma );
    	}
	
	return;

}

static void lcd_set_brightness(struct msm_fb_data_type *mfd, int gamma_level)
{
	//TODO: lock
       //spin_lock_irqsave(&bl_ctrl_lock, irqflags);

       s6e8aa0_lcd.stored_gamma = gamma_level;
       s6e8aa0_lcd.stored_elvss= gamma_level;
       s6e8aa0_lcd.stored_acl = gamma_level;

	lcd_gamma_ctl(mfd, &s6e8aa0_lcd);	

	lcd_set_elvss(mfd, &s6e8aa0_lcd);
	
	lcd_set_acl(mfd, &s6e8aa0_lcd);
	
#if !defined(CONFIG_HAS_EARLYSUSPEND)
	if (s6e8aa0_lcd.lcd_gamma <= LCD_OFF_GAMMA_VALUE) {
		lcd_off_seq( mfd );	// brightness = 0 -> LCD Off 
	}
#endif 

	//TODO: unlock
	//spin_unlock_irqrestore(&bl_ctrl_lock, irqflags);

}


#define DIM_BL 20
#define MIN_BL 30
#define MAX_BL 255

static int get_gamma_value_from_bl(int bl_value )// same as Seine, Celox 
{
	int gamma_value =0;
	int gamma_val_x10 =0;

	if(bl_value >= MIN_BL){
		gamma_val_x10 = 10 *(MAX_GAMMA_VALUE-1)*bl_value/(MAX_BL-MIN_BL) + (10 - 10*(MAX_GAMMA_VALUE-1)*(MIN_BL)/(MAX_BL-MIN_BL));
		gamma_value=(gamma_val_x10 +5)/10;
	}else{
		gamma_value =0;
	}

	return gamma_value;
}

static void set_backlight(struct msm_fb_data_type *mfd)
{	
	int bl_level = mfd->bl_level;
	int gamma_level;
	static int pre_bl_level = 0;

	// brightness tuning
	gamma_level = get_gamma_value_from_bl(bl_level);

	if ((s6e8aa0_lcd.lcd_state.initialized) 
		&& (s6e8aa0_lcd.lcd_state.powered_up) 
		&& (s6e8aa0_lcd.lcd_state.display_on))
	{
		if(s6e8aa0_lcd.lcd_gamma != gamma_level)
		{
	       	DPRINT("brightness!!! %d(bl)->gamma=%d stored=%d\n",bl_level,gamma_level,s6e8aa0_lcd.stored_gamma);
			lcd_set_brightness(mfd, gamma_level);
		}
		else
		{
		       s6e8aa0_lcd.stored_gamma = gamma_level;
		       s6e8aa0_lcd.stored_elvss = gamma_level;
			lcd_set_brightness(mfd, gamma_level);
//		       DPRINT("brightness(Same) bl_level=%d, gamma=%d curr=%d lcd.stored_gamma=%d\n",bl_level,gamma_level,s6e8aa0_lcd.lcd_gamma, s6e8aa0_lcd.stored_gamma);
		}
	}
	else
	{
	       s6e8aa0_lcd.stored_gamma = gamma_level;
	       s6e8aa0_lcd.stored_elvss = gamma_level;
       	DPRINT("brightness(Ignore_OFF) %d(bl)->gamma=%d stored=%d\n",bl_level,gamma_level,s6e8aa0_lcd.stored_gamma);
	}

	pre_bl_level = bl_level;

}


#define GET_NORMAL_ELVSS_ID(i) (lcd_elvss_table[i].cmd->payload[GET_NORMAL_ELVSS_ID_ADDRESS])
#define GET_EACH_ELVSS_ID(i) (lcd_elvss_delta_table[i])
static void lcd_apply_elvss(struct msm_fb_data_type *mfd, int srcElvss, struct lcd_setting *lcd)
{
	int	calc_elvss;
	char	update_elvss;

	if( lcd->factory_id_elvssType == lcd_id_elvss_each )
	{
			calc_elvss = s6e8aa0_lcd.eachElvss_value+ s6e8aa0_lcd.eachElvss_vector +GET_EACH_ELVSS_ID(srcElvss);
			if( calc_elvss > LCD_ELVSS_RESULT_LIMIT ) calc_elvss = LCD_ELVSS_RESULT_LIMIT;
			update_elvss = (char) calc_elvss | 0x80; //0x80 = indentity of elvss;
			EACH_ELVSS_COND_SET[EACH_ELVSS_COND_UPDATE_POS] = update_elvss;
		
			mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, &(lcd_each_elvss_table[0]), 1);
			// elvss is always ON, It's not need update-cmd.

			s6e8aa0_lcd.lcd_elvss = srcElvss;
			DPRINT("lcd_set_elvss %d->%d(lcd) (%x+%x:%x)\n", srcElvss, s6e8aa0_lcd.lcd_elvss, 
				s6e8aa0_lcd.eachElvss_value, GET_EACH_ELVSS_ID(srcElvss), update_elvss);
	}

	if( lcd->factory_id_elvssType == lcd_id_elvss_normal )
	{
			mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, lcd_elvss_table[srcElvss].cmd,	1);
			// elvss is always ON, It's not need update-cmd.

			s6e8aa0_lcd.lcd_elvss = srcElvss;
			DPRINT("lcd_set_elvss %d->%d(lcd) %x\n", srcElvss, s6e8aa0_lcd.lcd_elvss,
				GET_NORMAL_ELVSS_ID(srcElvss));
	}
}

static void lcd_set_elvss(struct msm_fb_data_type *mfd, struct lcd_setting *lcd)
{
	int elvss_level;


	elvss_level = s6e8aa0_lcd.stored_elvss; // gamma level 
	if( lcd->factory_id_elvssType == lcd_id_elvss_each )
	{
		if( GET_EACH_ELVSS_ID(s6e8aa0_lcd.lcd_elvss) == GET_EACH_ELVSS_ID(elvss_level) )
		{
			s6e8aa0_lcd.lcd_elvss = s6e8aa0_lcd.stored_elvss;
		}
		else
		{
			mutex_lock(&(s6e8aa0_lcd.lock));
			lcd_apply_elvss( mfd, elvss_level, &s6e8aa0_lcd );
			mutex_unlock(&(s6e8aa0_lcd.lock));

		}
	}

	if( lcd->factory_id_elvssType == lcd_id_elvss_normal )
	{
		if(GET_NORMAL_ELVSS_ID(s6e8aa0_lcd.lcd_elvss) == GET_NORMAL_ELVSS_ID(elvss_level) ) // if same as LCD
		{
			s6e8aa0_lcd.lcd_elvss = s6e8aa0_lcd.stored_elvss;
		}
		else
		{
			mutex_lock(&(s6e8aa0_lcd.lock));
			lcd_apply_elvss( mfd, elvss_level, &s6e8aa0_lcd );
			// elvss is always ON, It's not need update-cmd.
			mutex_unlock(&(s6e8aa0_lcd.lock));

			s6e8aa0_lcd.lcd_elvss = s6e8aa0_lcd.stored_elvss;
		}
	}
}




static void lcd_apply_acl(struct msm_fb_data_type *mfd, unsigned int srcAcl, boolean nowOffNeedOn)
{
#if 1 // Q1 : Not ACL-OFF by brightness
	if( s6e8aa0_lcd.lcd_acl_table[srcAcl].lux == 0 )
	{
		mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, &s6e8aa0_acl_off_cmd, 1);
		DPRINT( "lcd_set_acl : OFF : %s,%d\n", s6e8aa0_lcd.lcd_acl_table[srcAcl].strID, s6e8aa0_lcd.lcd_acl_table[srcAcl].lux);
	}
	else
#endif 	
	{
		mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, s6e8aa0_lcd.lcd_acl_table[srcAcl].cmd, 1);
		if( nowOffNeedOn || s6e8aa0_lcd.lcd_acl_table[s6e8aa0_lcd.lcd_acl].lux == 0 )
		{
			mipi_dsi_cmds_tx(mfd, &s6e8aa0_tx_buf, &s6e8aa0_acl_on_cmd, 1);
			DPRINT( "lcd_set_acl : ON : %s,%d\n", s6e8aa0_lcd.lcd_acl_table[srcAcl].strID, s6e8aa0_lcd.lcd_acl_table[srcAcl].lux);
		}
		else
		{
			DPRINT( "lcd_set_acl : %s,%d\n", s6e8aa0_lcd.lcd_acl_table[srcAcl].strID, s6e8aa0_lcd.lcd_acl_table[srcAcl].lux);
		}
	}
}

static void lcd_set_acl(struct msm_fb_data_type *mfd, struct lcd_setting *lcd)
{
	unsigned int acl_level;

	if( s6e8aa0_lcd.enabled_acl )	acl_level = s6e8aa0_lcd.stored_acl;
	else acl_level = 0;

	if( s6e8aa0_lcd.lcd_acl_table[s6e8aa0_lcd.lcd_acl].lux == s6e8aa0_lcd.lcd_acl_table[acl_level].lux) // if same as LCD
	{
		s6e8aa0_lcd.lcd_acl = acl_level;
	}
	else
	{
		mutex_lock(&(s6e8aa0_lcd.lock));
		lcd_apply_acl(mfd, acl_level, FALSE );
		// acl is always ON, It's not need update-cmd.
		mutex_unlock(&(s6e8aa0_lcd.lock));

		s6e8aa0_lcd.lcd_acl = acl_level;
	}
}


/////////////////[
struct class *pwm_backlight_class;
struct device *pwm_backlight_dev;

static ssize_t acl_set_show(struct device *dev, struct 
device_attribute *attr, char *buf)
{
//	struct ld9040 *lcd = dev_get_drvdata(dev);
	char temp[3];

	sprintf(temp, "%d\n", s6e8aa0_lcd.enabled_acl);
	strcpy(buf, temp);

	return strlen(buf);
}

static ssize_t acl_set_store(struct device *dev, struct 
device_attribute *attr, const char *buf, size_t size)
{
	int value;
	int rc;
	struct msm_fb_data_type *mfd;
	mfd = dev_get_drvdata(dev);

	rc = strict_strtoul(buf, (unsigned int) 0, (unsigned long *)&value);
	if (rc < 0) return rc;
	else {
		if (s6e8aa0_lcd.enabled_acl != value) {
			s6e8aa0_lcd.enabled_acl = value;
			//			if (lcd->ldi_enable)
			{         
				DPRINT("acl_set_store(changed) : %d\n", value);	
				lcd_set_acl(pMFD, &s6e8aa0_lcd);    // Arimy to make func
			}
		}
		return 0;
	}
}

static DEVICE_ATTR(acl_set, 0664,
		acl_set_show, acl_set_store);

static ssize_t lcdtype_show(struct device *dev, struct 
device_attribute *attr, char *buf)
{

	char temp[15];
	sprintf(temp, "SMD_AMS465GS02\n");
	strcat(buf, temp);
	return strlen(buf);
}

static DEVICE_ATTR(lcdtype, 0664,
		lcdtype_show, NULL);

static ssize_t octa_lcdtype_show(struct device *dev, struct 
device_attribute *attr, char *buf)
{
	char pBuffer[64] = {0,};
	char cntBuffer;

	cntBuffer = 0;
	cntBuffer += sprintf(pBuffer +cntBuffer, "HD720");

	switch(s6e8aa0_lcd.factory_id_line )
	{
	case lcd_id_a1_m3_line: 
		cntBuffer += sprintf(pBuffer +cntBuffer, " A1M3");
	break;
	
	case lcd_id_a1_sm2_line: 
		cntBuffer += sprintf(pBuffer +cntBuffer, " A1SM2");
	break;
	
	case lcd_id_a2_line: 
		cntBuffer += sprintf(pBuffer +cntBuffer, " A2");
	break;

	case lcd_id_unknown: 
		cntBuffer += sprintf(pBuffer +cntBuffer, " A?");
	break;
	} // end switch

	switch(s6e8aa0_lcd.factory_id_colorTemp )
	{
	case lcd_id_temp_7500k: 
		cntBuffer += sprintf(pBuffer +cntBuffer, " 7500K");
	break;
	
	case lcd_id_temp_8500k: 
		cntBuffer += sprintf(pBuffer +cntBuffer, " 8500K");
	break;

	case lcd_id_unknown: 
		cntBuffer += sprintf(pBuffer +cntBuffer, " 7500?K");
	break;
	} // end switch

	switch(s6e8aa0_lcd.factory_id_elvssType)
	{
	case lcd_id_elvss_normal:
		cntBuffer += sprintf(pBuffer +cntBuffer, " elvss");
	break;
	
	case lcd_id_elvss_each:
		cntBuffer += sprintf(pBuffer +cntBuffer, " IElvss(%x)", s6e8aa0_lcd.eachElvss_value);
	break;

	case lcd_id_unknown: 
		cntBuffer += sprintf(pBuffer +cntBuffer, " ?elvss");
	break;
	} // end switch

#ifdef SmartDimming_useSampleValue
	if( isFAIL_mtp_load )
	{
		if(s6e8aa0_lcd.isSmartDimming)
			cntBuffer += sprintf(pBuffer +cntBuffer, " SMARTDIM");
	} else 				
#endif 				
	if(s6e8aa0_lcd.isSmartDimming)
		cntBuffer += sprintf(pBuffer +cntBuffer, " SmartDim");


	strcpy( buf, pBuffer );
	
	return strlen(buf);
}

static DEVICE_ATTR(octa_lcdtype, 0664,
		octa_lcdtype_show, NULL);

static ssize_t lcd_sysfs_show_gamma_mode(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
//	struct lcd_setting *plcd = dev_get_drvdata(dev);
	char temp[10];

	switch (s6e8aa0_lcd.gamma_mode) {
	case 0:
		sprintf(temp, "2.2 mode\n");
		strcat(buf, temp);
		break;
	case 1:
		sprintf(temp, "1.9 mode\n");
		strcat(buf, temp);
		break;
	default:
		dev_info(dev, "gamma mode could be 0:2.2, 1:1.9 or 2:1.7)n");
		break;
	}

	return strlen(buf);
}

static ssize_t lcd_sysfs_store_gamma_mode(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct lcd_setting *plcd = dev_get_drvdata(dev);
	int rc;

	dev_info(dev, "lcd_sysfs_store_gamma_mode\n");

	rc = strict_strtoul(buf, 0, (unsigned long *)&plcd->gamma_mode);
	if (rc < 0)
		return rc;
	
//	if (lcd->ldi_enable)
//		ld9040_gamma_ctl(lcd);  Arimy to make func

	return len;
}

static DEVICE_ATTR(gamma_mode, 0664,
		lcd_sysfs_show_gamma_mode, lcd_sysfs_store_gamma_mode);

static ssize_t lcd_sysfs_show_gamma_table(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
//	struct lcd_setting *plcd = dev_get_drvdata(dev);
	char temp[3];

	sprintf(temp, "%d\n", s6e8aa0_lcd.gamma_table_count);
	strcpy(buf, temp);

	return strlen(buf);
}

static DEVICE_ATTR(gamma_table, 0664,
		lcd_sysfs_show_gamma_table, NULL);


static int lcd_power(struct msm_fb_data_type *mfd, struct lcd_setting *plcd, int power)
{
	int ret = 0;
	
    if(power == FB_BLANK_UNBLANK)
    {
		if(s6e8aa0_lcd.lcd_state.display_on == TRUE)
			return ret;

    		// LCD ON
    		DPRINT("%s : LCD_ON\n", __func__);
    		lcd_on_seq(mfd);
    }
    else if(power == FB_BLANK_POWERDOWN)
    {
        if (s6e8aa0_lcd.lcd_state.powered_up && s6e8aa0_lcd.lcd_state.display_on) {
			
		// LCD OFF
    		DPRINT("%s : LCD_OFF\n", __func__);
		lcd_off_seq(mfd);
	    }
    }

	return ret;
}


static ssize_t lcd_sysfs_store_lcd_power(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	int rc;
	int lcd_enable;
//	struct lcd_setting *plcd = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd;
	mfd = dev_get_drvdata(dev);
	
	dev_info(dev, "lcd_sysfs_store_lcd_power\n");

	rc = strict_strtoul(buf, 0, (unsigned long *)&lcd_enable);
	if (rc < 0)
		return rc;

	if(lcd_enable) {
		lcd_power(mfd, &s6e8aa0_lcd, FB_BLANK_UNBLANK);
	}
	else {
		lcd_power(mfd, &s6e8aa0_lcd, FB_BLANK_POWERDOWN);
	}

	return len;
}

static DEVICE_ATTR(lcd_power, 0664,
		NULL, lcd_sysfs_store_lcd_power);

/////////////////]



#ifdef CONFIG_HAS_EARLYSUSPEND
static void early_suspend(struct early_suspend *h) 
{
	DPRINT("%s +\n", __func__);
	lcd_off_seq(pMFD);
	DPRINT("%s -\n", __func__);

 return;
}

static void late_resume(struct early_suspend *h) 
{
	DPRINT("%s\n", __func__);
	lcd_on_seq(pMFD);
	return;
}
#endif



static int __devinit lcd_probe(struct platform_device *pdev)
{
	int ret = 0;
	int default_gamma_value;

	if (pdev->id == 0) {
		pdata = pdev->dev.platform_data;
		DPRINT("%s\n", __func__);
		return 0;
	}

	DPRINT("%s+ \n", __func__);

	pMFD = platform_get_drvdata(pdev);

	// get default-gamma address from gamma table
	for( default_gamma_value = 0; default_gamma_value <= MAX_GAMMA_VALUE; default_gamma_value++)
		if( lcd_brightness_7500k_table[default_gamma_value].lux >= DEFAULT_CANDELA_VALUE ) break;

	s6e8aa0_lcd.stored_gamma = default_gamma_value;
	s6e8aa0_lcd.stored_elvss = default_gamma_value;
	s6e8aa0_lcd.lcd_gamma = -1;
	s6e8aa0_lcd.enabled_acl = 1;
	s6e8aa0_lcd.stored_acl= default_gamma_value;
	s6e8aa0_lcd.lcd_acl = -1;
	s6e8aa0_lcd.lcd_state.display_on = false;
	s6e8aa0_lcd.lcd_state.initialized= false;
	s6e8aa0_lcd.lcd_state.powered_up= false;
	s6e8aa0_lcd.factory_id_line	= lcd_id_unknown;
	s6e8aa0_lcd.factory_id_colorTemp	= lcd_id_unknown;
	s6e8aa0_lcd.factory_id_elvssType	= lcd_id_unknown;
	s6e8aa0_lcd.lcd_brightness_table = lcd_brightness_7500k_table;
	s6e8aa0_lcd.lcd_acl_table = lcd_acl_const40_table;
	s6e8aa0_lcd.eachElvss_value = 0;
	s6e8aa0_lcd.eachElvss_vector = 0;
	s6e8aa0_lcd.isSmartDimming = FALSE;
	s6e8aa0_lcd.isSmartDimming_loaded = FALSE;	
	mutex_init( &(s6e8aa0_lcd.lock) );

#ifdef CONFIG_HAS_EARLYSUSPEND
	 s6e8aa0_lcd.early_suspend.suspend = early_suspend;
	 s6e8aa0_lcd.early_suspend.resume = late_resume;
	 s6e8aa0_lcd.early_suspend.level = LCD_OFF_GAMMA_VALUE;
	 register_early_suspend(&s6e8aa0_lcd.early_suspend);
#endif

	DPRINT("msm_fb_add_device +\n");
	msm_fb_add_device(pdev);
	DPRINT("msm_fb_add_device -\n");

/////////////[ sysfs
    pwm_backlight_class = class_create(THIS_MODULE, "pwm-backlight");
	if (IS_ERR(pwm_backlight_class))
		pr_err("Failed to create class(pwm_backlight)!\n");

	pwm_backlight_dev = device_create(pwm_backlight_class,
		NULL, 0, NULL, "device");
	if (IS_ERR(pwm_backlight_dev))
		pr_err("Failed to create device(pwm_backlight_dev)!\n");

	ret = device_create_file(pwm_backlight_dev, &dev_attr_acl_set);
	if (ret < 0)
		dev_err(&(pdev->dev), "failed to add sysfs entries\n");

	ret = device_create_file(pwm_backlight_dev, &dev_attr_lcdtype);  
	if (ret < 0)
		dev_err(&(pdev->dev), "failed to add sysfs entries\n");

	ret = device_create_file(pwm_backlight_dev, &dev_attr_octa_lcdtype);  
	if (ret < 0)
		DPRINT("octa_lcdtype failed to add sysfs entries\n");
//		dev_err(&(pdev->dev), "failed to add sysfs entries\n");

	ret = device_create_file(pwm_backlight_dev, &dev_attr_lcd_power);  
	if (ret < 0)
		DPRINT("lcd_power failed to add sysfs entries\n");
//		dev_err(&(pdev->dev), "failed to add sysfs entries\n");

	// mdnie sysfs create
	init_mdnie_class();
////////////]	
	DPRINT("%s-\n", __func__ );

	return 0;
}

static struct platform_driver this_driver = {
	.probe  = lcd_probe,
	.driver = {
		.name   = "mipi_lcd_hd720",
	},
};

static struct msm_fb_panel_data s6e8aa0_panel_data = {
	.on		= lcd_on,
	.off		= lcd_off,
	.set_backlight = set_backlight,
};

static int ch_used[3];

int mipi_s6e8aa0_hd720_device_register(struct msm_panel_info *pinfo,
					u32 channel, u32 panel)
{
	struct platform_device *pdev = NULL;
	int ret;

	if ((channel >= 3) || ch_used[channel])
		return -ENODEV;

	ch_used[channel] = TRUE;

	pdev = platform_device_alloc("mipi_lcd_hd720", (panel << 8)|channel);
	if (!pdev)
		return -ENOMEM;

	s6e8aa0_panel_data.panel_info = *pinfo;

	ret = platform_device_add_data(pdev, &s6e8aa0_panel_data,
		sizeof(s6e8aa0_panel_data));
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

static int __init lcd_init(void)
{
	DPRINT("%s \n", __func__);
	mipi_dsi_buf_alloc(&s6e8aa0_tx_buf, DSI_BUF_SIZE);
	mipi_dsi_buf_alloc(&s6e8aa0_rx_buf, DSI_BUF_SIZE);

	return platform_driver_register(&this_driver);
}

module_init(lcd_init);
