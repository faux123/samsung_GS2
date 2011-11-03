/*
 *  SMB136-charger.c
 *  SMB136 charger interface driver
 *
 *  Copyright (C) 2011 Samsung Electronics
 *
 *  <jongmyeong.ko@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/power_supply.h>
#include <linux/regulator/machine.h>
#include <linux/smb136_charger.h>
#include <linux/i2c/fsa9480.h>

/* Register define */
#define SMB136_INPUT_AND_CHARGE_CURRENTS		0x00
#define	SMB136_CURRENT_TERMINATION			0x01
#define SMB136_FLOAT_VOLTAGE				0x02
#define SMB136_FUNCTION_CONTROL_A1			0x03
#define SMB136_FUNCTION_CONTROL_A2			0x04
#define SMB136_FUNCTION_CONTROL_B			0x05
#define SMB136_OTG_PWR_AND_LDO_CONTROL			0x06
#define SMB136_FAULT_INT_REGISTER			0x07
#define SMB136_CELL_TEMPERATURE_MONITOR			0x08
#define SMB136_SAFTYTIMER_THERMALSHUTDOWN		0x09
#define SMB136_I2C_BUS_SLAVE_ADDRESS			0x0A

#define SMB136_CLEAR_IRQ				0x30
#define SMB136_COMMAND					0x31
#define SMB136_INTERRUPT_STATUS_A			0x32
#define SMB136_INTERRUPT_STATUS_B			0x33
#define SMB136_INTERRUPT_STATUS_C			0x34
#define SMB136_INTERRUPT_STATUS_D			0x35
#define SMB136_INTERRUPT_STATUS_E			0x36
#define SMB136_INTERRUPT_STATUS_F			0x37
#define SMB136_INTERRUPT_STATUS_G			0x38
#define SMB136_INTERRUPT_STATUS_H			0x39

enum {
	BAT_NOT_DETECTED,
	BAT_DETECTED
};

enum {
	CHG_MODE_NONE,
	CHG_MODE_AC,
	CHG_MODE_USB,
	CHG_MODE_MISC,
	CHG_MODE_UNKNOWN
};

struct smb136_chip {
	struct i2c_client		*client;
	struct delayed_work		work;
	struct power_supply		psy_bat;
	struct smb136_platform_data	*pdata;
	struct mutex		mutex;

	int chg_mode;
	unsigned int batt_vcell;
	int chg_set_current; /* fast charging current */
	int chg_icl; /* input current limit */
	int lpm_chg_mode;
	unsigned int float_voltage; /* float voltage */
	int aicl_current;
	int aicl_status;
};

extern unsigned int get_hw_rev(void);
extern unsigned int is_lpcharging_state(void);

static enum power_supply_property smb136_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
};


static int smb136_charger_fast_current[] = {
	500,
	650,
	750,
	850,
	950,
	1100,
	1300,
	1500,
};

static int smb136_charger_top_off[] = {
	35,
	50,
	100,
	150,
};
		
static int smb136_charger_usbin_icl[] = {
	700,
	800,
	900,
	1000,
	1100,
	1200,
	1300,
	1400,
};

static int smb136_fast_current_to_regval(int curr)
{
	int i;
	int reg_current;
	
	if (curr < smb136_charger_fast_current[0])
		return 0;
 
	for (i = 0; i<ARRAY_SIZE(smb136_charger_fast_current); i++) {
		if (curr < smb136_charger_fast_current[i])
			return i-1;
	}
	
	return i-1;
}

static int smb136_top_off_to_regval(int curr)
{
	int i;
	int reg_current;
	
 	if (curr < smb136_charger_top_off[1])
		return 3;

	for (i = 2; i<ARRAY_SIZE(smb136_charger_top_off); i++) {
		if (curr < smb136_charger_top_off[i])
			return i-2;
	}

	return i-2;
}

static int smb136_icl_to_regval(int curr)
{
	int i;
	int reg_current;


	if (curr < smb136_charger_usbin_icl[0])
		return 0;

	for (i=0; i<ARRAY_SIZE(smb136_charger_usbin_icl); i++){
		if (curr < smb136_charger_usbin_icl[i])
			return i-1;
	}
	
	return i-1;
}


static int smb136_write_reg(struct i2c_client *client, int reg, u8 value)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mutex);
	
	ret = i2c_smbus_write_byte_data(client, reg, value);

	if (ret < 0) {
		pr_err("%s: err %d, try again!\n", __func__, ret);
		ret = i2c_smbus_write_byte_data(client, reg, value);
		if (ret < 0)
			pr_err("%s: err %d\n", __func__, ret);
	}

	mutex_unlock(&chip->mutex);
	
	return ret;
}

static int smb136_read_reg(struct i2c_client *client, int reg)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->mutex);
	
	ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0) {
		pr_err("%s: err %d, try again!\n", __func__, ret);
		ret = i2c_smbus_read_byte_data(client, reg);
		if (ret < 0)
			pr_err("%s: err %d\n", __func__, ret);
	}

	mutex_unlock(&chip->mutex);

	return ret;
}

static void smb136_print_reg(struct i2c_client *client, int reg)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);
	u8 data = 0;

	mutex_lock(&chip->mutex);
	
	data = i2c_smbus_read_byte_data(client, reg);

	if (data < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, data);
	else
		printk("%s : reg (0x%x) = 0x%x\n", __func__, reg, data);

	mutex_unlock(&chip->mutex);
}

static void smb136_print_all_regs(struct i2c_client *client)
{
	smb136_print_reg(client, 0x31);
	smb136_print_reg(client, 0x32);
	smb136_print_reg(client, 0x33);
	smb136_print_reg(client, 0x34);
	smb136_print_reg(client, 0x35);
	smb136_print_reg(client, 0x36);
	smb136_print_reg(client, 0x37);
	smb136_print_reg(client, 0x38);
	smb136_print_reg(client, 0x39);
	smb136_print_reg(client, 0x00);
	smb136_print_reg(client, 0x01);
	smb136_print_reg(client, 0x02);
	smb136_print_reg(client, 0x03);
	smb136_print_reg(client, 0x04);
	smb136_print_reg(client, 0x05);
	smb136_print_reg(client, 0x06);
	smb136_print_reg(client, 0x07);
	smb136_print_reg(client, 0x08);
	smb136_print_reg(client, 0x09);
	smb136_print_reg(client, 0x0a);
}

static void smb136_allow_volatile_writes(struct i2c_client *client)
{
	int val, reg;
	u8 data;

	reg = SMB136_COMMAND;
	val = smb136_read_reg(client, reg);
	
	if ((val >= 0) && !(val&0x80)) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		data |= (0x1 << 7);
		if (smb136_write_reg(client, reg, data) < 0)
			pr_err("%s : error!\n", __func__);
		val = smb136_read_reg(client, reg);
		if (val >= 0) {
			data = (u8)data;
			pr_info("%s : => reg (0x%x) = 0x%x\n", __func__, reg, data);
		}
	}
}

static void smb136_set_command_reg(struct i2c_client *client)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);
	int val, reg;
	u8 data;

	reg = SMB136_COMMAND;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		if (chip->chg_mode == CHG_MODE_AC ||
			chip->chg_mode == CHG_MODE_MISC ||
			chip->chg_mode == CHG_MODE_UNKNOWN)
			data = 0xad;
		else
			data = 0xa9; /* usb */
		if (smb136_write_reg(client, reg, data) < 0)
			pr_err("%s : error!\n", __func__);
		val = smb136_read_reg(client, reg);
		if (val >= 0) {
			data = (u8)data;
			pr_info("%s : => reg (0x%x) = 0x%x\n", __func__, reg, data);
		}
	}
}

static void smb136_charger_function_conrol(struct i2c_client *client)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);
	int val, reg;
	u8 data, set_data;

	smb136_allow_volatile_writes(client);

	reg = SMB136_INPUT_AND_CHARGE_CURRENTS;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		if (chip->chg_mode == CHG_MODE_AC) {
			set_data = 0x8C; /* fast 950mA, Pre-charge 100mA, TermCurr 150mA */
		} else if (chip->chg_mode == CHG_MODE_MISC) {
			set_data = 0x4C; /* fast 750mA, Pre-charge 100mA, TermCurr 150mA */
		} else
			set_data = 0x0C; /* fast 500mA, Pre-charge 100mA, TermCurr 150mA */ 
		if (data != set_data) { /* this can be changed with top-off setting */
			data = set_data;
			if (smb136_write_reg(client, reg, data) < 0)
				pr_err("%s : error! 0x%x\n", __func__,reg);
			val = smb136_read_reg(client, reg);
			if (val >= 0) {
				data = (u8)val;
				dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
					__func__, reg, data);
			}
		}
	}

	smb136_allow_volatile_writes(client);

	reg = SMB136_CURRENT_TERMINATION;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		if (chip->chg_mode == CHG_MODE_AC) {
			set_data = 0x70; /* input 1A, AICL enable, AICL threshold 4.25V */
		} else if (chip->chg_mode == CHG_MODE_MISC) {
			set_data = 0x10; /* input 700mA, AICL enable, AICL threshold 4.25V */
		} else
			set_data = 0x10; /* input 700mA, AICL enable, AICL threshold 4.25V */
		if (data != set_data) { /* AICL enable */
			data = set_data;
			if (smb136_write_reg(client, reg, data) < 0)
				pr_err("%s : error! 0x%x\n", __func__,reg);
			val = smb136_read_reg(client, reg);
			if (val >= 0) {
				data = (u8)val;
				dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
					__func__, reg, data);
			}
		}
	}

	smb136_allow_volatile_writes(client);

	reg = SMB136_FLOAT_VOLTAGE;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		if (data != 0xca) {
			data = 0xca; /* 4.2V float voltage */
			if (smb136_write_reg(client, reg, data) < 0)
				pr_err("%s : error! 0x%x\n", __func__,reg);
			val = smb136_read_reg(client, reg);
			if (val >= 0) {
				data = (u8)val;
				dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
					__func__, reg, data);
			}
		}
	}

	smb136_allow_volatile_writes(client);

	reg = SMB136_FUNCTION_CONTROL_A1;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		if (data != 0xd4) {
			/*disable auto recharge, not allowe end of charge cycle */
			/*pre-charge->fast_charge threshold 2.6v, */
			data = 0xd4; 
			if (smb136_write_reg(client, reg, data) < 0)
				pr_err("%s : error! 0x%x\n", __func__,reg);
			val = smb136_read_reg(client, reg);
			if (val >= 0) {
				data = (u8)val;
				dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
					__func__, reg, data);
			}
		}
	}

	smb136_allow_volatile_writes(client);

	reg = SMB136_FUNCTION_CONTROL_A2;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		if (data != 0x29) {
			data = 0x29;
			if (smb136_write_reg(client, reg, data) < 0)
				pr_err("%s : error! 0x%x\n", __func__,reg);
			val = smb136_read_reg(client, reg);
			if (val >= 0) {
				data = (u8)val;
				dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
					__func__, reg, data);
			}
		}
	}

	smb136_allow_volatile_writes(client);

	reg = SMB136_FUNCTION_CONTROL_B;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		if (data != 0x0) {
			data = 0x0;
			if (smb136_write_reg(client, reg, data) < 0)
				pr_err("%s : error! 0x%x\n", __func__,reg);
			val = smb136_read_reg(client, reg);
			if (val >= 0) {
				data = (u8)val;
				dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
					__func__, reg, data);
			}
		}
	}

	smb136_allow_volatile_writes(client);

	reg = SMB136_OTG_PWR_AND_LDO_CONTROL;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		set_data = 0x6e;
		if (data != set_data) {
			data = set_data;
			if (smb136_write_reg(client, reg, data) < 0)
				pr_err("%s : error! 0x%x\n", __func__,reg);
			val = smb136_read_reg(client, reg);
			if (val >= 0) {
				data = (u8)val;
				dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
					__func__, reg, data);
			}
		}
	}

	smb136_allow_volatile_writes(client);

	reg = SMB136_CELL_TEMPERATURE_MONITOR;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		if (data != 0xdf) {
			data = 0xdf; /* Thermistor currnt 0, -5, +65 */
			if (smb136_write_reg(client, reg, data) < 0)
				pr_err("%s : error! 0x%x\n", __func__,reg);
			val = smb136_read_reg(client, reg);
			if (val >= 0) {
				data = (u8)val;
				dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
					__func__, reg, data);
			}
		}
	}

	smb136_allow_volatile_writes(client);

	reg = SMB136_SAFTYTIMER_THERMALSHUTDOWN;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		if (data != 0x4b) {
			data = 0x4b;
			if (smb136_write_reg(client, reg, data) < 0)
				pr_err("%s : error! 0x%x\n", __func__,reg);
			val = smb136_read_reg(client, reg);
			if (val >= 0) {
				data = (u8)val;
				dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
					__func__, reg, data);
			}
		}
	}
}

static int smb136_check_charging_status(struct i2c_client *client)
{
	int val, reg;
	u8 data = 0;
	int ret = -1;

	reg = SMB136_INTERRUPT_STATUS_E;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);

		ret = (data&(0x3<<1))>>1;
		dev_info(&client->dev, "%s : status = 0x%x\n",
			__func__, data);
	}

	return ret;
}

static bool smb136_check_is_charging(struct i2c_client *client)
{
	int val, reg;
	u8 data = 0;
	bool ret = false;

	reg = SMB136_INTERRUPT_STATUS_E;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);

		if (data&0x1)
			ret = true; /* charger enabled */
	}

	return ret;
}

static bool smb136_check_bat_full(struct i2c_client *client)
{
	int val, reg;
	u8 data = 0;
	bool ret = false;

	reg = SMB136_INTERRUPT_STATUS_E;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		/* printk("%s : reg (0x%x) = 0x%x\n",
			__func__, SMB136_BATTERY_CHARGING_STATUS_C, data); */

		if (data&(0x1<<6))
			ret = true; /* full */
	}

	return ret;
}

/* vf check */
static bool smb136_check_bat_missing(struct i2c_client *client)
{
	int val, reg;
	u8 data = 0;
	bool ret = false;

	reg = SMB136_INTERRUPT_STATUS_F;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		//printk("%s : reg (0x%x) = 0x%x\n", __func__, reg, data);

		if (data&(0x1<<4)) {
			pr_info("%s : reg (0x%x) = 0x%x\n", __func__, reg, data);
			ret = true; /* missing battery */
		}
	}

	return ret;
}

/* whether valid dcin or not */
static bool smb136_check_vdcin(struct i2c_client *client)
{
	int val, reg;
	u8 data = 0;
	bool ret = false;

	reg = SMB136_INTERRUPT_STATUS_B;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		//printk("%s : reg (0x%x) = 0x%x\n", __func__, reg, data);

		if (!(data&0x3))
			ret = true;
	}
	
	return ret;
}

static bool smb136_check_bmd_disabled(struct i2c_client *client)
{
	int val, reg;
	u8 data = 0;
	bool ret = false;

	reg = SMB136_OTG_PWR_AND_LDO_CONTROL;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		//printk("%s : reg (0x%x) = 0x%x\n", __func__, reg, data);

		if (data&(0x1<<7)) {
			ret = true;
			pr_info("%s : return ture : reg(0x%x)=0x%x (0x%x)\n", __func__,
				reg, data, data&(0x1<<7));
		}
	}

	return ret;
}

static int smb136_chg_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct smb136_chip *chip = container_of(psy,
				struct smb136_chip, psy_bat);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (smb136_check_vdcin(chip->client))
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		if (smb136_check_bat_missing(chip->client))
			val->intval = BAT_NOT_DETECTED;
		else
			val->intval = BAT_DETECTED;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		//printk("%s : check bmd available\n", __func__);
		//smb136_print_all_regs(chip->client);
		/* check VF check available */
		if (smb136_check_bmd_disabled(chip->client))
			val->intval = 1;
		else
			val->intval = 0;
		//printk("smb136_check_bmd_disabled is %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		if (smb136_check_bat_full(chip->client))
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		switch (smb136_check_charging_status(chip->client)) {
			case 0:
				val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
				break;
			case 1:
				val->intval = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
				break;
			case 2:
				val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
				break;
			case 3:
				val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
				break;
			default:
				pr_err("%s : get charge type error!\n", __func__);
				return -EINVAL;
		}
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		if (smb136_check_is_charging(chip->client))
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CURRENT_ADJ:
		//pr_info("%s : get charging current\n", __func__);
		if (chip->chg_set_current != 0) {
			if(chip->chg_set_current == 850)
				val->intval = 900;
			else if(chip->chg_set_current == 950)
				val->intval = 1000;
			else
				val->intval = chip->chg_set_current;
		} else {
			
			return -EINVAL;
		}
		
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int smb136_set_top_off(struct i2c_client *client, int top_off)
{
	int val, reg, set_val = 0;
	
	u8 data;

	smb136_allow_volatile_writes(client);

	val = smb136_top_off_to_regval(top_off);
	printk("top_off  : %d ======\n",val);
	reg = SMB136_INPUT_AND_CHARGE_CURRENTS;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		data &= ~(0x3 << 1);
		data |= (set_val << 1);
		if (smb136_write_reg(client, reg, data) < 0) {
			pr_err("%s : error!\n", __func__);
			return -1;
		}
		data = smb136_read_reg(client, reg);
		dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
			__func__, reg, data);
	}

	return 0;
}

static int smb136_set_charging_current(struct i2c_client *client,
				       int chg_current)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);

	dev_info(&client->dev, "%s : \n", __func__);

	if (chg_current < 450 || chg_current > 1200)
		return -EINVAL;

	chip->chg_set_current = chg_current;

	if (chg_current == 500) {
		chip->chg_mode = CHG_MODE_USB;
	} else if (chg_current == 900) {
		chip->chg_mode = CHG_MODE_AC;
	} else if (chg_current == 700) {
		chip->chg_mode = CHG_MODE_MISC;
	} else if (chg_current == 450) {
		chip->chg_mode = CHG_MODE_UNKNOWN;
	} else {
		pr_err("%s : error! invalid setting current (%d)\n",
			__func__, chg_current);
		chip->chg_mode = CHG_MODE_NONE;
		chip->chg_set_current = 0;
		return -1;
	}
	
	return 0;
}

static int smb136_set_fast_current(struct i2c_client *client,
				   int fast_current)
{
	int val, reg, set_val = 0;
	u8 data;

	smb136_allow_volatile_writes(client);

	reg = SMB136_INPUT_AND_CHARGE_CURRENTS;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x, set_val = 0x%x\n",
			__func__, reg, data, set_val);
		data &= ~(0x7 << 5);
		data |= (set_val << 5);
		dev_info(&client->dev, "%s : write data = 0x%x\n", __func__, data);
		if (smb136_write_reg(client, reg, data) < 0) {
			pr_err("%s : error!\n", __func__);
			return -1;
		}
		data = smb136_read_reg(client, reg);
		dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
			__func__, reg, data);
	}

	return 0;
}

static int smb136_set_input_current_limit(struct i2c_client *client,
					  int input_current)
{
	int val, reg, set_val = 0;
	u8 data;

	smb136_allow_volatile_writes(client);

	reg = SMB136_CURRENT_TERMINATION;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)input_current;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x, set_val = 0x%x\n",
			__func__, reg, data, set_val);
		data &= ~(0x7 << 5);
		data |= (set_val << 5);
		dev_info(&client->dev, "%s : write data = 0x%x\n", __func__, data);
		if (smb136_write_reg(client, reg, data) < 0) {
			pr_err("%s : error!\n", __func__);
			return -1;
		}
		data = smb136_read_reg(client, reg);
		dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
			__func__, reg, data);
	}

	return 0;
}

static int smb136_adjust_charging_current(struct i2c_client *client,
							int chg_current)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);
	int ret = 0;
	int chg_current_regval;
	
	dev_info(&client->dev, "%s : \n", __func__);

	chg_current_regval = smb136_fast_current_to_regval(chg_current);
	printk("chg_current_regval : %d \n",chg_current_regval);
	ret = smb136_set_fast_current(client, chg_current_regval);

	dev_info(&client->dev, "%s : fast current is set as  %d mA\n",
			 __func__, smb136_charger_fast_current[chg_current_regval]);

	chip->chg_set_current = smb136_charger_fast_current[chg_current_regval];

	return ret;
}

static int smb136_adjust_input_current_limit(struct i2c_client *client,
											int chg_current)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);
	int ret = 0;
	int chg_current_regval;
	
	dev_info(&client->dev, "%s : \n", __func__);

	chg_current_regval  = smb136_icl_to_regval(chg_current);
	printk("chg_current_regval : %d \n",chg_current_regval);
	ret = smb136_set_input_current_limit(client, chg_current_regval);

	dev_info(&client->dev, "%s : input current limit is set as  %d mA\n",
		 __func__, smb136_charger_usbin_icl[chg_current_regval]);

	chip->chg_icl = smb136_charger_usbin_icl[chg_current_regval];
	
	return ret;
}

static int smb136_get_input_current_limit(struct i2c_client *client)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);
	int val, reg = 0;
	u8 data = 0;

	dev_info(&client->dev, "%s : \n", __func__);

	reg = SMB136_CURRENT_TERMINATION;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);

		data &= (0x7<<5);
		data >>= 5;

		if (data > 7) {
			pr_err("%s: invalid icl value(%d)\n", __func__, data);
			return -EINVAL;
		}
		
		if (data == 0) {
			chip->chg_icl = 700;
		} else {
			chip->chg_icl = 700+(data*100);
		}

		dev_info(&client->dev, "%s : get icl = %d, data = %d\n",
			__func__, chip->chg_icl, data);
	} else {
		pr_err("%s: get icl failed\n", __func__);
		chip->chg_icl = 0;
		return -EINVAL;
	}

	return 0;
}

static int smb136_get_AICL_status(struct i2c_client *client)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);
	int val, reg = 0;
	u8 data = 0;

	dev_dbg(&client->dev, "%s : \n", __func__);

	reg = SMB136_INTERRUPT_STATUS_C;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_dbg(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);

		chip->aicl_status = (data&0x8)>>3;

		data &= (0xf<<4);
		data >>= 4;

		if (data > 8) {
			pr_err("%s: invalid aicl value(%d)\n", __func__, data);
			return -EINVAL;
		}
		
		if (data == 0x0) {
			chip->aicl_current= 700;
		} else if (data == 0xe) {
			chip->aicl_current = 275;
		} else if (data == 0xf) {
			chip->aicl_current = 500;
		} else {
			chip->aicl_current = (data*100)+700;
		}

		dev_dbg(&client->dev, "%s : get aicl = %d, status = %d, data = %d\n",
			__func__, chip->aicl_current, chip->aicl_status, data);
	} else {
		pr_err("%s: get aicl failed\n", __func__);
		chip->aicl_current = 0;
		return -EINVAL;
	}

	return 0;
}

static int smb136_adjust_float_voltage(struct i2c_client *client,
											int float_voltage)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);
	int val, reg, set_val = 0;
	u8 rdata, wdata;

	dev_info(&client->dev, "%s : \n", __func__);

	if (chip->chg_mode != CHG_MODE_AC) {
		pr_err("%s: not AC, return!\n", __func__);
		return -EINVAL;
	}
	
	if (float_voltage < 3460 || float_voltage > 4720) {
		pr_err("%s: invalid set data\n", __func__);
		return -EINVAL;
	}
	/* support only pre-defined range */
	if (float_voltage == 4200)
		wdata = 0xca;
	else if (float_voltage == 4220)
		wdata = 0xcc;
	else if (float_voltage == 4240)
		wdata = 0xce;
	else {
		wdata = 0xca;
		pr_err("%s: it's not supported\n", __func__);
		return -EINVAL;
	}

	reg = SMB136_FLOAT_VOLTAGE;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		rdata = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x, set_val = 0x%x\n",
			__func__, reg, rdata, set_val);
		if (rdata != wdata) {
			smb136_allow_volatile_writes(client);
			dev_info(&client->dev, "%s : write data = 0x%x\n", __func__, wdata);
			if (smb136_write_reg(client, reg, wdata) < 0) {
				pr_err("%s : error!\n", __func__);
				return -1;
			}
			rdata = smb136_read_reg(client, reg);
			dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
				__func__, reg, rdata);
			chip->float_voltage = float_voltage;
		}
	}
	
	return 0;
}

static unsigned int smb136_get_float_voltage(struct i2c_client *client)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);
	unsigned int float_voltage = 0;
	int val, reg;
	u8 data;

	dev_dbg(&client->dev, "%s : \n", __func__);

	reg = SMB136_FLOAT_VOLTAGE;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		float_voltage = 3460 + ((data&0x7F)/2)*20;
		chip->float_voltage = float_voltage;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x, float vol = %d\n",
			__func__, reg, data, float_voltage);
	} else {
		/* for re-setting, set to zero */
		chip->float_voltage = 0;
	}
	
	return 0;
}

static int smb136_enable_otg(struct i2c_client *client)
{
	int val, reg;
	u8 data;

	dev_info(&client->dev, "%s : \n", __func__);
	
	reg = SMB136_COMMAND;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		if (data != 0xbb)
		{
			dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
				__func__, reg, data);
			data = 0xbb;
			if (smb136_write_reg(client, reg, data) < 0) {
				pr_err("%s : error!\n", __func__);
				return -1;
			}
			msleep(100);
		
			data = smb136_read_reg(client, reg);
			dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
				__func__, reg, data);
		}
	}
	return 0;
}

static int smb136_disable_otg(struct i2c_client *client)
{
	int val, reg;
	u8 data;

	dev_info(&client->dev, "%s : \n", __func__);

	reg = SMB136_FUNCTION_CONTROL_B;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		data = 0x0c;
		if (smb136_write_reg(client, reg, data) < 0) {
			pr_err("%s : error!\n", __func__);
			return -1;
		}
		msleep(100);
		data = smb136_read_reg(client, reg);
		dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
			__func__, reg, data);
						
	}

	reg = SMB136_COMMAND;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		data = 0xb9;
		if (smb136_write_reg(client, reg, data) < 0) {
			pr_err("%s : error!\n", __func__);
			return -1;
		}		
		msleep(100);
		data = smb136_read_reg(client, reg);
		dev_info(&client->dev, "%s : => reg (0x%x) = 0x%x\n",
			__func__, reg, data);
		fsa9480_otg_detach();
	}
	return 0;
}

static int smb136_chgen_bit_control(struct i2c_client *client, bool enable)
{
	int val, reg;
	u8 data;
	struct smb136_chip *chip = i2c_get_clientdata(client);

	dev_info(&client->dev, "%s : \n", __func__);

	reg = SMB136_COMMAND;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
								__func__, reg, data);
		if (enable)
			data &= ~(0x1 << 4); /* "0" turn off the charger */
		else
			data |= (0x1 << 4); /* "1" turn off the charger */
		if (smb136_write_reg(client, reg, data) < 0) {
			pr_err("%s : error!\n", __func__);
			return -1;
		}
		data = smb136_read_reg(client, reg);
		pr_info("%s : => reg (0x%x) = 0x%x\n", __func__, reg, data);
	}

	return 0;
}

static int smb136_enable_charging(struct i2c_client *client)
{
	int val, reg;
	u8 data;
	struct smb136_chip *chip = i2c_get_clientdata(client);

	dev_info(&client->dev, "%s : \n", __func__);

	reg = SMB136_COMMAND;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
									__func__, reg, data);
		if (chip->chg_mode == CHG_MODE_AC ||
			chip->chg_mode == CHG_MODE_MISC||
			chip->chg_mode == CHG_MODE_UNKNOWN)
			data = 0xad;
		else if (chip->chg_mode == CHG_MODE_USB)
			data = 0xa9;
		else
			data = 0xb9;
		if (smb136_write_reg(client, reg, data) < 0) {
			pr_err("%s : error!\n", __func__);
			return -1;
		}
		data = smb136_read_reg(client, reg);
		pr_info("%s : => reg (0x%x) = 0x%x\n", __func__, reg, data);
	}

	return 0;
}

static int smb136_disable_charging(struct i2c_client *client)
{
	int val, reg;
	u8 data;
	struct smb136_chip *chip = i2c_get_clientdata(client);

	dev_info(&client->dev, "%s : \n", __func__);

	reg = SMB136_COMMAND;
	val = smb136_read_reg(client, reg);
	if (val >= 0) {
		data = (u8)val;
		dev_info(&client->dev, "%s : reg (0x%x) = 0x%x\n",
									__func__, reg, data);
		data = 0xb9;
		if (smb136_write_reg(client, reg, data) < 0) {
			pr_err("%s : error!\n", __func__);
			return -1;
		}
		data = smb136_read_reg(client, reg);
		pr_info("%s : => reg (0x%x) = 0x%x\n", __func__, reg, data);
	}

	chip->chg_mode = CHG_MODE_NONE;
	chip->chg_set_current = 0;
	chip->chg_icl = 0;
	
	return 0;
}

static int smb136_chg_set_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    const union power_supply_propval *val)
{
	struct smb136_chip *chip = container_of(psy,
				struct smb136_chip, psy_bat);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_NOW: /* step1) Set charging current */
		ret = smb136_set_charging_current(chip->client, val->intval);
		smb136_set_command_reg(chip->client);
		smb136_charger_function_conrol(chip->client);

		//smb136_adjust_float_voltage(chip->client, 4240); /* TEST */
		smb136_get_input_current_limit(chip->client);
		smb136_get_float_voltage(chip->client);
		//smb136_print_all_regs(chip->client);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL: /* step2) Set top-off current */
		ret = smb136_set_top_off(chip->client, val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW: /* step3) Notify Vcell Now */
		chip->batt_vcell = val->intval;
		pr_info("%s : vcell(%d)\n", __func__, chip->batt_vcell);
		/* TEST : at the high voltage threshold, set normal float voltage */
		/*
		if (chip->float_voltage != 4200 &&
			chip->batt_vcell >= 4200000) {
			smb136_adjust_float_voltage(chip->client, 4200);
		}
		*/
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_STATUS: /* step4) Enable/Disable charging */
		if (val->intval == POWER_SUPPLY_STATUS_CHARGING) {
			ret = smb136_enable_charging(chip->client);
		} else
			ret = smb136_disable_charging(chip->client);
		//smb136_print_all_regs(chip->client);
		break;
	case POWER_SUPPLY_PROP_OTG:	
		if (val->intval == POWER_SUPPLY_CAPACITY_OTG_ENABLE)
		{
			smb136_charger_function_conrol(chip->client);		
			ret = smb136_enable_otg(chip->client);
		}
		else
			ret = smb136_disable_otg(chip->client);
		break;
	case POWER_SUPPLY_PROP_CURRENT_ADJ:
		pr_info("%s : adjust charging current from %d to %d\n",
			__func__, chip->chg_set_current, val->intval);
		if (chip->chg_mode == CHG_MODE_AC) {
			ret = smb136_adjust_charging_current(chip->client, val->intval);
		} else {
			pr_info("%s : not AC mode, skip fast current adjusting\n",
														__func__);
		}
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static ssize_t sec_smb136_show_property(struct device *dev,
				    struct device_attribute *attr, char *buf);
static ssize_t sec_smb136_store_property(struct device *dev,
			     	struct device_attribute *attr,
			     	const char *buf, size_t count);

#define SEC_SMB136_ATTR(_name)				\
{											\
	.attr = { .name = #_name,				\
		  .mode = 0664,						\
		  .owner = THIS_MODULE },			\
	.show = sec_smb136_show_property,		\
	.store = sec_smb136_store_property,	\
}

static struct device_attribute sec_smb136_attrs[] = {
	SEC_SMB136_ATTR(smb_read_36h),
	SEC_SMB136_ATTR(smb_wr_icl),
	SEC_SMB136_ATTR(smb_wr_fast),
	SEC_SMB136_ATTR(smb_read_fv),
	SEC_SMB136_ATTR(smb_read_aicl),
};

enum {
	SMB_READ_36H = 0,
	SMB_WR_ICL,
	SMB_WR_FAST,
	SMB_READ_FV,
	SMB_READ_AICL,
};

static ssize_t sec_smb136_show_property(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct smb136_chip *chip = container_of(psy,
						  struct smb136_chip,
						  psy_bat);

	int i = 0;
	const ptrdiff_t off = attr - sec_smb136_attrs;
	int val, reg;
	u8 data = 0;

	switch (off) {
	case SMB_READ_36H:
		reg = SMB136_INTERRUPT_STATUS_E;
		val = smb136_read_reg(chip->client, reg);
		if (val >= 0) {
			data = (u8)val;
			//printk("%s : reg (0x%x) = 0x%x\n", __func__, reg, data);
			i += scnprintf(buf + i, PAGE_SIZE - i, "0x%x (bit6 : %d)\n",
					data, (data&0x40)>>6);
		} else {
			i = -EINVAL;
		}
		break;
	case SMB_WR_ICL:
		reg = SMB136_CURRENT_TERMINATION;
		val = smb136_read_reg(chip->client, reg);
		if (val >= 0) {
			data = (u8)val;
			//printk("%s : reg (0x%x) = 0x%x\n", __func__, reg, data);
			i += scnprintf(buf + i, PAGE_SIZE - i, "%d (0x%x)\n",
					chip->chg_icl, data);
		} else {
			i = -EINVAL;
		}
		break;
	case SMB_WR_FAST:
		reg = SMB136_INPUT_AND_CHARGE_CURRENTS;
		val = smb136_read_reg(chip->client, reg);
		if (val >= 0) {
			data = (u8)val;
			//printk("%s : reg (0x%x) = 0x%x\n", __func__, reg, data);
			i += scnprintf(buf + i, PAGE_SIZE - i, "%d (0x%x)\n",
					chip->chg_set_current, data);
		} else {
			i = -EINVAL;
		}
		break;
	case SMB_READ_FV:
		reg = SMB136_FLOAT_VOLTAGE;
		val = smb136_read_reg(chip->client, reg);
		if (val >= 0) {
			data = (u8)val;
			//printk("%s : reg (0x%x) = 0x%x\n", __func__, reg, data);
			i += scnprintf(buf + i, PAGE_SIZE - i, "0x%x (%dmV)\n",
					data, 3460+((data&0x7F)/2)*20);
		} else {
			i = -EINVAL;
		}
		break;
	case SMB_READ_AICL:
		val = smb136_get_AICL_status(chip->client);
		if (val >= 0) {
			i += scnprintf(buf + i, PAGE_SIZE - i, "%dmA (%d)\n",
					chip->aicl_current, chip->aicl_status);
		} else {
			i = -EINVAL;
		}
		break;
	default:
		i = -EINVAL;
	}

	return i;
}

static ssize_t sec_smb136_store_property(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct smb136_chip *chip = container_of(psy,
						  struct smb136_chip,
						  psy_bat);

	int x = 0;
	int ret = 0;
	const ptrdiff_t off = attr - sec_smb136_attrs;

	switch (off) {
	case SMB_WR_ICL:
		if (sscanf(buf, "%d\n", &x) == 1) {
			if (chip->chg_mode == CHG_MODE_AC) {
				ret = smb136_adjust_input_current_limit(chip->client, x);
			} else {
				pr_info("%s : not AC mode, skip icl adjusting\n", __func__);
				ret = count;
			}
		}
		break;
	case SMB_WR_FAST:
		if (sscanf(buf, "%d\n", &x) == 1) {
			if (chip->chg_mode == CHG_MODE_AC) {
				ret = smb136_adjust_charging_current(chip->client, x);
			} else {
				pr_info("%s : not AC mode, skip fast current adjusting\n",
					__func__);
				ret = count;
			}
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int smb136_create_attrs(struct device *dev)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(sec_smb136_attrs); i++) {
		rc = device_create_file(dev, &sec_smb136_attrs[i]);
		if (rc)
			goto smb136_attrs_failed;
	}
	goto succeed;

smb136_attrs_failed:
	while (i--)
		device_remove_file(dev, &sec_smb136_attrs[i]);
succeed:
	return rc;
}

static irqreturn_t smb136_int_work_func(int irq, void *smb_chip)
{
	struct smb136_chip *chip = smb_chip;
	int val, reg;
	//u8 intr_a = 0;
	//u8 intr_b = 0;
	//u8 intr_c = 0;
	u8 chg_status = 0;
	
	printk("%s\n", __func__);

	msleep(100);
	
	reg = SMB136_INTERRUPT_STATUS_E;
	val = smb136_read_reg(chip->client, reg);
	if (val >= 0) {
		chg_status = (u8)val;
		pr_info("%s : reg (0x%x) = 0x%x\n", __func__, reg, chg_status);
	}

	if(chip->pdata->chg_intr_trigger)
		chip->pdata->chg_intr_trigger((int)(chg_status&0x1));

	/* clear IRQ */
	reg = SMB136_CLEAR_IRQ;
	if (smb136_write_reg(chip->client, reg, 0xff) < 0) {
		pr_err("%s : irq clear error!\n", __func__);
	}
	
	return IRQ_HANDLED;
}

static int __devinit smb136_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct smb136_chip *chip;
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	pr_info("%s: SMB136 driver Loading! \n", __func__);

#if defined (CONFIG_KOR_MODEL_SHV_E160S)
	if (get_hw_rev() >= 0x4) {
		pr_info("%s: SMB136 driver Loading SKIP!!!\n", __func__);
		return 0;
	}
#endif

#if defined (CONFIG_USA_MODEL_SGH_I717)
	if (get_hw_rev() != 0x1) {
		pr_info("%s: SMB136 driver Loading SKIP!!!\n", __func__);
		return 0;
	}
#endif

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	chip->pdata = client->dev.platform_data;
	if (!chip->pdata) {
		pr_err("%s: no charger platform data\n",	__func__);
		goto err_kfree;
	}

	i2c_set_clientdata(client, chip);

	chip->pdata->hw_init(); /* important */

	chip->chg_mode = CHG_MODE_NONE;
	chip->chg_set_current = 0;
	chip->chg_icl = 0;
	chip->lpm_chg_mode = 0;
	chip->float_voltage = 0;
	if (is_lpcharging_state()) {
		chip->lpm_chg_mode = 1;
		printk("%s : is lpm charging mode (%d)\n",
			__func__, chip->lpm_chg_mode);
	}

	mutex_init(&chip->mutex);
	
	chip->psy_bat.name = "sec-charger",
	chip->psy_bat.type = POWER_SUPPLY_TYPE_BATTERY,
	chip->psy_bat.properties = smb136_battery_props,
	chip->psy_bat.num_properties = ARRAY_SIZE(smb136_battery_props),
	chip->psy_bat.get_property = smb136_chg_get_property,
	chip->psy_bat.set_property = smb136_chg_set_property,
	ret = power_supply_register(&client->dev, &chip->psy_bat);
	if (ret) {
		pr_err("Failed to register power supply psy_bat\n");
		goto err_mutex_destroy;
	}

	ret = request_threaded_irq(chip->client->irq, NULL, smb136_int_work_func,
				   IRQF_TRIGGER_FALLING, "smb136", chip);
	if (ret) {
		pr_err("%s : Failed to request smb136 charger irq\n", __func__);
		goto err_request_irq;
	}

	ret = enable_irq_wake(chip->client->irq);
	if (ret) {
		pr_err("%s : Failed to enable smb136 charger irq wake\n", __func__);
		goto err_irq_wake;
	}
	
	//smb136_charger_function_conrol(client);
	smb136_print_all_regs(client);

	/* create smb136 attributes */
	smb136_create_attrs(chip->psy_bat.dev);

	return 0;

err_irq_wake:
	free_irq(chip->client->irq, NULL);
err_request_irq:
	power_supply_unregister(&chip->psy_bat);
err_mutex_destroy:
	mutex_destroy(&chip->mutex);
err_kfree:
	kfree(chip);
	return ret;
}

static int __devexit smb136_remove(struct i2c_client *client)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);

	power_supply_unregister(&chip->psy_bat);
	mutex_destroy(&chip->mutex);
	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM
static int smb136_suspend(struct i2c_client *client,
		pm_message_t state)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);

	return 0;
}

static int smb136_resume(struct i2c_client *client)
{
	struct smb136_chip *chip = i2c_get_clientdata(client);

	return 0;
}
#else
#define smb136_suspend NULL
#define smb136_resume NULL
#endif /* CONFIG_PM */

static const struct i2c_device_id smb136_id[] = {
	{ "smb136", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, smb136_id);

static struct i2c_driver smb136_i2c_driver = {
	.driver	= {
		.name	= "smb136",
	},
	.probe		= smb136_probe,
	.remove		= __devexit_p(smb136_remove),
	.suspend	= smb136_suspend,
	.resume		= smb136_resume,
	.id_table	= smb136_id,
};

static int __init smb136_init(void)
{
	return i2c_add_driver(&smb136_i2c_driver);
}
module_init(smb136_init);

static void __exit smb136_exit(void)
{
	i2c_del_driver(&smb136_i2c_driver);
}
module_exit(smb136_exit);


MODULE_DESCRIPTION("SMB136 charger control driver");
MODULE_AUTHOR("<jongmyeong.ko@samsung.com>");
MODULE_LICENSE("GPL");


