
#ifndef _SEC_CAM_PMIC_H
#define _SEC_CAM_PMIC_H


#define	CAM_8M_RST		50
#define	CAM_VGA_RST		41
#define	CAM_VGA_EN		42
#define	CAM_IO_EN		37	

#define GPIO_ISP_INT   49

#define	ON		1
#define	OFF		0
#define LOW		0
#define HIGH		1

int cam_ldo_power_on(void);
int cam_ldo_power_off(void);


int sub_cam_ldo_power(int onoff);


#endif
