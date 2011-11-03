/*
  SEC S5K4ECGX
 */
/***************************************************************
CAMERA DRIVER FOR 5M CAM (SYS.LSI)
****************************************************************/

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <media/msm_camera.h>
#include <mach/gpio.h>
#include <mach/camera.h>


#include "sec_s5k4ecgx.h"

#include "sec_cam_pmic.h"
#include "sec_cam_dev.h"


#include <linux/clk.h>
#include <linux/io.h>
#include <mach/board.h>
#include <mach/msm_iomap.h>


//#define CONFIG_LOAD_FILE

#ifdef CONFIG_LOAD_FILE

#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#else

#include "sec_s5k4ecgx_reg.h"	

#endif



struct s5k4ecgx_work_t {
	struct work_struct work;
};

static struct  s5k4ecgx_work_t *s5k4ecgx_sensorw;
static struct  i2c_client *s5k4ecgx_client;


static struct s5k4ecgx_ctrl_t *s5k4ecgx_ctrl;

static DECLARE_WAIT_QUEUE_HEAD(s5k4ecgx_wait_queue);
DECLARE_MUTEX(s5k4ecgx_sem);

#ifdef CONFIG_LOAD_FILE

struct test {
	u8 data;
	struct test *nextBuf;
};
static struct test *testBuf;
static s32 large_file;


#define TEST_INIT	\
{			\
	.data = 0;	\
	.nextBuf = NULL;	\
}


static int s5k4ecgx_write_regs_from_sd(char *name);
static int s5k4ecgx_regs_table_write(char *name);
#endif

static int s5k4ecgx_start(void);
 

#define S5K4ECGX_WRITE_LIST(A) \
    s5k4ecgx_i2c_write_list(A,(sizeof(A) / sizeof(A[0])),#A);



static int32_t s5k4ecgx_i2c_write_32bit(unsigned long packet)
{
	int32_t rc = -EFAULT;
	int retry_count = 1;
	int i;
	
	unsigned char buf[4];
	struct i2c_msg msg = {
		.addr = s5k4ecgx_client->addr,
		.flags = 0,
		.len = 4,
		.buf = buf,
	};
	*(unsigned long *)buf = cpu_to_be32(packet);

	//for(i=0; i< retry_count; i++) {
	rc = i2c_transfer(s5k4ecgx_client->adapter, &msg, 1); 		
  	//}

	return rc;
}



static int s5k4ecgx_i2c_write_list(const u32 *list, int size, char *name)
{
	int ret = 0;

#ifdef CONFIG_LOAD_FILE

	ret = s5k4ecgx_write_regs_from_sd(name);
#else
	int i;
	u16 m_delay = 0;
	unsigned long temp_packet;
	
 	CAM_DEBUG("%s, size=%d",name, size);
	
	for (i = 0; i < size; i++) {
		temp_packet = list[i];

		if ((temp_packet & S5K4ECGX_DELAY) == S5K4ECGX_DELAY) {
			m_delay = temp_packet & 0xFFFF;
			cam_info("delay = %d",m_delay );
			mdelay(m_delay);
			continue;
		}

		if (s5k4ecgx_i2c_write_32bit(temp_packet) < 0) {
			cam_err("fail(0x%x:%d)", temp_packet, i);
		}
		//udelay(10);
	}
#endif	
	return ret;
}


static int32_t s5k4ecgx_i2c_write(unsigned short subaddr, unsigned short val)
{
	unsigned long packet;
	packet = (subaddr << 16) | (val&0xFFFF);

	return s5k4ecgx_i2c_write_32bit(packet);
}


static int32_t s5k4ecgx_i2c_read(unsigned short subaddr, unsigned short *data)
{

	int ret;
	unsigned char buf[2];

	struct i2c_msg msg = {
		.addr = s5k4ecgx_client->addr,
		.flags = 0,		
		.len = 2,		
		.buf = buf,	
	};

	buf[0] = (subaddr >> 8);
	buf[1] = (subaddr & 0xFF);

	ret = i2c_transfer(s5k4ecgx_client->adapter, &msg, 1) == 1 ? 0 : -EIO;
	
	if (ret == -EIO) 
	    goto error;
	
	msg.flags = I2C_M_RD;

	ret = i2c_transfer(s5k4ecgx_client->adapter, &msg, 1) == 1 ? 0 : -EIO;
	if (ret == -EIO) 
	    goto error;

	*data = ((buf[0] << 8) | buf[1]);
	

error:
	return ret;

}




#ifdef CONFIG_LOAD_FILE
static inline int s5k4ecgx_write(struct i2c_client *client,
		u32 packet)
{
	u8 buf[4];
	int err = 0, retry_count = 5;

	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.buf	= buf,
		.len	= 4,
	};

	if (!client->adapter) {
		cam_err("ERR - can't search i2c client adapter");
		return -EIO;
	}

	while (retry_count--) {
		*(u32 *)buf = cpu_to_be32(packet);
		err = i2c_transfer(client->adapter, &msg, 1);
		if (likely(err == 1))
			break;
		mdelay(10);
	}

	if (unlikely(err < 0)) {
		cam_err("ERR - 0x%08x write failed err=%d",(u32)packet, err);
		return err;
	}

	return (err != 1) ? -1 : 0;
}


void s5k4ecgx_regs_table_init(void)
{
	struct file *fp = NULL;
	struct test *nextBuf = NULL;

	u8 *nBuf = NULL;
	size_t file_size = 0, max_size = 0, testBuf_size = 0;
	ssize_t nread = 0;
	s32 check = 0, starCheck = 0;
	s32 tmp_large_file = 0;
	s32 i = 0;
	int ret = 0;
	loff_t pos;

	mm_segment_t fs = get_fs();
	set_fs(get_ds());

	BUG_ON(testBuf);

	//fp = filp_open("/mnt/sdcard/external_sd/sec_s5k4ecgx_reg.h", O_RDONLY, 0);
	fp = filp_open("/mnt/sdcard/sec_s5k4ecgx_reg.h", O_RDONLY, 0);
	if (IS_ERR(fp)) {
		cam_err("failed to open /mnt/sdcard/sec_s5k4ecgx_reg.h");
		return PTR_ERR(fp);
	}

	file_size = (size_t) fp->f_path.dentry->d_inode->i_size;
	max_size = file_size;

	cam_info("file_size = %d", file_size);

	nBuf = kmalloc(file_size, GFP_ATOMIC);
	if (nBuf == NULL) {
		cam_err("Fail to 1st get memory");
		nBuf = vmalloc(file_size);
		if (nBuf == NULL) {
			cam_err("ERR: nBuf Out of Memory");
			ret = -ENOMEM;
			goto error_out;
		}
		tmp_large_file = 1;
	}

	testBuf_size = sizeof(struct test) * file_size;
	if (tmp_large_file) {
		testBuf = (struct test *)vmalloc(testBuf_size);
		large_file = 1;
	} else {
		testBuf = kmalloc(testBuf_size, GFP_ATOMIC);
		if (testBuf == NULL) {
			cam_err("Fail to get mem(%d bytes)", testBuf_size);
			testBuf = (struct test *)vmalloc(testBuf_size);
			large_file = 1;
		}
	}
	if (testBuf == NULL) {
		cam_err("ERR: Out of Memory");
		ret = -ENOMEM;
		goto error_out;
	}

	pos = 0;
	memset(nBuf, 0, file_size);
	memset(testBuf, 0, file_size * sizeof(struct test));

	nread = vfs_read(fp, (char __user *)nBuf, file_size, &pos);
	if (nread != file_size) {
		cam_err("failed to read file ret = %d", nread);
		ret = -1;
		goto error_out;
	}

	set_fs(fs);

	i = max_size;

	cam_info("i = %d", i);

	while (i) {
		testBuf[max_size - i].data = *nBuf;
		if (i != 1) {
			testBuf[max_size - i].nextBuf = &testBuf[max_size - i + 1];
		} else {
			testBuf[max_size - i].nextBuf = NULL;
			break;
		}
		i--;
		nBuf++;
	}

	i = max_size;
	nextBuf = &testBuf[0];

#if 1
	while (i - 1) {
		if (!check && !starCheck) {
			if (testBuf[max_size - i].data == '/') {
				if (testBuf[max_size-i].nextBuf != NULL) {
					if (testBuf[max_size-i].nextBuf->data
								== '/') {
						check = 1;/* when find '//' */
						i--;
					} else if (testBuf[max_size-i].nextBuf->data == '*') {
						starCheck = 1;/* when find '/ *' */
						i--;
					}
				} else
					break;
			}
			if (!check && !starCheck) {
				/* ignore '\t' */
				if (testBuf[max_size - i].data != '\t') {
					nextBuf->nextBuf = &testBuf[max_size-i];
					nextBuf = &testBuf[max_size - i];
				}
			}
		} else if (check && !starCheck) {
			if (testBuf[max_size - i].data == '/') {
				if(testBuf[max_size-i].nextBuf != NULL) {
					if (testBuf[max_size-i].nextBuf->data == '*') {
						starCheck = 1; /* when find '/ *' */
						check = 0;
						i--;
					}
				} else
					break;
			}

			 /* when find '\n' */
			if (testBuf[max_size - i].data == '\n' && check) {
				check = 0;
				nextBuf->nextBuf = &testBuf[max_size - i];
				nextBuf = &testBuf[max_size - i];
			}

		} else if (!check && starCheck) {
			if (testBuf[max_size - i].data == '*') {
				if (testBuf[max_size-i].nextBuf != NULL) {
					if (testBuf[max_size-i].nextBuf->data == '/') {
						starCheck = 0; /* when find '* /' */
						i--;
					}
				} else
					break;
			}
		}

		i--;

		if (i < 2) {
			nextBuf = NULL;
			break;
		}

		if (testBuf[max_size - i].nextBuf == NULL) {
			nextBuf = NULL;
			break;
		}
	}
#endif

#if 0 // for print
	printk("i = %d\n", i);
	nextBuf = &testBuf[0];
	while (1) {
		//printk("sdfdsf\n");
		if (nextBuf->nextBuf == NULL)
			break;
		printk("%c", nextBuf->data);
		nextBuf = nextBuf->nextBuf;
	}
#endif

error_out:

	if (nBuf)
		tmp_large_file ? vfree(nBuf) : kfree(nBuf);
	if (fp)
		filp_close(fp, current->files);
	return ret;
}


void s5k4ecgx_regs_table_exit(void)
{
	if (testBuf) {
		large_file ? vfree(testBuf) : kfree(testBuf);
		large_file = 0;
		testBuf = NULL;
	}
}

static int s5k4ecgx_write_regs_from_sd(char *name)
{
	struct test *tempData = NULL;

	int ret = -EAGAIN;
	u32 temp;
	u32 delay = 0;
	u8 data[11];
	s32 searched = 0;
	size_t size = strlen(name);
	s32 i;

	CAM_DEBUG("E size = %d, string = %s", size, name);
	tempData = &testBuf[0];

	while (!searched) {
		searched = 1;
		for (i = 0; i < size; i++) {
			if (tempData->data != name[i]) {
				searched = 0;
				break;
			}
			tempData = tempData->nextBuf;
		}
		tempData = tempData->nextBuf;
	}
	/* structure is get..*/

	while (1) {
		if (tempData->data == '{')
			break;
		else
			tempData = tempData->nextBuf;
	}

	while (1) {
		searched = 0;
		while (1) {
			if (tempData->data == 'x') {
				/* get 10 strings.*/
				data[0] = '0';
				for (i = 1; i < 11; i++) {
					data[i] = tempData->data;
					tempData = tempData->nextBuf;
				}
				/*CAM_DEBUG("%s\n", data);*/
				temp = simple_strtoul(data, NULL, 16);
				break;
			} else if (tempData->data == '}') {
				searched = 1;
				break;
			} else
				tempData = tempData->nextBuf;

			if (tempData->nextBuf == NULL)
				return -1;
		}

		if (searched)
			break;
		if ((temp & S5K4ECGX_DELAY) == S5K4ECGX_DELAY) {
			delay = temp & 0xFFFF;
			cam_info("delay(%d)",delay);
			msleep(delay);
			continue;
		}
		ret = s5k4ecgx_write(s5k4ecgx_client,temp);

		/* In error circumstances */
		/* Give second shot */
		if (unlikely(ret)) {
			ret = s5k4ecgx_write(s5k4ecgx_client,temp);

			/* Give it one more shot */
			if (unlikely(ret)) {
				ret = s5k4ecgx_write(s5k4ecgx_client, temp);
			}
		}
	}
	return 0;
}
#endif

static void s5k4ecgx_set_ae_lock(char value)
{
	int err = -EINVAL;
	CAM_DEBUG("%d",value);

    	switch (value) {
        case EXT_CFG_AE_LOCK:
            	s5k4ecgx_ctrl->setting.ae_lock = EXT_CFG_AE_LOCK;
            	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_ae_lock);
        	break;
        case EXT_CFG_AE_UNLOCK:
            	s5k4ecgx_ctrl->setting.ae_lock = EXT_CFG_AE_UNLOCK;
            	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_ae_unlock);
        	break;
        case EXT_CFG_AWB_LOCK:
            	s5k4ecgx_ctrl->setting.awb_lock = EXT_CFG_AWB_LOCK;
           	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_awb_lock);
        	break;
        case EXT_CFG_AWB_UNLOCK:
            	s5k4ecgx_ctrl->setting.awb_lock = EXT_CFG_AWB_UNLOCK;
            	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_awb_unlock);
        	break;
        default:
		cam_err("Invalid(%d)", value);
        	break;
    	}


}

static long s5k4ecgx_set_effect(int8_t value)
{
	int err = -EINVAL;
	CAM_DEBUG("%d",value);
	
retry:
	switch (value) {
	case CAMERA_EFFECT_OFF:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Effect_Normal);
		break;
	case CAMERA_EFFECT_SEPIA:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Effect_Sepia);
		break;
	case CAMERA_EFFECT_MONO:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Effect_Black_White);
		break;
	case CAMERA_EFFECT_NEGATIVE:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Effect_Normal);
		break;
	default:
		cam_err("Invalid(%d)", value);
		value = CAMERA_EFFECT_OFF;
		goto retry;
	}
	
	s5k4ecgx_ctrl->setting.effect = value;
	return err;
}

static void s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(int bit, int set)
{
	int REG_TC_DBG_AutoAlgEnBits = 0;
    
   	/* Read 04E6 */
	s5k4ecgx_i2c_write(0x002C,0x7000);
    	s5k4ecgx_i2c_write(0x002E,0x04E6);
    	s5k4ecgx_i2c_read(0x0F12, (unsigned short*)&REG_TC_DBG_AutoAlgEnBits);

    	if (bit == 3 && set == true) {
        	if (REG_TC_DBG_AutoAlgEnBits & 0x8 == 1)
			return;
		
        	if (s5k4ecgx_ctrl->setting.scene == EXT_CFG_SCENE_NIGHTSHOT)
			mdelay(250);
        	else
			mdelay(100);
		
	        REG_TC_DBG_AutoAlgEnBits = REG_TC_DBG_AutoAlgEnBits | 0x8;
	        s5k4ecgx_i2c_write(0x0028, 0x7000);
	        s5k4ecgx_i2c_write(0x002A, 0x04E6);
	        s5k4ecgx_i2c_write(0x0F12, REG_TC_DBG_AutoAlgEnBits);
    	}
    	else if (bit == 3 && set == false) {
	        if (REG_TC_DBG_AutoAlgEnBits & 0x8 == 0)
			return;
	        if (s5k4ecgx_ctrl->setting.scene == EXT_CFG_SCENE_NIGHTSHOT)
			mdelay(250);
	        else 
			mdelay(100);
		
	        REG_TC_DBG_AutoAlgEnBits = REG_TC_DBG_AutoAlgEnBits & 0xFFF7;
	        s5k4ecgx_i2c_write(0x0028, 0x7000);
	        s5k4ecgx_i2c_write(0x002A, 0x04E6);
	        s5k4ecgx_i2c_write(0x0F12, REG_TC_DBG_AutoAlgEnBits);
    	}
    	else if (bit == 5 && set == true) {
	        if (REG_TC_DBG_AutoAlgEnBits & 0x20 == 1)
			return;
	        if( s5k4ecgx_ctrl->setting.scene == EXT_CFG_SCENE_NIGHTSHOT)
			mdelay(250);
	        else 
			mdelay(100);
		
	        REG_TC_DBG_AutoAlgEnBits = REG_TC_DBG_AutoAlgEnBits | 0x20;
	        s5k4ecgx_i2c_write(0x0028, 0x7000);
	        s5k4ecgx_i2c_write(0x002A, 0x04E6);
	        s5k4ecgx_i2c_write(0x0F12, REG_TC_DBG_AutoAlgEnBits);
    	}
    	else if (bit == 5 && set == false) {
	        if (REG_TC_DBG_AutoAlgEnBits & 0x20 == 0)
			return;
	        if (s5k4ecgx_ctrl->setting.scene == EXT_CFG_SCENE_NIGHTSHOT)
			mdelay(250);
	        else 
			mdelay(100);
		
	        REG_TC_DBG_AutoAlgEnBits = REG_TC_DBG_AutoAlgEnBits & 0xFFDF;
	        s5k4ecgx_i2c_write(0x0028, 0x7000);
	        s5k4ecgx_i2c_write(0x002A, 0x04E6);
	        s5k4ecgx_i2c_write(0x0F12, REG_TC_DBG_AutoAlgEnBits);
    	}

   	return;
}

static int s5k4ecgx_set_whitebalance(int8_t value)
{
	int err = -EINVAL;
	CAM_DEBUG("%d",value);

    	switch (value) {
        case WHITE_BALANCE_AUTO :
            	s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(3,1);
            	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_WB_Auto);
        	break;
        case WHITE_BALANCE_SUNNY:
            	s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(3,0);
            	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_WB_Sunny);
        	break;
        case WHITE_BALANCE_CLOUDY :
            	s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(3,0);
            	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_WB_Cloudy);
        	break;
        case WHITE_BALANCE_FLUORESCENT:
            	s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(3,0);
            	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_WB_Fluorescent);
        	break;
        case WHITE_BALANCE_INCANDESCENT:
            	s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(3,0);
            	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_WB_Tungsten);
        	break;
        default :
		cam_err("Invalid(%d)", value);
        	break;
        }

	s5k4ecgx_ctrl->setting.whiteBalance = value;
	return err;
}

static int  s5k4ecgx_set_brightness(int8_t value)
{
	int err = -EINVAL;
	
	CAM_DEBUG("%d",value);
	
	//if(s5k4ecgx_ctrl->check_dataline)
	//	return 0;

	switch (value) {
	case EV_MINUS_4 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_EV_Minus_4);
		break;
	case EV_MINUS_3 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_EV_Minus_3);
		break;
	case EV_MINUS_2 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_EV_Minus_2);
		break;
	case EV_MINUS_1 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_EV_Minus_3);
		break;
	case EV_DEFAULT :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_EV_Default);
		break;
	case EV_PLUS_1 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_EV_Plus_1);
		break;
	case EV_PLUS_2 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_EV_Plus_2);
		break;
	case EV_PLUS_3 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_EV_Plus_3);
		break;
	case EV_PLUS_4 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_EV_Plus_4);
		break;
	default :
		cam_err("Invalid(%d)", value);
		break;
	}

	s5k4ecgx_ctrl->setting.brightness = value;
	return err;
}


static int  s5k4ecgx_set_iso(int8_t value)
{
	int err = -EINVAL;
	CAM_DEBUG("%d", value);
	
	switch (value) {
        case ISO_AUTO :
            	s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(5,1);
            	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_ISO_Auto);
        	break;
        case ISO_50 :
           	s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(5,0);
            	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_ISO_50);
       	 	break;
        case ISO_100 :
            	s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(5,0);
            	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_ISO_100);
        	break;
        case ISO_200 :
		s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(5,0);
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_ISO_200);
		break;
        case ISO_400 :
            	s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(5,0);
            	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_ISO_400);
        	break;
        default :
		cam_err("Invalid(%d)", value);
        	break;
	}

	s5k4ecgx_ctrl->setting.iso = value;    
	return err;
}


static int s5k4ecgx_set_metering(int8_t value)
{
	int err = -EINVAL;
	CAM_DEBUG("%d", value);

retry:
	switch (value) {
	case METERING_MATRIX:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Metering_Matrix);
		break;
	case METERING_CENTER:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Metering_Center);
		break;
	case METERING_SPOT:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Metering_Spot);
		break;
	default:
		cam_err("Invalid(%d)", value);
		value = METERING_CENTER;
		goto retry;
	}

	s5k4ecgx_ctrl->setting.metering = value;
	return err;
}



static int s5k4ecgx_set_contrast(int8_t value)
{
	int err = -EINVAL;
	CAM_DEBUG("%d",value);

retry:
	switch (value) {
	case CONTRAST_MINUS_2 :
		S5K4ECGX_WRITE_LIST(s5k4ecgx_Contrast_Minus_2);
		break;
        case CONTRAST_MINUS_1 :
		S5K4ECGX_WRITE_LIST(s5k4ecgx_Contrast_Minus_1);
		break;
        case CONTRAST_DEFAULT :
		S5K4ECGX_WRITE_LIST(s5k4ecgx_Contrast_Default);
		break;
        case CONTRAST_PLUS_1 :
		S5K4ECGX_WRITE_LIST(s5k4ecgx_Contrast_Plus_1);
		break;
	case CONTRAST_PLUS_2 :
		S5K4ECGX_WRITE_LIST(s5k4ecgx_Contrast_Plus_2);
        	break;
        default :
		cam_err("Invalid(%d)", value);
		value = METERING_CENTER;
		goto retry;
   	}

	s5k4ecgx_ctrl->setting.contrast = value;	
	return err;
}


static int s5k4ecgx_set_saturation(int8_t value)
{
	int err = -EINVAL;
	CAM_DEBUG("%d",value);

retry:
	switch (value) {
	case SATURATION_MINUS_2 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Saturation_Minus_2);
		break;
        case SATURATION_MINUS_1 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Saturation_Minus_1);
		break;
        case SATURATION_DEFAULT :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Saturation_Default);
		break;
        case SATURATION_PLUS_1 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Saturation_Plus_1);
		break;
	case SATURATION_PLUS_2 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Saturation_Plus_2);
        	break;
        default :
		cam_err("Invalid(%d)", value);
		value = METERING_CENTER;
		goto retry;
   	}

	s5k4ecgx_ctrl->setting.saturation = value;
	return err;
}


static int s5k4ecgx_set_sharpness(int8_t value)
{
	int err = -EINVAL;
	CAM_DEBUG("%d",value);

retry:
	switch (value) {
	case SHARPNESS_MINUS_2 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Sharpness_Minus_2);
		break;
        case SHARPNESS_MINUS_1 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Sharpness_Minus_1);
		break;
        case SHARPNESS_DEFAULT :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Sharpness_Default);
		break;
        case SHARPNESS_PLUS_1 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Sharpness_Plus_1);
		break;
	case SHARPNESS_PLUS_2 :
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Sharpness_Plus_2);
        	break;
        default :
		cam_err("Invalid(%d)", value);
		value = METERING_CENTER;
		goto retry;
   	}

	s5k4ecgx_ctrl->setting.sharpness = value;
	return err;
}




static int  s5k4ecgx_set_scene(int8_t value)
{
	int err = -EINVAL;
	CAM_DEBUG("%d",value);

	if (value != EXT_CFG_SCENE_OFF) {
	    err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Default);

	}
	
	switch (value) {
	case EXT_CFG_SCENE_OFF:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Default);
	    	break;
	case EXT_CFG_SCENE_PORTRAIT: 
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Portrait);
		break;
	case EXT_CFG_SCENE_LANDSCAPE:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Landscape);
		err = s5k4ecgx_set_metering(EXT_CFG_METERING_NORMAL);
		break;
	case EXT_CFG_SCENE_SPORTS:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Sports);
	    	break;
	case EXT_CFG_SCENE_PARTY:
		s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(5,0);
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Party_Indoor);
	    	break;
	case EXT_CFG_SCENE_BEACH:
		s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(5,0);
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Beach_Snow);
		break;
	case EXT_CFG_SCENE_SUNSET:
		s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(3,0);
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Sunset);
		break;
	case EXT_CFG_SCENE_DAWN:
		s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(3,0);
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Duskdawn);
	    	break;
	case EXT_CFG_SCENE_FALL:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Fall_Color);
	    	break;
	case EXT_CFG_SCENE_NIGHTSHOT:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Nightshot);
	    	break;
	case EXT_CFG_SCENE_BACKLIGHT:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Backlight);
		//if(s5k4ecgx_status.flash_mode == EXT_CFG_FLASH_ON ||s5k4ecgx_status.flash_mode == EXT_CFG_FLASH_AUTO)s5k4ecgx_set_metering(EXT_CFG_METERING_CENTER);
		//else s5k4ecgx_set_metering(EXT_CFG_METERING_SPOT);
	   	break;
	case EXT_CFG_SCENE_FIREWORK:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Fireworks);
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_ISO_50);
		break;
	case EXT_CFG_SCENE_TEXT:
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Text);
		break;
	case EXT_CFG_SCENE_CANDLE:
		s5k4ecgx_set_REG_TC_DBG_AutoAlgEnBits(3,0);
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Scene_Candle_Light);
		break;
	default:
		cam_err("Invalid(%d)", value);
	    	break;
	}


	s5k4ecgx_ctrl->setting.scene = value;
	return err;
}



static int s5k4ecgx_set_preview_size( int8_t value)
{
	CAM_DEBUG("%d",value);
		
	#if 0
	if(HD_mode) {
		    HD_mode = 0;
		    S5K4ECGX_WRITE_LIST(s5k4ecgx_1280_Preview_D)
	}
	#endif
	
	switch (value) {
	case EXT_CFG_PREVIEW_SIZE_640x480_VGA:
	case EXT_CFG_PREVIEW_SIZE_176x144_QCIF:
		S5K4ECGX_WRITE_LIST(s5k4ecgx_640_Preview);
		break;
	case EXT_CFG_PREVIEW_SIZE_800x480_WVGA:	
		S5K4ECGX_WRITE_LIST(s5k4ecgx_800_Preview);
		break;
	case EXT_CFG_PREVIEW_SIZE_320x240_QVGA: 
		S5K4ECGX_WRITE_LIST(s5k4ecgx_176_Preview);
		break;
	case EXT_CFG_PREVIEW_SIZE_720x480_D1: 
		S5K4ECGX_WRITE_LIST(s5k4ecgx_720_Preview);
		break;
	case EXT_CFG_PREVIEW_SIZE_1280x720_D1: 
		//HD_mode = 1;
		S5K4ECGX_WRITE_LIST(s5k4ecgx_1280_Preview_E);
		break;
	default:
		cam_err("Invalid");
		break;
	}
	

	s5k4ecgx_ctrl->setting.preview_size= value;
	return 0;
}




static int s5k4ecgx_set_picture_size(int value)
{
	CAM_DEBUG("%d",value);
	
	switch (value) {
	case EXT_CFG_SNAPSHOT_SIZE_2560x1920_5M:
		S5K4ECGX_WRITE_LIST(s5k4ecgx_5M_Capture);
		//s5k4ecgx_set_zoom(EXT_CFG_ZOOM_STEP_0);
		break;
	case EXT_CFG_SNAPSHOT_SIZE_2560x1536_4M_WIDE:
		S5K4ECGX_WRITE_LIST(s5k4ecgx_4M_WIDE_Capture);
		break;
	case EXT_CFG_SNAPSHOT_SIZE_2048x1536_3M:
		S5K4ECGX_WRITE_LIST((s5k4ecgx_3M_Capture));
		break;
	case EXT_CFG_SNAPSHOT_SIZE_2048x1232_2_4M_WIDE:
		S5K4ECGX_WRITE_LIST(s5k4ecgx_2_4M_WIDE_Capture);
		break;
	case EXT_CFG_SNAPSHOT_SIZE_1600x1200_2M:
		S5K4ECGX_WRITE_LIST(s5k4ecgx_2M_Capture);
		break;
	case EXT_CFG_SNAPSHOT_SIZE_1600x960_1_5M_WIDE:
		S5K4ECGX_WRITE_LIST(s5k4ecgx_1_5M_WIDE_Capture);
		break;
	case EXT_CFG_SNAPSHOT_SIZE_1280x960_1M:
		S5K4ECGX_WRITE_LIST(s5k4ecgx_1M_Capture);
		break;
	case EXT_CFG_SNAPSHOT_SIZE_800x480_4K_WIDE:
		S5K4ECGX_WRITE_LIST(s5k4ecgx_4K_WIDE_Capture);
		break;
	case EXT_CFG_SNAPSHOT_SIZE_640x480_VGA:
		S5K4ECGX_WRITE_LIST(s5k4ecgx_VGA_Capture);
		break;
	case EXT_CFG_SNAPSHOT_SIZE_320x240_QVGA:
		S5K4ECGX_WRITE_LIST(s5k4ecgx_QVGA_Capture);
		break;
	default:
		cam_err("Invalid");
		return -EINVAL;
	}


//	if(size != EXT_CFG_SNAPSHOT_SIZE_2560x1920_5M && s5k4ecgx_status.zoom != EXT_CFG_ZOOM_STEP_0)
//		s5k4ecgx_set_zoom(s5k4ecgx_status.zoom);


	s5k4ecgx_ctrl->setting.snapshot_size = value;
	return 0;
}



static int s5k4ecgx_set_movie_mode(int mode)
{

	if ((mode != SENSOR_CAMERA) && (mode != SENSOR_MOVIE)) {
		return -EINVAL;
	}


	return 0;
}


static int s5k4ecgx_check_dataline(s32 val)
{
	int err = -EINVAL;

	CAM_DEBUG("%s", val ? "ON" : "OFF");
	if (val) {
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_DTP_init);
		
	} else {
		err = S5K4ECGX_WRITE_LIST(s5k4ecgx_DTP_stop);
	}

	return err;
}

static int s5k4ecgx_mipi_mode(int mode)
{
	int rc = 0;
	struct msm_camera_csi_params s5k4ecgx_csi_params;
	
	CAM_DEBUG("E");

	if (!s5k4ecgx_ctrl->status.config_csi1) {
		s5k4ecgx_csi_params.lane_cnt = 0;
		s5k4ecgx_csi_params.data_format = CSI_8BIT;
		s5k4ecgx_csi_params.lane_assign = 0xe4;
		s5k4ecgx_csi_params.dpcm_scheme = 0;
		s5k4ecgx_csi_params.settle_cnt = 24;// 0x14; //0x7; //0x14;
		rc = msm_camio_csi_config(&s5k4ecgx_csi_params);
		
		if (rc < 0)
			printk(KERN_ERR "config csi controller failed \n");
		
		s5k4ecgx_ctrl->status.config_csi1 = 1;
	}
	
	CAM_DEBUG("X");
	return rc;
}


static int s5k4ecgx_start(void)
{
	int rc=0;
	int err = -EINVAL;

	CAM_DEBUG("E");
	
	if (s5k4ecgx_ctrl->status.started) {
		CAM_DEBUG("X : already started");
		return rc;
	}
	s5k4ecgx_mipi_mode(1);
	msleep(30); //=> Please add some delay 
	
	
	S5K4ECGX_WRITE_LIST(s5k4ecgx_init_reg1);
	msleep(10);
	
        S5K4ECGX_WRITE_LIST(s5k4ecgx_init_reg2);

	s5k4ecgx_ctrl->status.initialized = 1;
	s5k4ecgx_ctrl->status.started = 1;
	CAM_DEBUG("X");
	return rc;
}



static long s5k4ecgx_video_config(int mode)
{
	int err = -EINVAL;
	CAM_DEBUG("E");

	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Preview_Return);

	return err;

}

static long s5k4ecgx_snapshot_config(int mode)
{
	int err = -EINVAL;
	CAM_DEBUG("E");



	err = S5K4ECGX_WRITE_LIST(s5k4ecgx_Capture_Start);


	
	return err;
}



static long s5k4ecgx_set_sensor_mode(int mode)
{
	int err = -EINVAL;

	switch (mode) {
	case SENSOR_PREVIEW_MODE:
		CAM_DEBUG("SENSOR_PREVIEW_MODE START");
		s5k4ecgx_start();
		
		//if (s5k4ecgx_ctrl->sensor_mode != SENSOR_MOVIE)
		err= s5k4ecgx_video_config(SENSOR_PREVIEW_MODE);

		break;

	case SENSOR_SNAPSHOT_MODE:
		CAM_DEBUG("SENSOR_SNAPSHOT_MODE START");		
		err= s5k4ecgx_snapshot_config(SENSOR_SNAPSHOT_MODE);

		break;

	
	case SENSOR_SNAPSHOT_TRANSFER:
		CAM_DEBUG("SENSOR_SNAPSHOT_TRANSFER START");

		break;
		
	default:
		return -EFAULT;
	}

	return 0;
}

static int s5k4ecgx_sensor_init_probe(const struct msm_camera_sensor_info *data)
{
	int rc = 0;

	CAM_DEBUG("POWER ON START ");
	
	gpio_set_value_cansleep(CAM_VGA_EN, LOW);
	gpio_set_value_cansleep(CAM_8M_RST, LOW);
	gpio_set_value_cansleep(CAM_VGA_RST, LOW);
	gpio_set_value_cansleep(CAM_IO_EN, LOW);  //HOST 1.8V
	//cam_ldo_power_off();	
	//mdelay(10);

	cam_ldo_power_on();
 
	//msm_camio_clk_rate_set(info->mclk);
	msm_camio_clk_rate_set(24000000);
//	mdelay(5); //min 50us

#ifdef CONFIG_LOAD_FILE
	s5k4ecgx_regs_table_init();
#endif
	
	gpio_set_value_cansleep(CAM_VGA_RST, HIGH);
	mdelay(2);//min 50us
	
	CAM_DEBUG("POWER ON END ");

	return rc;
}




int s5k4ecgx_sensor_open_init(const struct msm_camera_sensor_info *data)
{
	int rc = 0;
	CAM_DEBUG("E");
	
	s5k4ecgx_ctrl = kzalloc(sizeof(struct s5k4ecgx_ctrl_t), GFP_KERNEL);
	if (!s5k4ecgx_ctrl) {
		cam_err("failed!");
		rc = -ENOMEM;
		goto init_done;
	}	
	
	if (data)
		s5k4ecgx_ctrl->sensordata = data;
 
	
	rc = s5k4ecgx_sensor_init_probe(data);
	if (rc < 0) {
		cam_err("s5k4ecgx_sensor_open_init failed!");
		goto init_fail;
	}


	
	s5k4ecgx_ctrl->status.started = 0;
	s5k4ecgx_ctrl->status.initialized = 0;
	s5k4ecgx_ctrl->status.config_csi1 = 0;
	
	s5k4ecgx_ctrl->setting.check_dataline = 0;
	s5k4ecgx_ctrl->setting.camera_mode = SENSOR_CAMERA;

	

	CAM_DEBUG("X");
init_done:
	return rc;

init_fail:
	kfree(s5k4ecgx_ctrl);
	return rc;
}

static int s5k4ecgx_init_client(struct i2c_client *client)
{
	/* Initialize the MSM_CAMI2C Chip */
	init_waitqueue_head(&s5k4ecgx_wait_queue);
	return 0;
}



int s5k4ecgx_sensor_ext_config(void __user *argp)
{

	sensor_ext_cfg_data		cfg_data;
	int rc=0;


	if (copy_from_user((void *)&cfg_data, (const void *)argp, sizeof(cfg_data))){
		cam_err("fail copy_from_user!");
	}

	CAM_DEBUG("cmd = %d , param1 = %d",cfg_data.cmd,cfg_data.value_1);
	#if 0
	if( (cfg_data.cmd != EXT_CFG_SET_DTP)
		&& (cfg_data.cmd != EXT_CFG_SET_VT_MODE)	
		&& (cfg_data.cmd != EXT_CFG_SET_MOVIE_MODE)	
		&& (!s5k4ecgx_ctrl->status.initialized)){
		cam_err("camera isn't initialized\n");
		return 0;
	}
	#endif
	switch (cfg_data.cmd) {
	case EXT_CFG_SET_BRIGHTNESS:
		rc = s5k4ecgx_set_brightness(cfg_data.value_1);
		break;

	case EXT_CFG_SET_EFFECT:
		rc = s5k4ecgx_set_effect(cfg_data.value_1);
		break;	
		
	case EXT_CFG_SET_ISO:
		rc = s5k4ecgx_set_iso(cfg_data.value_1);
		break;
		
	case EXT_CFG_SET_WB:
		rc = s5k4ecgx_set_whitebalance(cfg_data.value_1);
		break;

	case EXT_CFG_SET_SCENE:
		rc = s5k4ecgx_set_scene(cfg_data.value_1);
		break;

	case EXT_CFG_SET_METERING:	// auto exposure mode
		rc = s5k4ecgx_set_metering(cfg_data.value_1);
		break;

	case EXT_CFG_SET_CONTRAST:
		rc = s5k4ecgx_set_contrast(cfg_data.value_1);
		break;

	case EXT_CFG_SET_SHARPNESS:
		rc = s5k4ecgx_set_sharpness(cfg_data.value_1);
		break;

	case EXT_CFG_SET_SATURATION:
		rc = s5k4ecgx_set_saturation(cfg_data.value_1);
		break;
		
	case EXT_CFG_SET_PREVIEW_SIZE:	
		rc = s5k4ecgx_set_preview_size(cfg_data.value_1);
		break;

	case EXT_CFG_SET_PICTURE_SIZE:	
		rc = s5k4ecgx_set_picture_size(cfg_data.value_1);
		break;

	case EXT_CFG_SET_JPEG_QUALITY:	
		//rc = s5k4ecgx_set_jpeg_quality(cfg_data.value_1);
		break;
		
	case EXT_CFG_SET_FPS:
		//rc = s5k4ecgx_set_frame_rate(cfg_data.value_1,cfg_data.value_2);
		break;

	case EXT_CFG_SET_DTP:
		break;

 	case EXT_CFG_SET_VT_MODE:
		cam_info("VTCall mode : %d",cfg_data.value_1);
		break;
		
	case EXT_CFG_SET_MOVIE_MODE:
		cam_info("MOVIE mode : %d",cfg_data.value_1);
		s5k4ecgx_set_movie_mode(cfg_data.value_1);
		break;

	default:
		break;
	}

	if (copy_to_user((void *)argp, (const void *)&cfg_data, sizeof(cfg_data))){
		cam_err("fail copy_from_user!");
	}
	
	return rc;	
}

int s5k4ecgx_sensor_config(void __user *argp)
{
	struct sensor_cfg_data cfg_data;
	long   rc = 0;

	if (copy_from_user(&cfg_data,(void *)argp, sizeof(struct sensor_cfg_data)))
		return -EFAULT;

	/* down(&m5mo_sem); */

	CAM_DEBUG("cfgtype = %d, mode = %d", cfg_data.cfgtype, cfg_data.mode);

	switch (cfg_data.cfgtype) {
	case CFG_SET_MODE:
		rc = s5k4ecgx_set_sensor_mode(cfg_data.mode);
		break;
		
	default:
		rc = -EFAULT;
		break;
	}

	/* up(&m5mo_sem); */

	return rc;
}

int s5k4ecgx_sensor_release(void)
{
	int rc = 0;

	/* down(&m5mo_sem); */

	CAM_DEBUG("POWER OFF START");

	gpio_set_value_cansleep(CAM_VGA_RST, LOW);
	mdelay(1);

	sub_cam_ldo_power(OFF);	// have to turn off MCLK before PMIC

	s5k4ecgx_ctrl->status.initialized = 1;
	kfree(s5k4ecgx_ctrl);
	
#ifdef CONFIG_LOAD_FILE
	s5k4ecgx_regs_table_exit();
#endif
	CAM_DEBUG("POWER OFF END");
	/* up(&m5mo_sem); */

	return rc;
}


 static int s5k4ecgx_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int rc = 0;

	CAM_DEBUG("E");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		rc = -ENOTSUPP;
		goto probe_failure;
	}

	s5k4ecgx_sensorw = kzalloc(sizeof(struct s5k4ecgx_work_t), GFP_KERNEL);

	if (!s5k4ecgx_sensorw) {
		rc = -ENOMEM;
		goto probe_failure;
	}

	i2c_set_clientdata(client, s5k4ecgx_sensorw);
	s5k4ecgx_init_client(client);
	s5k4ecgx_client = client;

	CAM_DEBUG("E");


	return 0;

probe_failure:
	kfree(s5k4ecgx_sensorw);
	s5k4ecgx_sensorw = NULL;
	cam_err("s5k4ecgx_i2c_probe failed!");
	return rc;
}


static int __exit s5k4ecgx_i2c_remove(struct i2c_client *client)
{

	struct s5k4ecgx_work_t *sensorw = i2c_get_clientdata(client);
	free_irq(client->irq, sensorw);
//	i2c_detach_client(client);
	s5k4ecgx_client = NULL;
	s5k4ecgx_sensorw = NULL;
	kfree(sensorw);
	return 0;

}


static const struct i2c_device_id s5k4ecgx_id[] = {
    { "s5k4ecgx_i2c", 0 },
    { }
};

//PGH MODULE_DEVICE_TABLE(i2c, s5k4ecgx);

static struct i2c_driver s5k4ecgx_i2c_driver = {
	.id_table	= s5k4ecgx_id,
	.probe  	= s5k4ecgx_i2c_probe,
	.remove 	= __exit_p(s5k4ecgx_i2c_remove),
	.driver 	= {
		.name = "s5k4ecgx",
	},
};


int32_t s5k4ecgx_i2c_init(void)
{
	int32_t rc = 0;

	CAM_DEBUG("E");

	rc = i2c_add_driver(&s5k4ecgx_i2c_driver);

	if (IS_ERR_VALUE(rc))
		goto init_failure;

	return rc;



init_failure:
	cam_err("failed to s5k4ecgx_i2c_init, rc = %d", rc);
	return rc;
}


void s5k4ecgx_exit(void)
{
	i2c_del_driver(&s5k4ecgx_i2c_driver); 	
}


//int m5mo_sensor_probe(void *dev, void *ctrl)
int s5k4ecgx_sensor_probe(const struct msm_camera_sensor_info *info,
				struct msm_sensor_ctrl *s)
{
	int rc = 0;

	CAM_DEBUG("E");
/*	struct msm_camera_sensor_info *info =
		(struct msm_camera_sensor_info *)dev; 

	struct msm_sensor_ctrl *s =
		(struct msm_sensor_ctrl *)ctrl;
*/

 
	rc = s5k4ecgx_i2c_init();
	if (rc < 0)
		goto probe_done;

 	s->s_init	= s5k4ecgx_sensor_open_init;
	s->s_release	= s5k4ecgx_sensor_release;
	s->s_config	= s5k4ecgx_sensor_config;
	s->s_ext_config	= s5k4ecgx_sensor_ext_config;
	s->s_camera_type = BACK_CAMERA_2D;
	s->s_mount_angle = 0;

probe_done:
	cam_err("error, rc = %d", rc);
	return rc;
	
}

#if 0
static int msm_vga_camera_remove(struct platform_device *pdev)
{
	return msm_camera_drv_remove(pdev);
}
#endif
static int __sec_s5k4ecgx_probe(struct platform_device *pdev)
{
	printk("############# S5K4ECGX probe ##############\n");
	return msm_camera_drv_start(pdev, s5k4ecgx_sensor_probe);
}

static struct platform_driver msm_vga_camera_driver = {
	.probe = __sec_s5k4ecgx_probe,
//	.remove	 = msm_vga_camera_remove,
	.driver = {
		.name = "msm_camera_s5k4ecgx",
		.owner = THIS_MODULE,
	},
};

static int __init sec_s5k4ecgx_camera_init(void)
{
	return platform_driver_register(&msm_vga_camera_driver);
}

static void __exit sec_s5k4ecgx_camera_exit(void)
{
	platform_driver_unregister(&msm_vga_camera_driver);
}

module_init(sec_s5k4ecgx_camera_init);
module_exit(sec_s5k4ecgx_camera_exit);

