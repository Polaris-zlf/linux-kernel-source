/* ir-sysfs.c - sysfs interface for RC devices (/sys/class/rc)
 *
 * Copyright (C) 2009-2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/input.h>
#include <linux/device.h>
#include "ir-core-priv.h"

#define IRRCV_NUM_DEVICES	256

/* bit array to represent IR sysfs device number */
static unsigned long ir_core_dev_number;

/* class for /sys/class/rc */
static char *ir_devnode(struct device *dev, mode_t *mode)
{
	return kasprintf(GFP_KERNEL, "rc/%s", dev_name(dev));
}

static struct class ir_input_class = {
	.name		= "rc",
	.devnode	= ir_devnode,
};

/**
 * show_protocols() - shows the current IR protocol(s)
 * @d:		the device descriptor
 * @mattr:	the device attribute struct (unused)
 * @buf:	a pointer to the output buffer
 *
 * This routine is a callback routine for input read the IR protocol type(s).
 * it is trigged by reading /sys/class/rc/rc?/protocols.
 * It returns the protocol names of supported protocols.
 * Enabled protocols are printed in brackets.
 */
static ssize_t show_protocols(struct device *d,
			      struct device_attribute *mattr, char *buf)
{
	struct ir_input_dev *ir_dev = dev_get_drvdata(d);
	u64 allowed, enabled;
	char *tmp = buf;

	if (ir_dev->props && ir_dev->props->driver_type == RC_DRIVER_SCANCODE) {
		enabled = ir_dev->rc_tab.ir_type;
		allowed = ir_dev->props->allowed_protos;
	} else if (ir_dev->raw) {
		enabled = ir_dev->raw->enabled_protocols;
		allowed = ir_raw_get_allowed_protocols();
	} else
		return sprintf(tmp, "[builtin]\n");

	IR_dprintk(1, "allowed - 0x%llx, enabled - 0x%llx\n",
		   (long long)allowed,
		   (long long)enabled);

	if (allowed & enabled & IR_TYPE_UNKNOWN)
		tmp += sprintf(tmp, "[unknown] ");
	else if (allowed & IR_TYPE_UNKNOWN)
		tmp += sprintf(tmp, "unknown ");

	if (allowed & enabled & IR_TYPE_RC5)
		tmp += sprintf(tmp, "[rc5] ");
	else if (allowed & IR_TYPE_RC5)
		tmp += sprintf(tmp, "rc5 ");

	if (allowed & enabled & IR_TYPE_NEC)
		tmp += sprintf(tmp, "[nec] ");
	else if (allowed & IR_TYPE_NEC)
		tmp += sprintf(tmp, "nec ");

	if (allowed & enabled & IR_TYPE_RC6)
		tmp += sprintf(tmp, "[rc6] ");
	else if (allowed & IR_TYPE_RC6)
		tmp += sprintf(tmp, "rc6 ");

	if (allowed & enabled & IR_TYPE_JVC)
		tmp += sprintf(tmp, "[jvc] ");
	else if (allowed & IR_TYPE_JVC)
		tmp += sprintf(tmp, "jvc ");

	if (allowed & enabled & IR_TYPE_SONY)
		tmp += sprintf(tmp, "[sony] ");
	else if (allowed & IR_TYPE_SONY)
		tmp += sprintf(tmp, "sony ");

	if (allowed & enabled & IR_TYPE_RC5_SZ)
		tmp += sprintf(tmp, "[rc5sz] ");
	else if (allowed & IR_TYPE_RC5_SZ)
		tmp += sprintf(tmp, "rc5sz ");

	if (allowed & enabled & IR_TYPE_LIRC)
		tmp += sprintf(tmp, "[lirc] ");
	else if (allowed & IR_TYPE_LIRC)
		tmp += sprintf(tmp, "lirc ");

	if (tmp != buf)
		tmp--;
	*tmp = '\n';
	return tmp + 1 - buf;
}

/**
 * store_protocols() - changes the current IR protocol(s)
 * @d:		the device descriptor
 * @mattr:	the device attribute struct (unused)
 * @buf:	a pointer to the input buffer
 * @len:	length of the input buffer
 *
 * This routine is a callback routine for changing the IR protocol type.
 * It is trigged by writing to /sys/class/rc/rc?/protocols.
 * Writing "+proto" will add a protocol to the list of enabled protocols.
 * Writing "-proto" will remove a protocol from the list of enabled protocols.
 * Writing "proto" will enable only "proto".
 * Returns -EINVAL if an invalid protocol combination or unknown protocol name
 * is used, otherwise @len.
 */
static ssize_t store_protocols(struct device *d,
			       struct device_attribute *mattr,
			       const char *data,
			       size_t len)
{
	struct ir_input_dev *ir_dev = dev_get_drvdata(d);
	bool enable, disable;
	const char *tmp;
	u64 type;
	u64 mask;
	int rc;
	unsigned long flags;

	if (!ir_dev->props && !ir_dev->raw)
		return -EINVAL;

	tmp = skip_spaces(data);

	if (*tmp == '+') {
		enable = true;
		disable = false;
		tmp++;
	} else if (*tmp == '-') {
		enable = false;
		disable = true;
		tmp++;
	} else {
		enable = false;
		disable = false;
	}

	if (!strncasecmp(tmp, "unknown", 7)) {
		tmp += 7;
		mask = IR_TYPE_UNKNOWN;
	} else if (!strncasecmp(tmp, "rc5", 3)) {
		tmp += 3;
		mask = IR_TYPE_RC5;
	} else if (!strncasecmp(tmp, "nec", 3)) {
		tmp += 3;
		mask = IR_TYPE_NEC;
	} else if (!strncasecmp(tmp, "rc6", 3)) {
		tmp += 3;
		mask = IR_TYPE_RC6;
	} else if (!strncasecmp(tmp, "jvc", 3)) {
		tmp += 3;
		mask = IR_TYPE_JVC;
	} else if (!strncasecmp(tmp, "sony", 4)) {
		tmp += 4;
		mask = IR_TYPE_SONY;
	} else if (!strncasecmp(tmp, "rc5sz", 5)) {
		tmp += 5;
		mask = IR_TYPE_RC5_SZ;
	} else if (!strncasecmp(tmp, "lirc", 4)) {
		tmp += 4;
		mask = IR_TYPE_LIRC;
	} else {
		IR_dprintk(1, "Unknown protocol\n");
		return -EINVAL;
	}

	tmp = skip_spaces(tmp);
	if (*tmp != '\0') {
		IR_dprintk(1, "Invalid trailing characters\n");
		return -EINVAL;
	}

	if (ir_dev->props->driver_type == RC_DRIVER_SCANCODE)
		type = ir_dev->rc_tab.ir_type;
	else
		type = ir_dev->raw->enabled_protocols;

	if (enable)
		type |= mask;
	else if (disable)
		type &= ~mask;
	else
		type = mask;

	if (ir_dev->props && ir_dev->props->change_protocol) {
		rc = ir_dev->props->change_protocol(ir_dev->props->priv,
						    type);
		if (rc < 0) {
			IR_dprintk(1, "Error setting protocols to 0x%llx\n",
				   (long long)type);
			return -EINVAL;
		}
	}

	if (ir_dev->props && ir_dev->props->driver_type == RC_DRIVER_SCANCODE) {
		spin_lock_irqsave(&ir_dev->rc_tab.lock, flags);
		ir_dev->rc_tab.ir_type = type;
		spin_unlock_irqrestore(&ir_dev->rc_tab.lock, flags);
	} else {
		ir_dev->raw->enabled_protocols = type;
	}


	IR_dprintk(1, "Current protocol(s): 0x%llx\n",
		   (long long)type);

	return len;
}

#define ADD_HOTPLUG_VAR(fmt, val...)					\
	do {								\
		int err = add_uevent_var(env, fmt, val);		\
		if (err)						\
			return err;					\
	} while (0)

static int rc_dev_uevent(struct device *device, struct kobj_uevent_env *env)
{
	struct ir_input_dev *ir_dev = dev_get_drvdata(device);

	if (ir_dev->rc_tab.name)
		ADD_HOTPLUG_VAR("NAME=%s", ir_dev->rc_tab.name);
	if (ir_dev->driver_name)
		ADD_HOTPLUG_VAR("DRV_NAME=%s", ir_dev->driver_name);

	return 0;
}

/*
 * Static device attribute struct with the sysfs attributes for IR's
 */
static DEVICE_ATTR(protocols, S_IRUGO | S_IWUSR,
		   show_protocols, store_protocols);

static struct attribute *rc_dev_attrs[] = {
	&dev_attr_protocols.attr,
	NULL,
};

static struct attribute_group rc_dev_attr_grp = {
	.attrs	= rc_dev_attrs,
};

static const struct attribute_group *rc_dev_attr_groups[] = {
	&rc_dev_attr_grp,
	NULL
};

static struct device_type rc_dev_type = {
	.groups		= rc_dev_attr_groups,
	.uevent		= rc_dev_uevent,
};

/**
 * ir_register_class() - creates the sysfs for /sys/class/rc/rc?
 * @input_dev:	the struct input_dev descriptor of the device
 *
 * This routine is used to register the syfs code for IR class
 */
int ir_register_class(struct input_dev *input_dev)
{
	int rc;
	const char *path;
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	int devno = find_first_zero_bit(&ir_core_dev_number,
					IRRCV_NUM_DEVICES);

	if (unlikely(devno < 0))
		return devno;

	ir_dev->dev.type = &rc_dev_type;
	ir_dev->dev.class = &ir_input_class;
	ir_dev->dev.parent = input_dev->dev.parent;
	dev_set_name(&ir_dev->dev, "rc%d", devno);
	dev_set_drvdata(&ir_dev->dev, ir_dev);
	rc = device_register(&ir_dev->dev);
	if (rc)
		return rc;


	input_dev->dev.parent = &ir_dev->dev;
	rc = input_register_device(input_dev);
	if (rc < 0) {
		device_del(&ir_dev->dev);
		return rc;
	}

	__module_get(THIS_MODULE);

	path = kobject_get_path(&ir_dev->dev.kobj, GFP_KERNEL);
	printk(KERN_INFO "%s: %s as %s\n",
		dev_name(&ir_dev->dev),
		input_dev->name ? input_dev->name : "Unspecified device",
		path ? path : "N/A");
	kfree(path);

	ir_dev->devno = devno;
	set_bit(devno, &ir_core_dev_number);

	return 0;
};

/**
 * ir_unregister_class() - removes the sysfs for sysfs for
 *			   /sys/class/rc/rc?
 * @input_dev:	the struct input_dev descriptor of the device
 *
 * This routine is used to unregister the syfs code for IR class
 */
void ir_unregister_class(struct input_dev *input_dev)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);

	clear_bit(ir_dev->devno, &ir_core_dev_number);
	input_unregister_device(input_dev);
	device_del(&ir_dev->dev);

	module_put(THIS_MODULE);
}

/*
 * Init/exit code for the module. Basically, creates/removes /sys/class/rc
 */

static int __init ir_core_init(void)
{
	int rc = class_register(&ir_input_class);
	if (rc) {
		printk(KERN_ERR "ir_core: unable to register rc class\n");
		return rc;
	}

	/* Initialize/load the decoders/keymap code that will be used */
	ir_raw_init();

	return 0;
}

static void __exit ir_core_exit(void)
{
	class_unregister(&ir_input_class);
}

module_init(ir_core_init);
module_exit(ir_core_exit);
