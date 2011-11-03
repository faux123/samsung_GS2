/*
** =========================================================================
** File:
**     ImmVibeSPI.c
**
** Description: 
**     Device-dependent functions called by Immersion TSP API
**     to control PWM duty cycle, amp enable/disable, save IVT file, etc...
**
** Portions Copyright (c) 2008-2009 Immersion Corporation. All Rights Reserved. 
**
** This file contains Original Code and/or Modifications of Original Code 
** as defined in and that are subject to the GNU Public License v2 - 
** (the 'License'). You may not use this file except in compliance with the 
** License. You should have received a copy of the GNU General Public License 
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or contact 
** TouchSenseSales@immersion.com.
**
** The Original Code and all software distributed under the License are 
** distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER 
** EXPRESS OR IMPLIED, AND IMMERSION HEREBY DISCLAIMS ALL SUCH WARRANTIES, 
** INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS 
** FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. Please see 
** the License for the specific language governing rights and limitations 
** under the License.
** =========================================================================
*/
#include <linux/pwm.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/clk.h>

#include "tspdrv.h"

#include <linux/module.h>
#include <linux/mfd/pmic8058.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>


#define COUNT_OF_MOTOR	0

#ifdef IMMVIBESPIAPI
#undef IMMVIBESPIAPI
#endif
#define IMMVIBESPIAPI static

/*
** This SPI supports only one actuator.
*/
#define NUM_ACTUATORS 1

#define PWM_DUTY_MAX    579 /* 13MHz / (579 + 1) = 22.4kHz */

#if defined(CONFIG_KOR_MODEL_SHV_E110S)
#define FREQ_COUNT		256 
#else
#define FREQ_COUNT		264 /*89284*/
#endif

#define PWM_DEVICE	1

//#define GPD0_TOUT_1		(2 << 4)

struct clk *android_vib_clk; /* sfpb_clk */

extern unsigned int  get_hw_rev();

#define ISA1200_I2C_ADDRESS 0x90 /*0x92 when SADD is high*/
#define SCTRL         (0)     /* 0x0F, System(LDO) Register Group 0*/
#define HCTRL0     (0x30)     /* 0x09 */ /* Haptic Motor Driver Control Register Group 0*/
#define HCTRL1     (0x31)     /* 0x4B */ /* Haptic Motor Driver Control Register Group 1*/
#define HCTRL2     (0x32)     /* 0x00*/ /* Haptic Motor Driver Control Register Group 2*/
#define HCTRL3     (0x33)     /* 0x13 */ /* Haptic Motor Driver Control Register Group 3*/
#define HCTRL4     (0x34)     /* 0x00 */ /* Haptic Motor Driver Control Register Group 4*/
#define HCTRL5     (0x35)     /* 0x6B */ /* Haptic Motor Driver Control Register Group 5*/
#define HCTRL6     (0x36)     /* 0xD6 */ /* Haptic Motor Driver Control Register Group 6*/
#define HCTRL7     (0x37)     /* 0x00 */ /* Haptic Motor Driver Control Register Group 7*/
#define HCTRL8     (0x38)     /* 0x00 */ /* Haptic Motor Driver Control Register Group 8*/
#define HCTRL9     (0x39)     /* 0x40 */ /* Haptic Motor Driver Control Register Group 9*/
#define HCTRLA     (0x3A)     /* 0x2C */ /* Haptic Motor Driver Control Register Group A*/
#define HCTRLB     (0x3B)     /* 0x6B */ /* Haptic Motor Driver Control Register Group B*/
#define HCTRLC     (0x3C)     /* 0xD6 */ /* Haptic Motor Driver Control Register Group C*/
#define HCTRLD     (0x3D)     /* 0x19 */ /* Haptic Motor Driver Control Register Group D*/

#define LDO_VOLTAGE_23V 0x08
#define LDO_VOLTAGE_24V 0x09
#define LDO_VOLTAGE_25V 0x0A
#define LDO_VOLTAGE_26V 0x0B
#define LDO_VOLTAGE_27V 0x0C
#define LDO_VOLTAGE_28V 0x0D
#define LDO_VOLTAGE_29V 0x0E
#define LDO_VOLTAGE_30V 0x0F
#define LDO_VOLTAGE_31V 0x00
#define LDO_VOLTAGE_32V 0x01
#define LDO_VOLTAGE_33V 0x02
#define LDO_VOLTAGE_34V 0x03
#define LDO_VOLTAGE_35V 0x04
#define LDO_VOLTAGE_36V 0x05
#define LDO_VOLTAGE_37V 0x06
#define LDO_VOLTAGE_38V 0x07

#define RETRY_CNT 10
//struct pwm_device	*Immvib_pwm;

static bool g_bAmpEnabled = false;

long int freq_count = FREQ_COUNT;

#ifdef COUNT_OF_MOTOR
extern struct class *sec_class;
struct device *s5p_TSP;
static unsigned int motor_cnt = 0;

static ssize_t vibecnt_show(struct device *dev, struct device_attribute *attr, char *buf)
{	
	unsigned int retcnt=0;
	printk(KERN_DEBUG "vibecnt_show\n");

	retcnt=motor_cnt;
	motor_cnt=0;

	return sprintf(buf, "%d\n", retcnt);

}

static DEVICE_ATTR(vibecnt, S_IRUGO |S_IRUSR, vibecnt_show, NULL);
#endif

int vibe_set_pwm_freq(int nForce)
{
	/* Put the MND counter in reset mode for programming */
	HWIO_OUTM(GP_NS_REG, HWIO_GP_NS_REG_MNCNTR_EN_BMSK, 0);
//	HWIO_OUTM(GP_NS_REG, HWIO_GP_NS_REG_GP_CLK_BRANCH_ENA_BMSK, 1<<HWIO_GP_NS_REG_GP_CLK_BRANCH_ENA_SHFT);	
//	HWIO_OUTM(GP_NS_REG, HWIO_GP_NS_REG_GP_ROOT_ENA_BMSK, 1<<HWIO_GP_NS_REG_GP_ROOT_ENA_SHFT);	
	HWIO_OUTM(GP_NS_REG, HWIO_GP_NS_REG_PRE_DIV_SEL_BMSK, 3 << HWIO_GP_NS_REG_PRE_DIV_SEL_SHFT); /* P: 0 => Freq/1, 1 => Freq/2, 3 => Freq/4 */
	HWIO_OUTM(GP_NS_REG, HWIO_GP_NS_REG_SRC_SEL_BMSK, 0 << HWIO_GP_NS_REG_SRC_SEL_SHFT); /* S : 0 => TXCO(19.2MHz), 1 => Sleep XTAL(32kHz) */
	HWIO_OUTM(GP_NS_REG, HWIO_GP_NS_REG_MNCNTR_MODE_BMSK, 2 << HWIO_GP_NS_REG_MNCNTR_MODE_SHFT); /* Dual-edge mode */
	HWIO_OUTM(GP_MD_REG, HWIO_GP_MD_REG_M_VAL_BMSK, g_nLRA_GP_CLK_M << HWIO_GP_MD_REG_M_VAL_SHFT);
	g_nForce_32 = ((nForce * g_nLRA_GP_CLK_PWM_MUL) >> 8) + g_nLRA_GP_CLK_N;
//	printk("%s, g_nForce_32 : %d\n",__FUNCTION__,g_nForce_32);
	HWIO_OUTM(GP_MD_REG, HWIO_GP_MD_REG_D_VAL_BMSK, ( ~((VibeInt16)g_nForce_32 << 1) ) << HWIO_GP_MD_REG_D_VAL_SHFT);
	HWIO_OUTM(GP_NS_REG, HWIO_GP_NS_REG_GP_N_VAL_BMSK, ~(g_nLRA_GP_CLK_N - g_nLRA_GP_CLK_M) << HWIO_GP_NS_REG_GP_N_VAL_SHFT);
	HWIO_OUTM(GP_NS_REG, HWIO_GP_NS_REG_MNCNTR_EN_BMSK, 1 << HWIO_GP_NS_REG_MNCNTR_EN_SHFT);					/* Enable M/N counter */
//	printk("%x, %x, %x\n",( ~((VibeInt16)g_nForce_32 << 1) ) << HWIO_GP_MD_REG_D_VAL_SHFT,~(g_nLRA_GP_CLK_N - g_nLRA_GP_CLK_M) << HWIO_GP_NS_REG_GP_N_VAL_SHFT,1 << HWIO_GP_NS_REG_MNCNTR_EN_SHFT);

	return VIBE_S_SUCCESS;
}

int vibe_pwm_onoff(u8 onoff)
{
	if(onoff) {
		HWIO_OUTM(GP_NS_REG, HWIO_GP_NS_REG_GP_CLK_BRANCH_ENA_BMSK, 1<<HWIO_GP_NS_REG_GP_CLK_BRANCH_ENA_SHFT);	
		HWIO_OUTM(GP_NS_REG, HWIO_GP_NS_REG_GP_ROOT_ENA_BMSK, 1<<HWIO_GP_NS_REG_GP_ROOT_ENA_SHFT);
	} else {
		HWIO_OUTM(GP_NS_REG, HWIO_GP_NS_REG_GP_CLK_BRANCH_ENA_BMSK, 0<<HWIO_GP_NS_REG_GP_CLK_BRANCH_ENA_SHFT);	
		HWIO_OUTM(GP_NS_REG, HWIO_GP_NS_REG_GP_ROOT_ENA_BMSK, 0<<HWIO_GP_NS_REG_GP_ROOT_ENA_SHFT);
	}
	return VIBE_S_SUCCESS;
}

static int vib_power_onoff(u8 onoff)
{
	int rc;
	struct regulator *l3b;

	printk("%s: start! (%d)\n", __func__, __LINE__);
#if defined (CONFIG_USA_MODEL_SGH_T989) || defined (CONFIG_USA_MODEL_SGH_I727)
	if (get_hw_rev() > 0x04 ) return 0;
    
#elif defined (CONFIG_USA_MODEL_SGH_I717)
    return 0;

#endif
	l3b = regulator_get(NULL, "8901_l3");
	if (IS_ERR(l3b)) {
		rc = PTR_ERR(l3b);
		printk("%s: l3b get failed (%d)\n", __func__, rc);
		return rc;
	}

	if(onoff)	{
		printk("%s: ON! (%d)\n", __func__, __LINE__);

		rc = regulator_set_voltage(l3b, 3300000, 3300000);
		if (rc) {
			printk("%s: l3b set level failed (%d)\n", __func__, rc);
			regulator_put(l3b);
			return rc;
		}

		rc = regulator_enable(l3b);
		if (rc) {
			printk("%s: l3b vreg enable failed (%d)\n", __func__, rc);
			regulator_put(l3b);
			return rc;
		}
	} else {
		printk("%s: OFF! (%d)\n", __func__, __LINE__);
		rc = regulator_disable(l3b);
		if (rc) {
			printk("%s: l3b vreg disable failed (%d)\n", __func__, rc);
			regulator_put(l3b);
			return rc;
		}
	}
	regulator_put(l3b);

	return 0;
}


int vib_isa1200_onoff(u8 onoff)
{
//	printk("%s: start! (%d)\n", __func__, __LINE__);

	if(onoff)	{
#if defined (CONFIG_KOR_MODEL_SHV_E110S)		
		vibrator_write_register(0x30, 0x89);
		vibrator_write_register(0x31, 0x40);
		vibrator_write_register(0x34, 0x19);
		vibrator_write_register(0x35, 0x00);	
		vibrator_write_register(0x36, 0x00);	
#elif defined (CONFIG_KOR_SHV_E120L_HD720)|| defined(CONFIG_KOR_MODEL_SHV_E120S) || defined(CONFIG_KOR_MODEL_SHV_E120K)
		vibrator_write_register(0x30, 0x89);
		vibrator_write_register(0x31, 0x40);
		vibrator_write_register(0x32, 0x00);
		vibrator_write_register(0x33, 0x13);
		vibrator_write_register(0x34, 0x02);
		vibrator_write_register(0x35, 0x00);
		vibrator_write_register(0x36, 0x00);
#elif defined(CONFIG_KOR_MODEL_SHV_E160S) 
		vibrator_write_register(0x30, 0x89);
		vibrator_write_register(0x31, 0x40);
		vibrator_write_register(0x32, 0x00);
		vibrator_write_register(0x33, 0x13);
		vibrator_write_register(0x34, 0x03);
		vibrator_write_register(0x35, 0x00);
		vibrator_write_register(0x36, 0x00);
#elif defined(CONFIG_USA_MODEL_SGH_T989) || defined(CONFIG_USA_MODEL_SGH_I727)
		vibrator_write_register(0x30, 0x89);
		vibrator_write_register(0x31, 0x40);
			#if defined (CONFIG_USA_MODEL_SGH_T989)
			if (get_hw_rev() >= 0x0d)
				vibrator_write_register(0x34, 0x01);
			else				
				vibrator_write_register(0x34, 0x16);
			#else
			vibrator_write_register(0x34, 0x19);
			#endif
		vibrator_write_register(0x35, 0x00);	
		vibrator_write_register(0x36, 0x00);
#elif defined (CONFIG_USA_MODEL_SGH_I717)

		vibrator_write_register(0x30, 0x89);
		vibrator_write_register(0x31, 0x40);
        vibrator_write_register(0x34, 0x19);
        vibrator_write_register(0x35, 0x00);
		vibrator_write_register(0x36, 0x00);
        
#elif defined (CONFIG_JPN_MODEL_SC_03D)
		vibrator_write_register(0x30, 0x89);
		vibrator_write_register(0x31, 0x40);
		vibrator_write_register(0x34, 0x19);
		vibrator_write_register(0x35, 0x00);	
		vibrator_write_register(0x36, 0x00);	
#endif		
	} else {
		vibrator_write_register(0x30, 0x09);
	}
	return 0;
}

/*
** Called to disable amp (disable output force)
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_AmpDisable(VibeUInt8 nActuatorIndex)
{

    if (g_bAmpEnabled)
    {
		g_bAmpEnabled = false;


//		printk("[VIBETONZ] %s \n",__func__);
#ifdef COUNT_OF_MOTOR
		motor_cnt++;
#endif
#if defined (CONFIG_KOR_MODEL_SHV_E110S)		
		if (get_hw_rev() > 0x00){
			vibrator_write_register(0x30, 0x09);			
		} else {
			gpio_set_value(VIB_EN, VIBRATION_OFF);
			gpio_tlmm_config(GPIO_CFG(VIB_PWM,  0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),1);
			gpio_set_value(VIB_PWM, VIBRATION_OFF);
			vib_power_onoff(0);		
		}
#elif defined (CONFIG_KOR_SHV_E120L_HD720) || defined(CONFIG_KOR_MODEL_SHV_E120K) || defined(CONFIG_KOR_MODEL_SHV_E120S) || defined(CONFIG_KOR_MODEL_SHV_E160S)
		vibrator_write_register(0x30, 0x09);
#elif defined (CONFIG_USA_MODEL_SGH_T989) || defined (CONFIG_USA_MODEL_SGH_I727)
		if (get_hw_rev() > 0x04 ){
			vibrator_write_register(0x30, 0x09);
		} else {
			gpio_set_value(VIB_EN, VIBRATION_OFF);
			gpio_tlmm_config(GPIO_CFG(VIB_PWM,  0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),1);
			gpio_set_value(VIB_PWM, VIBRATION_OFF);
			vib_power_onoff(0);		
		}
#elif defined (CONFIG_USA_MODEL_SGH_I717)
		vibrator_write_register(0x30, 0x09);

#elif defined (CONFIG_JPN_MODEL_SC_03D)
		if (get_hw_rev() > 0x00){
			vibrator_write_register(0x30, 0x09);			
		} else {
			gpio_set_value(VIB_EN, VIBRATION_OFF);
			gpio_tlmm_config(GPIO_CFG(VIB_PWM,  0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),1);
			gpio_set_value(VIB_PWM, VIBRATION_OFF);
			vib_power_onoff(0);		
		}
#else
		gpio_set_value(VIB_EN, VIBRATION_OFF);
		gpio_tlmm_config(GPIO_CFG(VIB_PWM,  0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),1);
		gpio_set_value(VIB_PWM, VIBRATION_OFF);
		vib_power_onoff(0);
#endif		
    }

	return VIBE_S_SUCCESS;
}

/*
** Called to enable amp (enable output force)
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_AmpEnable(VibeUInt8 nActuatorIndex)
{

    if (!g_bAmpEnabled)
    {
    		g_bAmpEnabled = true;

//		printk("[VIBETONZ] %s \n",__func__);

#if defined (CONFIG_KOR_MODEL_SHV_E110S)		
		if (get_hw_rev() > 0x00){
			vibrator_write_register(0x30, 0x89);			
		} else {
			gpio_tlmm_config(GPIO_CFG(VIB_PWM,  2, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),1);
			vib_power_onoff(1);
			gpio_set_value(VIB_EN, VIBRATION_ON);			
		}
#elif defined (CONFIG_KOR_SHV_E120L_HD720) || defined(CONFIG_KOR_MODEL_SHV_E120K) || defined(CONFIG_KOR_MODEL_SHV_E120S) || defined(CONFIG_KOR_MODEL_SHV_E160S)
		vibrator_write_register(0x30, 0x89);
#elif defined (CONFIG_USA_MODEL_SGH_T989)|| defined (CONFIG_USA_MODEL_SGH_I727)
		if (get_hw_rev() > 0x04 ){
			vibrator_write_register(0x30, 0x89);
		} else {
			gpio_tlmm_config(GPIO_CFG(VIB_PWM,  2, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),1);
			vib_power_onoff(1);
			gpio_set_value(VIB_EN, VIBRATION_ON);			
		}
#elif defined (CONFIG_USA_MODEL_SGH_I717)

        vibrator_write_register(0x30, 0x89);
        
#elif defined (CONFIG_JPN_MODEL_SC_03D)
		if (get_hw_rev() > 0x00){
			vibrator_write_register(0x30, 0x89);			
		} else {
			gpio_tlmm_config(GPIO_CFG(VIB_PWM,  2, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),1);
			vib_power_onoff(1);
			gpio_set_value(VIB_EN, VIBRATION_ON);			
		}
#else
		gpio_tlmm_config(GPIO_CFG(VIB_PWM,  2, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),1);
		vib_power_onoff(1);
		gpio_set_value(VIB_EN, VIBRATION_ON);		
#endif
    }

    return VIBE_S_SUCCESS;
}

/*
** Called at initialization time to set PWM freq, disable amp, etc...
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_Initialize(void)
{
    int cnt = 0;	
    unsigned char I2C_data[2];
    int ret = VIBE_S_SUCCESS;
	g_bAmpEnabled = true; 

	printk("[VIBETONZ] %s \n",__func__);

#if 0
	Immvib_pwm = pwm_request(PWM_DEVICE, "Immvibtonz");
	pwm_config(Immvib_pwm, freq_count/2, freq_count);
#endif	
#if defined (CONFIG_KOR_MODEL_SHV_E110S)		
	if (get_hw_rev() > 0x00){
	gpio_tlmm_config(GPIO_CFG(VIB_PWM,  2, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),1);
	gpio_set_value(VIB_EN, VIBRATION_ON);	
	}
#elif defined (CONFIG_KOR_MODEL_SHV_E120L) || defined(CONFIG_KOR_MODEL_SHV_E120K) || defined(CONFIG_KOR_MODEL_SHV_E120S) || defined(CONFIG_KOR_MODEL_SHV_E160S)
	gpio_tlmm_config(GPIO_CFG(VIB_PWM,  2, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),1);
	gpio_set_value(VIB_EN, VIBRATION_ON);	
#elif defined (CONFIG_USA_MODEL_SGH_T989)|| defined (CONFIG_USA_MODEL_SGH_I727)
	if (get_hw_rev() > 0x04 ){
	gpio_tlmm_config(GPIO_CFG(VIB_PWM,  2, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),1);
	gpio_set_value(VIB_EN, VIBRATION_ON);	
	}
#elif defined (CONFIG_USA_MODEL_SGH_I717)

    gpio_tlmm_config(GPIO_CFG(VIB_PWM,  2, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),1);
    gpio_set_value(VIB_EN, VIBRATION_ON);
    
#elif defined (CONFIG_JPN_MODEL_SC_03D)
	if (get_hw_rev() >= 0x01 ){
	gpio_tlmm_config(GPIO_CFG(VIB_PWM,	2, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),1);
	gpio_set_value(VIB_EN, VIBRATION_ON);	
	}
#endif	

	msleep(1);
	
#if defined (CONFIG_KOR_MODEL_SHV_E110S)
	if (get_hw_rev() > 0x00){	
		vibrator_write_register(0x30, 0x09);
		vibrator_write_register(0x31, 0x40);
		vibrator_write_register(0x34, 0x19);
		vibrator_write_register(0x35, 0x00);	
		vibrator_write_register(0x36, 0x00);
	}
#elif defined (CONFIG_KOR_SHV_E120L_HD720) || defined(CONFIG_KOR_MODEL_SHV_E120S) || defined(CONFIG_KOR_MODEL_SHV_E120K)
	vibrator_write_register(0x30, 0x09);
	vibrator_write_register(0x31, 0x40);
	vibrator_write_register(0x32, 0x00);
	vibrator_write_register(0x33, 0x13);
	vibrator_write_register(0x34, 0x02);
	vibrator_write_register(0x35, 0x00);
	vibrator_write_register(0x36, 0x00);
#elif defined(CONFIG_KOR_MODEL_SHV_E160S)
	vibrator_write_register(0x30, 0x09);
	vibrator_write_register(0x31, 0x40);
	vibrator_write_register(0x32, 0x00);
	vibrator_write_register(0x33, 0x13);
	vibrator_write_register(0x34, 0x03);
	vibrator_write_register(0x35, 0x00);
	vibrator_write_register(0x36, 0x00);
#elif defined (CONFIG_USA_MODEL_SGH_T989) 
	if (get_hw_rev() > 0x04 ){	
		vibrator_write_register(0x30, 0x09);
		vibrator_write_register(0x31, 0x40);
		if (get_hw_rev() >= 0x0d)
			vibrator_write_register(0x34, 0x01);
		else				
			vibrator_write_register(0x34, 0x16);
		vibrator_write_register(0x35, 0x00);	
		vibrator_write_register(0x36, 0x00);
	}
#elif defined (CONFIG_USA_MODEL_SGH_I727)
		if (get_hw_rev() > 0x04 ){	
		vibrator_write_register(0x30, 0x09);
		vibrator_write_register(0x31, 0x40);
		vibrator_write_register(0x34, 0x19);
		vibrator_write_register(0x35, 0x00);	
		vibrator_write_register(0x36, 0x00);
	}	
#elif defined (CONFIG_USA_MODEL_SGH_I717)

        vibrator_write_register(0x30, 0x09);
        vibrator_write_register(0x31, 0x40);
        vibrator_write_register(0x34, 0x19);
        vibrator_write_register(0x35, 0x00);    
        vibrator_write_register(0x36, 0x00);
        
#elif defined (CONFIG_JPN_MODEL_SC_03D)
	if (get_hw_rev() >= 0x01 ){ 
		vibrator_write_register(0x30, 0x09);
		vibrator_write_register(0x31, 0x40);
		vibrator_write_register(0x34, 0x19);
		vibrator_write_register(0x35, 0x00);	
		vibrator_write_register(0x36, 0x00);
	}
#endif

	ImmVibeSPI_ForceOut_AmpDisable(0);

#ifdef COUNT_OF_MOTOR
	s5p_TSP = device_create(sec_class, NULL, 0, NULL, "vibetonz");
	if (IS_ERR(s5p_TSP))
		printk("[Vibetonz] Failed to create device(s5p_TSP)!\n");
	if (device_create_file(s5p_TSP, &dev_attr_vibecnt) < 0)
		printk("[Vibetonz] Failed to create device file(%s)!\n", dev_attr_vibecnt.attr.name);	
#endif

    return VIBE_S_SUCCESS;
}

/*
** Called at termination time to set PWM freq, disable amp, etc...
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_Terminate(void)
{
//#error Please review the code between the #if and #endif

 //   DbgOut((KERN_DEBUG "ImmVibeSPI_ForceOut_Terminate.\n"));

    /* 
    ** Disable amp.
    ** If multiple actuators are supported, please make sure to call ImmVibeSPI_ForceOut_AmpDisable
    ** for each actuator (provide the actuator index as input argument).
    */

//	printk("[VIBETONZ] %s \n",__func__);    

    ImmVibeSPI_ForceOut_AmpDisable(0);

    return VIBE_S_SUCCESS;
}

/*
** Called by the real-time loop to set PWM duty cycle, and enable amp if required
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_Set(VibeUInt8 nActuatorIndex, VibeInt8 nForce)
{
	int pwm_duty=freq_count/2 + ((freq_count/2 - 2) * nForce)/127;
	static int pwm_duty_mem = 0;

	if (nForce == 0)
	{
		ImmVibeSPI_ForceOut_AmpDisable(0);
		vibe_pwm_onoff(0);		
		pwm_duty = 0;
		pwm_duty_mem = 0;
	}
	else
	{
		if(pwm_duty_mem != pwm_duty)
		{
//			pwm_config(Immvib_pwm, pwm_duty, freq_count);
//			printk("[VIBETONZ] %s nForce: %d, pwm_duty: %d\n",__func__, nForce, pwm_duty);
			vibe_pwm_onoff(1);
			vibe_set_pwm_freq(pwm_duty);
			ImmVibeSPI_ForceOut_AmpEnable(0);
//			vibe_set_pwm_freq(216);
			pwm_duty_mem = pwm_duty;
		}
	}

//	printk("[VIBETONZ] %s nForce: %d\n",__func__, nForce);

	return VIBE_S_SUCCESS;
}

/*
** Called by the real-time loop to set force output, and enable amp if required
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_SetSamples(VibeUInt8 nActuatorIndex, VibeUInt16 nOutputSignalBitDepth, VibeUInt16 nBufferSizeInBytes, VibeInt8* pForceOutputBuffer)
{
    /* This function is not called for LRA device */
    return VIBE_S_SUCCESS;
}

/*
** Called to set force output frequency parameters
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_ForceOut_SetFrequency(VibeUInt8 nActuatorIndex, VibeUInt16 nFrequencyParameterID, VibeUInt32 nFrequencyParameterValue)
{
//#error Please review the code between the #if and #endif

#if 0 
    #error  "The OEM must handle different frequency parameters here"
#endif

    return VIBE_S_SUCCESS;
}

/*
** Called to get the device name (device name must be returned as ANSI char)
*/
IMMVIBESPIAPI VibeStatus ImmVibeSPI_Device_GetName(VibeUInt8 nActuatorIndex, char *szDevName, int nSize)
{
//#error Please review the code between the #if and #endif

#if 0   /* The following code is provided as a sample. Please modify as required. */
    if ((!szDevName) || (nSize < 1)) return VIBE_E_FAIL;

    DbgOut((KERN_DEBUG "ImmVibeSPI_Device_GetName.\n"));

    strncpy(szDevName, "Generic Linux Device", nSize-1);
    szDevName[nSize - 1] = '\0';    /* make sure the string is NULL terminated */
#endif

    return VIBE_S_SUCCESS;
}
