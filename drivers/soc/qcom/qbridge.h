/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef QBRIDGE_H_
#define QBRIDGE_H_

#include <linux/types.h>
#include <linux/ioctl.h>

#define QBRIDGE_IOCTL_BASE 'q'

struct qbridge_compat_ioctl {
	__u32 fd;
	__u32 cmd;
	__u32 arg;
};
#define QBRIDGE_COMPAT_IOCTL \
	_IOW(QBRIDGE_IOCTL_BASE, 0, struct qbridge_compat_ioctl)

struct qbridge_compat_robust_list {
	__u32 head;
	__u32 len;
};
#define QBRIDGE_COMPAT_SET_ROBUST_LIST \
	_IOW(QBRIDGE_IOCTL_BASE, 1, struct qbridge_compat_robust_list)

#define QBRIDGE_COMPAT_GET_ROBUST_LIST \
	_IOR(QBRIDGE_IOCTL_BASE, 2, struct qbridge_compat_robust_list)

#endif /* QBRIDGE_H_ */
