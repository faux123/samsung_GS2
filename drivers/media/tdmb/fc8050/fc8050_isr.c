/*****************************************************************************
 Copyright(c) 2009 FCI Inc. All Rights Reserved

 File name : fc8050_isr.c

 Description : fc8050 interrupt service routine

 History :
 ----------------------------------------------------------------------
 2009/08/29 	jason		initial
*******************************************************************************/
#include <linux/kernel.h>

#include "fci_types.h"
#include "fci_hal.h"
#include "fc8050_regs.h"

static u8 ficBuffer[512+4];
static u8 mscBuffer[8192+4];

int (*pFicCallback)(u32 userdata, u8 *data, int length) = NULL;
int (*pMscCallback)(u32 userdata, u8 subchid, u8 *data, int length) = NULL;

u32 gFicUserData;
u32 gMscUserData;


#if 0//def FEATURE_FC8050_DEBUG
#define DPRINTK(x...) printk("TDMB " x)

extern u16 gDmbMode;
u8 fc8050_overrun_check(void)
{
	u16 mfoverStatus;
	
	bbm_word_read(NULL, BBM_BUF_OVERRUN, &mfoverStatus);		

	if(mfoverStatus & gDmbMode)
	{
		//overrun clear
		bbm_word_write(NULL, BBM_BUF_OVERRUN, mfoverStatus);
		bbm_word_write(NULL, BBM_BUF_OVERRUN, 0x0000);
		DPRINTK("FC8050 Overrun Occured !!! Buffer Reset !!! \n");
                
		return 1;
	}
	return 0;
}

void fc8050_buffer_reset(void)
{
	u16 veri_val=0;
	
	bbm_word_read(NULL, BBM_BUF_ENABLE, &veri_val);
	veri_val &= ~gDmbMode;
	bbm_word_write(NULL, BBM_BUF_ENABLE, veri_val);
	veri_val |= gDmbMode; 
	bbm_word_write(NULL, BBM_BUF_ENABLE, veri_val);
	
	DPRINTK("FC8050 Overrun Occured !!! Buffer Reset !!! \n");
}
#endif

void fc8050_isr(HANDLE hDevice)
{
	u8	extIntStatus = 0;
#if 0//def FEATURE_FC8050_DEBUG
    u8      overrun=0;
#endif
	
	//bbm_write(hDevice, BBM_COM_INT_ENABLE, 0);
	bbm_read(hDevice, BBM_COM_INT_STATUS, &extIntStatus);
#if 1
	bbm_write(hDevice, BBM_COM_INT_STATUS, extIntStatus);
	bbm_write(hDevice, BBM_COM_INT_STATUS, 0x00);
#endif

	if(extIntStatus & BBM_MF_INT) {
		u16	mfIntStatus = 0;
		u16	size;
		int  	i;

        	bbm_word_read(hDevice, BBM_BUF_STATUS, &mfIntStatus);
#if 1
		bbm_word_write(hDevice, BBM_BUF_STATUS, mfIntStatus);
		bbm_word_write(hDevice, BBM_BUF_STATUS, 0x0000);
#endif

#if 0//def FEATURE_FC8050_DEBUG
            overrun=fc8050_overrun_check();
#endif
        	if(mfIntStatus & 0x0100) {
        		bbm_word_read(hDevice, BBM_BUF_FIC_THR, &size);
        		size += 1;
        		if(size-1) {
        			bbm_data(hDevice, BBM_COM_FIC_DATA, &ficBuffer[4], size);
#ifdef CONFIG_TDMB_SPI
        			if(pFicCallback)
        				(*pFicCallback)(gFicUserData, &ficBuffer[6], size);
#else
        			if(pFicCallback)
        				(*pFicCallback)(gFicUserData, &ficBuffer[4], size);
#endif

        		}
        	}

        	for(i=0; i<8; i++) {
        		if(mfIntStatus & (1<<i)) {
        			bbm_word_read(hDevice, BBM_BUF_CH0_THR+i*2, &size);
        			size += 1;

        			if(size-1) {
        				u8  subChId;

        				bbm_read(hDevice, BBM_BUF_CH0_SUBCH+i, &subChId);
        				subChId = subChId & 0x3f;

        				bbm_data(hDevice, (BBM_COM_CH0_DATA+i), &mscBuffer[4], size);

#ifdef CONFIG_TDMB_SPI
        				if(pMscCallback)
        					(*pMscCallback)(gMscUserData, subChId, &mscBuffer[6], size);
#else
        				if(pMscCallback)
        					(*pMscCallback)(gMscUserData, subChId, &mscBuffer[4], size);
#endif        					
        			}
        		}
        	}
		
#if 0//def FEATURE_FC8050_DEBUG
        	if(overrun)
				fc8050_buffer_reset();
#endif

#if 0
		bbm_word_write(hDevice, BBM_BUF_STATUS, mfIntStatus);
		bbm_word_write(hDevice, BBM_BUF_STATUS, 0x0000);
#endif
	}
#if 0
	bbm_write(hDevice, BBM_COM_INT_STATUS, extIntStatus);
	bbm_write(hDevice, BBM_COM_INT_STATUS, 0x00);
#endif

#if 0
	if(extIntStatus & BBM_SCI_INT) {
		extern void PL131_IntHandler(void);
		PL131_IntHandler();
	}

	if(extIntStatus & BBM_WAGC_INT) {
	}

	if(extIntStatus & BBM_RECFG_INT) {
	}

	if(extIntStatus & BBM_TII_INT) {
	}

	if(extIntStatus & BBM_SYNC_INT) {
	}

	if(extIntStatus & BBM_I2C_INT) {
	}

	if(extIntStatus & BBM_MP2_INT) {
	}
#endif
	//bbm_write(hDevice, BBM_COM_INT_ENABLE, BBM_MF_INT);
}

