/* Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Code Aurora Forum nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * Alternatively, provided that this notice is retained in full, this software
 * may be relicensed by the recipient under the terms of the GNU General Public
 * License version 2 ("GPL") and only version 2, in which case the provisions of
 * the GPL apply INSTEAD OF those given above.  If the recipient relicenses the
 * software under the GPL, then the identification text in the MODULE_LICENSE
 * macro must be changed to reflect "GPLv2" instead of "Dual BSD/GPL".  Once a
 * recipient changes the license terms to the GPL, subsequent recipients shall
 * not relicense under alternate licensing terms, including the BSD or dual
 * BSD/GPL terms.  In addition, the following license statement immediately
 * below and between the words START and END shall also then apply when this
 * software is relicensed under the GPL:
 *
 * START
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 and only version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * END
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/delay.h>
#include <linux/mfd/pmic8058.h>
#include <mach/gpio.h>
#include "msm_fb.h"
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/miscdevice.h>
#include <lcdc_ld9040_seq.h>
#include <lcdc_ea8868_seq.h>
#include <mdp4_video_enhance.h>

#define LCDC_DEBUG

//#define LCDC_19GAMMA_ENABLE

#ifdef LCDC_DEBUG
#define DPRINT(x...)	printk("ld9040 " x)
#else
#define DPRINT(x...)	
#endif

#define PMIC_GPIO_MLCD_RESET	PM8058_GPIO(17)  /* PMIC GPIO Number 17 */

/*
 * Serial Interface
 */
#define LCD_CSX_HIGH	gpio_set_value(spi_cs, 1);
#define LCD_CSX_LOW	gpio_set_value(spi_cs, 0);

#define LCD_SCL_HIGH	gpio_set_value(spi_sclk, 1);
#define LCD_SCL_LOW		gpio_set_value(spi_sclk, 0);

#define LCD_SDI_HIGH	gpio_set_value(spi_sdi, 1);
#define LCD_SDI_LOW		gpio_set_value(spi_sdi, 0);

#define DEFAULT_USLEEP	1	

// brightness tuning
#define MAX_BRIGHTNESS_LEVEL 255
#define LOW_BRIGHTNESS_LEVEL 20
#define MAX_BACKLIGHT_VALUE 27 
#define LOW_BACKLIGHT_VALUE 1
#define DIM_BACKLIGHT_VALUE 0
#define DFT_BACKLIGHT_VALUE 10
#define BRIGHTNESS_LEVEL_DIVIDER 9
// static DEFINE_SPINLOCK(bl_ctrl_lock);


#define POWER_IS_ON(pwr)	((pwr) <= FB_BLANK_NORMAL)

//
struct ld9040 {
	struct device			*dev;
	struct spi_device		*spi;
	unsigned int			power;
	unsigned int			gamma_mode;
	unsigned int			current_brightness;
	unsigned int			gamma_table_count;
	unsigned int			bl;
	unsigned int			beforepower;
	unsigned int			ldi_enable;
	unsigned int 			acl_enable;
	unsigned int 			cur_acl;
	struct mutex	lock;
	struct lcd_device		*ld;
	struct backlight_device		*bd;
	struct lcd_platform_data	*lcd_pd;
	struct early_suspend    early_suspend;
};

static struct ld9040 lcd;
//

int isEA8868 = 0;
int isEA8868_M3 = 0;

#if defined (CONFIG_KOR_MODEL_SHV_E110S) || defined (CONFIG_JPN_MODEL_SC_03D) \
 || defined (CONFIG_USA_MODEL_SGH_T989) || defined (CONFIG_USA_MODEL_SGH_I727)
extern unsigned int get_hw_rev();
#endif




static struct setting_table gamma_update[] = {
        // Gamma Update
        { 0xFB, 2,
                { 0x02, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
        0 },
};

static struct setting_table ea8868_gamma_update_enable[] = {
        // Gamma Update
        { 0xFB, 2,
                { 0x00, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
        0 },
};

static struct setting_table ea8868_gamma_update_disable[] = {
        // Gamma Update
        { 0xFB, 2,
                { 0x00, 0xA5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
        0 },
};

#if defined(CONFIG_USA_MODEL_SGH_T989) || defined (CONFIG_USA_MODEL_SGH_I727)
static struct setting_table sleep_out_display[] = {
   	// Sleep Out Command
	{ 0x11,	0, 
	  	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	
};
static struct setting_table display_on[] = {
	// Display On Command
	{ 0x29,	0, 
	  	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	
};

static struct setting_table power_auto_sequence_control[] =
{
{ 0xF5,	1, 
  	{ 0x04, 0x21, 0x22, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
0 },	
};
#define POWER_AUTO_SEQ	(int)(sizeof(power_auto_sequence_control)/sizeof(struct setting_table))

#else
static struct setting_table sleep_out_display[] = {
   	// Sleep Out Command
	{ 0x11,	0, 
	  	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	130 },	
};
static struct setting_table display_on[] = {
	// Display On Command
	{ 0x29,	0, 
	  	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	10 },	
};

#endif


#define SLEEP_OUT_SEQ	(int)(sizeof(sleep_out_display)/sizeof(struct setting_table))
#define DISPLAY_ON_SEQ	(int)(sizeof(display_on)/sizeof(struct setting_table))

static struct setting_table power_on_sequence_ea8868[] = {
	//[1] Level 2 Command Access
	{ 0xF0,	2, 
	  	{ 0x5A, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },

	//[2] Panel Condition Set
	// 1) Display Control Set
	{ 0xF2,	5, 
		{ 0x02, 0x08, 0x08, 0x28, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },		

	// 2) GTCON Control Set
	{ 0xF7,	3 ,
		{ 0x09, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	

	// 3) LTPS Timing Set
	{ 0xF8,	23, 
		{ 0x05, 0x6D, 0xA4, 0x7A, 0x89, 0x0E, 0x45, 0x00, 0x00, 0x37, 0x00, 0xF7, 0x00, 0x00, 0x00, 0x00,
	  	    0x07, 0x06, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },

  	//[3] Dynamic ELVSS Set
	{ 0xB1,	3, 
		{ 0x0F, 0x00, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	
	
	{ 0xB2,	4, 
		{ 0x14, 0x14, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	

  	//[5] Display Condition Set
	{ 0xF1,	2, 
		{ 0x5A, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	

	{ 0xF4,	5, 
		{ 0xAB, 0x6A, 0x00, 0x55, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	


};

static struct setting_table power_on_sequence_2[] = {
	//[1] Level 2 Command Access
	{ 0xF0,	2, 
	  	{ 0x5A, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },

	//[2] Panel Condition Set
	{ 0xF8,	23, 
		{ 0x05, 0x5E, 0x96, 0x6B, 0x7D, 0x0D, 0x3F, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x07, 0x07, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },
	
	//[3] Display Condition Set
	// 1) Display Control Set
	{ 0xF2,	5, 
		{ 0x02, 0x06, 0x0A, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },		
	//2) GTCON Control Set
	{ 0xF7,	3 ,
		{ 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	

  	//[4] Dynamic ELVSS Set
	{ 0xB1,	3, 
		{ 0x0D, 0x00, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	
	
	{ 0xB2,	4, 
		{ 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	

};


// For 4.5"
static struct setting_table power_on_sequence[] = {
	//[1] Level 2 Command Access
	{ 0xF0,	2, 
	  	{ 0x5A, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },

	//[2] Panel Condition Set
	{ 0xF8,	23, 
		{ 0x05, 0x5E, 0x96, 0x6B, 0x7D, 0x0D, 0x3F, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x07, 0x07, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },

	//[3] Display Condition Set
	// 1) Display Control Set
	{ 0xF2,	5, 
		{ 0x02, 0x06, 0x0A, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },		

	//2) GTCON Control Set
	{ 0xF7,	3 ,
		{ 0x09, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	

  	//[4] Dynamic ELVSS Set
	{ 0xB1,	3, 
		{ 0x0F, 0x00, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	
	
	{ 0xB2,	4, 
		{ 0x14, 0x14, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	
};

static struct setting_table power_on_sequence_S6E63M0[] = {
/* Panel Condition Set Command*/
	{ 0xF8,	14, 
	  	{ 0x01, 0x27, 0x27, 0x07, 0x07, 0x54, 0x9F, 0x63, 0x86, 0x1A, 0x33, 0x0D, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },

	// 1) Display Control Set
	{ 0xF2,	5, 
		{ 0x02, 0x03, 0x1C, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },		

	// 2) GTCON Control Set
	{ 0xF7,	3 ,
		{ 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	
	// 3) Gamma condtion Set
{ 0xFA,	22,
		{ 0x02, 0x18, 0x08, 0x24, 0x70, 0x6E, 0x4E, 0xBC, 0xC0, 0xAF, 0xB3, 0xB8, 0xA5, 0xC5, 0xC7, 0xBB,
	  	    0x00, 0xB9, 0x00, 0xB8, 0x00, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },
		// 3) Gamma condtion Set Update
{ 0xFA,	1,
		{ 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },		

{ 0xF6,	3,
		{ 0x00, 0x8E, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },		
{ 0xB3,	1,
		{ 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },		
{ 0xB5,	32,
		{ 0x2C, 0x12, 0x0C, 0x0A, 0x10, 0x0E, 0x17, 0x13, 0x1F, 0x1A, 0x2A, 0x24, 0x1F, 0x1B, 0x1A, 0x17,
	  	    0x2B, 0x26, 0x22, 0x20, 0x3A, 0x34, 0x30, 0x2C, 0x29, 0x26, 0x25, 0x23, 0x21, 0x20, 0x1E, 0x1E,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },		
{ 0xb6,	16,
		{ 0x00, 0x00, 0x11, 0x22, 0x33, 0x44, 0x44, 0x44, 0x55, 0x55, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },		
{ 0xb7,	32,
		{ 0x2C, 0x12, 0x0C, 0x0A, 0x10, 0x0E, 0x17, 0x13, 0x1F, 0x1A, 0x2A, 0x24, 0x1F, 0x1B, 0x1A, 0x17,
	  	    0x2B, 0x26, 0x22, 0x20, 0x3A, 0x34, 0x30, 0x2C, 0x29, 0x26, 0x25, 0x23, 0x21, 0x20, 0x1E, 0x1E,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },		
{ 0xB8,	16,
		{ 0x00, 0x00, 0x11, 0x22, 0x33, 0x44, 0x44, 0x44, 0x55, 0x55, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },		
{ 0xb9,     32,
		{ 0x2C, 0x12, 0x0C, 0x0A, 0x10, 0x0E, 0x17, 0x13, 0x1F, 0x1A, 0x2A, 0x24, 0x1F, 0x1B, 0x1A, 0x17,
	  	    0x2B, 0x26, 0x22, 0x20, 0x3A, 0x34, 0x30, 0x2C, 0x29, 0x26, 0x25, 0x23, 0x21, 0x20, 0x1E, 0x1E,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },		
{ 0xbA,	16,
		{ 0x00, 0x00, 0x11, 0x22, 0x33, 0x44, 0x44, 0x44, 0x55, 0x55, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },		
{ 0x11,	0,
		{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	120 },		
	{ 0x29,	0, 
	  	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },		
	
 
};



#define POWER_ON_SEQ	(int)(sizeof(power_on_sequence)/sizeof(struct setting_table))
#define POWER_ON_SEQ_2	(int)(sizeof(power_on_sequence_2)/sizeof(struct setting_table))
#define POWER_ON_SEQ_EA8868	(int)(sizeof(power_on_sequence_ea8868)/sizeof(struct setting_table))
#define POWER_ON_SEQ_S6E63M0	(int)(sizeof(power_on_sequence_S6E63M0)/sizeof(struct setting_table))

static struct setting_table power_off_sequence[] = {
	// Display Off Command
	{ 0x28,	0, 
	  	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	0 },	

	// Sleep In Command
	{ 0x10,	0, 
	  	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,},
	130 },
};
#define POWER_OFF_SEQ	(int)(sizeof(power_off_sequence)/sizeof(struct setting_table))


static int lcdc_ld9040_panel_off(struct platform_device *pdev);

//static int flag_gammaupdate = 0;

static int spi_cs;
static int spi_sclk;
//static int spi_sdo;
static int spi_sdi;
//static int spi_dac;
static int lcd_reset;

static int delayed_backlight_value = -1;
static void lcdc_ld9040_set_brightness(int level);
static void ld9040_set_acl(struct ld9040 *lcd);
static void ld9040_set_elvss(struct ld9040 *lcd);

struct ld9040_state_type{
	boolean disp_initialized;
	boolean display_on;
	boolean disp_powered_up;
};

volatile struct ld9040_state_type ld9040_state = { 0 };
static struct msm_panel_common_pdata *lcdc_ld9040_pdata;

/* LCD DATA LINE TEST FUNCTIONS------------- START*/
#define CONFIG_LD9040_DATA_LINE_TEST
#if defined (CONFIG_LD9040_DATA_LINE_TEST)
static void lcdtool_write_byte(boolean dc, u8 data)
{
	uint32 bit;
	int bnum;

	LCD_SCL_LOW
	if(dc)
		LCD_SDI_HIGH
	else
		LCD_SDI_LOW
	udelay(1);			/* at least 20 ns */
	LCD_SCL_HIGH	/* clk high */
	udelay(1);			/* at least 20 ns */

	bnum = 8;			/* 8 data bits */
	bit = 0x80;
	while (bnum--) {
		LCD_SCL_LOW /* clk low */
		if(data & bit)
			LCD_SDI_HIGH
		else
			LCD_SDI_LOW
		udelay(1);
		LCD_SCL_HIGH /* clk high */
		udelay(1);
		bit >>= 1;
	}
	LCD_SDI_LOW

}

static void lcdtool_read_bytes(u8 cmd, u8 *data, int num)
{
	int bnum;

	/* Chip Select - low */
	LCD_CSX_LOW
	udelay(2);

	/* command byte first */
	lcdtool_write_byte(0, cmd);
	udelay(2);

	gpio_direction_input(spi_sdi);

	if (num > 1) {
		/* extra dummy clock */
		LCD_SCL_LOW
		udelay(1);
		LCD_SCL_HIGH
		udelay(1);
	}

	/* followed by data bytes */
	bnum = num * 8;	/* number of bits */
	*data = 0;
	while (bnum) {
		LCD_SCL_LOW /* clk low */
		udelay(1);
		*data <<= 1;
		*data |= gpio_get_value(spi_sdi) ? 1 : 0;
		LCD_SCL_HIGH /* clk high */
		udelay(1);
		--bnum;
		if ((bnum % 8) == 0)
			++data;
	}

	gpio_direction_output(spi_sdi, 0);

	/* Chip Select - high */
	udelay(2);
	LCD_CSX_HIGH
}

static ssize_t lcdtool_read(struct file *fp, char __user *buf,
	size_t count, loff_t *pos)
{
	int r = count;
	u8 data;
	udelay(10);
	/* 0x06: Read Red from first pixel */
	lcdtool_read_bytes(0x06, &data, 1);
	DPRINT("%s: RED = %x\n", __func__, data);
	buf[2] = data;
	udelay(10);
	/* 0x07: Read Green from first pixel */
	lcdtool_read_bytes(0x07, &data, 1);
	DPRINT("%s: GREEN = %x\n", __func__, data);
	buf[1] = data;
	udelay(10);
	/* 0x08: Read Blue from first pixel */
	lcdtool_read_bytes(0x08, &data, 1);
	DPRINT("%s: BLUE = %x\n", __func__, data);
	buf[0] = data;
	return r;
}

static const struct file_operations lcdtestfops = {
	.owner = THIS_MODULE,
	.read = lcdtool_read,
};

static struct miscdevice lcdtest_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lcdtest",
	.fops = &lcdtestfops,
};
#endif
/* LCD DATA LINE TEST FUNCTIONS------------- END*/

static void setting_table_write(struct setting_table *table)
{
	long i, j;
	
mutex_lock(&lcd.lock);

	LCD_CSX_HIGH
	udelay(DEFAULT_USLEEP);
	LCD_SCL_HIGH 
	udelay(DEFAULT_USLEEP);

	/* Write Command */
	LCD_CSX_LOW
	udelay(DEFAULT_USLEEP);
	LCD_SCL_LOW 
	udelay(DEFAULT_USLEEP);		
	LCD_SDI_LOW 
	udelay(DEFAULT_USLEEP);
	
	LCD_SCL_HIGH 
	udelay(DEFAULT_USLEEP); 

   	for (i = 7; i >= 0; i--) { 
		LCD_SCL_LOW
		udelay(DEFAULT_USLEEP);
		if ((table->command >> i) & 0x1)
			LCD_SDI_HIGH
		else
			LCD_SDI_LOW
		udelay(DEFAULT_USLEEP);	
		LCD_SCL_HIGH
		udelay(DEFAULT_USLEEP);	
	}

	LCD_CSX_HIGH
	udelay(DEFAULT_USLEEP);	

	/* Write Parameter */
	if ((table->parameters) > 0) {
		for (j = 0; j < table->parameters; j++) {
			LCD_CSX_LOW 
			udelay(DEFAULT_USLEEP); 	
			
			LCD_SCL_LOW 
			udelay(DEFAULT_USLEEP); 	
			LCD_SDI_HIGH 
			udelay(DEFAULT_USLEEP);
			LCD_SCL_HIGH 
			udelay(DEFAULT_USLEEP); 	

			for (i = 7; i >= 0; i--) { 
				LCD_SCL_LOW
				udelay(DEFAULT_USLEEP);	
				if ((table->parameter[j] >> i) & 0x1)
					LCD_SDI_HIGH
				else
					LCD_SDI_LOW
				udelay(DEFAULT_USLEEP);	
				LCD_SCL_HIGH
				udelay(DEFAULT_USLEEP);					
			}

			LCD_CSX_HIGH
			udelay(DEFAULT_USLEEP);	
		}
	}
	
mutex_unlock(&lcd.lock);

	#if defined (CONFIG_USA_MODEL_SGH_T989)
		if(get_hw_rev() == 0x05)
			msleep(table->wait);
		else
			if(table->wait)
				msleep(table->wait);
	#elif defined (CONFIG_USA_MODEL_SGH_I727)
		if((get_hw_rev() == 0x05) ||(get_hw_rev() == 0x06))
			msleep(table->wait);
		else
			if(table->wait)
				msleep(table->wait);
	#elif defined (CONFIG_KOR_MODEL_SHV_E110S) 
		if(table->wait)
			msleep(table->wait);
	#else	
		msleep(table->wait);
	#endif
		
}


static void spi_read_id(u8 cmd, u8 *data, int num)
{
	int bnum;

	/* Chip Select - low */
	LCD_CSX_LOW
	udelay(2);

	/* command byte first */
	lcdtool_write_byte(0, cmd);
	udelay(2);

	gpio_direction_input(spi_sdi);

	if (num > 1) {
		/* extra dummy clock */
		LCD_SCL_LOW
		udelay(1);
		LCD_SCL_HIGH
		udelay(1);
	}

	/* followed by data bytes */
	bnum = num * 8;	/* number of bits */
	*data = 0;
	while (bnum) {
		LCD_SCL_LOW /* clk low */
		udelay(1);
		*data <<= 1;
		*data |= gpio_get_value(spi_sdi) ? 1 : 0;
		LCD_SCL_HIGH /* clk high */
		udelay(1);
		--bnum;
	}
//	DPRINT("ld9040 Dummy id 0x%x\n", *data);
	bnum = num * 8;	/* number of bits */
	*data = 0;
	while (bnum) {
		LCD_SCL_LOW /* clk low */
		udelay(1);
		*data <<= 1;
		*data |= gpio_get_value(spi_sdi) ? 1 : 0;
		LCD_SCL_HIGH /* clk high */
		udelay(1);
		--bnum;
	}
//	DPRINT("ld9040 id 0x%x\n", *data);
	
	gpio_direction_output(spi_sdi, 0);

	/* Chip Select - high */
	udelay(2);
	LCD_CSX_HIGH
}
static void spi_init(void)
{
        DPRINT("start %s\n", __func__);	
	/* Setting the Default GPIO's */
	spi_sclk = *(lcdc_ld9040_pdata->gpio_num);
	spi_cs   = *(lcdc_ld9040_pdata->gpio_num + 1);
	spi_sdi  = *(lcdc_ld9040_pdata->gpio_num + 2);
	DPRINT("clk : %d, cs : %d, sdi : %d\n", spi_sclk,spi_cs , spi_sdi);
#if 0 
#if (CONFIG_BOARD_REVISION == 0x00)
	lcd_reset= PM8058_GPIO_PM_TO_SYS(PMIC_GPIO_MLCD_RESET);
#endif
#if (CONFIG_BOARD_REVISION >= 0x01)
	lcd_reset  = *(lcdc_ld9040_pdata->gpio_num + 3);
#endif
#else 
        lcd_reset = 28;
#endif

	#if defined (CONFIG_USA_MODEL_SGH_T989) || defined (CONFIG_USA_MODEL_SGH_I727)
		static int jump_from_boot=0;

		if(!jump_from_boot)
			return;
		else 
			jump_from_boot=1;
	#endif
	
		/* Set the output so that we dont disturb the slave device */
		gpio_set_value(spi_sclk, 0);
		gpio_set_value(spi_sdi, 0);

		/* Set the Chip Select De-asserted */
		gpio_set_value(spi_cs, 0);
	DPRINT("end %s\n", __func__);	

}

static void ld9040_disp_powerup(void)
{
	DPRINT("start %s\n", __func__);	

	if (!ld9040_state.disp_powered_up && !ld9040_state.display_on) {
		/* Reset the hardware first */

		// SPI High
		LCD_CSX_HIGH
		LCD_SCL_HIGH
		//TODO: turn on ldo
		#if 1//defined (CONFIG_USA_MODEL_SGH_T989)
			msleep(2);
		#else
			msleep(50);
			//LCD_RESET_N_HI
			gpio_set_value(lcd_reset, 1);
			msleep(20);
			//LCD_RESET_N_LO
			gpio_set_value(lcd_reset, 0);
			msleep(20);	
		#endif

		//LCD_RESET_N_HI
		gpio_set_value(lcd_reset, 1);
		msleep(20);
		
		/* Include DAC power up implementation here */
		
	    ld9040_state.disp_powered_up = TRUE;
	}
	
}

static void ld9040_disp_powerdown(void)
{
	DPRINT("start %s\n", __func__);	
	
	/* Reset Assert */
	gpio_set_value(lcd_reset, 0);

	// SPI low
	LCD_CSX_LOW
	LCD_SCL_LOW

	/* turn off LDO */
	//TODO: turn off LDO

	ld9040_state.disp_powered_up = FALSE;
	
}

static void ld9040_init(void)
{
	mdelay(1);
}


int ld9040_read_lcd_id(void)
{

	u8 data;
	u8 idcheck[3];

	static int init_lcd_id=0;

	if(init_lcd_id)
	{
		DPRINT("LCD_M3 : %d\n",isEA8868_M3);
		return 0;
	}

	msleep(120);

	gpio_request(spi_sdi, "ld9040_read_lcd_id");	
	udelay(2);
	spi_read_id(0xda, &data, 1);
	idcheck[0] = data;

	spi_read_id(0xdb, &data, 1);
	idcheck[1] = data;	

	spi_read_id(0xdc, &data, 1);
	idcheck[2] = data;	
	
	DPRINT("%s: %x %x %x\n", __func__, idcheck[0], idcheck[1], idcheck[2]);

	if(idcheck[2] == 0x22)
	{
		// EA8868 
		if(idcheck[1] == 0x12)
		{
			DPRINT("lcd EA8868 SM2\n");
			isEA8868_M3 = 0;
		}
		else if(idcheck[1] == 0x03)
		{
			DPRINT("lcd EA8868 M3\n");
			isEA8868_M3 = 1;
		}

	}

	init_lcd_id = 1;

	return 0;
}

void ld9040_disp_on(void)
{
	int i;

	
#if defined (CONFIG_KOR_MODEL_SHV_E110S) || defined (CONFIG_JPN_MODEL_SC_03D) || defined (CONFIG_USA_MODEL_SGH_T989) || defined (CONFIG_USA_MODEL_SGH_I727)
	DPRINT("start %s - HW Rev: %d\n", __func__,get_hw_rev());	
#endif

	if (ld9040_state.disp_powered_up && !ld9040_state.display_on) {
		#if 0//!defined (CONFIG_USA_MODEL_SGH_T989)
			ld9040_init();
			mdelay(20);
		#endif
		
		/* ld9040 setting */
#if 1//defined (CONFIG_FB_MSM_S3C_TL2796)
	if (ld9040_state.disp_powered_up && !ld9040_state.display_on) {
				for (i = 0; i < POWER_ON_SEQ_S6E63M0; i++)
					setting_table_write(&power_on_sequence_S6E63M0[i]);	
		}
		
#elif defined (CONFIG_KOR_MODEL_SHV_E110S)
		if(isEA8868)
		{
			// For EA8868
			for (i = 0; i < POWER_ON_SEQ_EA8868; i++)
				setting_table_write(&power_on_sequence_ea8868[i]);
			ld9040_read_lcd_id();
		}
		else
		{
			// For LD9040
			if (get_hw_rev() ==0x01 ) 
			{
				for (i = 0; i < POWER_ON_SEQ_2; i++)
					setting_table_write(&power_on_sequence_2[i]);			
			}
			else
			{
				for (i = 0; i < POWER_ON_SEQ; i++)
					setting_table_write(&power_on_sequence[i]);			
			}
		}
#elif  defined (CONFIG_JPN_MODEL_SC_03D) 
		if(isEA8868)
		{
			// For EA8868
			for (i = 0; i < POWER_ON_SEQ_EA8868; i++)
				setting_table_write(&power_on_sequence_ea8868[i]);
			ld9040_read_lcd_id();
		}
		else
		{
			// For LD9040
			for (i = 0; i < POWER_ON_SEQ; i++)
				setting_table_write(&power_on_sequence[i]); 		
		}
#elif defined (CONFIG_USA_MODEL_SGH_I727) || defined (CONFIG_USA_MODEL_SGH_T989)
		if(isEA8868)
		{
			// For EA8868
			for (i = 0; i < POWER_ON_SEQ_EA8868; i++)
				setting_table_write(&power_on_sequence_ea8868[i]);
		}
		else
		{

			for (i = 0; i < POWER_ON_SEQ; i++)
				setting_table_write(&power_on_sequence[i]);			
		}
#else
        {
			for (i = 0; i < POWER_ON_SEQ_2; i++)
				setting_table_write(&power_on_sequence_2[i]);			
		}
#endif
#if 1//defined (CONFIG_FB_MSM_S3C_TL2796)
		// Sleep Out
		for (i = 0; i < SLEEP_OUT_SEQ; i++)
			setting_table_write(&sleep_out_display[i]);
				mdelay(120);
		// Display On
		for (i = 0; i < DISPLAY_ON_SEQ; i++)
			setting_table_write(&display_on[i]);
	#elif defined (CONFIG_USA_MODEL_SGH_T989) || defined (CONFIG_USA_MODEL_SGH_I727)
		ld9040_read_lcd_id();

		for (i = 0; i < POWER_AUTO_SEQ; i++)
			setting_table_write(&power_auto_sequence_control[i]);

		// Gamma Set
		if(lcd.current_brightness <= 0)
		  lcdc_ld9040_set_brightness(DFT_BACKLIGHT_VALUE);
		else
		  lcdc_ld9040_set_brightness(lcd.current_brightness);
		
		// Sleep Out
		for (i = 0; i < SLEEP_OUT_SEQ; i++)
			setting_table_write(&sleep_out_display[i]);

		// Display On
		for (i = 0; i < DISPLAY_ON_SEQ; i++)
			setting_table_write(&display_on[i]);
			
	#else
	    // Gamma Set
	    if(lcd.current_brightness <0)
	        lcdc_ld9040_set_brightness(DFT_BACKLIGHT_VALUE);
	    else
	        lcdc_ld9040_set_brightness(lcd.current_brightness);

	    // Sleep Out
	    for (i = 0; i < SLEEP_OUT_SEQ; i++)
			setting_table_write(&sleep_out_display[i]);
		
	    // Display On
	    for (i = 0; i < DISPLAY_ON_SEQ; i++)
			setting_table_write(&display_on[i]);
			
		mdelay(1);
	#endif
		ld9040_state.display_on = TRUE;
	}
}

void ld9040_sleep_off(void)
{
	int i;

	DPRINT("start %s\n", __func__);	


	for (i = 0; i < POWER_ON_SEQ_2; i++)
				setting_table_write(&power_on_sequence_2[i]);	
	
	mdelay(1);
}

void ld9040_sleep_in(void)
{
	int i;

	DPRINT("start %s\n", __func__); 


		for (i = 0; i < POWER_OFF_SEQ; i++)
			setting_table_write(&power_off_sequence[i]);			

	mdelay(1);
	
}
#if 0 
extern void key_led_control(int on);
#endif

static int lcdc_ld9040_panel_on(struct platform_device *pdev)
{
	DPRINT("%s  +  (%d,%d,%d)\n", __func__, ld9040_state.disp_initialized, ld9040_state.disp_powered_up, ld9040_state.display_on);	
	
	if (!ld9040_state.disp_initialized) {
		/* Configure reset GPIO that drives DAC */
		lcdc_ld9040_pdata->panel_config_gpio(1);
		spi_init();	/* LCD needs SPI */
		ld9040_disp_powerup();
		ld9040_disp_on();
		ld9040_state.disp_initialized = TRUE;
		if(lcd.current_brightness != lcd.bl)
		{
		        lcdc_ld9040_set_brightness(lcd.current_brightness);
		}
//		flag_gammaupdate = 0;
#if 0 
		if ( get_hw_rev() >= 12 ) // TEMP
			key_led_control(1);
#endif			
	}

	DPRINT("%s  -  (%d,%d,%d)\n", __func__,ld9040_state.disp_initialized, ld9040_state.disp_powered_up, ld9040_state.display_on);	

	return 0;
}

static int lcdc_ld9040_panel_off(struct platform_device *pdev)
{
	int i;

	DPRINT("%s +  (%d,%d,%d)\n", __func__,ld9040_state.disp_initialized, ld9040_state.disp_powered_up, ld9040_state.display_on);	

#if 0 
	if ( get_hw_rev() >= 12 )	// TEMP
		key_led_control(0);
#endif		

	if (ld9040_state.disp_powered_up && ld9040_state.display_on) {
		for (i = 0; i < POWER_OFF_SEQ; i++)
			setting_table_write(&power_off_sequence[i]);	
		
		lcdc_ld9040_pdata->panel_config_gpio(0);
		ld9040_state.display_on = FALSE;
		ld9040_state.disp_initialized = FALSE;
		ld9040_disp_powerdown();
//		flag_gammaupdate = 0;
	}
	
	DPRINT("%s -\n", __func__);	
	return 0;
}


static void ld9040_gamma_ctl(struct ld9040 *lcd)
{
	int tune_level = lcd->bl;

	if(isEA8868)
	{
		setting_table_write(&ea8868_gamma_update_enable[0]);

		if (tune_level <= 0) 
	    	{
			if(isEA8868_M3)
			{
#ifdef LCDC_19GAMMA_ENABLE			
				if(lcd->gamma_mode)
					setting_table_write(lcd_ea8868_m3_table_19gamma[0]);
				else
#endif					
					setting_table_write(lcd_ea8868_m3_table_22gamma[0]);
			}
	    		else
				setting_table_write(lcd_ea8868_table_22gamma[0]);
		} 
		else 
		{
			/* keep back light ON */
			if(unlikely(lcd->current_brightness < 0)) {
				lcd->current_brightness = DFT_BACKLIGHT_VALUE;
			}
			if(isEA8868_M3)
			{
#ifdef LCDC_19GAMMA_ENABLE			
				if(lcd->gamma_mode)
					setting_table_write(lcd_ea8868_m3_table_19gamma[tune_level]);
				else
#endif					
					setting_table_write(lcd_ea8868_m3_table_22gamma[tune_level]);
			}
	    		else
				setting_table_write(lcd_ea8868_table_22gamma[tune_level]);
		
//               ld9040_state.display_on = TRUE;
	    }

		lcd->current_brightness = lcd->bl;
		setting_table_write(&ea8868_gamma_update_disable[0]);
DPRINT("ea8868_gamma_ctl %d %d\n", tune_level, lcd->current_brightness );
		return;
	}
	
        if (tune_level <= 0) 
	    {
#if defined (CONFIG_KOR_MODEL_SHV_E110S)
	        if (get_hw_rev() ==0x01 ) 
               	setting_table_write(lcd_brightness_table_2[0]);
		    else
			    setting_table_write(lcd_brightness_table_22gamma[0]);
#elif defined (CONFIG_JPN_MODEL_SC_03D)			
			setting_table_write(lcd_brightness_table_22gamma[0]);
#elif defined (CONFIG_USA_MODEL_SGH_I727) || defined (CONFIG_USA_MODEL_SGH_T989)
			setting_table_write(lcd_brightness_table_22gamma[0]);
#else
            setting_table_write(lcd_brightness_table_2[0]);
#endif
	} 
	else 
	{
		/* keep back light ON */
		if(unlikely(lcd->current_brightness < 0)) {
			lcd->current_brightness = DFT_BACKLIGHT_VALUE;
		}
#if defined (CONFIG_KOR_MODEL_SHV_E110S)
            if (get_hw_rev() ==0x01 ) 
               	setting_table_write(lcd_brightness_table_2[tune_level]);
		    else
			    setting_table_write(lcd_brightness_table_22gamma[tune_level]);
#elif defined (CONFIG_JPN_MODEL_SC_03D)			
			setting_table_write(lcd_brightness_table_22gamma[tune_level]);
#elif defined (CONFIG_USA_MODEL_SGH_I727) || defined (CONFIG_USA_MODEL_SGH_T989)
			    setting_table_write(lcd_brightness_table_22gamma[tune_level]);
#else
            setting_table_write(lcd_brightness_table_2[tune_level]);
#endif
		
//               ld9040_state.display_on = TRUE;
	    }
	lcd->current_brightness = lcd->bl;
DPRINT("%s %d %d\n", __func__,tune_level, lcd->current_brightness );

	//gamma_update
	setting_table_write(&gamma_update[0]);

}

static void lcdc_ld9040_set_brightness(int level)
{
// unsigned long irqflags;
	int tune_level = level;
// int i;
 	// LCD should be turned on prior to backlight
/* 	
	if(ld9040_state.disp_initialized == FALSE && tune_level > 0) {
		delayed_backlight_value = tune_level;
		return;
	}
	else {
		delayed_backlight_value = -1;
	}
*/

	//TODO: lock
       //spin_lock_irqsave(&bl_ctrl_lock, irqflags);

   lcd.bl = tune_level;

	ld9040_set_elvss(&lcd);
	
	ld9040_set_acl(&lcd);
	
	ld9040_gamma_ctl(&lcd);	

	//TODO: unlock
	//spin_unlock_irqrestore(&bl_ctrl_lock, irqflags);

}

#define DIM_BL 20
#define MIN_BL 30
#define MAX_BL 255
#define MAX_GAMMA_VALUE 24

static int get_gamma_value_from_bl(int bl)
{
	int gamma_value =0;
	int gamma_val_x10 =0;

	if(bl >= MIN_BL){
		gamma_val_x10 = 10 *(MAX_GAMMA_VALUE-1)*bl/(MAX_BL-MIN_BL) + (10 - 10*(MAX_GAMMA_VALUE-1)*(MIN_BL)/(MAX_BL-MIN_BL));
		gamma_value=(gamma_val_x10 +5)/10;
	}else{
		gamma_value =0;
	}
	
	return gamma_value;
}
static void lcdc_ld9040_set_backlight(struct msm_fb_data_type *mfd)
{	
	int bl_level = mfd->bl_level;
	int tune_level;
	static int pre_bl_level = 0;

	// brightness tuning
#if 0	
	if(bl_level > LOW_BRIGHTNESS_LEVEL){
		tune_level = (bl_level - LOW_BRIGHTNESS_LEVEL) / BRIGHTNESS_LEVEL_DIVIDER + 1; 
		if(tune_level==2)
			tune_level=1;
	}
	else if(bl_level > 0)
		tune_level = DIM_BACKLIGHT_VALUE;
	else
		tune_level = bl_level;
#else
	tune_level = get_gamma_value_from_bl(bl_level);
#endif

	if(ld9040_state.disp_initialized)
		DPRINT("brightness!!! bl_level=%d, tune_level=%d curr=%d\n",bl_level,tune_level,lcd.current_brightness);
	
#if 1//sspark 
 	if ((ld9040_state.disp_initialized) 
		&& (ld9040_state.disp_powered_up) 
		&& (ld9040_state.display_on))
	{
		if(lcd.current_brightness != tune_level)
		{
			lcdc_ld9040_set_brightness(tune_level);
//			flag_gammaupdate = 1;
		}
//		return;
	}
	else
	{
		lcd.current_brightness = tune_level;
	}
#endif
	// turn on lcd if needed
#if 0
	if(tune_level > 0)	{
		if(!ld9040_state.disp_powered_up)
			ld9040_disp_powerup();
		if(!ld9040_state.display_on)
			ld9040_disp_on();
	}
	lcdc_ld9040_set_brightness(tune_level);
#endif

#if 0
	if(bl_level < 1)
	{
		int i;
		if(flag_gammaupdate)
		{
			if(pre_bl_level > bl_level)
			{
				DPRINT("bl_level < 1, so lcd power off +  (%d,%d)\n",ld9040_state.disp_powered_up, ld9040_state.display_on);
				if (ld9040_state.disp_powered_up && ld9040_state.display_on) {
					for (i = 0; i < POWER_OFF_SEQ; i++)
						setting_table_write(&power_off_sequence[i]);	
					
					lcdc_ld9040_pdata->panel_config_gpio(0);
					ld9040_state.display_on = FALSE;
					ld9040_state.disp_initialized = FALSE;
					ld9040_disp_powerdown();
				}
				DPRINT("bl_level < 1, so lcd power off -\n");			
			}
		}else
		{
			DPRINT("bl_level < 1, BUT Not lcd power off \n");
		}
	}

#endif
	pre_bl_level = bl_level;

}

/////////////////////// sysfs
struct class *pwm_backlight_class;
struct device *pwm_backlight_dev;

static int ld9040_get_brightness(struct backlight_device *bd)
{
	return bd->props.brightness;
}

static int ld9040_set_brightness(struct backlight_device *bd)
{
	int ret = 0, bl = bd->props.brightness;
	struct ld9040 *lcd = bl_get_data(bd);

	if (bl < LOW_BRIGHTNESS_LEVEL ||
		bl > bd->props.max_brightness) {
		dev_err(&bd->dev, "lcd brightness should be %d to %d. now %d\n",
			LOW_BRIGHTNESS_LEVEL, MAX_BRIGHTNESS_LEVEL, bl);
		return -EINVAL;
	}
//#define MIN_BRIGHTNESS		0
//#define MAX_BRIGHTNESS		255

/*	lcd->bl = get_gamma_value_from_bl(bl);  Arimy to make func

	if (lcd->ldi_enable) {
		ret = update_brightness(lcd);
		if (ret < 0) {
			//dev_err(&bd->dev, "skip update brightness. because ld9040 is on suspend state...\n");
			return -EINVAL;
		}
	}
*/
	return ret;
}

static void ld9040_set_elvss(struct ld9040 *lcd)
{
	int ret = 0;
		if(isEA8868)
		{
			switch (lcd->bl) {
			case 0 ... 7: /* 30cd ~ 100cd */
				setting_table_write(SEQ_M3_ELVSS_set[0]);
				break;
			case 8 ... 13: /* 110cd ~ 160cd */
				setting_table_write(SEQ_M3_ELVSS_set[1]);
				break;
			case 14 ... 17: /* 170cd ~ 200cd */
				setting_table_write(SEQ_M3_ELVSS_set[2]);
				break;
			case 18 ... 27: /* 210cd ~ 300cd */
				setting_table_write(SEQ_M3_ELVSS_set[3]);
				break;
			default:
				break;
			}
		}else{
			switch (lcd->bl) {
			case 0 ... 7: /* 30cd ~ 100cd */
				setting_table_write(SEQ_SM2_ELVSS_set[0]);
				break;
			case 8 ... 13: /* 110cd ~ 160cd */
				setting_table_write(SEQ_SM2_ELVSS_set[1]);
				break;
			case 14 ... 17: /* 170cd ~ 200cd */
				setting_table_write(SEQ_SM2_ELVSS_set[2]);
				break;
			case 18 ... 27: /* 210cd ~ 300cd */
				setting_table_write(SEQ_SM2_ELVSS_set[3]);
				break;
			default:
				break;
			}
		}		

}


static void ld9040_set_acl(struct ld9040 *lcd)
{
	int ret = 0;
#if 1
	if (lcd->acl_enable) {
		if(lcd->cur_acl == 0)  {
			if(lcd->bl ==0 || lcd->bl ==1)
				DPRINT("if bl_value is 0 or 1, acl_on skipped\n");
			else
			{
				setting_table_write(ACL_cutoff_set[1]);
				setting_table_write(&SEQ_ACL_ON[0]);
				}
		}
		switch (lcd->bl) {
		case 0 ... 1: /* 30cd ~ 40cd */
			if (lcd->cur_acl != 0) {
				setting_table_write(ACL_cutoff_set[0]);
				DPRINT("ACL_cutoff_set Percentage : off!!\n");
				lcd->cur_acl = 0;
			}
			break;
		case 2 ... 12: /* 70cd ~ 180cd */
			if (lcd->cur_acl != 40) {
				setting_table_write(ACL_cutoff_set[1]);
				setting_table_write(&SEQ_ACL_ON[0]);
				DPRINT("ACL_cutoff_set Percentage : 40!!\n");
				lcd->cur_acl = 40;
			}
			break;
		case 13: /* 190cd */
			if (lcd->cur_acl != 43) {
				setting_table_write(ACL_cutoff_set[2]);
				setting_table_write(&SEQ_ACL_ON[0]);
				DPRINT("ACL_cutoff_set Percentage : 43!!\n");
				lcd->cur_acl = 43;
			}
			break;
		case 14: /* 200cd */
			if (lcd->cur_acl != 45) {
				setting_table_write(ACL_cutoff_set[3]);
				setting_table_write(&SEQ_ACL_ON[0]);
				DPRINT("ACL_cutoff_set Percentage : 45!!\n");
				lcd->cur_acl = 45;
			}
			break;
		case 15: /* 210cd */
			if (lcd->cur_acl != 47) {
				setting_table_write(ACL_cutoff_set[4]);
				setting_table_write(&SEQ_ACL_ON[0]);
				DPRINT("ACL_cutoff_set Percentage : 47!!\n");
				lcd->cur_acl = 47;
			}
			break;
		case 16: /* 220cd */
			if (lcd->cur_acl != 48) {
				setting_table_write(ACL_cutoff_set[5]);
				setting_table_write(&SEQ_ACL_ON[0]);
				DPRINT("ACL_cutoff_set Percentage : 48!!\n");
				lcd->cur_acl = 48;
			}
			break;
		default:
			if (lcd->cur_acl != 50) {
				setting_table_write(ACL_cutoff_set[6]);
				setting_table_write(&SEQ_ACL_ON[0]);
				DPRINT("ACL_cutoff_set Percentage : 50!!\n");
				lcd->cur_acl = 50;
			}
			break;
		}
	}
	else{
			setting_table_write(ACL_cutoff_set[0]);
			lcd->cur_acl = 0;
			DPRINT("ACL_cutoff_set Percentage : off!!\n");
	}

	if (ret) {
		DPRINT("failed to initialize ldi.\n");
		return -EIO;
	}
#endif	
	return ret;
}
static ssize_t acl_set_show(struct device *dev, struct 
device_attribute *attr, char *buf)
{
//	struct ld9040 *lcd = dev_get_drvdata(dev);
	char temp[3];

	sprintf(temp, "%d\n", lcd.acl_enable);
	strcpy(buf, temp);

	return strlen(buf);
}
static ssize_t acl_set_store(struct device *dev, struct 
device_attribute *attr, const char *buf, size_t size)
{
//	struct ld9040 *lcd = dev_get_drvdata(dev);
	int value;
	int rc;
	
	rc = strict_strtoul(buf, (unsigned int) 0, (unsigned long *)&value);
DPRINT("acl_set_store : %d\n", value);	
	if (rc < 0)
		return rc;
	else{
		if (lcd.acl_enable != value) {
			lcd.acl_enable = value;
//			if (lcd->ldi_enable)
            {         
				ld9040_set_acl(&lcd);    // Arimy to make func
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
	sprintf(temp, "SMD_AMS452GP08\n");
	strcat(buf, temp);
	return strlen(buf);
}

static DEVICE_ATTR(lcdtype, 0664,
		lcdtype_show, NULL);

static ssize_t octa_lcdtype_show(struct device *dev, struct 
device_attribute *attr, char *buf)
{
	char temp[15];
	if(isEA8868)
	{
		if(isEA8868_M3)
		{
			sprintf(temp, "OCTA : M3\n");
		}
		else
		{
			sprintf(temp, "OCTA : SM2\n");
		}
	}
	else
	{
		sprintf(temp, "OCTA :: SM2\n");
	}
	strcat(buf, temp);
	return strlen(buf);

}

static DEVICE_ATTR(octa_lcdtype, 0664,
		octa_lcdtype_show, NULL);

static ssize_t ld9040_sysfs_show_gamma_mode(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
//	struct ld9040 *lcd = dev_get_drvdata(dev);
	char temp[10];

	switch (lcd.gamma_mode) {
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

static ssize_t ld9040_sysfs_store_gamma_mode(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
//	struct ld9040 *lcd = dev_get_drvdata(dev);
	int rc;

	dev_info(dev, "ld9040_sysfs_store_gamma_mode\n");

	rc = strict_strtoul(buf, 0, (unsigned long *)&lcd.gamma_mode);
	printk(KERN_ERR "store_gamma_mode (0:2.2, 1:1.9) %d\n", lcd.gamma_mode);
	if (rc < 0)
		return rc;
	
#ifdef LCDC_19GAMMA_ENABLE
	ld9040_gamma_ctl(&lcd);  
#endif
	return len;
}

static DEVICE_ATTR(gamma_mode, 0664,
		ld9040_sysfs_show_gamma_mode, ld9040_sysfs_store_gamma_mode);

static ssize_t ld9040_sysfs_show_gamma_table(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct ld9040 *lcd = dev_get_drvdata(dev);
	char temp[3];

	sprintf(temp, "%d\n", lcd->gamma_table_count);
	strcpy(buf, temp);

	return strlen(buf);
}

static DEVICE_ATTR(gamma_table, 0664,
		ld9040_sysfs_show_gamma_table, NULL);


static int ld9040_power(struct ld9040 *lcd, int power)
{
	int ret = 0;
    int i;

    if(power == FB_BLANK_UNBLANK)
    {
    		DPRINT("ld9040_power : UNBLANK\n");
        /* Configure reset GPIO that drives DAC */
		lcdc_ld9040_pdata->panel_config_gpio(1);
		spi_init();	/* LCD needs SPI */
		ld9040_disp_powerup();
		ld9040_disp_on();
		ld9040_state.disp_initialized = TRUE;
    }
    else if(power == FB_BLANK_POWERDOWN)
    {
    		DPRINT("ld9040_power : POWERDOWN\n");    
        if (ld9040_state.disp_powered_up && ld9040_state.display_on) {
		
        	for (i = 0; i < POWER_OFF_SEQ; i++)
        		setting_table_write(&power_off_sequence[i]);	
        	
        	lcdc_ld9040_pdata->panel_config_gpio(0);
        	ld9040_state.display_on = FALSE;
        	ld9040_state.disp_initialized = FALSE;
        	ld9040_disp_powerdown();
	    }
    }
	
/*
	if (POWER_IS_ON(power) && !POWER_IS_ON(lcd->power))
		ret = ld9040_power_on(lcd);
	else if (!POWER_IS_ON(power) && POWER_IS_ON(lcd->power))
		ret = ld9040_power_off(lcd);

	if (!ret)
		lcd->power = power;
*/
	return ret;
}


static ssize_t ld9040_sysfs_store_lcd_power(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	int rc;
	int lcd_enable;
	struct ld9040 *lcd = dev_get_drvdata(dev);

	dev_info(dev, "ld9040_sysfs_store_lcd_power\n");

	rc = strict_strtoul(buf, 0, (unsigned long *)&lcd_enable);
	if (rc < 0)
		return rc;

	if(lcd_enable) {
		ld9040_power(lcd, FB_BLANK_UNBLANK);
	}
	else {
		ld9040_power(lcd, FB_BLANK_POWERDOWN);
	}

	return len;
}

static DEVICE_ATTR(lcd_power, 0664,
		NULL, ld9040_sysfs_store_lcd_power);

///////////////////////

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ld9040_early_suspend(struct early_suspend *h) {

	int i;
	
	DPRINT("panel off at early_suspend (%d,%d,%d)\n",
			ld9040_state.disp_initialized,
			ld9040_state.disp_powered_up, 
			ld9040_state.display_on);
	
	if (ld9040_state.disp_powered_up && ld9040_state.display_on) {
		for (i = 0; i < POWER_OFF_SEQ; i++)
			setting_table_write(&power_off_sequence[i]);	
		
		lcdc_ld9040_pdata->panel_config_gpio(0);
		ld9040_state.display_on = FALSE;
		ld9040_state.disp_initialized = FALSE;
		ld9040_disp_powerdown();
	}
	
	return;
}

static void ld9040_late_resume(struct early_suspend *h) {

	DPRINT("panel on at late_resume (%d,%d,%d)\n",
			ld9040_state.disp_initialized,
			ld9040_state.disp_powered_up,
			ld9040_state.display_on);	

	if (!ld9040_state.disp_initialized) {
		/* Configure reset GPIO that drives DAC */
		lcdc_ld9040_pdata->panel_config_gpio(1);
		spi_init();	/* LCD needs SPI */
		ld9040_disp_powerup();
		ld9040_disp_on();
		ld9040_state.disp_initialized = TRUE;
//		flag_gammaupdate = 0;
	}

	return;
}
#endif


static int __devinit ld9040_probe(struct platform_device *pdev)
{
	int ret = 0;
    struct device *temp;
    struct msm_fb_data_type *mfd;
	DPRINT("start %s: pdev->name:%s\n", __func__,pdev->name );	

	if (pdev->id == 0) {
		lcdc_ld9040_pdata = pdev->dev.platform_data;
//		return 0;
	}

	mutex_init(&lcd.lock);
	
	DPRINT("msm_fb_add_device START\n");
	msm_fb_add_device(pdev);
	DPRINT("msm_fb_add_device end\n");

///////////// sysfs
    pwm_backlight_class = class_create(THIS_MODULE, "pwm-backlight");
	if (IS_ERR(pwm_backlight_class))
		pr_err("Failed to create class(pwm_backlight)!\n");

	pwm_backlight_dev = device_create(pwm_backlight_class,
		NULL, 0, NULL, "device");
	if (IS_ERR(pwm_backlight_dev))
		pr_err("Failed to create device(pwm_backlight_dev)!\n");

	lcd.bl = DFT_BACKLIGHT_VALUE;
	lcd.current_brightness = lcd.bl;

	lcd.acl_enable = 1;
	lcd.cur_acl = 0;
	
#ifdef LCDC_19GAMMA_ENABLE
    ret = device_create_file(pwm_backlight_dev, &dev_attr_gamma_mode);
	if (ret < 0)
		dev_err(&(pdev->dev), "failed to add sysfs entries\n");
#endif
//	ret = device_create_file(&(pdev->dev), &dev_attr_gamma_table);
//	if (ret < 0)
//		dev_err(&(pdev->dev), "failed to add sysfs entries\n");

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
////////////

#ifdef CONFIG_HAS_EARLYSUSPEND
	lcd.early_suspend.suspend = ld9040_early_suspend;
	lcd.early_suspend.resume = ld9040_late_resume;
	lcd.early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	register_early_suspend(&lcd.early_suspend);
#endif

	return 0;
}

static void ld9040_shutdown(struct platform_device *pdev)
{
	DPRINT("start %s\n", __func__);	

	lcdc_ld9040_panel_off(pdev);
}

static struct platform_driver this_driver = {
	.probe  = ld9040_probe,
	.shutdown	= ld9040_shutdown,
	.driver = {
		.name   = "lcdc_S6E63M0_wvga",
	},
};

static struct msm_fb_panel_data ld9040_panel_data = {
	.on = lcdc_ld9040_panel_on,
	.off = lcdc_ld9040_panel_off,
	.set_backlight = lcdc_ld9040_set_backlight,
};

static struct platform_device this_device = {
	.name   = "lcdc_panel",
	.id	= 1,
	.dev	= {
		.platform_data = &ld9040_panel_data,
	}
};

#define LCDC_FB_XRES	480
#define LCDC_FB_YRES	800
#define LCDC_HPW		2
#define LCDC_HBP		16
#define LCDC_HFP		16
#define LCDC_VPW		2
#define LCDC_VBP		4//6
#define LCDC_VFP		10

#define LCDC_EA8868_HPW		2
#define LCDC_EA8868_HBP		40
#define LCDC_EA8868_HFP		40
#define LCDC_EA8868_VPW		2
#define LCDC_EA8868_VBP		8
#define LCDC_EA8868_VFP		8


#define LCDC_S6E63M0_HPW		2
#define LCDC_S6E63M0_HBP		16
#define LCDC_S6E63M0_HFP		        16
#define LCDC_S6E63M0_VPW		2
#define LCDC_S6E63M0_VBP		1
#define LCDC_S6E63M0_VFP		30

 
static int __init lcdc_ld9040_panel_init(void)
{
	int ret;
	struct msm_panel_info *pinfo;

#ifdef CONFIG_FB_MSM_MDDI_AUTO_DETECT
	if (msm_fb_detect_client("lcdc_ld9040_wvga"))
	{
		printk(KERN_ERR "%s: msm_fb_detect_client failed!\n", __func__); 
		return 0;
	}
#endif
	DPRINT("start %s\n", __func__);	
	
	ret = platform_driver_register(&this_driver);
	if (ret)
	{
		printk(KERN_ERR "%s: platform_driver_register failed! ret=%d\n", __func__, ret); 
		return ret;
	}
        DPRINT("platform_driver_register(&this_driver) is done \n");
	pinfo = &ld9040_panel_data.panel_info;
	pinfo->xres = LCDC_FB_XRES;
	pinfo->yres = LCDC_FB_YRES;
	pinfo->type = LCDC_PANEL;
	pinfo->pdest = DISPLAY_1;
	pinfo->wait_cycle = 0;
	pinfo->bpp = 24;
	pinfo->fb_num = 2;

#if 1

		// for LDI : EA8868
		DPRINT("LDI : S6E63M0, pixelclock 23800000\n");
		pinfo->clk_rate = 23800000;
		isEA8868 = 1;
		pinfo->lcdc.h_back_porch = LCDC_S6E63M0_HBP;
		pinfo->lcdc.h_front_porch = LCDC_S6E63M0_HFP;
		pinfo->lcdc.h_pulse_width = LCDC_S6E63M0_HPW;
		pinfo->lcdc.v_back_porch = LCDC_S6E63M0_VBP;
		pinfo->lcdc.v_front_porch = LCDC_S6E63M0_VFP;
		pinfo->lcdc.v_pulse_width = LCDC_S6E63M0_VPW;
		

#else
	
#if defined (CONFIG_KOR_MODEL_SHV_E110S)
	if (get_hw_rev() < 0x05 ) 
#elif defined (CONFIG_USA_MODEL_SGH_T989)
	if (get_hw_rev() < 0x06 ) 
#elif defined (CONFIG_USA_MODEL_SGH_I727)
	if (get_hw_rev() < 0x07 ) 
#elif defined (CONFIG_JPN_MODEL_SC_03D)
	if (get_hw_rev() < 0x03 ) 
#else
	if (1)
#endif
	{
		// for LDI : LD9040
		DPRINT("LDI : LD9040, pixelclock 25600000\n");
		pinfo->clk_rate = 25600000;//24576000;
		isEA8868 = 0;
		pinfo->lcdc.h_back_porch = LCDC_HBP;
		pinfo->lcdc.h_front_porch = LCDC_HFP;
		pinfo->lcdc.h_pulse_width = LCDC_HPW;
		pinfo->lcdc.v_back_porch = LCDC_VBP;
		pinfo->lcdc.v_front_porch = LCDC_VFP;
		pinfo->lcdc.v_pulse_width = LCDC_VPW;
		
	}
	else
	{
		// for LDI : EA8868
		DPRINT("LDI : EA8868, pixelclock 27400000\n");
		pinfo->clk_rate = 27400000;
		isEA8868 = 1;
		pinfo->lcdc.h_back_porch = LCDC_EA8868_HBP;
		pinfo->lcdc.h_front_porch = LCDC_EA8868_HFP;
		pinfo->lcdc.h_pulse_width = LCDC_EA8868_HPW;
		pinfo->lcdc.v_back_porch = LCDC_EA8868_VBP;
		pinfo->lcdc.v_front_porch = LCDC_EA8868_VFP;
		pinfo->lcdc.v_pulse_width = LCDC_EA8868_VPW;
		
	}
#endif	
	
	pinfo->bl_max = 255;
	pinfo->bl_min = 1;

	pinfo->lcdc.border_clr = 0;     /* blk */
	pinfo->lcdc.underflow_clr = 0x00;//   black   0xff;       /* blue */
	pinfo->lcdc.hsync_skew = 0;

	ret = platform_device_register(&this_device);
	DPRINT("platform_device_register(&this_device) is done \n");
	if (ret)
	{
		printk(KERN_ERR "%s: platform_device_register failed! ret=%d\n", __func__, ret); 
		platform_driver_unregister(&this_driver);
	}
#if defined (CONFIG_LD9040_DATA_LINE_TEST)
	/*register misc device to for LCD data line test tool - mahaboob vali*/
	ret = misc_register(&lcdtest_device);
	if (ret)
		printk(KERN_ERR "%s: misc_register failed! ret=%d\n", __func__, ret);
#endif
	return ret;
}

module_init(lcdc_ld9040_panel_init);


