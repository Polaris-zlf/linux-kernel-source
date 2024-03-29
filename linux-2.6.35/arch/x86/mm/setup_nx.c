#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/proto.h>

static int disable_nx __cpuinitdata;

/*
 * noexec = on|off
 *
 * Control non-executable mappings for processes.
 *
 * on      Enable
 * off     Disable
 */
static int __init noexec_setup(char *str)
{
	if (!str)
		return -EINVAL;
	if (!strncmp(str, "on", 2)) {
		disable_nx = 0;
	} else if (!strncmp(str, "off", 3)) {
		disable_nx = 1;
#ifdef CONFIG_X86_32
		exec_shield = 0;
#endif
	}
	x86_configure_nx();
	return 0;
}
early_param("noexec", noexec_setup);

void __cpuinit x86_configure_nx(void)
{
	if (cpu_has_nx && !disable_nx)
		__supported_pte_mask |= _PAGE_NX;
	else
		__supported_pte_mask &= ~_PAGE_NX;
}

void __init x86_report_nx(void)
{
	int nx_emulation = 0;

	if (!cpu_has_nx) {
		printk(KERN_NOTICE "Notice: NX (Execute Disable) protection "
		       "missing in CPU or disabled in BIOS!\n");
		nx_emulation = 1;
	} else {
#if defined(CONFIG_X86_64) || defined(CONFIG_X86_PAE)
		if (disable_nx) {
			printk(KERN_INFO "NX (Execute Disable) protection: "
			       "disabled by kernel command line option\n");
		} else {
			printk(KERN_INFO "NX (Execute Disable) protection: "
			       "active\n");
		}
#else
		/* 32bit non-PAE kernel, NX cannot be used */
		printk(KERN_NOTICE "Notice: NX (Execute Disable) protection "
		       "cannot be enabled in hardware: non-PAE kernel!\n");
		nx_emulation = 1;
#endif
	}

	if (nx_emulation) {
#ifdef CONFIG_X86_32
		if (exec_shield)
			printk(KERN_INFO "NX (Execute Disable) protection: "
			       "approximated by x86 segment limits\n");
		else
			printk(KERN_INFO "NX (Execute Disable) protection: "
			       "approximation disabled by kernel command "
			       "line option\n");
#endif
	}
}
