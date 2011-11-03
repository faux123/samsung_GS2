/*
  SEC S5K5AAFA
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

#include "sec_m5mo.h"

#include "sec_s5k5aafa.h"
//#include "reg_val_s5k5aafa.h"	// from StealthV
#include  "5AAsensorsetting.h"	// from Truly

#include <asm/gpio.h> //PGH

#include <linux/clk.h>
#include <linux/io.h>
#include <mach/board.h>

#define S5K5AAFA_DEBUG
#ifdef S5K5AAFA_DEBUG
#define CAM_DEBUG(fmt, arg...)	\
		do{\
		printk("\033[[s5k5aafa] %s:%d: " fmt "\033[0m\n", __FUNCTION__, __LINE__, ##arg);}\
		while(0)
#else
#define CAM_DEBUG(fmt, arg...)	
#endif


/*	Read setting file from SDcard
	- There must be no "0x" string in comment. If there is, it cause some problem.
*/
//#define CONFIG_LOAD_FILE

struct s5k5aafa_work_t {
	struct work_struct work;
};

static struct  s5k5aafa_work_t *s5k5aafa_sensorw;
static struct  i2c_client *s5k5aafa_client;

struct s5k5aafa_ctrl_t {
	int8_t  opened;
	const struct  msm_camera_sensor_info 	*sensordata;
	int dtp_mode;
	int app_mode;	// camera or camcorder
	int vtcall_mode;
};



static struct s5k5aafa_ctrl_t *s5k5aafa_ctrl;
static DECLARE_WAIT_QUEUE_HEAD(s5k5aafa_wait_queue);
DECLARE_MUTEX(s5k5aafa_sem);

#ifdef CONFIG_LOAD_FILE
static int s5k5aafa_regs_table_write(char *name);
#endif
static int s5k5aafa_start(void);

int s5k5aafa_i2c_tx_data(char* txData, int length)
{
	int rc; 

	struct i2c_msg msg[] = {
		{
			.addr = s5k5aafa_client->addr,
			.flags = 0,
			.len = length,
			.buf = txData,		
		},
	};
    
	rc = i2c_transfer(s5k5aafa_client->adapter, msg, 1);
	if (rc < 0) {
		printk(KERN_ERR "s5k5aafa: s5k5aafa_i2c_tx_data error %d\n", rc);
		return rc;
	}

	return 0;
}

static int s5k5aafa_i2c_read(unsigned short page, unsigned short subaddr, unsigned short *data)
{
	int ret;
	unsigned char buf[1] = {0};
	struct i2c_msg msg = { s5k5aafa_client->addr, 0, 2, buf };

	/* page select */
	buf[0] = 0xFC;
	buf[1] = page;
	ret = i2c_transfer(s5k5aafa_client->adapter, &msg, 1) == 1 ? 0 : -EIO;
	if (ret == -EIO) 
		goto error;
	
	/* read data */
	msg.buf[0] = subaddr;
	msg.len = 1;
	ret = i2c_transfer(s5k5aafa_client->adapter, &msg, 1) == 1 ? 0 : -EIO;
	if (ret == -EIO) 
		goto error;

	msg.flags = I2C_M_RD;
	
	ret = i2c_transfer(s5k5aafa_client->adapter, &msg, 1) == 1 ? 0 : -EIO;
	if (ret == -EIO) 
		goto error;

//	*data = ((buf[0] << 8) | buf[1]);
	*data = buf[0];

error:
	return ret;
}

static int s5k5aafa_i2c_write(unsigned char u_addr, unsigned char u_data)
{
	unsigned char buf[2] = {0};
	struct i2c_msg msg = { s5k5aafa_client->addr, 0, 2, buf };

	buf[0] = u_addr;
	buf[1] = u_data;

	//printk("addr : 0x%x , value : 0x%x\n",u_addr,u_data);
	return i2c_transfer(s5k5aafa_client->adapter, &msg, 1) == 1 ? 0 : -EIO;
}



static int s5k5aafa_i2c_write_list(const unsigned short *list,int size, char *name)
{

	int ret = 0;
	int i;
	unsigned char addr, value;

#ifdef CONFIG_LOAD_FILE		
	ret = s5k5aafa_regs_table_write(name);
#else

	CAM_DEBUG("list name : %s\n",name);
	for (i = 0; i < size; i++)
	{
		addr = (unsigned char)((list[i] & 0xFF00)>>8);
		value =(unsigned char)( list[i] & 0x00FF);

		//printk("addr = 0x%x, value=0x%x \n",addr,value);

		if(addr == 0xff)
		{
			CAM_DEBUG("Delays for Snapshot - %d ms\n",value*10);
			msleep(value*10);
		}
		else
		{
			if(s5k5aafa_i2c_write(addr, value) < 0)
			{
				printk("<=PCAM=> sensor_write_list fail...-_-\n");
				return -1;
			}
		}
		udelay(10);

	}
#endif
	return ret;
}



#ifdef CONFIG_LOAD_FILE
static char *s5k5aafa_regs_table = NULL;

static int s5k5aafa_regs_table_size;

void s5k5aafa_regs_table_init(void)
{
	struct file *filp;
	char *dp;
	long l;
	loff_t pos;
//	int i;
	int ret;
	mm_segment_t fs = get_fs();

	printk("%s %d\n", __func__, __LINE__);

	set_fs(get_ds());

	filp = filp_open("/sdcard/5AAsensorsetting.h", O_RDONLY, 0);

	if (IS_ERR(filp)) {
		printk("file open error\n");
		return;
	}
	l = filp->f_path.dentry->d_inode->i_size;	
	printk("l = %ld\n", l);
	dp = kmalloc(l, GFP_KERNEL);
	if (dp == NULL) {
		printk("Out of Memory\n");
		filp_close(filp, current->files);
	}
	pos = 0;
	memset(dp, 0, l);
	ret = vfs_read(filp, (char __user *)dp, l, &pos);
	if (ret != l) {
		printk("Failed to read file ret = %d\n", ret);
		kfree(dp);
		filp_close(filp, current->files);
		return;
	}

	filp_close(filp, current->files);
	
	set_fs(fs);

	s5k5aafa_regs_table = dp;
	
	s5k5aafa_regs_table_size = l;

	*((s5k5aafa_regs_table + s5k5aafa_regs_table_size) - 1) = '\0';

//	printk("s5k5aafa_regs_table 0x%x, %ld\n", dp, l);
}

void s5k5aafa_regs_table_exit(void)
{
	printk("%s %d\n", __func__, __LINE__);
	if (s5k5aafa_regs_table) {
		kfree(s5k5aafa_regs_table);
		s5k5aafa_regs_table = NULL;
	}	
}

static int s5k5aafa_regs_table_write(char *name)
{
	char *start, *end, *reg;//, *data;	
	unsigned short addr, value;
	char reg_buf[3]={0,}, data_buf[3]={0,};

	addr = value = 0;

/*	*(reg_buf + 4) = '\0';
	*(data_buf + 4) = '\0';
*/
	CAM_DEBUG("list name : %s\n",name);

	start = strstr(s5k5aafa_regs_table, name);
	
	end = strstr(start, "};");

	while (1) {	
		/* Find Address */	
		reg = strstr(start,"0x");		
		if (reg)
			start = (reg + 7);
		if ((reg == NULL) || (reg > end))
			break;
		/* Write Value to Address */	
		if (reg != NULL) {
			memcpy(reg_buf, (reg + 2), 2);	
			memcpy(data_buf, (reg + 4), 2);	

			addr = (unsigned short)simple_strtoul(reg_buf, NULL, 16); 
			value = (unsigned short)simple_strtoul(data_buf, NULL, 16); 

			//printk("addr 0x%x, value 0x%x\n", addr, value);

			if(addr == 0xff)
			{
				CAM_DEBUG("Delays for Snapshot - %d ms\n",value*10);
				msleep(value*10);
			}	
			else
			{
				if( s5k5aafa_i2c_write(addr, value) < 0 )
				{
					printk("<=PCAM=> sensor_write_list fail...-_-\n");
					return -1;
				}
			}
			udelay(10);	
		}
		else
			printk("<=PCAM=> EXCEPTION! reg value : %c  addr : 0x%x,  value : 0x%x\n", *reg, addr, value);
	}

	return 0;
}
#endif



//static long s5k5aafa_set_sensor_mode(enum sensor_mode_t mode)
static long s5k5aafa_set_sensor_mode(int mode)
{
	unsigned short value =0;
	int shade_value = 0;
	unsigned short agc_value = 0;
	switch (mode) 
	{
	case SENSOR_PREVIEW_MODE:
		CAM_DEBUG("SENSOR_PREVIEW_MODE START\n");

		if(s5k5aafa_ctrl->dtp_mode == 1)
		{
			//s5k5aafa_i2c_write_list(reg_dtp_preview_camera_list,sizeof(reg_dtp_preview_camera_list)/sizeof(reg_dtp_preview_camera_list[0]),"reg_dtp_preview_camera_list");
		}
		else
		{
			if(s5k5aafa_ctrl->vtcall_mode)
			{
				s5k5aafa_i2c_write_list(vtmode_list,sizeof(vtmode_list)/sizeof(vtmode_list[0]),"vtmode_list");
			}
			else
			{
				s5k5aafa_i2c_read(0x00,0xE5,&value);
				CAM_DEBUG(" 0x00,0xE5 value : 0x%x\n",value);
				shade_value = value * 0x100;
				s5k5aafa_i2c_read(0x00,0xE6,&value);
				CAM_DEBUG(" 0x00,0xE6 value : 0x%x\n",value);
				shade_value += value;

				if((shade_value >= reg_AgcValue_list[3]) && (shade_value <= reg_AgcValue_list[4]))
				{
						s5k5aafa_i2c_write_list(reg_shade_dnp_list,sizeof(reg_shade_dnp_list)/sizeof(reg_shade_dnp_list[0]),"reg_shade_dnp_list");
				}
				else
				{
					s5k5aafa_i2c_write_list(reg_shade_etc_list,sizeof(reg_shade_etc_list)/sizeof(reg_shade_etc_list[0]),"reg_shade_etc_list");				
				}

				
				if(s5k5aafa_ctrl->app_mode == CAMCORDER_MODE) 
				{
					s5k5aafa_i2c_write_list(reg_preview_camcorder_list,sizeof(reg_preview_camcorder_list)/sizeof(reg_preview_camcorder_list[0]),"reg_preview_camcorder_list");
				}
				else		// camera mode preview
				{				
					s5k5aafa_i2c_write_list(reg_preview_camera_list,sizeof(reg_preview_camera_list)/sizeof(reg_preview_camera_list[0]),"reg_preview_camera_list");
					s5k5aafa_i2c_read(0x00,0xF8,&agc_value);
					if(agc_value <= reg_AgcValue_list[0])
					{
						s5k5aafa_i2c_write_list(reg_preview_high_light_list,sizeof(reg_preview_high_light_list)/sizeof(reg_preview_high_light_list[0]),"reg_preview_high_light_list");				
					}
					else
					{
						s5k5aafa_i2c_write_list(reg_preview_normal_light_list,sizeof(reg_preview_normal_light_list)/sizeof(reg_preview_normal_light_list[0]),"reg_preview_normal_light_list");				
					}
				}
			}
		}
	
		//test
		printk("<=PCAM=> test delay 150~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
		//mdelay(150);
		msleep(300);
	
		break;

	case SENSOR_SNAPSHOT_MODE:
		CAM_DEBUG("SENSOR_SNAPSHOT_MODE START\n");		

		printk("<=PCAM=> SENSOR_SNAPSHOT_MODE\n");
		
		s5k5aafa_i2c_read(0x00,0xE5,&value);
		CAM_DEBUG(" 0x00,0xE5 value : 0x%x\n",value);
		shade_value = value * 0x100;
		s5k5aafa_i2c_read(0x00,0xE6,&value);
		CAM_DEBUG(" 0x00,0xE6 value : 0x%x\n",value);
		shade_value += value;

		if((shade_value >= reg_AgcValue_list[3]) && (shade_value <= reg_AgcValue_list[4]))
		{
			s5k5aafa_i2c_write_list(reg_shade_dnp_list,sizeof(reg_shade_dnp_list)/sizeof(reg_shade_dnp_list[0]),"reg_shade_dnp_list");
		}
		else 
		{
			s5k5aafa_i2c_write_list(reg_shade_etc_list,sizeof(reg_shade_etc_list)/sizeof(reg_shade_etc_list[0]),"reg_shade_etc_list");				
		}


		s5k5aafa_i2c_read(0x00,0xf8,&agc_value);
		CAM_DEBUG("agc_value = 0x%x\n",agc_value);

		if(agc_value >= reg_AgcValue_list[2])
		{
			s5k5aafa_i2c_write_list(reg_snapshot_lowlight_list,sizeof(reg_snapshot_lowlight_list)/sizeof(reg_snapshot_lowlight_list[0]),"reg_snapshot_lowlight_list");
		}
		else if(agc_value >= reg_AgcValue_list[1])
		{
			s5k5aafa_i2c_write_list(reg_snapshot_mid_lowlight_list,sizeof(reg_snapshot_mid_lowlight_list)/sizeof(reg_snapshot_mid_lowlight_list[0]),"reg_snapshot_mid_lowlight_list");
		}
		else if(agc_value <= reg_AgcValue_list[0])
		{
			s5k5aafa_i2c_write_list(reg_snapshot_list,sizeof(reg_snapshot_list)/sizeof(reg_snapshot_list[0]),"reg_snapshot_list");
		}
		else
		{
			s5k5aafa_i2c_write_list(reg_snapshot_midlight_list,sizeof(reg_snapshot_midlight_list)/sizeof(reg_snapshot_midlight_list[0]),"reg_snapshot_midlight_list");
		}
		//test
		printk("<=PCAM=> so many 200msecdelay~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
		//mdelay(200);
		msleep(200);
		
		break;

	
	case SENSOR_SNAPSHOT_TRANSFER:
		CAM_DEBUG("SENSOR_SNAPSHOT_TRANSFER START\n");


		break;
		
	default:
		return -EFAULT;
	}

	return 0;
}


static long s5k5aafa_set_effect(
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



static int s5k5aafa_reset(void)
{
	CAM_DEBUG("s5k5aafa_reset");
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

	return 0;
}


static int
s5k5aafa_set_ev(
	int8_t ev)
{
	CAM_DEBUG(" ev : %d \n",ev);
	
	switch(ev)
	{
		case S5K5AAFA_EV_MINUS_4:
			s5k5aafa_i2c_write_list(reg_brightness_0_list,sizeof(reg_brightness_0_list)/sizeof(reg_brightness_0_list[0]),"reg_brightness_0_list");
			break;
		case S5K5AAFA_EV_MINUS_3:
			s5k5aafa_i2c_write_list(reg_brightness_1_list,sizeof(reg_brightness_1_list)/sizeof(reg_brightness_1_list[0]),"reg_brightness_1_list");
			break;
		case S5K5AAFA_EV_MINUS_2:
			s5k5aafa_i2c_write_list(reg_brightness_2_list,sizeof(reg_brightness_2_list)/sizeof(reg_brightness_2_list[0]),"reg_brightness_2_list");
			break;
		case S5K5AAFA_EV_MINUS_1:
			s5k5aafa_i2c_write_list(reg_brightness_3_list,sizeof(reg_brightness_3_list)/sizeof(reg_brightness_3_list[0]),"reg_brightness_3_list");
			break;
		case S5K5AAFA_EV_DEFAULT:
			s5k5aafa_i2c_write_list(reg_brightness_4_list,sizeof(reg_brightness_4_list)/sizeof(reg_brightness_4_list[0]),"reg_brightness_4_list");
			break;
		case S5K5AAFA_EV_PLUS_1:
			s5k5aafa_i2c_write_list(reg_brightness_5_list,sizeof(reg_brightness_5_list)/sizeof(reg_brightness_5_list[0]),"reg_brightness_5_list");
			break;
		case S5K5AAFA_EV_PLUS_2:
			s5k5aafa_i2c_write_list(reg_brightness_6_list,sizeof(reg_brightness_6_list)/sizeof(reg_brightness_6_list[0]),"reg_brightness_6_list");
			break;	
		case S5K5AAFA_EV_PLUS_3:
			s5k5aafa_i2c_write_list(reg_brightness_7_list,sizeof(reg_brightness_7_list)/sizeof(reg_brightness_7_list[0]),"reg_brightness_7_list");
			break;
		case S5K5AAFA_EV_PLUS_4:
			s5k5aafa_i2c_write_list(reg_brightness_8_list,sizeof(reg_brightness_8_list)/sizeof(reg_brightness_8_list[0]),"reg_brightness_8_list");
			break;
		default:
			printk("[EV] Invalid EV !!!\n");
			return -EINVAL;
	}

	return 0;
}

static int
s5k5aafa_set_dtp(
	int* onoff)
{
	CAM_DEBUG(" onoff : %d ",*onoff);

	switch((*onoff))
	{
		case S5K5AAFA_DTP_OFF:
			if(s5k5aafa_ctrl->dtp_mode)
				s5k5aafa_reset();			
			s5k5aafa_ctrl->dtp_mode = 0;

			/* set ACK value */
			
			//s5k5aafa_start();
			s5k5aafa_i2c_write_list(reg_init_list,sizeof(reg_init_list)/sizeof(reg_init_list[0]),"reg_init_list");
			
			//s5k5aafa_set_sensor_mode(SENSOR_PREVIEW_MODE);

			(*onoff) = M5MO_DTP_OFF_ACK;
			break;
		case S5K5AAFA_DTP_ON:
			s5k5aafa_reset();
			s5k5aafa_i2c_write_list(reg_DTP_list,sizeof(reg_DTP_list)/sizeof(reg_DTP_list[0]),"reg_DTP_list");
			s5k5aafa_ctrl->dtp_mode = 1;

			/* set ACK value */
			(*onoff) = M5MO_DTP_ON_ACK;
			break;
		default:
			printk("[DTP]Invalid DTP mode!!!\n");
			return -EINVAL;
	}
	return 0;
}


static int
s5k5aafa_set_fps(
	unsigned int mode, unsigned int fps)
{
	CAM_DEBUG(" %s -mode : %d, fps : %d \n",__FUNCTION__,mode,fps);

	if(mode)
	{
		CAM_DEBUG("mode change to CAMCORDER_MODE");
		s5k5aafa_ctrl->app_mode = CAMCORDER_MODE;
	}
	else
	{
		CAM_DEBUG("mode change to CAMERA_MODE");
		s5k5aafa_ctrl->app_mode = CAMERA_MODE;
	}
	return 0;
}
#if 0
int cam_pmic_onoff(int onoff)
{
	static int last_state = -1;

	if(last_state == onoff)
	{
		CAM_DEBUG("%s : PMIC already %d\n",__FUNCTION__,onoff);
		return 0;
	}
	
	if(onoff)		// ON
	{
		CAM_DEBUG("%s ON\n",__FUNCTION__);

		//gpio_direction_output(CAM_PMIC_STBY, LOW);		// set PMIC to STBY mode
		gpio_set_value(CAM_PMIC_STBY, 0);
		mdelay(2);
		
		cam_pm_lp8720_i2c_write(0x07, 0x09); // BUCKS2:1.2V, no delay
		cam_pm_lp8720_i2c_write(0x04, 0x05); // LDO4:1.2V, no delay
		cam_pm_lp8720_i2c_write(0x02, 0x19); // LDO2 :2.8V, no delay
		cam_pm_lp8720_i2c_write(0x05, 0x0C); // LDO5 :1.8V, no delay
		cam_pm_lp8720_i2c_write(0x01, 0x19); // LDO1 :2.8V, no delay
		cam_pm_lp8720_i2c_write(0x08, 0xBB); // Enable all power without LDO3
		
		//gpio_direction_output(CAM_PMIC_STBY, HIGH);
		gpio_set_value(CAM_PMIC_STBY, 1);
		mdelay(5);
	}
	else
	{
		CAM_DEBUG("%s OFF\n",__FUNCTION__);
	}

	return 0;
}
#endif

#if 0 // for_8M_cam
static int s5k5aafa_init_regs(void)
{
	CAM_DEBUG("%s E\n",__FUNCTION__);
	return 0;
}


static int s5k5aafa_i2c_test(void)
{
	unsigned short value=0;
	CAM_DEBUG("%s E\n",__FUNCTION__);
	CAM_DEBUG("----- WRITE -----\n");
	s5k5aafa_i2c_write(0xfc,0x00);
	CAM_DEBUG("write : 0x02 - 0x08, 0x03 - 0x4b\n");
	s5k5aafa_i2c_write(0x02,0x08);
	s5k5aafa_i2c_write(0x03,0x4b);
	
	CAM_DEBUG("----- READ -----\n");
	s5k5aafa_i2c_read(0x00,0x02,&value);
	CAM_DEBUG(" 0x02, value : 0x%x\n",value);
	s5k5aafa_i2c_read(0x00,0x03,&value);
	CAM_DEBUG(" 0x03 value : 0x%x\n",value);
	s5k5aafa_i2c_read(0x01,0x01,&value);
	CAM_DEBUG(" 0x01 value : 0x%x\n",value);

	return 0;
}
#endif

static int s5k5aafa_start(void)
{
	int rc=0;
	unsigned short value=0;

	CAM_DEBUG("%s E\n",__FUNCTION__);

	//s5k5aafa_i2c_test();
	
	//s5k5aafa_i2c_write_list(INIT_DATA,sizeof(INIT_DATA)/sizeof(INIT_DATA[0]),"INIT_DATA");
	s5k5aafa_i2c_write_list(reg_init_list,sizeof(reg_init_list)/sizeof(reg_init_list[0]),"reg_init_list");
	s5k5aafa_i2c_write_list(reg_effect_none_list,sizeof(reg_effect_none_list)/sizeof(reg_effect_none_list[0]),"reg_effect_none_list");
	s5k5aafa_i2c_write_list(reg_meter_center_list,sizeof(reg_meter_center_list)/sizeof(reg_meter_center_list[0]),"reg_meter_center_list");
	s5k5aafa_i2c_write_list(reg_wb_auto_list,sizeof(reg_wb_auto_list)/sizeof(reg_wb_auto_list[0]),"reg_wb_auto_list");
	
	s5k5aafa_i2c_read(0x00,0x7b,&value);
	CAM_DEBUG(" 0x7b value : 0x%x\n",value);
/*
	s5k5aafa_i2c_read(0x00,0x00,&value);
	CAM_DEBUG("read test -- reg - 0x%x, value - 0x%x\n",0x00 , value);
*/	return rc;
}

static int s5k5aafa_sensor_init_probe(const struct msm_camera_sensor_info *data)
{
	int rc = 0;
//	int read_value=-1;
//	unsigned short read_value_1 = 0;
//	int i; //for loop
//	int cnt = 0;
	CAM_DEBUG("s5k5aafa_sensor_init_probe start");
	if (system_rev >= 4) 
		gpio_set_value(CAM_MEGA_EN_REV04, LOW);
	else
		gpio_set_value(CAM_MEGA_EN, LOW);
	//gpio_set_value(CAM_8M_RST, LOW);
	gpio_set_value_cansleep(CAM_8M_RST, LOW);

	CAM_DEBUG(" CAM PM already on \n");
	//CAM_DEBUG(" 1. PMIC ON ");
	//cam_pmic_onoff(ON);

	//CAM_DEBUG(" 1. CAM_8M_RST = 0 ");
	//gpio_set_value(CAM_8M_RST, LOW);

	mdelay(7);
	CAM_DEBUG(" 2. CAM_VGA_EN = 1 ");
	gpio_set_value_cansleep(CAM_VGA_EN, HIGH);

	//mdelay(20);
	mdelay(5);

	CAM_DEBUG(" 3. CAM_VGA_RST = 1 ");
	gpio_set_value_cansleep(CAM_VGA_RST, HIGH);
	mdelay(100); // min 350ns
	
#if 0
////////////////////////////////////////////////////
#if 0//Mclk_timing for M4Mo spec.		// -Jeonhk clock was enabled in vfe31_init
	msm_camio_clk_enable(CAMIO_VFE_CLK);
	msm_camio_clk_enable(CAMIO_MDC_CLK);
	msm_camio_clk_enable(CAMIO_VFE_MDC_CLK);
#endif	

	CAM_DEBUG("START MCLK:24Mhz~~~~~");
//	msm_camio_clk_rate_set(24000000);
	mdelay(5);
	msm_camio_camif_pad_reg_reset();		// this is not
	mdelay(10);
////////////////////////////////////////////////////
#endif

	s5k5aafa_start();

	return rc;

// for_8M_cam init_probe_fail:
	return rc;
}




int s5k5aafa_sensor_init(const struct msm_camera_sensor_info *data)
{
	int rc = 0;

	CAM_DEBUG("%s E\n",__FUNCTION__);

	
	s5k5aafa_ctrl = kzalloc(sizeof(struct s5k5aafa_ctrl_t), GFP_KERNEL);
	if (!s5k5aafa_ctrl) {
		CDBG("s5k5aafa_sensor_init failed!\n");
		rc = -ENOMEM;
		goto init_done;
	}	
	
	if (data)
		s5k5aafa_ctrl->sensordata = data;

#if 1//PGH
	/* Input MCLK = 24MHz */
	msm_camio_clk_rate_set(24000000);
	mdelay(5);

// for_8M_cam 	msm_camio_camif_pad_reg_reset();
#endif//PGH

#ifdef CONFIG_LOAD_FILE
	s5k5aafa_regs_table_init();
#endif

	printk("[PGH] %s 222222\n", __func__);
  rc = s5k5aafa_sensor_init_probe(data);
	if (rc < 0) {
		CDBG("s5k5aafa_sensor_init failed!\n");
		goto init_fail;
	}

	s5k5aafa_ctrl->app_mode = CAMERA_MODE;	
	printk("[PGH] %s 3333  rc:%d\n", __func__, rc);
init_done:
	return rc;

init_fail:
	kfree(s5k5aafa_ctrl);
	return rc;
}

static int s5k5aafa_init_client(struct i2c_client *client)
{
	/* Initialize the MSM_CAMI2C Chip */
	init_waitqueue_head(&s5k5aafa_wait_queue);
	return 0;
}



int s5k5aafa_sensor_ext_config(void __user *argp)
{
	sensor_ext_cfg_data		cfg_data;
	int rc=0;

	if(copy_from_user((void *)&cfg_data, (const void *)argp, sizeof(cfg_data)))
	{
		printk("<=PCAM=> %s fail copy_from_user!\n", __func__);
	}

	CAM_DEBUG("s5k5aafa_sensor_ext_config, cmd = %d , param1 = %d",cfg_data.cmd,cfg_data.value_1);
	
	switch(cfg_data.cmd) {
	case EXT_CFG_SET_BRIGHTNESS:
		rc = s5k5aafa_set_ev(cfg_data.value_1);
		break;
		
	case EXT_CFG_SET_DTP:
		rc = s5k5aafa_set_dtp(&cfg_data.value_1);
		break;

	case EXT_CFG_SET_FPS:
		rc = s5k5aafa_set_fps(cfg_data.value_1,cfg_data.value_2);
		break;

	case EXT_CFG_SET_FRONT_CAMERA_MODE:
		CAM_DEBUG("VTCall mode : %d",cfg_data.value_1);
		s5k5aafa_ctrl->vtcall_mode = cfg_data.value_1;
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

int s5k5aafa_sensor_config(void __user *argp)
{
	struct sensor_cfg_data cfg_data;
	long   rc = 0;

	if (copy_from_user(
				&cfg_data,
				(void *)argp,
				sizeof(struct sensor_cfg_data)))
		return -EFAULT;

	/* down(&m5mo_sem); */

	CDBG("s5k5aafa_sensor_config, cfgtype = %d, mode = %d\n",
		cfg_data.cfgtype, cfg_data.mode);

	switch (cfg_data.cfgtype) {
	case CFG_SET_MODE:
		rc = s5k5aafa_set_sensor_mode(cfg_data.mode);
		break;

	case CFG_SET_EFFECT:
		rc = s5k5aafa_set_effect(cfg_data.mode, cfg_data.cfg.effect);
		break;
		
	default:
		rc = -EFAULT;
		break;
	}

	/* up(&m5mo_sem); */

	return rc;
}

int s5k5aafa_sensor_release(void)
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

/*
	if(system_rev >= 4)
	{
		CAM_DEBUG(" 2. CAM_SENSOR_A_EN = 0 ");
		gpio_set_value_cansleep(CAM_SENSOR_A_EN, 0);

		CAM_DEBUG(" 2. CAM_SENSOR_A_EN_ALTER = 0 ");
		gpio_set_value_cansleep(CAM_SENSOR_A_EN_ALTER, 0);

		vreg_disable(vreg_CAM_AF28);
	}
*/
	//cam_pmic_onoff(OFF);	// have to turn off MCLK before PMIC
	kfree(s5k5aafa_ctrl);
	
#ifdef CONFIG_LOAD_FILE
	s5k5aafa_regs_table_exit();
#endif

	/* up(&m5mo_sem); */

	return rc;
}

//PGH:KERNEL2.6.25static int m5mo_probe(struct i2c_client *client)
static int s5k5aafa_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int rc = 0;

	CAM_DEBUG("%s E\n",__FUNCTION__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		rc = -ENOTSUPP;
		goto probe_failure;
	}

	s5k5aafa_sensorw =
		kzalloc(sizeof(struct s5k5aafa_work_t), GFP_KERNEL);

	if (!s5k5aafa_sensorw) {
		rc = -ENOMEM;
		goto probe_failure;
	}

	i2c_set_clientdata(client, s5k5aafa_sensorw);
	s5k5aafa_init_client(client);
	s5k5aafa_client = client;

	CDBG("s5k5aafa_i2c_probe successed!\n");
	CAM_DEBUG("%s X\n",__FUNCTION__);


	return 0;

probe_failure:
	kfree(s5k5aafa_sensorw);
	s5k5aafa_sensorw = NULL;
	CDBG("s5k5aafa_i2c_probe failed!\n");
	CAM_DEBUG("s5k5aafa_i2c_probe failed!\n");
	return rc;
}


static int __exit s5k5aafa_i2c_remove(struct i2c_client *client)
{

	struct s5k5aafa_work_t *sensorw = i2c_get_clientdata(client);
	free_irq(client->irq, sensorw);
//	i2c_detach_client(client);
	s5k5aafa_client = NULL;
	s5k5aafa_sensorw = NULL;
	kfree(sensorw);
	return 0;

}


static const struct i2c_device_id s5k5aafa_id[] = {
    { "s5k5aafa_i2c", 0 },
    { }
};

//PGH MODULE_DEVICE_TABLE(i2c, s5k5aafa);

static struct i2c_driver s5k5aafa_i2c_driver = {
	.id_table	= s5k5aafa_id,
	.probe  	= s5k5aafa_i2c_probe,
	.remove 	= __exit_p(s5k5aafa_i2c_remove),
	.driver 	= {
		.name = "s5k5aafa",
	},
};


int32_t s5k5aafa_i2c_init(void)
{
	int32_t rc = 0;

	CAM_DEBUG("%s E\n",__FUNCTION__);

	rc = i2c_add_driver(&s5k5aafa_i2c_driver);

	if (IS_ERR_VALUE(rc))
		goto init_failure;

	return rc;



init_failure:
	CDBG("failed to s5k5aafa_i2c_init, rc = %d\n", rc);
	return rc;
}


void s5k5aafa_exit(void)
{
	i2c_del_driver(&s5k5aafa_i2c_driver);
//PGH CHECK 	i2c_del_driver(&cam_pm_lp8720_driver);
	
}


//int m5mo_sensor_probe(void *dev, void *ctrl)
int s5k5aafa_sensor_probe(const struct msm_camera_sensor_info *info,
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

	rc = s5k5aafa_i2c_init();
	if (rc < 0)
		goto probe_done;

	/* Input MCLK = 24MHz */
#if 1//PGH
	msm_camio_clk_rate_set(24000000);
//	mdelay(5);
#endif//PGH

#if 0
	rc = m5mo_sensor_init_probe(info);
	if (rc < 0)
		goto probe_done;
#endif
	s->s_init		= s5k5aafa_sensor_init;
	s->s_release	= s5k5aafa_sensor_release;
	s->s_config	= s5k5aafa_sensor_config;

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
static int __sec_s5k5aafa_probe(struct platform_device *pdev)
{
	printk("############# S5K5AAFA probe ##############\n");
	return msm_camera_drv_start(pdev, s5k5aafa_sensor_probe);
}

static struct platform_driver msm_vga_camera_driver = {
	.probe = __sec_s5k5aafa_probe,
//	.remove	 = msm_vga_camera_remove,
	.driver = {
		.name = "msm_camera_s5k5aafa",
		.owner = THIS_MODULE,
	},
};

static int __init sec_s5k5aafa_camera_init(void)
{
	return platform_driver_register(&msm_vga_camera_driver);
}

static void __exit sec_s5k5aafa_camera_exit(void)
{
	platform_driver_unregister(&msm_vga_camera_driver);
}

module_init(sec_s5k5aafa_camera_init);
module_exit(sec_s5k5aafa_camera_exit);

