

/***************************************************************
CAMERA Power control
****************************************************************/


#include "sec_cam_pmic.h"


#include <asm/gpio.h> 

#include <linux/clk.h>
#include <linux/io.h>
#include <mach/board.h>
#include <mach/msm_iomap.h>

#include <linux/regulator/consumer.h>

static struct regulator *i_core12, *s_core12, *s_io18, *i_host18, *af28, *vt_core15;

#if defined (CONFIG_KOR_MODEL_SHV_E110S) || defined (CONFIG_KOR_MODEL_SHV_E120S)|| defined (CONFIG_KOR_MODEL_SHV_E120L) \
 || defined (CONFIG_JPN_MODEL_SC_03D) || defined (CONFIG_USA_MODEL_SGH_I727) || defined (CONFIG_USA_MODEL_SGH_T989) \
 || defined (CONFIG_KOR_MODEL_SHV_E120K) || defined(CONFIG_KOR_MODEL_SHV_E160S) \
 || defined (CONFIG_USA_MODEL_SGH_I717)
extern unsigned int get_hw_rev(void);
#endif


void cam_mclk_onoff(int onoff)
{
	unsigned int mclk_cfg;	
	if(onoff) {
		mclk_cfg = GPIO_CFG(32, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA);
		gpio_tlmm_config(mclk_cfg, GPIO_CFG_ENABLE);

	}
	else {
		mclk_cfg = GPIO_CFG(32, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA);
		gpio_tlmm_config(mclk_cfg, GPIO_CFG_ENABLE);
	}
}

int cam_ldo_power_on(void)
{
	int ret;
	
	cam_mclk_onoff(OFF);
	mdelay(5);
	
	//preempt_disable(); 
//ISP CORE 1.2V		
	i_core12 = regulator_get(NULL, "8901_s2"); //CORE 1.2V
	if (IS_ERR(i_core12))
		goto main_cam_power_fail;
		
#if defined (CONFIG_JPN_MODEL_SC_03D)
	ret = regulator_set_voltage(i_core12, 1300000, 1300000);
#else
	ret = regulator_set_voltage(i_core12, 1200000, 1200000);
#endif
	if (ret) {
		printk("%s:i_core12 error setting voltage\n", __func__);
	}
	ret = regulator_enable(i_core12);
	if (ret) {
		printk("%s:i_core12 error enabling regulator\n", __func__);
	}
	mdelay(1);

//SENSOR CORE 1.2V
	s_core12 = regulator_get(NULL, "8901_lvs1"); 
	if (IS_ERR(s_core12))
		goto main_cam_power_fail;

	ret = regulator_enable(s_core12);
	if (ret) {
		printk("%s:s_core12 error enabling regulator\n", __func__);
	}
	mdelay(1);

//SENSOR A2.8V
	gpio_set_value_cansleep(CAM_IO_EN, HIGH);  
	mdelay(1); //min 20us

//DVDD 1.5V (sub)
#ifdef CONFIG_USA_MODEL_SGH_T989
 	if (get_hw_rev() >= 0x09)
		vt_core15 = regulator_get(NULL, "8058_l24"); 
	else
#elif defined (CONFIG_KOR_MODEL_SHV_E110S)
	if (get_hw_rev() >= 0x05) //celox_REV05
		vt_core15 = regulator_get(NULL, "8058_l24"); 
	else
#elif defined (CONFIG_KOR_MODEL_SHV_E120S) || defined (CONFIG_KOR_MODEL_SHV_E120K)
	if (get_hw_rev() >= 0x06) //daliS_REV03
		vt_core15 = regulator_get(NULL, "8058_l24");
	else
#elif defined(CONFIG_KOR_MODEL_SHV_E160S)
	if (get_hw_rev() >= 0x02) //QuincyS_REV02
		vt_core15 = regulator_get(NULL, "8058_l24");
	else
#elif defined (CONFIG_KOR_MODEL_SHV_E120L)
	if (get_hw_rev() >= 0x02) //dali LGT REV02
		vt_core15 = regulator_get(NULL, "8058_l24");
	else
#elif defined (CONFIG_USA_MODEL_SGH_I717)
 	if (get_hw_rev() >= 0x01) //Q1_REV01
		vt_core15 = regulator_get(NULL, "8058_l24"); 
	else
#elif defined (CONFIG_USA_MODEL_SGH_I727)
 	if (get_hw_rev() >= 0x08)
		vt_core15 = regulator_get(NULL, "8058_l24"); 
	else
#elif defined (CONFIG_JPN_MODEL_SC_03D)
 	if (get_hw_rev() >= 0x05)
		vt_core15 = regulator_get(NULL, "8058_l24"); 
	else
#endif
	{
		printk("DVDD1.5V : 8058_l10\n");
		vt_core15 = regulator_get(NULL, "8058_l10"); 
	}

	if (IS_ERR(vt_core15))
		goto main_cam_power_fail;

	ret = regulator_set_voltage(vt_core15, 1500000, 1500000);
	if (ret) {
		printk("%s:vt_core15 error setting voltage\n", __func__);
	}
	ret = regulator_enable(vt_core15);
	if (ret) {
		printk("%s:vt_core15 error enabling regulator\n", __func__);
	}
	mdelay(1);  //min 15us

//AF 2.8V	
	af28 = regulator_get(NULL, "8058_l15"); //AF 2.8V

	if (IS_ERR(af28))
		goto main_cam_power_fail;
	
	ret = regulator_set_voltage(af28, 2850000, 2850000);
	if (ret) {
		printk("%s:af28 error setting voltage\n", __func__);
	}
	ret = regulator_enable(af28);
	if (ret) {
		printk("%s:af28 error enabling regulator\n", __func__);
	}
 	mdelay(5);  // min 5ms~max 10ms, 


//HOST 1.8V
#ifdef CONFIG_JPN_MODEL_SC_03D
	if (get_hw_rev() >= 0x02)
		i_host18 = regulator_get(NULL, "8901_usb_otg");
	else
#elif defined (CONFIG_KOR_MODEL_SHV_E110S) || defined (CONFIG_KOR_MODEL_SHV_E120S) || defined (CONFIG_KOR_MODEL_SHV_E120K)
	if (get_hw_rev() >= 0x04)
		i_host18 = regulator_get(NULL, "8901_usb_otg");
	else
#elif defined(CONFIG_KOR_MODEL_SHV_E160S)
	if (get_hw_rev() >= 0x02)
		i_host18 = regulator_get(NULL, "8901_usb_otg");
	else
#elif defined (CONFIG_KOR_MODEL_SHV_E120L)
	if (get_hw_rev() >= 0x02) //dali LGT REV02
		i_host18 = regulator_get(NULL, "8901_usb_otg");
	else
#elif defined (CONFIG_USA_MODEL_SGH_I717)
	if (get_hw_rev()>=0x01) //Q1_REV01
 		i_host18 = regulator_get(NULL, "8901_usb_otg");
	else
#elif defined (CONFIG_USA_MODEL_SGH_I727)
	if (get_hw_rev()>=0x06) //celox_REV06
 		i_host18 = regulator_get(NULL, "8901_usb_otg");
	else
#elif defined (CONFIG_USA_MODEL_SGH_T989)
	if (get_hw_rev()>=0x0D) //Hercules_rev06
 		i_host18 = regulator_get(NULL, "8901_usb_otg");
	else
#endif
	{
		printk("Host1.8V : 8058_l8\n");
		i_host18 = regulator_get(NULL, "8058_l8");  
		if (IS_ERR(i_host18))
			goto main_cam_power_fail;
		
		ret = regulator_set_voltage(i_host18, 1800000, 1800000);
		if (ret) {
			printk("%s:i_host18 error setting voltage\n", __func__);
		}
		
	}

	if (IS_ERR(i_host18))
		goto main_cam_power_fail;

	ret = regulator_enable(i_host18);
	if (ret) {
		printk("%s:i_host18 error enabling regulator\n", __func__);
	}
	mdelay(1);
	
	
//SENSOR IO 1.8V  - ISP
	s_io18 = regulator_get(NULL, "8058_lvs0");
	if (IS_ERR(s_io18))
		goto main_cam_power_fail;
	
	ret = regulator_enable(s_io18);
	if (ret) {
		printk("%s:s_io18 error enabling regulator\n", __func__);
	}
	mdelay(1);

	
	//preempt_enable(); 

	cam_mclk_onoff(ON);
	mdelay(1);

	return ret;

	
main_cam_power_fail:
	return -1;
		
}

int sub_cam_ldo_power(int onoff)
{
	int ret = 0;
	printk("%s: %d\n", __func__, onoff);

	if(onoff) { // power on
		cam_mclk_onoff(OFF);
		mdelay(5);

	//ISP CORE 1.2V		
		i_core12 = regulator_get(NULL, "8901_s2"); //CORE 1.2V
		
		if (IS_ERR(i_core12))
			goto sub_cam_power_fail;
		
#if defined (CONFIG_JPN_MODEL_SC_03D)
		ret = regulator_set_voltage(i_core12, 1300000, 1300000);
#else
		ret = regulator_set_voltage(i_core12, 1200000, 1200000);
#endif
		if (ret) {
			printk("%s: i_core12 error setting voltage\n", __func__);
		}
		ret = regulator_enable(i_core12);
		if (ret) {
			printk("%s: i_core12 error enabling regulator\n", __func__);
		}
		mdelay(2); 


	//SENSOR A2.8V
		gpio_set_value_cansleep(CAM_IO_EN, HIGH);  
		mdelay(1); //min 20us

	//DVDD 1.5V (sub)
#ifdef CONFIG_USA_MODEL_SGH_T989
		if (get_hw_rev() >= 0x09)
			vt_core15 = regulator_get(NULL, "8058_l24"); 
		else
#elif defined (CONFIG_KOR_MODEL_SHV_E110S)
		if (get_hw_rev() >= 0x05) //celox_REV05
			vt_core15 = regulator_get(NULL, "8058_l24"); 
		else
#elif defined (CONFIG_KOR_MODEL_SHV_E120S) || defined (CONFIG_KOR_MODEL_SHV_E120K)
		if (get_hw_rev() >= 0x06) //daliS_REV03
			vt_core15 = regulator_get(NULL, "8058_l24");
		else
#elif defined(CONFIG_KOR_MODEL_SHV_E160S)
		if (get_hw_rev() >= 0x02) //QuincyS_REV02
			vt_core15 = regulator_get(NULL, "8058_l24");
		else
#elif defined (CONFIG_KOR_MODEL_SHV_E120L)
		if (get_hw_rev() >= 0x02) //dali LGT REV02
			vt_core15 = regulator_get(NULL, "8058_l24");
		else
#elif defined (CONFIG_USA_MODEL_SGH_I717)
		if (get_hw_rev() >= 0x01) //Q1_REV01
			vt_core15 = regulator_get(NULL, "8058_l24"); 
		else
#elif defined (CONFIG_USA_MODEL_SGH_I727)
		if (get_hw_rev() >= 0x08)
			vt_core15 = regulator_get(NULL, "8058_l24"); 
		else
#elif defined (CONFIG_JPN_MODEL_SC_03D)
 	if (get_hw_rev() >= 0x05)
		vt_core15 = regulator_get(NULL, "8058_l24"); 
	else
#endif
		{
			printk("DVDD1.5V : 8058_l10\n");
			vt_core15 = regulator_get(NULL, "8058_l10"); 
		}
		if (IS_ERR(vt_core15))
			goto sub_cam_power_fail;

		ret = regulator_set_voltage(vt_core15, 1500000, 1500000);
		if (ret) {
			printk("%s:vt_core15 error setting voltage\n", __func__);
		}
		ret = regulator_enable(vt_core15);
		if (ret) {
			printk("%s:vt_core15 error enabling regulator\n", __func__);
		}
		udelay(50);  //min 15us

		
	//HOST 1.8V
#ifdef CONFIG_JPN_MODEL_SC_03D
		if (get_hw_rev() >= 0x02)
			i_host18 = regulator_get(NULL, "8901_usb_otg");
		else
#elif defined (CONFIG_KOR_MODEL_SHV_E110S) || defined (CONFIG_KOR_MODEL_SHV_E120S) || defined (CONFIG_KOR_MODEL_SHV_E120K)
		if (get_hw_rev() >= 0x04)
			i_host18 = regulator_get(NULL, "8901_usb_otg");
		else
#elif defined(CONFIG_KOR_MODEL_SHV_E160S)
		if (get_hw_rev() >= 0x02)
			i_host18 = regulator_get(NULL, "8901_usb_otg");
		else
#elif defined (CONFIG_KOR_MODEL_SHV_E120L)
		if (get_hw_rev() >= 0x02) //dali LGT REV02
			i_host18 = regulator_get(NULL, "8901_usb_otg");
		else
#elif defined (CONFIG_USA_MODEL_SGH_I717)
		if (get_hw_rev()>=0x01) //Q1_REV01
			i_host18 = regulator_get(NULL, "8901_usb_otg");
		else
#elif defined (CONFIG_USA_MODEL_SGH_I727)
		if (get_hw_rev()>=0x06) //celox_REV06
			i_host18 = regulator_get(NULL, "8901_usb_otg");
		else
#elif defined (CONFIG_USA_MODEL_SGH_T989)
		if (get_hw_rev()>=0x0D) //Hercules_rev06
			i_host18 = regulator_get(NULL, "8901_usb_otg");
		else
#endif
		{
			printk("Host1.8V : 8058_l8\n");
			i_host18 = regulator_get(NULL, "8058_l8");  
			if (IS_ERR(i_host18))
				goto sub_cam_power_fail;
			
			ret = regulator_set_voltage(i_host18, 1800000, 1800000);
			if (ret) {
				printk("%s:i_host18 error setting voltage\n", __func__);
			}
		}
		if (IS_ERR(i_host18))
			goto sub_cam_power_fail;

		ret = regulator_enable(i_host18);
		if (ret) {
			printk("%s: i_host18 error enabling regulator\n", __func__);
		}

		mdelay(1);

#if !defined (CONFIG_KOR_MODEL_SHV_E120L)
		//SENSOR IO 1.8V  - ISP //i2c
	#if defined (CONFIG_KOR_MODEL_SHV_E110S) || defined (CONFIG_KOR_MODEL_SHV_E120S) || defined (CONFIG_KOR_MODEL_SHV_E120K)
		if (get_hw_rev()< 0x5) //celoxS_REV03, DaliS_REV02
	#elif defined(CONFIG_KOR_MODEL_SHV_E160S)
		if (get_hw_rev()< 0x2) //celoxS_REV03, DaliS_REV02
	#elif defined (CONFIG_USA_MODEL_SGH_I717)
		if (get_hw_rev()< 0x01) //Q1_REV01			
	#elif defined (CONFIG_USA_MODEL_SGH_I727)
		if (get_hw_rev()< 0x06) //celoxS_REV06		
	#elif defined (CONFIG_USA_MODEL_SGH_T989)
		if (get_hw_rev()< 0x0D) //Hercules_rev06		
        #endif
		{
			s_io18 = regulator_get(NULL, "8058_lvs0");
			if (IS_ERR(s_io18))
				goto sub_cam_power_fail;
			
			ret = regulator_enable(s_io18);
			if (ret) {
				printk("%s:s_io18 error enabling regulator\n", __func__);
			}
			mdelay(1);
        }		
#endif

		gpio_set_value_cansleep(CAM_VGA_EN, HIGH); // STBY
		mdelay(2);	//udelay(50);

		cam_mclk_onoff(ON);
		mdelay(1); // min50us
	}
	else{ //off
		cam_mclk_onoff(OFF);// Disable MCLK 
		mdelay(2);	//udelay(50);

		gpio_set_value_cansleep(CAM_VGA_EN, LOW);
		mdelay(1);
	
	//SENSOR IO 1.8V  - ISP
#if !defined (CONFIG_KOR_MODEL_SHV_E120L)
			//SENSOR IO 1.8V  - ISP //i2c
	#if defined (CONFIG_KOR_MODEL_SHV_E110S) || defined (CONFIG_KOR_MODEL_SHV_E120S) || defined (CONFIG_KOR_MODEL_SHV_E120K)
		if (get_hw_rev()< 0x5) //celoxS_REV03, DaliS_REV02
	#elif defined(CONFIG_KOR_MODEL_SHV_E160S)
		if (get_hw_rev()< 0x2) //celoxS_REV03, DaliS_REV02
	#elif defined (CONFIG_USA_MODEL_SGH_I717)
		if (get_hw_rev()< 0x01) //Q1_REV01			
	#elif defined (CONFIG_USA_MODEL_SGH_I727) 
		if (get_hw_rev()< 0x06) //celoxS_REV06		
	#elif defined (CONFIG_USA_MODEL_SGH_T989)
		if (get_hw_rev()< 0x0D) //Hercules_rev06		
        #endif
		{
			if (regulator_is_enabled(s_io18)) {
				ret = regulator_disable(s_io18);
				if (ret) {
					printk("%s: s_io18 error disabling regulator\n", __func__);
				}
				//regulator_put(lvs0);
			}
			mdelay(1);
    	}
#endif

	//HOST 1.8V	
		if (regulator_is_enabled(i_host18)) {
			ret=regulator_disable(i_host18);
			if (ret) {
				printk("%s:i_host18 error disabling regulator\n", __func__);
			}
			//regulator_put(l8);
		}
		mdelay(1);
	
	//DVDD 1.5V (sub)		
		if (regulator_is_enabled(vt_core15)) {
			ret=regulator_disable(vt_core15);
			if (ret) {
				printk("%s:vt_core15 error disabling regulator\n", __func__);
			}
			//regulator_put(l24);
		}	
		mdelay(1);
	
	//SENSOR A2.8V
		gpio_set_value_cansleep(CAM_IO_EN, LOW);  //HOST 1.8V
		mdelay(1);
	
	
	//ISP CORE 1.2V
		if (regulator_is_enabled(i_core12)) {
			ret=regulator_disable(i_core12);
			if (ret) {
				printk("%s:i_core12 error disabling regulator\n", __func__);
			}
			//regulator_put(s2);
		}
		mdelay(5);
		
	}

	return ret;

sub_cam_power_fail:
	return -1;

}


int cam_ldo_power_off(void)
{
	int ret = 0;

// Disable MCLK 
	cam_mclk_onoff(OFF);
	mdelay(1);

//AF 2.8V 
	if (regulator_is_enabled(af28)) {
		ret=regulator_disable(af28);
		if (ret) {
			printk("%s:af28 error disabling regulator\n", __func__);
		}
		//regulator_put(l15);
	}
	mdelay(1);

//SENSOR IO 1.8V  - ISP
	if (regulator_is_enabled(s_io18)) {
		ret = regulator_disable(s_io18);
		if (ret) {
			printk("%s:s_io18 error disabling regulator\n", __func__);
		}
		//regulator_put(lvs0);
	}
	mdelay(1);

//HOST 1.8V	
	if (regulator_is_enabled(i_host18)) {
		ret=regulator_disable(i_host18);
		if (ret) {
			printk("%s:i_host18 error disabling regulator\n", __func__);
		}
		//regulator_put(l8);
	}
	mdelay(1);

//DVDD 1.5V (sub)		
	if (regulator_is_enabled(vt_core15)) {
		ret=regulator_disable(vt_core15);
		if (ret) {
			printk("%s:vt_core15 error disabling regulator\n", __func__);
		}
		//regulator_put(l24);
	}	
	mdelay(1);

//SENSOR A2.8V
	gpio_set_value_cansleep(CAM_IO_EN, LOW);  //HOST 1.8V
	mdelay(1);

//SENSOR CORE 1.2V	
	if (regulator_is_enabled(s_core12)) {
		ret=regulator_disable(s_core12);
		if (ret) {
			printk("%s:s_core12 error disabling regulator\n", __func__);
		}
		//regulator_put(lvs1);
	}
	mdelay(1);

//ISP CORE 1.2V
	if (regulator_is_enabled(i_core12)) {
		ret=regulator_disable(i_core12);
		if (ret) {
			printk("%s:i_core12 error disabling regulator\n", __func__);
		}
		//regulator_put(s2);
	}
	mdelay(5);
	
	return ret;
}

