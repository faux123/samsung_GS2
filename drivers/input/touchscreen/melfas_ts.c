/* drivers/input/touchscreen/melfas_ts.c
 *
 * Copyright (C) 2010 Melfas, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define SEC_TSP
#ifdef SEC_TSP
#define ENABLE_NOISE_TEST_MODE
#define TSP_FACTORY_TEST
#endif

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/melfas_ts.h>
#ifdef SEC_TSP
#include <linux/gpio.h>
#endif

#include <../mach-msm/smd_private.h>
#include <../mach-msm/smd_rpcrouter.h>

#define TS_MAX_Z_TOUCH			255
#define TS_MAX_W_TOUCH		100


#define TS_MAX_X_COORD 		720
#define TS_MAX_Y_COORD 		1280

#define FW_VERSION				0x29
#define TS_READ_START_ADDR 	0x0F
#define TS_READ_START_ADDR2 	0x10
#define TS_READ_VERSION_ADDR	0xF0
#ifdef SEC_TSP
#define P5_THRESHOLD 			0x05
#define TS_READ_REGS_LEN		5
#define TS_WRITE_REGS_LEN		16
#endif

#define TS_READ_REGS_LEN 		66
#define MELFAS_MAX_TOUCH		11

#define DEBUG_PRINT 			0


#define SET_DOWNLOAD_BY_GPIO	1

#define MIP_CONTACT_ON_EVENT_THRES		0x05	// (VC25_02, _04 는 110, VC25_03,_05은 40)
#define MIP_MOVING_EVENT_THRES			0x06	// jump limit (현재 1, ~ 255가능 1mm 12pixel 입니다.)
#define MIP_ACTIVE_REPORT_RATE			0x07	// 단위 hz (default 60,  30~ 255)
#define MIP_POSITION_FILTER_LEVEL		0x08	// = weight filter (default 40, 기존 f/w는 80, 1~ 255 가능함)

#define SET_TSP_CONFIG

#if SET_DOWNLOAD_BY_GPIO
#include <mcs8000_download.h>
#endif // SET_DOWNLOAD_BY_GPIO

unsigned long saved_rate;					
static bool lock_status;

static int tsp_enabled;
int touch_is_pressed;
static unsigned char is_inputmethod = 0;

struct muti_touch_info
{
	int strength;
	int width;	
	int posX;
	int posY;
};

struct melfas_ts_data 
{
	uint16_t addr;
	struct i2c_client *client; 
	struct input_dev *input_dev;
	struct work_struct  work;
	uint32_t flags;
	int (*power)(int on);
	int (*gpio)(void);
	struct early_suspend early_suspend;
};

struct melfas_ts_data *ts_data;

#ifdef SEC_TSP
extern struct class *sec_class;
extern char* get_s6e8aa0_id_buffer( void );
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void melfas_ts_early_suspend(struct early_suspend *h);
static void melfas_ts_late_resume(struct early_suspend *h);
#endif
#ifdef SEC_TSP
static int melfas_ts_suspend(struct i2c_client *client, pm_message_t mesg);
static int melfas_ts_resume(struct i2c_client *client);
static void release_all_fingers(struct melfas_ts_data *ts);
#endif

static struct muti_touch_info g_Mtouch_info[MELFAS_MAX_TOUCH];


static int melfas_init_panel(struct melfas_ts_data *ts)
{
	int buf = 0x00;
	int ret;
	ret = i2c_master_send(ts->client, &buf, 1);

	ret = i2c_master_send(ts->client, &buf, 1);

	if(ret <0)
	{
		printk(KERN_ERR "%s : i2c_master_send() failed\n [%d]", __func__, ret);	
		return 0;
	}

	return true;
}

static void melfas_ts_get_data(struct work_struct *work)
{
	struct melfas_ts_data *ts = container_of(work, struct melfas_ts_data, work);
	int ret = 0, i;
	uint8_t buf[TS_READ_REGS_LEN];
	int read_num, FingerID;
	int _touch_is_pressed;
#if DEBUG_PRINT
	printk(KERN_ERR "%s start\n", __func__);

	if(ts ==NULL)
			printk(KERN_ERR "%s : TS NULL\n", __func__);
#endif 

	buf[0] = TS_READ_START_ADDR;

	ret = i2c_master_send(ts->client, buf, 1);
	if(ret < 0)
	{
#if DEBUG_PRINT
		printk(KERN_ERR "%s: i2c failed(%d)\n",__func__, __LINE__);
		return ;	
#endif 
	}
	ret = i2c_master_recv(ts->client, buf, 1);
	if(ret < 0)
	{
#if DEBUG_PRINT
		printk(KERN_ERR "%s: i2c failed(%d)\n",__func__, __LINE__);
		return ;	
#endif 
	}

	read_num = buf[0];
	
	if(read_num>0)
	{
		buf[0] = TS_READ_START_ADDR2;

		ret = i2c_master_send(ts->client, buf, 1);
		if(ret < 0)
		{
#if DEBUG_PRINT
		printk(KERN_ERR "%s: i2c failed(%d)\n",__func__, __LINE__);
			return ;	
#endif 
		}
		ret = i2c_master_recv(ts->client, buf, read_num);
		if(ret < 0)
		{
#if DEBUG_PRINT
		printk(KERN_ERR "%s: i2c failed(%d)\n",__func__, __LINE__);
			return ;	
#endif 
		}

		for(i=0; i<read_num; i=i+6)
		{
			FingerID = (buf[i] & 0x0F) - 1;
			g_Mtouch_info[FingerID].posX= (uint16_t)(buf[i+1] & 0x0F) << 8 | buf[i+2];
			g_Mtouch_info[FingerID].posY= (uint16_t)(buf[i+1] & 0xF0) << 4 | buf[i+3];	
			
			if((buf[i] & 0x80)==0)
				g_Mtouch_info[FingerID].strength = 0;
			else
				g_Mtouch_info[FingerID].strength = buf[i+4];
			
			g_Mtouch_info[FingerID].width= buf[i+5];					
		}
	
	}

	_touch_is_pressed=0;
	if (ret < 0)
	{
		printk(KERN_ERR "%s: i2c failed(%d)\n",__func__, __LINE__);
		return ;	
	}
	else 
	{

		for(i=0; i<MELFAS_MAX_TOUCH; i++)
		{
			if(g_Mtouch_info[i].strength== -1)
				continue;
			
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, i);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, g_Mtouch_info[i].posX);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, g_Mtouch_info[i].posY);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, g_Mtouch_info[i].strength );
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, g_Mtouch_info[i].width);
			input_mt_sync(ts->input_dev);
//#if DEBUG_PRINT
			printk(KERN_ERR "[TSP] ID: %d, State : %d, x: %d, y: %d, z: %d w: %d\n", 
				i, (g_Mtouch_info[i].strength>0), g_Mtouch_info[i].posX, g_Mtouch_info[i].posY, g_Mtouch_info[i].strength, g_Mtouch_info[i].width);
//#endif	

			if(g_Mtouch_info[i].strength == 0)
				g_Mtouch_info[i].strength = -1;

			if(g_Mtouch_info[i].strength > 0)
				_touch_is_pressed = 1;
            
		}
		input_sync(ts->input_dev);
		touch_is_pressed = _touch_is_pressed;
			
		if(touch_is_pressed >0)	// when touch is pressed.
		{
			if (lock_status == 0) {
				lock_status = 1;
			}
		}
		else	// when touch is released.
		{
			if(read_num>0)
			{
				lock_status = 0;
			}
		}	
//		printk(KERN_ERR "melfas_ts_get_data: touch_is_pressed=%d \n",touch_is_pressed);   			

	}
}

static irqreturn_t melfas_ts_irq_handler(int irq, void *handle)
{
	struct melfas_ts_data *ts = (struct melfas_ts_data *)handle;
#if DEBUG_PRINT
	printk(KERN_ERR "melfas_ts_irq_handler\n");
#endif
		
	melfas_ts_get_data(&ts->work);
	
	return IRQ_HANDLED;
}

#ifdef SEC_TSP
static int melfas_i2c_read(struct i2c_client *client, u16 addr, u16 length, u8 *value)
{
	struct i2c_adapter *adapter = client->adapter;
	struct i2c_msg msg[2];

	msg[0].addr  = client->addr;
	msg[0].flags = 0x00;
	msg[0].len   = 2;
	msg[0].buf   = (u8 *) &addr;

	msg[1].addr  = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = length;
	msg[1].buf   = (u8 *) value;

	if  (i2c_transfer(adapter, msg, 2) == 2)
		return 0;
	else
		return -EIO;

}


static int melfas_i2c_write(struct i2c_client *client, char *buf, int length)
{
	int i;
	char data[TS_WRITE_REGS_LEN];

	if (length > TS_WRITE_REGS_LEN) {
		pr_err("[TSP] size error - %s\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < length; i++)
		data[i] = *buf++;

	i = i2c_master_send(client, (char *)data, length);

	if (i == length)
		return length;
	else
		return -EIO;
}

static ssize_t set_tsp_firm_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct melfas_ts_data *ts = dev_get_drvdata(dev);
	u8 fw_latest_version;
	int ret;
	uint8_t buff[4] = {0,};

	buff[0] = TS_READ_VERSION_ADDR;
	ret = i2c_master_send(ts->client, &buff, 1);
	if(ret < 0)
	{
		printk(KERN_ERR "%s : i2c_master_send [%d]\n", __func__, ret);			
	}

	ret = i2c_master_recv(ts->client, &buff, 4);
	if(ret < 0)
	{
		printk(KERN_ERR "%s : i2c_master_recv [%d]\n", __func__, ret);			
	}
	fw_latest_version = buff[3];

	pr_info("MELFAS Last firmware version is %d\n", fw_latest_version);
	return sprintf(buf, "%#02x\n", fw_latest_version);
}

static ssize_t set_tsp_firm_version_read_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct melfas_ts_data *ts = dev_get_drvdata(dev);
	u8 tsp_version_disp;
	int ret;
	uint8_t buff[4] = {0,};

	buff[0] = TS_READ_VERSION_ADDR;
	ret = i2c_master_send(ts->client, &buff, 1);
	if(ret < 0)
	{
		printk(KERN_ERR "%s : i2c_master_send [%d]\n", __func__, ret);			
	}

	ret = i2c_master_recv(ts->client, &buff, 4);
	if(ret < 0)
	{
		printk(KERN_ERR "%s : i2c_master_recv [%d]\n", __func__, ret);			
	}
	tsp_version_disp = buff[3];

	return sprintf(buf, "%#02x\n", tsp_version_disp);
}

static ssize_t set_tsp_threshold_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct melfas_ts_data *ts = dev_get_drvdata(dev);
	u8 threshold;

	melfas_i2c_read(ts->client, P5_THRESHOLD, 1, &threshold);

	return sprintf(buf, "%d\n", threshold);
}

ssize_t set_tsp_for_inputmethod_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk(KERN_ERR "[TSP] %s is called.. is_inputmethod=%d\n", __func__, is_inputmethod);
	if (is_inputmethod)
		*buf = '1';
	else
		*buf = '0';

	return 0;
}
ssize_t set_tsp_for_inputmethod_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct melfas_ts_data *ts = dev_get_drvdata(dev);
	u16 obj_address = 0;
	u16 size_one;
	int ret;
	u8 value;
	int jump_limit = 0;
	int mrgthr = 0;
	u8 val = 0;
	unsigned int register_address = 0;

	if (tsp_enabled == false) {
		printk(KERN_ERR "[TSP ]%s. tsp_enabled is 0\n", __func__);
		return 1;
	}


	if (*buf == '1' && (!is_inputmethod)) {
		is_inputmethod = 1;
		printk(KERN_ERR "[TSP] Set TSP inputmethod IN\n");
		/* to do */
	} else if (*buf == '0' && (is_inputmethod)) {
		is_inputmethod = 0;
		printk(KERN_ERR "[TSP] Set TSP inputmethod OUT\n");
		/* to do */
	}
	return 1;
}

static ssize_t tsp_call_release_touch(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct melfas_ts_data *ts = dev_get_drvdata(dev);

	printk(" %s is called\n", __func__);

	disable_irq(ts->client->irq);

	release_all_fingers(ts);

	ts->gpio();
	ts->power(false);
	msleep(200);
	ts->power(true);

	enable_irq(ts->client->irq);

	return sprintf(buf,"0\n");
}

static ssize_t tsp_touchtype_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char temp[15];

	sprintf(temp, "TSP : MMS144\n");
	strcat(buf, temp);
	return strlen(buf);
}

static DEVICE_ATTR(tsp_firm_version_phone, S_IRUGO | S_IWUSR | S_IWGRP, set_tsp_firm_version_show, NULL);/* PHONE*/	/* firmware version resturn in phone driver version */
static DEVICE_ATTR(tsp_firm_version_panel, S_IRUGO | S_IWUSR | S_IWGRP, set_tsp_firm_version_read_show, NULL);/*PART*/	/* firmware version resturn in TSP panel version */
static DEVICE_ATTR(tsp_threshold, S_IRUGO | S_IWUSR | S_IWGRP, set_tsp_threshold_mode_show, NULL);
static DEVICE_ATTR(set_tsp_for_inputmethod, S_IRUGO | S_IWUSR | S_IWGRP, set_tsp_for_inputmethod_show, set_tsp_for_inputmethod_store); /* For 3x4 Input Method, Jump limit changed API */
static DEVICE_ATTR(call_release_touch, S_IRUGO | S_IWUSR | S_IWGRP, tsp_call_release_touch, NULL);
static DEVICE_ATTR(mxt_touchtype, S_IRUGO | S_IWUSR | S_IWGRP,	tsp_touchtype_show, NULL);

static struct attribute *sec_touch_attributes[] = {
	&dev_attr_tsp_firm_version_phone.attr,
	&dev_attr_tsp_firm_version_panel.attr,
	&dev_attr_tsp_threshold.attr,
	&dev_attr_set_tsp_for_inputmethod.attr,
	&dev_attr_call_release_touch.attr,
	&dev_attr_mxt_touchtype.attr,
	NULL,
};

static struct attribute_group sec_touch_attr_group = {
	.attrs = sec_touch_attributes,
};
#endif

#ifdef TSP_FACTORY_TEST
static bool debug_print = true;
static u16 inspection_data[370] = { 0, };
static u16 lntensity_data[370] = { 0, };

static ssize_t set_tsp_module_on_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	struct melfas_ts_data *ts = dev_get_drvdata(dev);

	ret = melfas_ts_resume(ts->client);
	
	if (ret  = 0)
		*buf = '1';
	else
		*buf = '0';

	msleep(500);
	return 0;
}

static ssize_t set_tsp_module_off_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	struct melfas_ts_data *ts = dev_get_drvdata(dev);

	ret = melfas_ts_suspend(ts->client, PMSG_SUSPEND);

	if (ret  = 0)
		*buf = '1';
	else
		*buf = '0';

	return 0;
}

static int check_debug_data(struct melfas_ts_data *ts)
{
	u8 write_buffer[6];
	u8 read_buffer[2];
	int sensing_line, exciting_line;
	int ret = 0;
	int gpio = ts->client->irq - NR_MSM_IRQS;

	disable_irq(ts->client->irq);

	/* enter the debug mode */

	write_buffer[0] = 0xA0;
	write_buffer[1] = 0x1A;
	write_buffer[2] = 0x0;
	write_buffer[3] = 0x0;
	write_buffer[4] = 0x0;

	if (debug_print)
		pr_info("[TSP] read inspenction data\n");
	write_buffer[5] = 0x03;
	for (sensing_line = 0; sensing_line < 14; sensing_line++) {
		printk("sensing_line %02d ==> ", sensing_line);
		for (exciting_line =0; exciting_line < 26; exciting_line++) {
			write_buffer[2] = exciting_line;
			write_buffer[3] = sensing_line;
			melfas_i2c_write(ts->client, (char *)write_buffer, 6);
			melfas_i2c_read(ts->client, 0xA8, 2, read_buffer);
			lntensity_data[exciting_line + sensing_line * 26] =
				(read_buffer[1] & 0xf) << 8 | read_buffer[0];
			printk("%d.", lntensity_data[exciting_line + sensing_line * 26]);
			if(inspection_data[exciting_line + sensing_line * 26] < 900 
			|| inspection_data[exciting_line + sensing_line * 26] > 3000)
				ret = -1;
		}
		printk(" \n");
	}
	pr_info("[TSP] Reading data end.\n");

	enable_irq(ts->client->irq);

	return ret;
}

static ssize_t set_all_refer_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int status;
	struct melfas_ts_data *ts = dev_get_drvdata(dev);

	status = check_debug_data(ts);

	return sprintf(buf, "%u\n", status);
}

static int index =0;

static int atoi(char *str)
{
	int result = 0;
	int count = 0;
	if( str == NULL ) 
		return -1;
	while( str[count] != NULL && str[count] >= '0' && str[count] <= '9' )
	{		
		result = result * 10 + str[count] - '0';
		++count;
	}
	return result;
}

ssize_t disp_all_refdata_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%u\n",  inspection_data[index]);
}

ssize_t disp_all_refdata_store(struct device *dev, struct device_attribute *attr,
								   const char *buf, size_t size)
{
	index = atoi(buf);

	printk(KERN_ERR "%s : value %d\n", __func__, index);

  	return size;
}

static int check_delta_data(struct melfas_ts_data *ts)
{
	u8 write_buffer[6];
	u8 read_buffer[2];
	int sensing_line, exciting_line;
	int gpio = ts->client->irq - NR_MSM_IRQS;
	int ret = 0;
	disable_irq(ts->client->irq);
	/* enter the debug mode */
	write_buffer[0] = 0xA0;
	write_buffer[1] = 0x1A;
	write_buffer[2] = 0x0;
	write_buffer[3] = 0x0;
	write_buffer[4] = 0x0;
	write_buffer[5] = 0x01;
	melfas_i2c_write(ts->client, (char *)write_buffer, 6);

	/* wating for the interrupt*/
	while (gpio_get_value(gpio)) {
		printk(".");
		udelay(100);
	}

	if (debug_print)
		pr_info("[TSP] read dummy\n");

	/* read the dummy data */
	melfas_i2c_read(ts->client, 0xA8, 2, read_buffer);

	if (debug_print)
		pr_info("[TSP] read inspenction data\n");
	write_buffer[5] = 0x02;
	for (sensing_line = 0; sensing_line < 14; sensing_line++) {
		for (exciting_line =0; exciting_line < 26; exciting_line++) {
			write_buffer[2] = exciting_line;
			write_buffer[3] = sensing_line;
			melfas_i2c_write(ts->client, (char *)write_buffer, 6);
			melfas_i2c_read(ts->client, 0xA8, 2, read_buffer);
			inspection_data[exciting_line + sensing_line * 26] =
				(read_buffer[1] & 0xf) << 8 | read_buffer[0];
			printk("%d.", inspection_data[exciting_line + sensing_line * 26]);
			if(inspection_data[exciting_line + sensing_line * 26] < 100 
			|| inspection_data[exciting_line + sensing_line * 26] > 900)
				ret = -1;
		}
		printk(" \n");
	}
	pr_info("[TSP] Reading data end.\n");

//	release_all_fingers(ts);

	msleep(200);
	release_all_fingers(ts);
	ts->gpio();
	ts->power(false);

	msleep(200);
	ts->power(true);

	enable_irq(ts->client->irq);

	return ret;
}

static ssize_t set_all_delta_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int status = 0;
	struct melfas_ts_data *ts = dev_get_drvdata(dev);

	status = check_delta_data(ts);

	set_tsp_module_off_show(dev, attr, buf);
	set_tsp_module_on_show(dev, attr, buf);

	return sprintf(buf, "%u\n", status);
}

ssize_t disp_all_deltadata_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk(KERN_ERR ": value %d\n", lntensity_data[index]);
    return sprintf(buf, "%u\n",  lntensity_data[index]);
}


ssize_t disp_all_deltadata_store(struct device *dev, struct device_attribute *attr,
								   const char *buf, size_t size)
{
	index = atoi(buf);
	printk(KERN_ERR "Delta data %d", index);
  	return size;
}

static void check_intensity_data(struct melfas_ts_data *ts)
{

	u8 write_buffer[6];
	u8 read_buffer[2];
	int sensing_line, exciting_line;
	int gpio = ts->client->irq - NR_MSM_IRQS;

	disable_irq(ts->client->irq);
	if (0 == inspection_data[0]) {
		/* enter the debug mode */
		write_buffer[0] = 0xA0;
		write_buffer[1] = 0x1A;
		write_buffer[2] = 0x0;
		write_buffer[3] = 0x0;
		write_buffer[4] = 0x0;
		write_buffer[5] = 0x01;
		melfas_i2c_write(ts->client, (char *)write_buffer, 6);

		/* wating for the interrupt*/
		while (gpio_get_value(gpio)) {
			printk(".");
			udelay(100);
		}

		/* read the dummy data */
		melfas_i2c_read(ts->client, 0xA8, 2, read_buffer);

		write_buffer[5] = 0x02;
		for (sensing_line = 0; sensing_line < 14; sensing_line++) {
			for (exciting_line =0; exciting_line < 26; exciting_line++) {
				write_buffer[2] = exciting_line;
				write_buffer[3] = sensing_line;
				melfas_i2c_write(ts->client, (char *)write_buffer, 6);
				melfas_i2c_read(ts->client, 0xA8, 2, read_buffer);
				inspection_data[exciting_line + sensing_line * 26] =
					(read_buffer[1] & 0xf) << 8 | read_buffer[0];
			}
		}
		melfas_ts_suspend(ts->client, PMSG_SUSPEND);
		msleep(200);
		melfas_ts_resume(ts->client);
	}

	write_buffer[0] = 0xA0;
	write_buffer[1] = 0x1A;
	write_buffer[4] = 0x0;
	write_buffer[5] = 0x04;
	for (sensing_line = 0; sensing_line < 14; sensing_line++) {
		for (exciting_line =0; exciting_line < 26; exciting_line++) {
			write_buffer[2] = exciting_line;
			write_buffer[3] = sensing_line;
			melfas_i2c_write(ts->client, (char *)write_buffer, 6);
			melfas_i2c_read(ts->client, 0xA8, 2, read_buffer);
			lntensity_data[exciting_line + sensing_line * 26] =
				(read_buffer[1] & 0xf) << 8 | read_buffer[0];
		}
	}
	enable_irq(ts->client->irq);
/*
	pr_info("[TSP] lntensity data");
	int i;
	for (i = 0; i < 14*16; i++) {
		if (0 == i % 26)
			printk("\n");
		printk("%2u, ", lntensity_data[i]);
	}
*/
}

static ssize_t set_refer0_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 refrence = 0;
	struct melfas_ts_data *ts = dev_get_drvdata(dev);

	check_intensity_data(ts);

	refrence = inspection_data[28];
	return sprintf(buf, "%u\n", refrence);
}

static ssize_t set_refer1_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 refrence = 0;
	refrence = inspection_data[288];
	return sprintf(buf, "%u\n", refrence);
}

static ssize_t set_refer2_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 refrence = 0;
	refrence = inspection_data[194];
	return sprintf(buf, "%u\n", refrence);
}

static ssize_t set_refer3_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 refrence = 0;
	refrence = inspection_data[49];
	return sprintf(buf, "%u\n", refrence);
}

static ssize_t set_refer4_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 refrence = 0;
	refrence = inspection_data[309];
	return sprintf(buf, "%u\n", refrence);
}

static ssize_t set_intensity0_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 intensity = 0;
	intensity = lntensity_data[28];
	return sprintf(buf, "%u\n", intensity);
}

static ssize_t set_intensity1_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 intensity = 0;
	intensity = lntensity_data[288];
	return sprintf(buf, "%u\n", intensity);
}

static ssize_t set_intensity2_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 intensity = 0;
	intensity = lntensity_data[194];
	return sprintf(buf, "%u\n", intensity);
}

static ssize_t set_intensity3_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 intensity = 0;
	intensity = lntensity_data[49];
	return sprintf(buf, "%u\n", intensity);
}

static ssize_t set_intensity4_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 intensity = 0;
	intensity = lntensity_data[309];
	return sprintf(buf, "%u\n", intensity);
}


static DEVICE_ATTR(set_module_on, S_IRUGO | S_IWUSR | S_IWGRP, set_tsp_module_on_show, NULL);
static DEVICE_ATTR(set_module_off, S_IRUGO | S_IWUSR | S_IWGRP, set_tsp_module_off_show, NULL);
static DEVICE_ATTR(set_all_refer, S_IRUGO | S_IWUSR | S_IWGRP, set_all_refer_mode_show, NULL);
static DEVICE_ATTR(disp_all_refdata, S_IRUGO | S_IWUSR | S_IWGRP, disp_all_refdata_show, disp_all_refdata_store);
static DEVICE_ATTR(set_all_delta, S_IRUGO | S_IWUSR | S_IWGRP, set_all_delta_mode_show, NULL);
static DEVICE_ATTR(disp_all_deltadata, S_IRUGO | S_IWUSR | S_IWGRP, disp_all_deltadata_show, disp_all_deltadata_store);
static DEVICE_ATTR(set_refer0, S_IRUGO | S_IWUSR | S_IWGRP, set_refer0_mode_show, NULL);
static DEVICE_ATTR(set_delta0, S_IRUGO | S_IWUSR | S_IWGRP, set_intensity0_mode_show, NULL);
static DEVICE_ATTR(set_refer1, S_IRUGO | S_IWUSR | S_IWGRP, set_refer1_mode_show, NULL);
static DEVICE_ATTR(set_delta1, S_IRUGO | S_IWUSR | S_IWGRP, set_intensity1_mode_show, NULL);
static DEVICE_ATTR(set_refer2, S_IRUGO | S_IWUSR | S_IWGRP, set_refer2_mode_show, NULL);
static DEVICE_ATTR(set_delta2, S_IRUGO | S_IWUSR | S_IWGRP, set_intensity2_mode_show, NULL);
static DEVICE_ATTR(set_refer3, S_IRUGO | S_IWUSR | S_IWGRP, set_refer3_mode_show, NULL);
static DEVICE_ATTR(set_delta3, S_IRUGO | S_IWUSR | S_IWGRP, set_intensity3_mode_show, NULL);
static DEVICE_ATTR(set_refer4, S_IRUGO | S_IWUSR | S_IWGRP, set_refer4_mode_show, NULL);
static DEVICE_ATTR(set_delta4, S_IRUGO | S_IWUSR | S_IWGRP, set_intensity4_mode_show, NULL);
static DEVICE_ATTR(set_threshould, S_IRUGO | S_IWUSR | S_IWGRP, set_tsp_threshold_mode_show, NULL);	/* touch threshold return */

static struct attribute *sec_touch_facotry_attributes[] = {
	&dev_attr_set_module_on.attr,
	&dev_attr_set_module_off.attr,
	&dev_attr_set_all_refer.attr,
	&dev_attr_disp_all_refdata.attr,
	&dev_attr_set_all_delta.attr,
	&dev_attr_disp_all_deltadata.attr,
	&dev_attr_set_refer0.attr,
	&dev_attr_set_delta0.attr,
	&dev_attr_set_refer1.attr,
	&dev_attr_set_delta1.attr,
	&dev_attr_set_refer2.attr,
	&dev_attr_set_delta2.attr,
	&dev_attr_set_refer3.attr,
	&dev_attr_set_delta3.attr,
	&dev_attr_set_refer4.attr,
	&dev_attr_set_delta4.attr,
	&dev_attr_set_threshould.attr,
	NULL,
};

static struct attribute_group sec_touch_factory_attr_group = {
	.attrs = sec_touch_facotry_attributes,
};
#endif


static void release_all_fingers(struct melfas_ts_data *ts)
{
	int i;

	printk(KERN_ERR "%s start.\n", __func__);
	for(i=0; i<MELFAS_MAX_TOUCH; i++) {
		if(-1 == g_Mtouch_info[i].strength) {
			g_Mtouch_info[i].posX = 0;
			g_Mtouch_info[i].posY = 0;
			continue;
		}
	printk(KERN_ERR "%s %s(%d)\n", __func__, ts->input_dev->name, i);

		g_Mtouch_info[i].strength = 0;

		input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, i);
		input_report_abs(ts->input_dev, ABS_MT_POSITION_X, g_Mtouch_info[i].posX);
		input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, g_Mtouch_info[i].posY);
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, g_Mtouch_info[i].strength);
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, g_Mtouch_info[i].strength);
		input_mt_sync(ts->input_dev);

		g_Mtouch_info[i].posX = 0;
		g_Mtouch_info[i].posY = 0;

		if(0 == g_Mtouch_info[i].strength)
			g_Mtouch_info[i].strength = -1;
	}
	input_sync(ts->input_dev);
}

void TSP_force_released(void)
{
	printk(KERN_ERR "%s satrt!\n", __func__);

	if (tsp_enabled == false) {
		printk(KERN_ERR "[TSP] Disabled\n");
		return;
	}
	release_all_fingers(ts_data);

	touch_is_pressed = 0;
};
EXPORT_SYMBOL(TSP_force_released);

#ifdef SET_TSP_CONFIG
static int melfas_set_config(struct i2c_client *client, u8 reg, u8 value)
{
	u8 buffer[2];
	int ret;
	struct melfas_ts_data *ts = i2c_get_clientdata(client);

	buffer[0] = reg;
	buffer[1] = value;
	ret = melfas_i2c_write(ts->client, (char *)buffer, 2);

	return ret;	
}
#endif

static int melfas_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct melfas_ts_data *ts;
	struct melfas_tsi_platform_data *data;
#ifdef SEC_TSP
	struct device *sec_touchscreen;
	struct device *qt602240_noise_test;
	char lcd_id, *temp;
#endif

	int ret = 0, i; 
	
	uint8_t buf[4] = {0,};
	
#if DEBUG_PRINT
	printk(KERN_ERR "%s start.\n", __func__);
#endif

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
    {
        printk(KERN_ERR "%s: need I2C_FUNC_I2C\n", __func__);
        ret = -ENODEV;
        goto err_check_functionality_failed;
    }

    ts = kmalloc(sizeof(struct melfas_ts_data), GFP_KERNEL);
    if (ts == NULL)
    {
        printk(KERN_ERR "%s: failed to create a state of melfas-ts\n", __func__);
        ret = -ENOMEM;
        goto err_alloc_data_failed;
    }
	ts_data = ts;
	data = client->dev.platform_data;
	ts->power = data->power;
	ts->gpio = data->gpio;
    ts->client = client;
    i2c_set_clientdata(client, ts);
	ts->power(true);
    ret = i2c_master_send(ts->client, &buf, 1);


#if DEBUG_PRINT
	printk(KERN_ERR "%s: i2c_master_send() [%d], Add[%d]\n", __func__, ret, ts->client->addr);
#endif



#if SET_DOWNLOAD_BY_GPIO
	buf[0] = TS_READ_VERSION_ADDR;
	ret = i2c_master_send(ts->client, &buf, 1);
	if(ret < 0)
	{
		printk(KERN_ERR "%s: i2c_master_send [%d]\n", __func__, ret);			
	}

	ret = i2c_master_recv(ts->client, &buf, 4);
	if(ret < 0)
	{
		printk(KERN_ERR "%s: i2c_master_recv [%d]\n", __func__, ret);			
	}
	printk(KERN_ERR "FW_VERSION: 0x%02x\n", buf[3]);

	temp = get_s6e8aa0_id_buffer();
	lcd_id = *(temp+1);
	printk(KERN_ERR "LCD_ID : 0x%02x 0x%02x\n", *temp, lcd_id);

	if(lcd_id != 0x23 && lcd_id != 0x80 && buf[3] < FW_VERSION)
	{
		printk(KERN_ERR "FW Upgrading... FW_VERSION: 0x%02x\n", buf[3]);
		ret = mcsdl_download_binary_data(data);		
		if(ret == 0)
		{
			printk(KERN_ERR "SET Download Fail - error code [%d]\n", ret);			
		}
	}	
#endif // SET_DOWNLOAD_BY_GPIO
	
	ts->input_dev = input_allocate_device();
    if (!ts->input_dev)
    {
		printk(KERN_ERR "%s: Not enough memory\n", __func__);
		ret = -ENOMEM;
		goto err_input_dev_alloc_failed;
	} 

	ts->input_dev->name = "sec_touchscreen" ;

	ts->input_dev->evbit[0] = BIT_MASK(EV_ABS) | BIT_MASK(EV_KEY);
	

	ts->input_dev->keybit[BIT_WORD(KEY_MENU)] |= BIT_MASK(KEY_MENU);
	ts->input_dev->keybit[BIT_WORD(KEY_HOME)] |= BIT_MASK(KEY_HOME);
	ts->input_dev->keybit[BIT_WORD(KEY_BACK)] |= BIT_MASK(KEY_BACK);		
	ts->input_dev->keybit[BIT_WORD(KEY_SEARCH)] |= BIT_MASK(KEY_SEARCH);			


//	__set_bit(BTN_TOUCH, ts->input_dev->keybit);
//	__set_bit(EV_ABS,  ts->input_dev->evbit);
//	ts->input_dev->evbit[0] =  BIT_MASK(EV_SYN) | BIT_MASK(EV_ABS) | BIT_MASK(EV_KEY);	

	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, TS_MAX_X_COORD, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, TS_MAX_Y_COORD, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, TS_MAX_Z_TOUCH, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, MELFAS_MAX_TOUCH-1, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, TS_MAX_W_TOUCH, 0, 0);
//	__set_bit(EV_SYN, ts->input_dev->evbit); 
//	__set_bit(EV_KEY, ts->input_dev->evbit);	


    ret = input_register_device(ts->input_dev);
    if (ret)
    {
        printk(KERN_ERR "%s: Failed to register device\n", __func__);
        ret = -ENOMEM;
        goto err_input_register_device_failed;
    }

    if (ts->client->irq)
    {
#if DEBUG_PRINT
        printk(KERN_ERR "%s: trying to request irq: %s-%d\n", __func__, ts->client->name, ts->client->irq);
#endif
		ret = request_threaded_irq(client->irq, NULL, melfas_ts_irq_handler, IRQF_ONESHOT | IRQF_TRIGGER_LOW, ts->client->name, ts);
        if (ret > 0)
        {
            printk(KERN_ERR "%s: Can't allocate irq %d, ret %d\n", __func__, ts->client->irq, ret);
            ret = -EBUSY;
            goto err_request_irq;
        }
    }
	for (i = 0; i < MELFAS_MAX_TOUCH ; i++)  /* _SUPPORT_MULTITOUCH_ */
		g_Mtouch_info[i].strength = -1;	

	tsp_enabled = true;

#if DEBUG_PRINT	
	printk(KERN_ERR "%s: succeed to register input device\n", __func__);
#endif

#ifdef SEC_TSP
	sec_touchscreen = device_create(sec_class, NULL, 0, ts, "sec_touchscreen");
	if (IS_ERR(sec_touchscreen))
		pr_err("[TSP] Failed to create device for the sysfs\n");

	ret = sysfs_create_group(&sec_touchscreen->kobj, &sec_touch_attr_group);
	if (ret)
		pr_err("[TSP] Failed to create sysfs group\n");
#endif

#ifdef TSP_FACTORY_TEST
	qt602240_noise_test = device_create(sec_class, NULL, 0, ts, "qt602240_noise_test");
	if (IS_ERR(qt602240_noise_test))
		pr_err("[TSP] Failed to create device for the sysfs\n");

	ret = sysfs_create_group(&qt602240_noise_test->kobj, &sec_touch_factory_attr_group);
	if (ret)
		pr_err("[TSP] Failed to create sysfs group\n");
#endif

#if CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = melfas_ts_early_suspend;
	ts->early_suspend.resume = melfas_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif
#ifdef SET_TSP_CONFIG
	melfas_set_config(ts->client, MIP_ACTIVE_REPORT_RATE, 60);
//	melfas_set_config(ts->client, MIP_CONTACT_ON_EVENT_THRES, 60);
//	melfas_set_config(ts->client, MIP_MOVING_EVENT_THRES, 20);
//	melfas_set_config(ts->client, MIP_POSITION_FILTER_LEVEL, 10);
#endif
#if DEBUG_PRINT
	printk(KERN_INFO "%s: Start touchscreen. name: %s, irq: %d\n", __func__, ts->client->name, ts->client->irq);
#endif
	return 0;

err_request_irq:
	printk(KERN_ERR "melfas-ts: err_request_irq failed\n");
	free_irq(client->irq, ts);
err_input_register_device_failed:
	printk(KERN_ERR "melfas-ts: err_input_register_device failed\n");
	input_free_device(ts->input_dev);
err_input_dev_alloc_failed:
	printk(KERN_ERR "melfas-ts: err_input_dev_alloc failed\n");
err_alloc_data_failed:
	printk(KERN_ERR "melfas-ts: err_alloc_data failed_\n");	
err_detect_failed:
	printk(KERN_ERR "melfas-ts: err_detect failed\n");
	kfree(ts);
err_check_functionality_failed:
	printk(KERN_ERR "melfas-ts: err_check_functionality failed_\n");

	return ret;
}

static int melfas_ts_remove(struct i2c_client *client)
{
	struct melfas_ts_data *ts = i2c_get_clientdata(client);

	unregister_early_suspend(&ts->early_suspend);
	free_irq(client->irq, ts);
	ts->power(false);
	input_unregister_device(ts->input_dev);
	kfree(ts);
	return 0;
}

static int melfas_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	int i;    
	struct melfas_ts_data *ts = i2c_get_clientdata(client);

	disable_irq(client->irq);
	tsp_enabled = false;
	release_all_fingers(ts);
	ts->gpio();
	ts->power(false);

	return 0;
}

static int melfas_ts_resume(struct i2c_client *client)
{
	struct melfas_ts_data *ts = i2c_get_clientdata(client);

	ts->power(true);
	enable_irq(client->irq); // scl wave
#ifdef SET_TSP_CONFIG
	melfas_set_config(ts->client, MIP_ACTIVE_REPORT_RATE, 60);
//	melfas_set_config(ts->client, MIP_CONTACT_ON_EVENT_THRES, 60);
//	melfas_set_config(ts->client, MIP_MOVING_EVENT_THRES, 20);
//	melfas_set_config(ts->client, MIP_POSITION_FILTER_LEVEL, 10);
#endif
	tsp_enabled = true;
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void melfas_ts_early_suspend(struct early_suspend *h)
{
	struct melfas_ts_data *ts;

	ts = container_of(h, struct melfas_ts_data, early_suspend);
	melfas_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void melfas_ts_late_resume(struct early_suspend *h)
{
	struct melfas_ts_data *ts;
	ts = container_of(h, struct melfas_ts_data, early_suspend);
	melfas_ts_resume(ts->client);
}
#endif

static const struct i2c_device_id melfas_ts_id[] =
{
    { MELFAS_TS_NAME, 0 },
    { }
};

static struct i2c_driver melfas_ts_driver =
{
    .driver = {
    .name = MELFAS_TS_NAME,
    },
    .id_table = melfas_ts_id,
    .probe = melfas_ts_probe,
    .remove = __devexit_p(melfas_ts_remove),
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend		= melfas_ts_suspend,
	.resume		= melfas_ts_resume,
#endif
};

#ifdef CONFIG_BATTERY_SEC
extern unsigned int is_lpcharging_state(void);
#endif

static int __devinit melfas_ts_init(void)
{
#ifdef CONFIG_BATTERY_SEC
	if (is_lpcharging_state()) {
		pr_info("%s : LPM Charging Mode! return 0\n", __func__);
		return 0;
	}
#endif

	return i2c_add_driver(&melfas_ts_driver);
}

static void __exit melfas_ts_exit(void)
{
	i2c_del_driver(&melfas_ts_driver);
}

MODULE_DESCRIPTION("Driver for Melfas MTSI Touchscreen Controller");
MODULE_AUTHOR("MinSang, Kim <kimms@melfas.com>");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

module_init(melfas_ts_init);
module_exit(melfas_ts_exit);
