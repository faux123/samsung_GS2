/*
  SEC S5K6AAFX
 */
/***************************************************************
CAMERA DRIVER FOR 5M CAM(FUJITSU M4Mo) by PGH
ver 0.1 : only preview (base on universal)
****************************************************************/

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <media/msm_camera.h>
#include <mach/gpio.h>
#include <mach/camera.h>//PGH


#include "sec_s5k6aafx.h"
#include  "sec_s5k6aafx_reg.h"	

#include "sec_cam_pmic.h"


#if 0
#include <asm/gpio.h> //PGH

#include <linux/clk.h>
#include <linux/io.h>
#include <mach/board.h>
#endif


#define S5K6AAFX_DEBUG
#ifdef S5K6AAFX_DEBUG
#define CAM_DEBUG(fmt, arg...)	\
		do{\
		printk("\033[[s5k6aafx] %s:%d: " fmt "\033[0m\n", __FUNCTION__, __LINE__, ##arg);}\
		while(0)
#else
#define CAM_DEBUG(fmt, arg...)	
#endif


/*	Read setting file from SDcard
	- There must be no "0x" string in comment. If there is, it cause some problem.
*/
//#define CONFIG_LOAD_FILE

struct s5k6aafx_work_t {
	struct work_struct work;
};

static struct  s5k6aafx_work_t *s5k6aafx_sensorw;
static struct  i2c_client *s5k6aafx_client;

struct s5k6aafx_ctrl_t {
	int8_t  opened;
	const struct  msm_camera_sensor_info 	*sensordata;
	int dtp_mode;
	int app_mode;	// camera or camcorder
	int vtcall_mode;
	int started;
};

static unsigned int config_csi2;


static struct s5k6aafx_ctrl_t *s5k6aafx_ctrl;
static DECLARE_WAIT_QUEUE_HEAD(s5k6aafx_wait_queue);
DECLARE_MUTEX(s5k6aafx_sem);

#ifdef CONFIG_LOAD_FILE
static int s5k6aafx_regs_table_write(char *name);
#endif
static int s5k6aafx_start(void);

int s5k6aafx_i2c_tx_data(char* txData, int length)
{
	int rc; 

	struct i2c_msg msg[] = {
		{
			.addr = s5k6aafx_client->addr,
			.flags = 0,
			.len = length,
			.buf = txData,		
		},
	};
    
	rc = i2c_transfer(s5k6aafx_client->adapter, msg, 1);
	if (rc < 0) {
		printk(KERN_ERR "s5k6aafx: s5k6aafx_i2c_tx_data error %d\n", rc);
		return rc;
	}

	return 0;
}#if 0
static int s5k6aafx_i2c_read(unsigned short page, unsigned short subaddr, unsigned short *data)
{
	int ret;
	unsigned char buf[1] = {0};
	struct i2c_msg msg = { s5k6aafx_client->addr, 0, 2, buf };

	/* page select */
	buf[0] = 0xFC;
	buf[1] = page;
	ret = i2c_transfer(s5k6aafx_client->adapter, &msg, 1) == 1 ? 0 : -EIO;
	if (ret == -EIO) 
		goto error;
	
	/* read data */
	msg.buf[0] = subaddr;
	msg.len = 1;
	ret = i2c_transfer(s5k6aafx_client->adapter, &msg, 1) == 1 ? 0 : -EIO;
	if (ret == -EIO) 
		goto error;

	msg.flags = I2C_M_RD;
	
	ret = i2c_transfer(s5k6aafx_client->adapter, &msg, 1) == 1 ? 0 : -EIO;
	if (ret == -EIO) 
		goto error;

//	*data = ((buf[0] << 8) | buf[1]);
	*data = buf[0];

error:
	return ret;
}

static int s5k6aafx_i2c_write(unsigned char u_addr, unsigned char u_data)
{
	unsigned char buf[2] = {0};
	struct i2c_msg msg = { s5k6aafx_client->addr, 0, 2, buf };

	buf[0] = u_addr;
	buf[1] = u_data;

	//printk("addr : 0x%x , value : 0x%x\n",u_addr,u_data);
	//return i2c_transfer(s5k6aafx_client->adapter, &msg, 1) == 1 ? 0 : -EIO;


	
	if (i2c_transfer(s5k6aafx_client->adapter, &msg, 1) < 0) {
		printk("s5k6aafx_i2c_write failed(addr : 0x%x , value : 0x%x\n",u_addr,u_data);
		return -EIO;
	}
	return 0;
}#endif


static int32_t s5k6aafx_i2c_txdata(unsigned short saddr, unsigned long packet)
{
	unsigned char buf[4];
	int retry_count = 5;
	int err = 0;

	struct i2c_msg msg = {
		.addr = saddr,
		.flags = 0,
		.len = 4,
		.buf = buf,
	};

	while(retry_count--)
	{
		*(unsigned long *)buf = cpu_to_be32(packet);
		
		err = i2c_transfer(s5k6aafx_client->adapter, &msg, 1);
		if (likely(err == 1))
			break;
		

		mdelay(10);
		printk("s5k6aafx_i2c_txdata failed\n");
	}

	return 0;
}


#define S5K6AAFX_DELAY		0xFFFF0000

static int s5k6aafx_i2c_write_list(const unsigned long *list, int size, char *name)
{

	int ret = 0;
	int i;
	char m_delay = 0;

	unsigned long temp_packet;

	CAM_DEBUG("list name : %s\n",name);
	
	for (i = 0; i < size; i++)
	{
		temp_packet = list[i];

		if ((temp_packet & S5K6AAFX_DELAY) == S5K6AAFX_DELAY)
		{
			m_delay = temp_packet & 0xFFFF;
			printk("[cam]delay = %d\n",m_delay );
			mdelay(m_delay);
			continue;
		}

		//if(s5k6aafx_i2c_write(addr, value) < 0)
		if(s5k6aafx_i2c_txdata(s5k6aafx_client->addr, temp_packet) < 0)
		{
			printk("s5k6aafx_i2c_txdata fail(%ld)\n", temp_packet);
			return -1;
		}
		//udelay(10);

	}
	return ret;
}





//static long s5k6aafx_set_sensor_mode(enum sensor_mode_t mode)
static long s5k6aafx_set_sensor_mode(int mode)
{	//unsigned short value =0;
	//int shade_value = 0;
	//unsigned short agc_value = 0;
	switch (mode) 
	{
	case SENSOR_PREVIEW_MODE:
		CAM_DEBUG("SENSOR_PREVIEW_MODE START\n");	
		s5k6aafx_start();
		s5k6aafx_i2c_write_list(s5k6aafx_preview, sizeof(s5k6aafx_preview) / sizeof(s5k6aafx_preview[0]), "s5k6aafx_preview");
	#if 0
		if(s5k6aafx_ctrl->dtp_mode == 1)
		{
			//s5k6aafx_i2c_write_list(reg_dtp_preview_camera_list,sizeof(reg_dtp_preview_camera_list)/sizeof(reg_dtp_preview_camera_list[0]),"reg_dtp_preview_camera_list");
		}
		else
		{
			if(s5k6aafx_ctrl->vtcall_mode)
			{
				s5k6aafx_i2c_write_list(vtmode_list,sizeof(vtmode_list)/sizeof(vtmode_list[0]),"vtmode_list");
			}
			else
			{
				s5k6aafx_i2c_read(0x00,0xE5,&value);
				CAM_DEBUG(" 0x00,0xE5 value : 0x%x\n",value);
				shade_value = value * 0x100;
				s5k6aafx_i2c_read(0x00,0xE6,&value);
				CAM_DEBUG(" 0x00,0xE6 value : 0x%x\n",value);
				shade_value += value;

				if((shade_value >= reg_AgcValue_list[3]) && (shade_value <= reg_AgcValue_list[4]))
				{
						s5k6aafx_i2c_write_list(reg_shade_dnp_list,sizeof(reg_shade_dnp_list)/sizeof(reg_shade_dnp_list[0]),"reg_shade_dnp_list");
				}
				else
				{
					s5k6aafx_i2c_write_list(reg_shade_etc_list,sizeof(reg_shade_etc_list)/sizeof(reg_shade_etc_list[0]),"reg_shade_etc_list");				
				}

				
				if(s5k6aafx_ctrl->app_mode == CAMCORDER_MODE) 
				{
					s5k6aafx_i2c_write_list(reg_preview_camcorder_list,sizeof(reg_preview_camcorder_list)/sizeof(reg_preview_camcorder_list[0]),"reg_preview_camcorder_list");
				}
				else		// camera mode preview
				{				
					s5k6aafx_i2c_write_list(reg_preview_camera_list,sizeof(reg_preview_camera_list)/sizeof(reg_preview_camera_list[0]),"reg_preview_camera_list");
					s5k6aafx_i2c_read(0x00,0xF8,&agc_value);
					if(agc_value <= reg_AgcValue_list[0])
					{
						s5k6aafx_i2c_write_list(reg_preview_high_light_list,sizeof(reg_preview_high_light_list)/sizeof(reg_preview_high_light_list[0]),"reg_preview_high_light_list");				
					}
					else
					{
						s5k6aafx_i2c_write_list(reg_preview_normal_light_list,sizeof(reg_preview_normal_light_list)/sizeof(reg_preview_normal_light_list[0]),"reg_preview_normal_light_list");				
					}
				}
			}
		}
	#endif
	
		break;

	case SENSOR_SNAPSHOT_MODE:
		CAM_DEBUG("SENSOR_SNAPSHOT_MODE START\n");		

		#if 0
		s5k6aafx_i2c_read(0x00,0xE5,&value);
		CAM_DEBUG(" 0x00,0xE5 value : 0x%x\n",value);
		shade_value = value * 0x100;
		s5k6aafx_i2c_read(0x00,0xE6,&value);
		CAM_DEBUG(" 0x00,0xE6 value : 0x%x\n",value);
		shade_value += value;

		if((shade_value >= reg_AgcValue_list[3]) && (shade_value <= reg_AgcValue_list[4]))
		{
			s5k6aafx_i2c_write_list(reg_shade_dnp_list,sizeof(reg_shade_dnp_list)/sizeof(reg_shade_dnp_list[0]),"reg_shade_dnp_list");
		}
		else 
		{
			s5k6aafx_i2c_write_list(reg_shade_etc_list,sizeof(reg_shade_etc_list)/sizeof(reg_shade_etc_list[0]),"reg_shade_etc_list");				
		}


		s5k6aafx_i2c_read(0x00,0xf8,&agc_value);
		CAM_DEBUG("agc_value = 0x%x\n",agc_value);

		if(agc_value >= reg_AgcValue_list[2])
		{
			s5k6aafx_i2c_write_list(reg_snapshot_lowlight_list,sizeof(reg_snapshot_lowlight_list)/sizeof(reg_snapshot_lowlight_list[0]),"reg_snapshot_lowlight_list");
		}
		else if(agc_value >= reg_AgcValue_list[1])
		{
			s5k6aafx_i2c_write_list(reg_snapshot_mid_lowlight_list,sizeof(reg_snapshot_mid_lowlight_list)/sizeof(reg_snapshot_mid_lowlight_list[0]),"reg_snapshot_mid_lowlight_list");
		}
		else if(agc_value <= reg_AgcValue_list[0])
		{
			s5k6aafx_i2c_write_list(reg_snapshot_list,sizeof(reg_snapshot_list)/sizeof(reg_snapshot_list[0]),"reg_snapshot_list");
		}
		else
		{
			s5k6aafx_i2c_write_list(reg_snapshot_midlight_list,sizeof(reg_snapshot_midlight_list)/sizeof(reg_snapshot_midlight_list[0]),"reg_snapshot_midlight_list");
		}
		//test
		#endif

		
		break;

	
	case SENSOR_SNAPSHOT_TRANSFER:
		CAM_DEBUG("SENSOR_SNAPSHOT_TRANSFER START\n");


		break;
		
	default:
		return -EFAULT;
	}

	return 0;
}


static long s5k6aafx_set_effect(
	int mode,
	int8_t effect
)
{
	long rc = 0;
	switch(effect)
	{
/*		default:
			printk("[Effect]Invalid Effect !!!\n");
			return -EINVAL;
*/
	}
	return rc;
}
#if 0
static int s5k6aafx_reset(void)
{
	CAM_DEBUG("s5k6aafx_reset");
#if 0
	if (system_rev >= 4) 
		gpio_set_value(CAM_MEGA_EN_REV04, LOW);
	else
		gpio_set_value(CAM_MEGA_EN, LOW);
	//gpio_set_value(CAM_8M_RST, LOW);
	gpio_set_value_cansleep(CAM_8M_RST, LOW);

	CAM_DEBUG(" 2. CAM_VGA_EN = 0 ");
	gpio_set_value_cansleep(CAM_VGA_EN, LOW);

	CAM_DEBUG(" 3. CAM_VGA_RST = 0 ");
	gpio_set_value_cansleep(CAM_VGA_RST, LOW);

	mdelay(30);	
	CAM_DEBUG(" CAM PM already on \n");
	//CAM_DEBUG(" 1. PMIC ON ");
	//cam_pmic_onoff(ON);

	//CAM_DEBUG(" 1. CAM_8M_RST = 0 ");
	//gpio_set_value(CAM_8M_RST, LOW);
	
	CAM_DEBUG(" 2. CAM_VGA_EN = 1 ");
	gpio_set_value_cansleep(CAM_VGA_EN, HIGH);

	mdelay(20);

	CAM_DEBUG(" 3. CAM_VGA_RST = 1 ");
	gpio_set_value_cansleep(CAM_VGA_RST, HIGH);
	mdelay(30); // min 350ns
#endif
	return 0;
}#endif

static int
s5k6aafx_set_ev(
	int8_t ev)
{
	CAM_DEBUG(" ev : %d \n",ev);
	#if 0
	switch(ev)
	{
		case S5K6AAFX_EV_MINUS_4:
			s5k6aafx_i2c_write_list(reg_brightness_0_list,sizeof(reg_brightness_0_list)/sizeof(reg_brightness_0_list[0]),"reg_brightness_0_list");
			break;
		case S5K6AAFX_EV_MINUS_3:
			s5k6aafx_i2c_write_list(reg_brightness_1_list,sizeof(reg_brightness_1_list)/sizeof(reg_brightness_1_list[0]),"reg_brightness_1_list");
			break;
		case S5K6AAFX_EV_MINUS_2:
			s5k6aafx_i2c_write_list(reg_brightness_2_list,sizeof(reg_brightness_2_list)/sizeof(reg_brightness_2_list[0]),"reg_brightness_2_list");
			break;
		case S5K6AAFX_EV_MINUS_1:
			s5k6aafx_i2c_write_list(reg_brightness_3_list,sizeof(reg_brightness_3_list)/sizeof(reg_brightness_3_list[0]),"reg_brightness_3_list");
			break;
		case S5K6AAFX_EV_DEFAULT:
			s5k6aafx_i2c_write_list(reg_brightness_4_list,sizeof(reg_brightness_4_list)/sizeof(reg_brightness_4_list[0]),"reg_brightness_4_list");
			break;
		case S5K6AAFX_EV_PLUS_1:
			s5k6aafx_i2c_write_list(reg_brightness_5_list,sizeof(reg_brightness_5_list)/sizeof(reg_brightness_5_list[0]),"reg_brightness_5_list");
			break;
		case S5K6AAFX_EV_PLUS_2:
			s5k6aafx_i2c_write_list(reg_brightness_6_list,sizeof(reg_brightness_6_list)/sizeof(reg_brightness_6_list[0]),"reg_brightness_6_list");
			break;	
		case S5K6AAFX_EV_PLUS_3:
			s5k6aafx_i2c_write_list(reg_brightness_7_list,sizeof(reg_brightness_7_list)/sizeof(reg_brightness_7_list[0]),"reg_brightness_7_list");
			break;
		case S5K6AAFX_EV_PLUS_4:
			s5k6aafx_i2c_write_list(reg_brightness_8_list,sizeof(reg_brightness_8_list)/sizeof(reg_brightness_8_list[0]),"reg_brightness_8_list");
			break;
		default:
			printk("[EV] Invalid EV !!!\n");
			return -EINVAL;
	}
	#endif
	return 0;
}

static int
s5k6aafx_set_dtp(
	int* onoff)
{
	CAM_DEBUG(" onoff : %d ",*onoff);

	return 0;
}


static int
s5k6aafx_set_fps(
	unsigned int mode, unsigned int fps)
{
	CAM_DEBUG(" %s -mode : %d, fps : %d \n",__FUNCTION__,mode,fps);

	if(mode)
	{
		CAM_DEBUG("mode change to CAMCORDER_MODE");
		s5k6aafx_ctrl->app_mode = CAMCORDER_MODE;
	}
	else
	{
		CAM_DEBUG("mode change to CAMERA_MODE");
		s5k6aafx_ctrl->app_mode = CAMERA_MODE;
	}
	return 0;
}
#if 0
int cam_pmic_onoff(int onoff)

#endif

#if 0 // for_8M_cam
static int s5k6aafx_init_regs(void)
{
	CAM_DEBUG("%s E\n",__FUNCTION__);
	return 0;
}


static int s5k6aafx_i2c_test(void)
{
	unsigned short value=0;
	CAM_DEBUG("%s E\n",__FUNCTION__);
	CAM_DEBUG("----- WRITE -----\n");
	s5k6aafx_i2c_write(0xfc,0x00);
	CAM_DEBUG("write : 0x02 - 0x08, 0x03 - 0x4b\n");
	s5k6aafx_i2c_write(0x02,0x08);
	s5k6aafx_i2c_write(0x03,0x4b);
	
	CAM_DEBUG("----- READ -----\n");
	s5k6aafx_i2c_read(0x00,0x02,&value);
	CAM_DEBUG(" 0x02, value : 0x%x\n",value);
	s5k6aafx_i2c_read(0x00,0x03,&value);
	CAM_DEBUG(" 0x03 value : 0x%x\n",value);
	s5k6aafx_i2c_read(0x01,0x01,&value);
	CAM_DEBUG(" 0x01 value : 0x%x\n",value);

	return 0;
}
#endif

static int s5k6aafx_mipi_mode(int mode)
{
	int rc = 0;
	struct msm_camera_csi_params s5k6aafx_csi_params;
	
	CAM_DEBUG("%s E\n",__FUNCTION__);

	if (!config_csi2) {
		s5k6aafx_csi_params.lane_cnt = 1;
		s5k6aafx_csi_params.data_format = CSI_8BIT;
		s5k6aafx_csi_params.lane_assign = 0xe4;
		s5k6aafx_csi_params.dpcm_scheme = 0;
		s5k6aafx_csi_params.settle_cnt = 24;// 0x14; //0x7; //0x14;
		rc = msm_camio_csi_config(&s5k6aafx_csi_params);
		if (rc < 0)
			printk(KERN_ERR "config csi controller failed \n");
		config_csi2 = 1;
	}
	return rc;
}


static int s5k6aafx_start(void)
{
	int rc=0;

	CAM_DEBUG("%s E\n",__FUNCTION__);
	
	if(s5k6aafx_ctrl->started)
	{
		CAM_DEBUG("%s X : already started\n",__FUNCTION__);
		return rc;
	}
	s5k6aafx_mipi_mode(1);
	msleep(300); //=> Please add some delay 

	s5k6aafx_i2c_write_list(s5k6aafx_init,sizeof(s5k6aafx_init)/sizeof(s5k6aafx_init[0]),"s5k6aafx_init");
	//s5k6aafx_i2c_write_list(reg_effect_none_list,sizeof(reg_effect_none_list)/sizeof(reg_effect_none_list[0]),"reg_effect_none_list");
	//s5k6aafx_i2c_write_list(reg_meter_center_list,sizeof(reg_meter_center_list)/sizeof(reg_meter_center_list[0]),"reg_meter_center_list");
	//s5k6aafx_i2c_write_list(reg_wb_auto_list,sizeof(reg_wb_auto_list)/sizeof(reg_wb_auto_list[0]),"reg_wb_auto_list");
	
	s5k6aafx_ctrl->started = 1;
	return rc;
}

static int s5k6aafx_sensor_init_probe(const struct msm_camera_sensor_info *data)
{
	int rc = 0;

	CAM_DEBUG("s5k6aafx_sensor_init_probe start");
	
	CAM_DEBUG("0. Init. ");
	gpio_set_value_cansleep(CAM_VGA_EN, LOW);
	gpio_set_value_cansleep(CAM_8M_RST, LOW);
	gpio_set_value_cansleep(CAM_VGA_RST, LOW);
	gpio_set_value_cansleep(CAM_IO_EN, LOW);  //HOST 1.8V
	//cam_ldo_power_off();	

	
	mdelay(10);

	CAM_DEBUG("1. PMIC ON ");
	//s5k6aafx_ldo_power_on();
	cam_ldo_power_on();
	
	
	CAM_DEBUG("3. CAM_VGA_STBY HIGH ");
	gpio_set_value_cansleep(CAM_VGA_EN, HIGH);
	mdelay(10);

	CAM_DEBUG("2.set MCLK 24MHz");
	//msm_camio_clk_rate_set(info->mclk);
	msm_camio_clk_rate_set(24000000);
	mdelay(5);
	
	
	CAM_DEBUG("4. CAM_VGA_RST HIGH ");
	gpio_set_value_cansleep(CAM_VGA_RST, HIGH);
	mdelay(100);

	//s5k6aafx_start();
	//s5k6aafx_i2c_write_list(reg_reset,sizeof(reg_reset)/sizeof(reg_reset[0]),"reg_reset");
	//mdelay(100);

	return rc;
}




int s5k6aafx_sensor_open_init(const struct msm_camera_sensor_info *data)
{
	int rc = 0;

	CAM_DEBUG("%s E\n",__FUNCTION__);

	
	config_csi2 = 0;
	
	s5k6aafx_ctrl = kzalloc(sizeof(struct s5k6aafx_ctrl_t), GFP_KERNEL);
	if (!s5k6aafx_ctrl) {
		CDBG("s5k6aafx_sensor_open_init failed!\n");
		rc = -ENOMEM;
		goto init_done;
	}	
	
	if (data)
		s5k6aafx_ctrl->sensordata = data;

#if 0//PGH
	/* Input MCLK = 24MHz */
	gpio_set_value(CAM_PMIC_STBY, 1);

	msm_camio_clk_rate_set(24000000);
	mdelay(5);

 //       msm_camio_camif_pad_reg_reset();
#endif//PGH
	s5k6aafx_ctrl->started = 0;

	
	rc = s5k6aafx_sensor_init_probe(data);
	if (rc < 0) {
		CDBG("s5k6aafx_sensor_open_init failed!\n");
		goto init_fail;
	}

	s5k6aafx_ctrl->app_mode = CAMERA_MODE;	
	CAM_DEBUG("%s X\n",__FUNCTION__);
init_done:
	return rc;

init_fail:
	kfree(s5k6aafx_ctrl);
	return rc;
}

static int s5k6aafx_init_client(struct i2c_client *client)
{
	/* Initialize the MSM_CAMI2C Chip */
	init_waitqueue_head(&s5k6aafx_wait_queue);
	return 0;
}



int s5k6aafx_sensor_ext_config(void __user *argp)
{
	sensor_ext_cfg_data		cfg_data;
	int rc=0;

	if(copy_from_user((void *)&cfg_data, (const void *)argp, sizeof(cfg_data)))
	{
		printk("<=PCAM=> %s fail copy_from_user!\n", __func__);
	}

	CAM_DEBUG("s5k6aafx_sensor_ext_config, cmd = %d , param1 = %d",cfg_data.cmd,cfg_data.value_1);
	
	switch(cfg_data.cmd) {
	case EXT_CFG_SET_BRIGHTNESS:
		rc = s5k6aafx_set_ev(cfg_data.value_1);
		break;
		
	case EXT_CFG_SET_DTP:
		rc = s5k6aafx_set_dtp(&cfg_data.value_1);
		break;

	case EXT_CFG_SET_FPS:
		rc = s5k6aafx_set_fps(cfg_data.value_1,cfg_data.value_2);
		break;

	case EXT_CFG_SET_FRONT_CAMERA_MODE:
		CAM_DEBUG("VTCall mode : %d",cfg_data.value_1);
		s5k6aafx_ctrl->vtcall_mode = cfg_data.value_1;
		break;

	default:
		break;
	}

	if(copy_to_user((void *)argp, (const void *)&cfg_data, sizeof(cfg_data)))
	{
		printk(" %s : copy_to_user Failed \n", __func__);
	}
	
	return rc;	
}

int s5k6aafx_sensor_config(void __user *argp)
{
	struct sensor_cfg_data cfg_data;
	long   rc = 0;

	if (copy_from_user(
				&cfg_data,
				(void *)argp,
				sizeof(struct sensor_cfg_data)))
		return -EFAULT;

	/* down(&m5mo_sem); */

	CDBG("s5k6aafx_sensor_config, cfgtype = %d, mode = %d\n",
		cfg_data.cfgtype, cfg_data.mode);

	switch (cfg_data.cfgtype) {
	case CFG_SET_MODE:
		rc = s5k6aafx_set_sensor_mode(cfg_data.mode);
		break;

	case CFG_SET_EFFECT:
		rc = s5k6aafx_set_effect(cfg_data.mode, cfg_data.cfg.effect);
		break;
		
	default:
		rc = -EFAULT;
		break;
	}

	/* up(&m5mo_sem); */

	return rc;
}

int s5k6aafx_sensor_release(void)
{
	int rc = 0;

	/* down(&m5mo_sem); */

	CAM_DEBUG("POWER OFF");
	printk("camera turn off\n");

	CAM_DEBUG(" 1. CAM_VGA_EN = 0 ");
	gpio_set_value_cansleep(CAM_VGA_EN, LOW);
	mdelay(1);

	CAM_DEBUG(" 2. CAM_VGA_RST = 0 ");
	gpio_set_value_cansleep(CAM_VGA_RST, LOW);

	CAM_DEBUG(" 3. CAM_PMIC_STBY = 0 ");
	gpio_set_value(CAM_PMIC_STBY, 0);

	//cam_pmic_onoff(OFF);	// have to turn off MCLK before PMIC
	kfree(s5k6aafx_ctrl);
	
#ifdef CONFIG_LOAD_FILE
	s5k6aafx_regs_table_exit();
#endif
	/* up(&m5mo_sem); */

	return rc;
}

//PGH:KERNEL2.6.25static int m5mo_probe(struct i2c_client *client)
static int s5k6aafx_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int rc = 0;

	CAM_DEBUG("%s E\n",__FUNCTION__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		rc = -ENOTSUPP;
		goto probe_failure;
	}

	s5k6aafx_sensorw =
		kzalloc(sizeof(struct s5k6aafx_work_t), GFP_KERNEL);

	if (!s5k6aafx_sensorw) {
		rc = -ENOMEM;
		goto probe_failure;
	}

	i2c_set_clientdata(client, s5k6aafx_sensorw);
	s5k6aafx_init_client(client);
	s5k6aafx_client = client;

	CDBG("s5k6aafx_i2c_probe successed!\n");
	CAM_DEBUG("%s X\n",__FUNCTION__);


	return 0;

probe_failure:
	kfree(s5k6aafx_sensorw);
	s5k6aafx_sensorw = NULL;
	CDBG("s5k6aafx_i2c_probe failed!\n");
	CAM_DEBUG("s5k6aafx_i2c_probe failed!\n");
	return rc;
}


static int __exit s5k6aafx_i2c_remove(struct i2c_client *client)
{

	struct s5k6aafx_work_t *sensorw = i2c_get_clientdata(client);
	free_irq(client->irq, sensorw);
//	i2c_detach_client(client);
	s5k6aafx_client = NULL;
	s5k6aafx_sensorw = NULL;
	kfree(sensorw);
	return 0;

}


static const struct i2c_device_id s5k6aafx_id[] = {
    { "s5k6aafx_i2c", 0 },
    { }
};

//PGH MODULE_DEVICE_TABLE(i2c, s5k6aafx);

static struct i2c_driver s5k6aafx_i2c_driver = {
	.id_table	= s5k6aafx_id,
	.probe  	= s5k6aafx_i2c_probe,
	.remove 	= __exit_p(s5k6aafx_i2c_remove),
	.driver 	= {
		.name = "s5k6aafx",
	},
};


int32_t s5k6aafx_i2c_init(void)
{
	int32_t rc = 0;

	CAM_DEBUG("%s E\n",__FUNCTION__);

	rc = i2c_add_driver(&s5k6aafx_i2c_driver);

	if (IS_ERR_VALUE(rc))
		goto init_failure;

	return rc;



init_failure:
	CDBG("failed to s5k6aafx_i2c_init, rc = %d\n", rc);
	return rc;
}


void s5k6aafx_exit(void)
{
	i2c_del_driver(&s5k6aafx_i2c_driver);
//PGH CHECK 	i2c_del_driver(&cam_pm_lp8720_driver);
	
}


//int m5mo_sensor_probe(void *dev, void *ctrl)
int s5k6aafx_sensor_probe(const struct msm_camera_sensor_info *info,
				struct msm_sensor_ctrl *s)
{
	int rc = 0;

	CAM_DEBUG("%s E\n",__FUNCTION__);
/*	struct msm_camera_sensor_info *info =
		(struct msm_camera_sensor_info *)dev; 

	struct msm_sensor_ctrl *s =
		(struct msm_sensor_ctrl *)ctrl;
*/

#if 0//PGH
	CAM_DEBUG("START 5M SWI2C VER0.2 : %d", rc);

	gpio_direction_output(CAM_FLASH_EN, LOW); //CAM_FLASH_EN
	gpio_direction_output(CAM_FLASH_SET, LOW); //CAM_FLASH_SET
#endif//PGH

	rc = s5k6aafx_i2c_init();
	if (rc < 0)
		goto probe_done;

	/* Input MCLK = 24MHz */
#if 0//PGH
	msm_camio_clk_rate_set(24000000);
//	mdelay(5);
#endif//PGH

#if 0
	rc = m5mo_sensor_init_probe(info);
	if (rc < 0)
		goto probe_done;
#endif
	s->s_init	= s5k6aafx_sensor_open_init;
	s->s_release	= s5k6aafx_sensor_release;
	s->s_config	= s5k6aafx_sensor_config;
	s->s_mount_angle = 0;

probe_done:
	CDBG("%s %s:%d\n", __FILE__, __func__, __LINE__);
	return rc;
	
}

#if 0
static int msm_vga_camera_remove(struct platform_device *pdev)
{
	return msm_camera_drv_remove(pdev);
}
#endif
static int __sec_s5k6aafx_probe(struct platform_device *pdev)
{
	printk("############# S5K6AAFX probe ##############\n");
	return msm_camera_drv_start(pdev, s5k6aafx_sensor_probe);
}

static struct platform_driver msm_vga_camera_driver = {
	.probe = __sec_s5k6aafx_probe,
//	.remove	 = msm_vga_camera_remove,
	.driver = {
		.name = "msm_camera_s5k6aafx",
		.owner = THIS_MODULE,
	},
};

static int __init sec_s5k6aafx_camera_init(void)
{
	return platform_driver_register(&msm_vga_camera_driver);
}

static void __exit sec_s5k6aafx_camera_exit(void)
{
	platform_driver_unregister(&msm_vga_camera_driver);
}

module_init(sec_s5k6aafx_camera_init);
module_exit(sec_s5k6aafx_camera_exit);

