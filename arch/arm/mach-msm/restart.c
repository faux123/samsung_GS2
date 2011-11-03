/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/mfd/pmic8058.h>
#include <linux/mfd/pmic8901.h>

#include <mach/msm_iomap.h>
#include <mach/restart.h>
#include <mach/scm-io.h>
#include <asm/mach-types.h>
#include <mach/sec_debug.h>  // onlyjazz
#include <linux/notifier.h> // klaatu
#include <linux/ftrace.h> // klaatu

#define TCSR_WDT_CFG 0x30

#define WDT0_RST       (MSM_TMR0_BASE + 0x38)
#define WDT0_EN        (MSM_TMR0_BASE + 0x40)
#define WDT0_BARK_TIME (MSM_TMR0_BASE + 0x4C)
#define WDT0_BITE_TIME (MSM_TMR0_BASE + 0x5C)

#define PSHOLD_CTL_SU (MSM_TLMM_BASE + 0x820)

#define RESTART_REASON_ADDR 0x2A05F65C

#define RESTART_LPM_BOOT_MODE		0x77665506
#define RESTART_ARM11FOTA_MODE  	0x77665503
#define RESTART_RECOVERY_MODE   	0x77665502
#define RESTART_OTHERBOOT_MODE		0x77665501
#define RESTART_FASTBOOT_MODE   	0x77665500
#ifdef CONFIG_SEC_DEBUG
#define RESTART_SECDEBUG_MODE   	0x776655EE
#endif
// NOT USE 0x776655FF~0x77665608 command
#define RESTART_HOMEDOWN_MODE       	0x776655FF
#define RESTART_HOMEDOWN_MODE_END   	0x77665608

static int restart_mode;

#ifdef CONFIG_MSM_DLOAD_MODE
static int in_panic;

#if 0	/* onlyjazz.ef24 : intentionally remove it */
/* Download mode master kill-switch */
static int dload_set(const char *val, struct kernel_param *kp);
static int download_mode = 1;
module_param_call(download_mode, dload_set, param_get_int,
			&download_mode, 0644);
#endif	/* onlyjazz.ef24 : intentionally remove it */

static int panic_prep_restart(struct notifier_block *this,
			      unsigned long event, void *ptr)
{
	if(!sec_debug_is_enabled()) {
		printk(KERN_NOTICE "panic_prep_restart\n");
		return NOTIFY_DONE;
	}
	printk(KERN_NOTICE "panic_prep_restart, in_panic = 1\n");
	in_panic = 1;
	return NOTIFY_DONE;
}

static struct notifier_block panic_blk = {
	.notifier_call	= panic_prep_restart,
};

#if 1	/* onlyjazz.ef27 : make the dload_mode_addr global to avoid ioremap in interrupt context */
void *dload_mode_addr = NULL;
#endif

static void set_dload_mode(int on)
{
#if 0	/* onlyjazz.ef27 : make the dload_mode_addr global to avoid ioremap/iounmap in interrupt context */
	void *dload_mode_addr;
	dload_mode_addr = ioremap_nocache(0x2A05F000, SZ_4K);
#endif

	if (dload_mode_addr) {
		writel(on ? 0xE47B337D : 0, dload_mode_addr);
		writel(on ? 0xCE14091A : 0,
		       dload_mode_addr + sizeof(unsigned int));
		mb();
		
		// klaatu
		pr_err("set_dload_mode <%d> ( %x )\n", on, CALLER_ADDR0);
#if 0	/* onlyjazz.ef27 : make the dload_mode_addr global to avoid ioremap/iounmap in interrupt context */
		iounmap(dload_mode_addr);
#endif
	}
}

#if 0	/* onlyjazz.ef24 : intentionally remove it */
static int dload_set(const char *val, struct kernel_param *kp)
{
	int ret;
	int old_val = download_mode;

	ret = param_set_int(val, kp);

	if (ret)
		return ret;

	/* If download_mode is not zero or one, ignore. */
	if (download_mode >> 1) {
		download_mode = old_val;
		return -EINVAL;
	}

	set_dload_mode(download_mode);

	return 0;
}
#endif	/* onlyjazz.ef24 : intentionally remove it */
#else
#define set_dload_mode(x) do {} while (0)
#endif

void msm_set_restart_mode(int mode)
{
	restart_mode = mode;
}
EXPORT_SYMBOL(msm_set_restart_mode);

static void msm_power_off(void)
{
	printk(KERN_NOTICE "Powering off the SoC\n");
#ifdef CONFIG_MSM_DLOAD_MODE
	set_dload_mode(0);
#endif
	pm8058_reset_pwr_off(0);
	pm8901_reset_pwr_off(0);
	writel(0, PSHOLD_CTL_SU);
	mdelay(10000);
	printk(KERN_ERR "Powering off has failed\n");
	return;
}

#ifdef CONFIG_SEC_DEBUG
extern void* restart_reason;
#endif

void arch_reset(char mode, const char *cmd)
{
#ifndef CONFIG_SEC_DEBUG
	void *restart_reason;
#endif

#ifdef CONFIG_MSM_DLOAD_MODE

#ifdef CONFIG_SEC_DEBUG // klaatu
	if( sec_debug_is_enabled() && ((restart_mode == RESTART_DLOAD) || in_panic) )
		set_dload_mode(1);
	else
		set_dload_mode(0);
#else
	/* This looks like a normal reboot at this point. */
	set_dload_mode(0);

	/* Write download mode flags if we're panic'ing */
	set_dload_mode(in_panic);

	/* Write download mode flags if restart_mode says so */
	if (restart_mode == RESTART_DLOAD)
		set_dload_mode(1);
#endif

	#if 0	/* onlyjazz.ef24 : intentionally remove it */
	/* Kill download mode if master-kill switch is set */
	if (!download_mode)
		set_dload_mode(0);
	#endif 	/* onlyjazz.ef24 : intentionally remove it */

#endif

	printk(KERN_NOTICE "Going down for restart now\n");

	pm8058_reset_pwr_off(1);

#ifdef CONFIG_SEC_DEBUG
	/* onlyjazz.ed26  : avoid ioreamp is possible because arch_reset can be called in interrupt context */
	if (!restart_reason)
		restart_reason = ioremap_nocache(RESTART_REASON_ADDR, SZ_4K);
#endif
        // TODO:  Never use RESTART_LPM_BOOT_MODE/0x77665506 as another restart reason instead of LPM mode.
        // RESTART_LPM_BOOT_MODE/0x77665506 is reserved for LPM mode.
	if (cmd != NULL) {
#ifndef CONFIG_SEC_DEBUG
		restart_reason = ioremap_nocache(RESTART_REASON_ADDR, SZ_4K);
#endif
		if (!strncmp(cmd, "bootloader", 10)) {
			writel(RESTART_FASTBOOT_MODE, restart_reason);
		} else if (!strncmp(cmd, "recovery", 8)) {
			writel(RESTART_RECOVERY_MODE, restart_reason);
		} else if (!strncmp(cmd, "download", 8)) {
			unsigned long code;
			strict_strtoul(cmd + 8, 16, &code);
			code = code & 0xff;
			writel(RESTART_HOMEDOWN_MODE + code, restart_reason);
		} else if (!strncmp(cmd, "oem-", 4)) {
			unsigned long code;
			strict_strtoul(cmd + 4, 16, &code);
			code = code & 0xff;
			writel(0x6f656d00 | code, restart_reason);
#ifdef CONFIG_SEC_DEBUG
		} else if (!strncmp(cmd, "sec_debug_hw_reset", 18)) {
			writel(0x776655ee, restart_reason);
#endif
		} else if (!strncmp(cmd, "arm11_fota", 10)) {
			writel(RESTART_ARM11FOTA_MODE, restart_reason);
		} else {
			writel(RESTART_OTHERBOOT_MODE, restart_reason);
		}
#ifndef CONFIG_SEC_DEBUG
		iounmap(restart_reason);
#endif
	}
#ifdef CONFIG_SEC_DEBUG
	else {
		writel(0x12345678, restart_reason);    /* clear abnormal reset flag */
	}
#endif

#if 0	
	writel(0, WDT0_EN);
	if (!(machine_is_msm8x60_charm_surf() ||
	      machine_is_msm8x60_charm_ffa())) {
		dsb();
		writel(0, PSHOLD_CTL_SU); /* Actually reset the chip */
		mdelay(5000);
		pr_notice("PS_HOLD didn't work, falling back to watchdog\n");
	}
#endif

/* 	MDM dump corruption
	Give time to MDM to prepare Upload mode*/
//------------------
	__raw_writel(0, WDT0_EN);
	mdelay(1000);
//------------------	
	__raw_writel(1, WDT0_RST);
	__raw_writel(5*0x31F3, WDT0_BARK_TIME);
	__raw_writel(0x31F3, WDT0_BITE_TIME);
	__raw_writel(3, WDT0_EN);
	secure_writel(3, MSM_TCSR_BASE + TCSR_WDT_CFG);

	mdelay(10000);
	printk(KERN_ERR "Restarting has failed\n");
}

#ifdef CONFIG_SEC_DEBUG // klaatu

static int dload_mode_normal_reboot_handler(struct notifier_block *nb,
				unsigned long l, void *p)
{
	set_dload_mode(0);

	return 0;
}

static struct notifier_block dload_reboot_block = {
	.notifier_call = dload_mode_normal_reboot_handler
};
#endif

static int __init msm_restart_init(void)
{
#ifdef CONFIG_MSM_DLOAD_MODE

	/* onlyjazz.ef27 : make the dload_mode_addr global to avoid ioremap/iounmap in interrupt context */
	dload_mode_addr = ioremap_nocache(0x2A05F000, SZ_4K);

	atomic_notifier_chain_register(&panic_notifier_list, &panic_blk);

#ifdef CONFIG_SEC_DEBUG // klaatu
	register_reboot_notifier(&dload_reboot_block);
#endif
	/* Reset detection is switched on below.*/
	if( sec_debug_is_enabled() )
		set_dload_mode(1);
	else
		set_dload_mode(0);
#endif

	pm_power_off = msm_power_off;

	return 0;
}

late_initcall(msm_restart_init);
