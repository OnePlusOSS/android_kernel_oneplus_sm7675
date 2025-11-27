// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (C) 2024 Oplus. All rights reserved.
*/

#define pr_fmt(fmt) "mpam_game: " fmt

#include <linux/module.h>
#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/of.h>
#include <asm/current.h>
#include <trace/hooks/mpam.h>
#include <linux/sched/walt.h>
#include <soc/qcom/mpam.h>
#include <trace/hooks/sched.h>
#include <trace/events/sched.h>
#include <trace/events/task.h>
#include "../../../kernel/sched/walt/walt.h"
#include "mpam_regs.h"

#define RESERVED_PARTID 16
#define MPAM_GAME_SUPPORT_MODEL "pineapple"

enum part_id_enum {
	DEFAULT_PARTID = RESERVED_PARTID,
	HIGH_PRIO_PARTID,
	LOW_PRIO_PARTID,
};

static u32 cpbm_local[3] = {100, 100, 100};
static u64 config_local[3] = {0, 0, 0};

static inline void set_cpbm(int partid, const char *buf)
{
	int ret;
	u32 tmp[2];
	u32 cpbm_val;
	u64 config_ctrl;
	int i = 0;

	while (i < 2) {
		if (sscanf(buf, "%u", &tmp[i]) != 1) {
			pr_err("invalid argument\n");
			return;
		}

		buf = strpbrk(buf, " ");
		buf++;
		i++;
	}
	cpbm_val = tmp[0];
	config_ctrl = tmp[1];
	pr_info("mpam partid %d cpbm_val=%u config_ctrl= %llu\n", partid, cpbm_val, config_ctrl);

	ret = qcom_mpam_set_cache_portion(partid, cpbm_val, config_ctrl);
	if (ret) {
		pr_err("set cache portion failed ret %d\n", ret);
		return;
	}

	cpbm_local[partid - DEFAULT_PARTID] = cpbm_val;
	config_local[partid - DEFAULT_PARTID] = config_ctrl;
}

static inline u32 get_cpbm(int partid)
{
	return cpbm_local[partid - DEFAULT_PARTID];
}
static inline u32 get_config(int partid)
{
	return config_local[partid - DEFAULT_PARTID];
}
static ssize_t low_prio_cpbm_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u %llu\n", get_cpbm(LOW_PRIO_PARTID), get_config(LOW_PRIO_PARTID));
}

static ssize_t low_prio_cpbm_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	set_cpbm(LOW_PRIO_PARTID, buf);
	return count;
}

static ssize_t normal_cpbm_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u %llu\n", get_cpbm(DEFAULT_PARTID), get_config(DEFAULT_PARTID));
}

static ssize_t normal_cpbm_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	set_cpbm(DEFAULT_PARTID, buf);
	return count;
}

static ssize_t high_prio_cpbm_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u %llu\n", get_cpbm(HIGH_PRIO_PARTID), get_config(HIGH_PRIO_PARTID));
}

static ssize_t high_prio_cpbm_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	set_cpbm(HIGH_PRIO_PARTID, buf);
	return count;
}

static struct kobj_attribute attr_normal_cpbm = __ATTR_RW_MODE(normal_cpbm, 0660);
static struct kobj_attribute attr_low_prio_cpbm = __ATTR_RW_MODE(low_prio_cpbm, 0660);
static struct kobj_attribute attr_high_prio_cpbm = __ATTR_RW_MODE(high_prio_cpbm, 0660);

static ssize_t pids_show(char *buf, u8 part_id)
{
	ssize_t len = 0;
	struct task_struct *p, *t;
	struct walt_task_struct *wts;

	rcu_read_lock();
	for_each_process_thread(p, t) {
		wts = (struct walt_task_struct *) t->android_vendor_data1;
		if (wts->mpam_partid == part_id)
			len += scnprintf(buf + len, PAGE_SIZE - len, "%d ", t->pid);
	}
	rcu_read_unlock();
	len += scnprintf(buf + len, PAGE_SIZE - len, "\n");

	return len;
}

static void pids_store(const char *buf, u8 part_id)
{
	int ret;
	pid_t pid_input;
	char *kbuf, *token;
	struct task_struct *p;
	struct walt_task_struct *wts;

	kbuf = (char *)buf;
	while ((token = strsep(&kbuf, " ")) != NULL) {
		ret = kstrtouint(token, 10, &pid_input);
		if (ret < 0) {
			pr_err("%s invalid argument\n", __func__);
			return;
		}

		p = find_task_by_vpid(pid_input);
		if (IS_ERR_OR_NULL(p)) {
			pr_err("%s pid %d not exist\n", __func__, pid_input);
			continue;
		}

		wts = (struct walt_task_struct *) p->android_vendor_data1;
		wts->mpam_partid = part_id;
	}
}

static ssize_t normal_pids_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf)
{
	return pids_show(buf, DEFAULT_PARTID);
}

static ssize_t normal_pids_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	pids_store(buf, DEFAULT_PARTID);
	return count;
}

static ssize_t low_prio_pids_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf)
{
	return pids_show(buf, LOW_PRIO_PARTID);
}
void init_task_mapm(void)
{
	struct task_struct *g, *p;
	struct walt_task_struct *wts = NULL;

	read_lock(&tasklist_lock);

	do_each_thread(g, p) {
		wts = (struct walt_task_struct *) p->android_vendor_data1;
		wts->mpam_partid = 16;
	} while_each_thread(g, p);


	read_unlock(&tasklist_lock);
}
static ssize_t reset_prio_pids_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf)
{
	init_task_mapm();
	return 0;
}
static ssize_t low_prio_pids_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	pids_store(buf, LOW_PRIO_PARTID);
	return count;
}
static ssize_t reset_prio_pids_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	init_task_mapm();
	return 0;
}
static ssize_t high_prio_pids_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf)
{
	return pids_show(buf, HIGH_PRIO_PARTID);
}

static ssize_t high_prio_pids_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	pids_store(buf, HIGH_PRIO_PARTID);
	return count;
}

static struct kobj_attribute attr_normal_pids = __ATTR_RW_MODE(normal_pids, 0660);
static struct kobj_attribute attr_low_prio_pids = __ATTR_RW_MODE(low_prio_pids, 0660);
static struct kobj_attribute attr_high_prio_pids = __ATTR_RW_MODE(high_prio_pids, 0660);
static struct kobj_attribute attr_reset_prio_pids = __ATTR_RW_MODE(reset_prio_pids, 0660);

static void mpam_write_partid(u8 partid, pid_t next_pid)
{
	u64 reg;

	reg = (partid << PARTID_I_SHIFT) | (partid << PARTID_D_SHIFT);
	write_sysreg_s(reg, SYS_MPAM0_EL1);
	write_sysreg_s(reg, SYS_MPAM1_EL1);
}

static void mpam_game_switch_task(void *unused, struct task_struct *prev,
							struct task_struct *next)
{
	struct walt_task_struct *wts;

	wts = (struct walt_task_struct *) next->android_vendor_data1;
	mpam_write_partid(wts->mpam_partid, next->pid);
}

static struct attribute *mpam_game_attrs[] = {
	&attr_normal_cpbm.attr,
	&attr_low_prio_cpbm.attr,
	&attr_high_prio_cpbm.attr,
	&attr_normal_pids.attr,
	&attr_low_prio_pids.attr,
	&attr_high_prio_pids.attr,
	&attr_reset_prio_pids.attr,
	NULL
};

static const struct attribute_group mpam_game_group = {
	.name = "parameters",
	.attrs = mpam_game_attrs,
};

static void mpam_game_fork(void *unused, struct task_struct *t)
{
	u8 part_id;
	struct walt_task_struct *wts;

	wts =  (struct walt_task_struct *)current->android_vendor_data1;
	part_id = wts->mpam_partid;
	wts =  (struct walt_task_struct *)t->android_vendor_data1;
	wts->mpam_partid = part_id;
}

static int __init mpam_game_init(void)
{
	static struct kobject *kobj;
	static struct device_node *root;
	const char *compatible = "";
	int ret;

	root = of_find_node_by_path("/");
	if (!root) {
		pr_err("mpam game cannot find root node of dts, return\n");
		return 0;
	}
	compatible = of_get_property(root, "compatible", NULL);

	if (strstr(compatible, MPAM_GAME_SUPPORT_MODEL) == NULL) {
		pr_err("model %s do not support mpam game, return\n", compatible);
		return 0;
	}

	kobj = kobject_create_and_add("mpam_game", kernel_kobj);
	if (!kobj) {
		pr_err("kobj created failed\n");
		return -ENOMEM;
	}
	ret = sysfs_create_group(kobj, &mpam_game_group);
	if (ret)
		pr_err("sysfs created failed\n");

	register_trace_android_vh_mpam_set(mpam_game_switch_task, NULL);
	ret = register_trace_android_rvh_sched_fork(mpam_game_fork, NULL);
	if (ret != 0)
		pr_err("mpam_game_fork register_trace_android_rvh_sched_fork failed\n");

	init_task_mapm();

	return 0;
}
arch_initcall(mpam_game_init);

MODULE_DESCRIPTION("MPAM prototype driver");
MODULE_LICENSE("GPL");
