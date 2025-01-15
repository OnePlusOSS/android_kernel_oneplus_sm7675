// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/security.h>
#include <linux/compat.h>
#include "qbridge.h"

static long qbridge_compat_ioctl(struct qbridge_compat_ioctl __user *argp)
{
	struct qbridge_compat_ioctl args;
	struct fd f;
	long retval;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	f = fdget(args.fd);
	if (!f.file)
		return -EBADF;

	set_thread_flag(TIF_32BIT);

	retval = security_file_ioctl(f.file, args.cmd, args.arg);
	if (retval)
		goto out;

	retval = -ENOIOCTLCMD;
	if (f.file->f_op->compat_ioctl)
		retval = f.file->f_op->compat_ioctl(f.file, args.cmd, args.arg);

out:
	clear_thread_flag(TIF_32BIT);
	fdput(f);

	return retval;
}

static long
qbridge_compat_set_robust_list(struct qbridge_compat_robust_list __user *argp)
{
	struct qbridge_compat_robust_list args;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	if (args.len != sizeof(*current->compat_robust_list))
		return -EINVAL;

	current->compat_robust_list = compat_ptr(args.head);

	return 0;
}

static long
qbridge_compat_get_robust_list(struct qbridge_compat_robust_list __user *argp)
{
	struct qbridge_compat_robust_list out;

	out.head = ptr_to_compat(current->compat_robust_list);
	out.len = sizeof(*current->compat_robust_list);

	if (copy_to_user(argp, &out, sizeof(out)))
		return -EFAULT;

	return 0;
}

static long qbridge_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	if (test_thread_flag(TIF_32BIT))
		return -EINVAL;

	switch (cmd) {
	case QBRIDGE_COMPAT_IOCTL:
		return qbridge_compat_ioctl(argp);
	case QBRIDGE_COMPAT_SET_ROBUST_LIST:
		return qbridge_compat_set_robust_list(argp);
	case QBRIDGE_COMPAT_GET_ROBUST_LIST:
		return qbridge_compat_get_robust_list(argp);
	}

	return -ENOIOCTLCMD;
}

static const struct file_operations qbridge_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = qbridge_ioctl,
};

static struct miscdevice qbridge_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "qbridge",
	.fops = &qbridge_fops,
	.mode = 0666,
};

module_misc_device(qbridge_device);

MODULE_DESCRIPTION("Qbridge translator kernel module");
MODULE_LICENSE("GPL");
