/*
 * Gadget Driver for Android
 *
 * Copyright (C) 2008 Google, Inc.
 * Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
 * Author: Mike Lockwood <lockwood@android.com>
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

/* #define DEBUG */
/* #define VERBOSE_DEBUG */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/debugfs.h>

#include <linux/usb/android_composite.h>
#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>

#include "gadget_chips.h"

/*
 * Kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */
#include "usbstring.c"
#include "config.c"
#include "epautoconf.c"
#include "composite.c"

#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
#include <mach/devices-lte.h>
#endif

MODULE_AUTHOR("Mike Lockwood");
MODULE_DESCRIPTION("Android Composite USB Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");

static const char longname[] = "Gadget Android";

/* Default vendor and product IDs, overridden by platform data */
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
#  define VENDOR_ID		0x04e8	/* SAMSUNG */
#  define PRODUCT_ID		SAMSUNG_DEBUG_PRODUCT_ID
#else /* Original VID & PID */
#  define VENDOR_ID		0x18D1
#  define PRODUCT_ID		0x0001
#endif /* CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE */

struct android_dev {
	struct usb_composite_dev *cdev;
	struct usb_configuration *config;
	int num_products;
	struct android_usb_product *products;
	int num_functions;
	char **functions;

	int vendor_id;
	int product_id;
	int version;
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
	int current_usb_mode;   /* soonyong.cho : save usb mode except tethering and askon mode. */
	int requested_usb_mode; /*                requested usb mode from app included tethering and askon */
	int debugging_usb_mode; /*		  debugging usb mode */
#endif	
};

static struct android_dev *_android_dev;

#define MAX_STR_LEN		16
/* string IDs are assigned dynamically */

#define STRING_MANUFACTURER_IDX		0
#define STRING_PRODUCT_IDX		1
#define STRING_SERIAL_IDX		2

char serial_number[MAX_STR_LEN];
/* String Table */
static struct usb_string strings_dev[] = {
	/* These dummy values should be overridden by platform data */
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
	[STRING_MANUFACTURER_IDX].s = "SAMSUNG",
	[STRING_PRODUCT_IDX].s = "SAMSUNG_Android",
	[STRING_SERIAL_IDX].s = "0123456789ABCDEF",
#else /* Original */
	[STRING_MANUFACTURER_IDX].s = "Android",
	[STRING_PRODUCT_IDX].s = "Android",
	[STRING_SERIAL_IDX].s = "0123456789ABCDEF",
#endif /* CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE */
	{  }			/* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};

static struct usb_device_descriptor device_desc = {
	.bLength              = sizeof(device_desc),
	.bDescriptorType      = USB_DT_DEVICE,
	.bcdUSB               = __constant_cpu_to_le16(0x0200),
	.bDeviceClass         = USB_CLASS_PER_INTERFACE,
	.idVendor             = __constant_cpu_to_le16(VENDOR_ID),
	.idProduct            = __constant_cpu_to_le16(PRODUCT_ID),
	.bcdDevice            = __constant_cpu_to_le16(0xffff),
	.bNumConfigurations   = 1,
};

static struct usb_otg_descriptor otg_descriptor = {
	.bLength =		sizeof otg_descriptor,
	.bDescriptorType =	USB_DT_OTG,
	.bmAttributes =		USB_OTG_SRP | USB_OTG_HNP,
	.bcdOTG               = __constant_cpu_to_le16(0x0200),
};

static const struct usb_descriptor_header *otg_desc[] = {
	(struct usb_descriptor_header *) &otg_descriptor,
	NULL,
};

static struct list_head _functions = LIST_HEAD_INIT(_functions);
static int _registered_function_count = 0;

#ifndef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
static void android_set_default_product(int product_id);
#endif

void android_usb_set_connected(int connected)
{
	if (_android_dev && _android_dev->cdev && _android_dev->cdev->gadget) {
		if (connected)
			usb_gadget_connect(_android_dev->cdev->gadget);
		else
			usb_gadget_disconnect(_android_dev->cdev->gadget);
	}
}

#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
static void samsung_enable_function(int mode);
#endif

static struct android_usb_function *get_function(const char *name)
{
	struct android_usb_function	*f;
	list_for_each_entry(f, &_functions, list) {
		if (!strcmp(name, f->name))
			return f;
	}
	return 0;
}

static void bind_functions(struct android_dev *dev)
{
	struct android_usb_function	*f;
	char **functions = dev->functions;
	int i;

	for (i = 0; i < dev->num_functions; i++) {
		char *name = *functions++;
		f = get_function(name);
		if (f)
			f->bind_config(dev->config);
		else
			pr_err("%s: function %s not found\n", __func__, name);
	}

	/*
	 * set_alt(), or next config->bind(), sets up
	 * ep->driver_data as needed.
	 */
	usb_ep_autoconfig_reset(dev->cdev->gadget);
}

static int __ref android_bind_config(struct usb_configuration *c)
{
	struct android_dev *dev = _android_dev;

	pr_debug("android_bind_config\n");
	dev->config = c;

	/* bind our functions if they have all registered */
	if (_registered_function_count == dev->num_functions)
		bind_functions(dev);

	return 0;
}

#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
/* soonyong.cho : It is default config string. It'll be changed to real config string when last function driver is registered. */
#  define       ANDROID_DEFAULT_CONFIG_STRING "Samsung Android Shared Config"	/* android default config string */
#else /* original */
#  define	ANDROID_DEBUG_CONFIG_STRING "UMS + ADB (Debugging mode)"
#  define	ANDROID_NO_DEBUG_CONFIG_STRING "UMS Only (Not debugging mode)"
#endif /* CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE */


static int android_setup_config(struct usb_configuration *c,
		const struct usb_ctrlrequest *ctrl);

static struct usb_configuration android_config_driver = {
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
	.label		= ANDROID_DEFAULT_CONFIG_STRING,
#else /* original */
	.label		= ANDROID_NO_DEBUG_CONFIG_STRING,
#endif /* CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE */
	.bind		= android_bind_config,
	.setup		= android_setup_config,
	.bConfigurationValue = 1,
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
	.bMaxPower	= 0x30, /* 96ma */
#else /* original */
	.bMaxPower	= 0xFA, /* 500ma */
#endif /* CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE */
};

static int android_setup_config(struct usb_configuration *c,
		const struct usb_ctrlrequest *ctrl)
{
	int i;
	int ret = -EOPNOTSUPP;
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
	/* Do not call same function config when function has many interface.
	 * If another function driver has different config function, It needs calling.
	 */
	char temp_name[128]={0,};
#endif

	for (i = 0; i < android_config_driver.next_interface_id; i++) {
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE /*find same interface for to skip calling*/
		if (!android_config_driver.interface[i]->disabled && android_config_driver.interface[i]->setup) {
			if (!strcmp(temp_name, android_config_driver.interface[i]->name)) {
				continue;
			}
			else
				strcpy(temp_name,android_config_driver.interface[i]->name);
#else
		if (android_config_driver.interface[i]->setup) {
#endif			
			ret = android_config_driver.interface[i]->setup(
				android_config_driver.interface[i], ctrl);
			if (ret >= 0)
				return ret;
		}
	}
	return ret;
}

static int product_has_function(struct android_usb_product *p,
		struct usb_function *f)
{
	char **functions = p->functions;
	int count = p->num_functions;
	const char *name = f->name;
	int i;

	for (i = 0; i < count; i++) {
		if (!strcmp(name, *functions++))
			return 1;
	}
	return 0;
}

static int product_matches_functions(struct android_usb_product *p)
{
	struct usb_function		*f;
	list_for_each_entry(f, &android_config_driver.functions, list) {
		if (product_has_function(p, f) == !!f->disabled)
			return 0;
	}
	return 1;
}

static int get_vendor_id(struct android_dev *dev)
{
	struct android_usb_product *p = dev->products;
	int count = dev->num_products;
	int i;

	if (p) {
		for (i = 0; i < count; i++, p++) {
			if (p->vendor_id && product_matches_functions(p))
				return p->vendor_id;
		}
	}
	/* use default vendor ID */
	return dev->vendor_id;
}

static int get_product_id(struct android_dev *dev)
{
	struct android_usb_product *p = dev->products;
	int count = dev->num_products;
	int i;

	if (p) {
		for (i = 0; i < count; i++, p++) {
			if (product_matches_functions(p))
				return p->product_id;
		}
	}
	/* use default product ID */
	return dev->product_id;
}

static int __devinit android_bind(struct usb_composite_dev *cdev)
{
	struct android_dev *dev = _android_dev;
	struct usb_gadget	*gadget = cdev->gadget;
	int			gcnum, id, ret;

	pr_debug("android_bind\n");

	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */
	id = usb_string_id(cdev);
	if (id < 0)
		return id;
	strings_dev[STRING_MANUFACTURER_IDX].id = id;
	device_desc.iManufacturer = id;

	id = usb_string_id(cdev);
	if (id < 0)
		return id;
	strings_dev[STRING_PRODUCT_IDX].id = id;
	device_desc.iProduct = id;

	id = usb_string_id(cdev);
	if (id < 0)
		return id;
	strings_dev[STRING_SERIAL_IDX].id = id;
	device_desc.iSerialNumber = id;

	if (gadget_is_otg(cdev->gadget))
		android_config_driver.descriptors = otg_desc;

	if (!usb_gadget_set_selfpowered(gadget))
		android_config_driver.bmAttributes |= USB_CONFIG_ATT_SELFPOWER;
	
#ifndef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
	if (gadget->ops->wakeup)
		android_config_driver.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
#endif

	/* register our configuration */
	ret = usb_add_config(cdev, &android_config_driver);
	if (ret) {
		pr_err("%s: usb_add_config failed\n", __func__);
		return ret;
	}

	gcnum = usb_gadget_controller_number(gadget);
	if (gcnum >= 0)
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
		/* Samsung KIES needs fixed bcdDevice number */
		device_desc.bcdDevice = cpu_to_le16(0x0400);
#else
		device_desc.bcdDevice = cpu_to_le16(0x0200 + gcnum);
#endif
	else {
		/* gadget zero is so simple (for now, no altsettings) that
		 * it SHOULD NOT have problems with bulk-capable hardware.
		 * so just warn about unrcognized controllers -- don't panic.
		 *
		 * things like configuration and altsetting numbering
		 * can need hardware-specific attention though.
		 */
		pr_warning("%s: controller '%s' not recognized\n",
			longname, gadget->name);
		device_desc.bcdDevice = __constant_cpu_to_le16(0x9999);
	}

	usb_gadget_set_selfpowered(gadget);
	dev->cdev = cdev;
	device_desc.idVendor = __constant_cpu_to_le16(get_vendor_id(dev));
	device_desc.idProduct = __constant_cpu_to_le16(get_product_id(dev));
	cdev->desc.idProduct = device_desc.idProduct;

	return 0;
}

static struct usb_composite_driver android_usb_driver = {
	.name		= "android_usb",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.bind		= android_bind,
	.enable_function = android_enable_function,
};

static bool is_func_supported(struct android_usb_function *f)
{
	char **functions = _android_dev->functions;
	int count = _android_dev->num_functions;
	const char *name = f->name;
	int i;

	for (i = 0; i < count; i++) {
		if (!strcmp(*functions++, name))
			return true;
	}
	return false;
}

void android_register_function(struct android_usb_function *f)
{
	struct android_dev *dev = _android_dev;

	pr_debug("%s: %s\n", __func__, f->name);
	pr_info("%s: %s  %d, %d\n", __func__, f->name,_registered_function_count,dev->num_functions);

	if (!is_func_supported(f))
		return;

	list_add_tail(&f->list, &_functions);
	_registered_function_count++;

	/* bind our functions if they have all registered
	 * and the main driver has bound.
	 */
	if (dev->config && _registered_function_count == dev->num_functions) {
		bind_functions(dev);
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
/*Change usb mode and enable usb ip when device register last function driver */
		samsung_enable_function(USBSTATUS_SAMSUNG_KIES);
#else
		android_set_default_product(dev->product_id);
#endif
	}
}

#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
/*
 * Description  : Set enable functions
 * Parameters   : char** functions (product function list), int num_f (number of product functions)
 * Return value : Count of enable functions
 */
static int set_enable_functions(char **functions, int num_f)
{
	int i;
	struct usb_function		*func;
	int find = false;
	int count = 0;
	char **head_functions = functions;

	list_for_each_entry(func, &android_config_driver.functions, list) {

		pr_info("func->name=%s\n", func->name);

		functions = head_functions;
		for(i = 0; i < num_f; i++) {
			/* enable */
		pr_info("%s: %s - %s %d, %d\n", __func__, func->name,*functions,i,num_f);
			if (!strcmp(func->name, *functions++)) {
				usb_function_set_enabled(func, 1);
				find = true;
				++count;
				pr_info("enable %s\n", func->name);
				break;
			}
		}
		/* disable */
		if(find == false) {
			usb_function_set_enabled(func, 0);		}
		else /* finded */
			find = false;
	}
	return count;
}

/*
 * Description  : Set product using function as set_enable_function
 * Parameters   : struct android_dev *dev (Refer dev->products), __u16 mode (usb mode)
 * Return Value : -1 (fail to find product), positive value (number of functions)
 */
static int set_product(struct android_dev *dev, __u16 mode)
{
	struct android_usb_product *p = dev->products;
	int count = dev->num_products;
	int i, ret;

	dev->requested_usb_mode = mode; /* Save usb mode always even though it will be failed */

	if (p) {
		for (i = 0; i < count; i++, p++) {
			if(p->mode == mode) {
				/* It is for setting dynamic interface in composite.c */
				dev->cdev->product_num		= p->num_functions;
				dev->cdev->products		= p;

				dev->cdev->desc.bDeviceClass	 = p->bDeviceClass;
				dev->cdev->desc.bDeviceSubClass	 = p->bDeviceSubClass;
				dev->cdev->desc.bDeviceProtocol	 = p->bDeviceProtocol;
				android_config_driver.label	 = p->s;

				pr_info("%s:  %d, %d\n", __func__,  p->num_functions, mode);

				ret = set_enable_functions(p->functions, p->num_functions);
				pr_info("Change Device Descriptor : DeviceClass(0x%x),SubClass(0x%x),Protocol(0x%x)\n",
					p->bDeviceClass, p->bDeviceSubClass, p->bDeviceProtocol);
				pr_info("Change Label : [%d]%s\n", i, p->s);
				if(ret == 0)
					pr_info("Can't find functions(mode=0x%x)\n", mode);
				else
					pr_info("set function num=%d\n", ret);
				return ret;

			}
		}
	}

	return -1;
}

/*
 * Description  : Enable functions for samsung composite driver
 * Parameters   : struct usb_function *f (It depends on function's sysfs), int enable (1:enable, 0:disable)
 * Return value : void
 *
 */
int android_enable_function(struct usb_function *f, int enable)
{
	struct android_dev *dev = _android_dev;
	int product_id = 0;
	int ret = -1;

	if (dev->requested_usb_mode == USBSTATUS_RMNET) return;

	if(enable) {
		if (!strcmp(f->name, "acm")) {
			ret = set_product(dev, USBSTATUS_SAMSUNG_KIES);
			if (ret != -1)
				dev->current_usb_mode = USBSTATUS_SAMSUNG_KIES;
		}
		if (!strcmp(f->name, "adb")) {
			ret = set_product(dev, USBSTATUS_ADB);
			if (ret != -1)
				dev->debugging_usb_mode = 1; /* save debugging status */
		}
		if (!strcmp(f->name, "mtp")) {
			ret = set_product(dev, USBSTATUS_MTPONLY);
			if (ret != -1)
				dev->current_usb_mode = USBSTATUS_MTPONLY;
		}
		if (!strcmp(f->name, "rndis")) {
			ret = set_product(dev, USBSTATUS_VTP);
		}
		if (!strcmp(f->name, "usb_mass_storage")) {
			ret = set_product(dev, USBSTATUS_UMS);
			if (ret != -1)
				dev->current_usb_mode = USBSTATUS_UMS;
		}
		if (!strcmp(f->name, "rmnet_sdio")) {
			ret = set_product(dev, USBSTATUS_RMNET);
			if (ret != -1)
				dev->current_usb_mode = USBSTATUS_RMNET;
		}
	}
	else { /* for disable : Return old mode. If Non-GED model changes policy, below code has to be modified. */
		if (!strcmp(f->name, "rndis") && dev->debugging_usb_mode)
			ret = set_product(dev, USBSTATUS_ADB);
		else
			ret = set_product(dev, dev->current_usb_mode);

		if(!strcmp(f->name, "adb")) 
			dev->debugging_usb_mode = 0;
	} /* if(enable) */

	if(ret == -1) {
		//return ;
		return -EINVAL;
	}


	product_id = get_product_id(dev);
	device_desc.idProduct = __constant_cpu_to_le16(product_id);

	if (dev->cdev)
		dev->cdev->desc.idProduct = device_desc.idProduct;

	/* force reenumeration */
	usb_composite_force_reset(dev->cdev);
	
	return 0;
}

#else /* original code */

/**
 * android_set_function_mask() - enables functions based on selected pid.
 * @up: selected product id pointer
 *
 * This function enables functions related with selected product id.
 */
static void android_set_function_mask(struct android_usb_product *up)
{
	int index, found = 0;
	struct usb_function *func;

	list_for_each_entry(func, &android_config_driver.functions, list) {
		/* adb function enable/disable handled separetely */
		if (!strcmp(func->name, "adb") && !func->disabled)
			continue;

		for (index = 0; index < up->num_functions; index++) {
			if (!strcmp(up->functions[index], func->name)) {
				found = 1;
				break;
			}
		}

		if (found) { /* func is part of product. */
			/* if func is disabled, enable the same. */
			if (func->disabled)
				usb_function_set_enabled(func, 1);
			found = 0;
		} else { /* func is not part if product. */
			/* if func is enabled, disable the same. */
			if (!func->disabled)
				usb_function_set_enabled(func, 0);
		}
	}
}

/**
 * android_set_defaut_product() - selects default product id and enables
 * required functions
 * @product_id: default product id
 *
 * This function selects default product id using pdata information and
 * enables functions for same.
*/
static void android_set_default_product(int pid)
{
	struct android_dev *dev = _android_dev;
	struct android_usb_product *up = dev->products;
	int index;

	for (index = 0; index < dev->num_products; index++, up++) {
		if (pid == up->product_id)
			break;
	}
	android_set_function_mask(up);
}

/**
 * android_config_functions() - selects product id based on function need
 * to be enabled / disabled.
 * @f: usb function
 * @enable : function needs to be enable or disable
 *
 * This function selects first product id having required function.
 * RNDIS/MTP function enable/disable uses this.
*/
#ifdef CONFIG_USB_ANDROID_RNDIS
static void android_config_functions(struct usb_function *f, int enable)
{
	struct android_dev *dev = _android_dev;
	struct android_usb_product *up = dev->products;
	int index;

	/* Searches for product id having function */
	if (enable) {
		for (index = 0; index < dev->num_products; index++, up++) {
			if (product_has_function(up, f))
				break;
		}
		android_set_function_mask(up);
	} else
		android_set_default_product(dev->product_id);
}
#endif

void update_dev_desc(struct android_dev *dev)
{
	struct usb_function *f;
	struct usb_function *last_enabled_f = NULL;
	int num_enabled = 0;
	int has_iad = 0;

	dev->cdev->desc.bDeviceClass = USB_CLASS_PER_INTERFACE;
	dev->cdev->desc.bDeviceSubClass = 0x00;
	dev->cdev->desc.bDeviceProtocol = 0x00;

	list_for_each_entry(f, &android_config_driver.functions, list) {
		if (!f->disabled) {
			num_enabled++;
			last_enabled_f = f;
			if (f->descriptors[0]->bDescriptorType ==
					USB_DT_INTERFACE_ASSOCIATION)
				has_iad = 1;
		}
		if (num_enabled > 1 && has_iad) {
			dev->cdev->desc.bDeviceClass = USB_CLASS_MISC;
			dev->cdev->desc.bDeviceSubClass = 0x02;
			dev->cdev->desc.bDeviceProtocol = 0x01;
			break;
		}
	}

	if (num_enabled == 1) {
#ifdef CONFIG_USB_ANDROID_RNDIS
		if (!strcmp(last_enabled_f->name, "rndis")) {
#ifdef CONFIG_USB_ANDROID_RNDIS_WCEIS
			dev->cdev->desc.bDeviceClass =
					USB_CLASS_WIRELESS_CONTROLLER;
#else
			dev->cdev->desc.bDeviceClass = USB_CLASS_COMM;
#endif
		}
#endif
	}
}


static char *sysfs_allowed[] = {
	"rndis",
	"mtp",
	"adb",
};

static int is_sysfschange_allowed(struct usb_function *f)
{
	char **functions = sysfs_allowed;
	int count = ARRAY_SIZE(sysfs_allowed);
	int i;

	for (i = 0; i < count; i++) {
		if (!strncmp(f->name, functions[i], 32))
			return 1;
	}
	return 0;
}

int android_enable_function(struct usb_function *f, int enable)
{
	struct android_dev *dev = _android_dev;
	int disable = !enable;

	if (!is_sysfschange_allowed(f))
		return -EINVAL;
	if (!!f->disabled != disable) {
		usb_function_set_enabled(f, !disable);

#ifdef CONFIG_USB_ANDROID_RNDIS
		if (!strcmp(f->name, "rndis")) {

			/* We need to specify the COMM class in the device descriptor
			 * if we are using RNDIS.
			 */
			if (enable) {
#ifdef CONFIG_USB_ANDROID_RNDIS_WCEIS
				dev->cdev->desc.bDeviceClass = USB_CLASS_MISC;
				dev->cdev->desc.bDeviceSubClass      = 0x02;
				dev->cdev->desc.bDeviceProtocol      = 0x01;
#else
				dev->cdev->desc.bDeviceClass = USB_CLASS_COMM;
#endif
			} else {
				dev->cdev->desc.bDeviceClass = USB_CLASS_PER_INTERFACE;
				dev->cdev->desc.bDeviceSubClass      = 0;
				dev->cdev->desc.bDeviceProtocol      = 0;
			}

			android_config_functions(f, enable);
		}
#endif

#ifdef CONFIG_USB_ANDROID_MTP
		if (!strcmp(f->name, "mtp"))
			android_config_functions(f, enable);
#endif

		device_desc.idVendor = __constant_cpu_to_le16(get_vendor_id(dev));
		device_desc.idProduct = __constant_cpu_to_le16(get_product_id(dev));

		if (dev->cdev)
			dev->cdev->desc.idProduct = device_desc.idProduct;
		usb_composite_force_reset(dev->cdev);
	}
	return 0;
}
#endif /* CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE */

#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
static void samsung_enable_function(int mode)
{
	struct android_dev *dev = _android_dev;
	int product_id = 0;
	int ret = -1;

	switch(mode) {
		case USBSTATUS_UMS:
			ret = set_product(dev, USBSTATUS_UMS);
			break;
		case USBSTATUS_SAMSUNG_KIES:
			ret = set_product(dev, USBSTATUS_SAMSUNG_KIES);
			break;
		case USBSTATUS_MTPONLY:
			ret = set_product(dev, USBSTATUS_MTPONLY);
			break;
		case USBSTATUS_ADB:
			ret = set_product(dev, USBSTATUS_ADB);
			break;
#ifdef CONFIG_USB_ANDROID_RNDIS			
		case USBSTATUS_VTP: /* do not save usb mode */
			ret = set_product(dev, USBSTATUS_VTP);
			break;
#endif			
		case USBSTATUS_RMNET: /* do not save usb mode */
			ret = set_product(dev, USBSTATUS_RMNET);
			break;

		case USBSTATUS_ASKON: /* do not save usb mode */
			return;
	}

	if(ret == -1) {
		return ;
	}
	else if((mode != USBSTATUS_VTP) && (mode != USBSTATUS_ASKON)) {
		dev->current_usb_mode = mode;
	}

	product_id = get_product_id(dev);
	device_desc.idProduct = __constant_cpu_to_le16(product_id);

	if (dev->cdev)
		dev->cdev->desc.idProduct = device_desc.idProduct;

	/* force reenumeration */
	usb_composite_force_reset(dev->cdev);
}
#endif /* CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE */


#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE

 //Path (/sys/devices/platform/android_usb/tethering)
static ssize_t tethering_switch_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct android_dev *a_dev = _android_dev;
	int value = -1;

	if(a_dev->cdev) {
		if (a_dev->requested_usb_mode == USBSTATUS_VTP )
			value = 1;
		else
			value = 0;
	}
	return sprintf(buf, "%d\n", value);
}

 //Path (/sys/devices/platform/android_usb/tethering)
static ssize_t tethering_switch_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int value;
	struct android_dev *a_dev = _android_dev;
	sscanf(buf, "%d", &value);

	if (value) {
		if(a_dev->cdev) {
			if(a_dev->cdev->gadget->speed == USB_SPEED_UNKNOWN)
				a_dev->cdev->mute_switch = 1;
		}
		samsung_enable_function(USBSTATUS_VTP);
		if(a_dev->cdev)
			if(a_dev->cdev->gadget)
				usb_gadget_vbus_connect(a_dev->cdev->gadget);
	}
	else {
		if(a_dev->debugging_usb_mode)
			samsung_enable_function(USBSTATUS_ADB);
		else
			samsung_enable_function(a_dev->current_usb_mode);
	}

	return size;
}

static DEVICE_ATTR(tethering, 0664, tethering_switch_show, tethering_switch_store);

//Path (/sys/devices/platform/android_usb/UsbMenuSel)
static ssize_t UsbMenuSel_switch_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct android_dev *a_dev = _android_dev;
	int value = -1;

	if(a_dev->cdev) {
		switch(a_dev->requested_usb_mode) {
			case USBSTATUS_UMS:
				return sprintf(buf, "[UsbMenuSel] UMS\n");
			case USBSTATUS_SAMSUNG_KIES:
				return sprintf(buf, "[UsbMenuSel] ACM_MTP\n");
			case USBSTATUS_MTPONLY:
				return sprintf(buf, "[UsbMenuSel] MTP\n");
			case USBSTATUS_ASKON:
				return sprintf(buf, "[UsbMenuSel] ASK\n");
			case USBSTATUS_VTP:
				return sprintf(buf, "[UsbMenuSel] TETHERING\n");
			case USBSTATUS_ADB:
				return sprintf(buf, "[UsbMenuSel] ACM_ADB_UMS\n");
			case USBSTATUS_RMNET:
				return sprintf(buf, "[UsbMenuSel] RMNET\n");				
		}
	}
	return sprintf(buf, "%d\n", value);
}

 //Path (/sys/devices/platform/android_usb/UsbMenuSel)
ssize_t UsbMenuSel_switch_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int value;
	struct android_dev *andev = _android_dev;
	sscanf(buf, "%d", &value);
	printk("%s value %d %c",__func__,value, buf);
	switch(value) {
		case 0:
			samsung_enable_function(USBSTATUS_SAMSUNG_KIES);
			break;
		case 1:
			samsung_enable_function(USBSTATUS_MTPONLY);
			break;
		case 2:
			samsung_enable_function(USBSTATUS_UMS);
			break;
		case 3:
			samsung_enable_function(USBSTATUS_ASKON);
			break;
		case 4:
			samsung_enable_function(USBSTATUS_RMNET);
			break;
		case 5:
			samsung_enable_function(USBSTATUS_ADB);
			break;
		default:
			break;
	}
	return size;
}

static DEVICE_ATTR(UsbMenuSel, 0664, UsbMenuSel_switch_show, UsbMenuSel_switch_store);
#endif /* CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE */

#ifdef CONFIG_DEBUG_FS
static int android_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t android_debugfs_serialno_write(struct file *file, const char
				__user *buf,	size_t count, loff_t *ppos)
{
	char str_buf[MAX_STR_LEN];

	if (count > MAX_STR_LEN)
		return -EFAULT;

	if (copy_from_user(str_buf, buf, count))
		return -EFAULT;

	memcpy(serial_number, str_buf, count);

	if (serial_number[count - 1] == '\n')
		serial_number[count - 1] = '\0';

	strings_dev[STRING_SERIAL_IDX].s = serial_number;

	return count;
}
const struct file_operations android_fops = {
	.open	= android_debugfs_open,
	.write	= android_debugfs_serialno_write,
};

struct dentry *android_debug_root;
struct dentry *android_debug_serialno;

static int android_debugfs_init(struct android_dev *dev)
{
	android_debug_root = debugfs_create_dir("android", NULL);
	if (!android_debug_root)
		return -ENOENT;

	android_debug_serialno = debugfs_create_file("serial_number", S_IWUSR |S_IWGRP,
						android_debug_root, dev,
						&android_fops);
	if (!android_debug_serialno) {
		debugfs_remove(android_debug_root);
		android_debug_root = NULL;
		return -ENOENT;
	}
	return 0;
}

static void android_debugfs_cleanup(void)
{
       debugfs_remove(android_debug_serialno);
       debugfs_remove(android_debug_root);
}
#endif
static int __init android_probe(struct platform_device *pdev)
{
	struct android_usb_platform_data *pdata = pdev->dev.platform_data;
	struct android_dev *dev = _android_dev;
	int result;

	dev_dbg(&pdev->dev, "%s: pdata: %p\n", __func__, pdata);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	result = pm_runtime_get(&pdev->dev);
	if (result < 0) {
		dev_err(&pdev->dev,
			"Runtime PM: Unable to wake up the device, rc = %d\n",
			result);
		return result;
	}

	if (pdata) {
		dev->products = pdata->products;
		dev->num_products = pdata->num_products;
		dev->functions = pdata->functions;
		dev->num_functions = pdata->num_functions;
		if (pdata->vendor_id) {
			dev->vendor_id = pdata->vendor_id;
			device_desc.idVendor =
				__constant_cpu_to_le16(pdata->vendor_id);
		}
		if (pdata->product_id) {
			dev->product_id = pdata->product_id;
			device_desc.idProduct =
				__constant_cpu_to_le16(pdata->product_id);
		}
		if (pdata->version)
			dev->version = pdata->version;

		if (pdata->product_name)
			strings_dev[STRING_PRODUCT_IDX].s = pdata->product_name;
		if (pdata->manufacturer_name)
			strings_dev[STRING_MANUFACTURER_IDX].s =
					pdata->manufacturer_name;
		if (pdata->serial_number)
			strings_dev[STRING_SERIAL_IDX].s = pdata->serial_number;
	}

#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
/*             Create attribute of sysfs as '/sys/devices/platform/android_usb/UsbMenuSel'
 *               It is for USB menu selection.
 * 		  Application for USB Setting made by SAMSUNG uses property that uses below sysfs.
 */
		if (device_create_file(&pdev->dev, &dev_attr_UsbMenuSel) < 0)
		{
		}

		if (device_create_file(&pdev->dev, &dev_attr_tethering) < 0)
		{
		}

#endif

	
#ifdef CONFIG_DEBUG_FS
	result = android_debugfs_init(dev);
	if (result)
		pr_debug("%s: android_debugfs_init failed\n", __func__);
#endif
	return usb_composite_register(&android_usb_driver);
}

static int andr_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "pm_runtime: suspending...\n");
	return 0;
}

static int andr_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "pm_runtime: resuming...\n");
	return 0;
}

static struct dev_pm_ops andr_dev_pm_ops = {
	.runtime_suspend = andr_runtime_suspend,
	.runtime_resume = andr_runtime_resume,
};

static struct platform_driver android_platform_driver = {
	.driver = { .name = "android_usb", .pm = &andr_dev_pm_ops},
};

static int __init init(void)
{
	struct android_dev *dev;

	pr_debug("android init\n");

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	/* set default values, which should be overridden by platform data */
	dev->product_id = PRODUCT_ID;
	_android_dev = dev;

	return platform_driver_probe(&android_platform_driver, android_probe);
}
module_init(init);

static void __exit cleanup(void)
{
#ifdef CONFIG_DEBUG_FS
	android_debugfs_cleanup();
#endif
	usb_composite_unregister(&android_usb_driver);
	platform_driver_unregister(&android_platform_driver);
	kfree(_android_dev);
	_android_dev = NULL;
}
module_exit(cleanup);
