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


#ifndef S5K6AAFX_H
#define S5K6AAFX_H


#include <mach/board.h> //PGH



/* EV */
#define S5K6AAFX_EV_MINUS_4				0
#define S5K6AAFX_EV_MINUS_3				1
#define S5K6AAFX_EV_MINUS_2				2
#define S5K6AAFX_EV_MINUS_1				3
#define S5K6AAFX_EV_DEFAULT				4
#define S5K6AAFX_EV_PLUS_1					5
#define S5K6AAFX_EV_PLUS_2					6
#define S5K6AAFX_EV_PLUS_3					7
#define S5K6AAFX_EV_PLUS_4					8

/* DTP */
#define S5K6AAFX_DTP_OFF		0
#define S5K6AAFX_DTP_ON		1

#define CAMERA_MODE			0
#define CAMCORDER_MODE		1

int cam_pmic_onoff(int onoff);


#endif /* S5K6AAFX_H */

