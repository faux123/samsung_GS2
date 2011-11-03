/*
 * Copyright (c) 2008 QUALCOMM USA, INC.
 * Author: Haibo Jeff Zhong <hzhong@qualcomm.com>
 * 
 * All source code in this file is licensed under the following license
 * except where indicated.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can find it at http://www.fsf.org
 *
 */



#ifndef S5K5AAFA_H
#define S5K5AAFA_H


#include <mach/board.h> //PGH


#if 0
#if 0
//unuse standby pin #define CAM_STB						85
#define CAM_RST						17
#define CAM_ON						76//REV02
//#define CAM_ON						22//REV01
#define CAM_FLASH_EN				23
#define CAM_FLASH_SET				31
#endif
#include <linux/mfd/pmic8058.h>

#define	CAM_TYPE_8M	0
#define	CAM_TYPE_VGA	1

#if (CONFIG_BOARD_REVISION >= 0x01)
#define	CAM_8M_RST			PM8058_GPIO_PM_TO_SYS(25)	// must sub 1 - PM_GPIO_26
#define	CAM_VGA_RST			PM8058_GPIO_PM_TO_SYS(29)	//PM_GPIO_30
#else
#define	CAM_8M_RST			0
#define	CAM_VGA_RST		31
#endif
#define	CAM_MEGA_EN			1	// STEALTH REV03 below
#define	CAM_MEGA_EN_REV04	74	// STEALTH REV04 
#if (CONFIG_BOARD_REVISION >= 0x01)
#define	CAM_VGA_EN		PM8058_GPIO_PM_TO_SYS(24)	// PM_GPIO_25
#else
#define	CAM_VGA_EN		2
#endif
#define	CAM_PMIC_STBY			3	// PMIC EN (CAM_IO_EN)

#define	CAM_SENSOR_A_EN		PM8058_GPIO_PM_TO_SYS(39)	// STEALTH HW REV04 PM8058 GPIO 40

#define	ON		1
#define	OFF		0
#define LOW							0
#define HIGH							1
#endif

/* EV */
#define S5K5AAFA_EV_MINUS_4				0
#define S5K5AAFA_EV_MINUS_3				1
#define S5K5AAFA_EV_MINUS_2				2
#define S5K5AAFA_EV_MINUS_1				3
#define S5K5AAFA_EV_DEFAULT				4
#define S5K5AAFA_EV_PLUS_1					5
#define S5K5AAFA_EV_PLUS_2					6
#define S5K5AAFA_EV_PLUS_3					7
#define S5K5AAFA_EV_PLUS_4					8

/* DTP */
#define S5K5AAFA_DTP_OFF		0
#define S5K5AAFA_DTP_ON		1

#define CAMERA_MODE			0
#define CAMCORDER_MODE		1

int cam_pmic_onoff(int onoff);


#endif /* S5K5AAFA_H */

