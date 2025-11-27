// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */

#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/printk.h>
#include <linux/version.h>
#include <linux/mutex.h>


#define OPLUS_LPM_SMP2P_DIR_NAME      "oplus_lpm_smp2p"
#define OPLUS_SMP2P_STATS_PROC     "oplus_smp2p_stats"
#define OPLUS_SMP2P_STATS_SWITCH_PROC     "oplus_smp2p_stats_switch"

#define SMP2P_STAT_ARRAY_SIZE  50

struct smp2p_stats {
	unsigned local_pid;
	unsigned remote_pid;
	unsigned int cnt;
	u32 type;
};

struct smp2p_stats_info {
	struct smp2p_stats stats[SMP2P_STAT_ARRAY_SIZE];
	unsigned int array_size;
};

static struct smp2p_stats_info stats_info;

static bool oplus_smp2p_stats_on = true;
static DEFINE_MUTEX(smp2p_array_lock);

static struct proc_dir_entry *oplus_lpm_smp2p_proc = NULL;
static struct proc_dir_entry *oplus_smp2p_stats_entry = NULL;
static struct proc_dir_entry *oplus_smp2p_stats_switch_entry = NULL;

bool update_smp2p_stats_info(unsigned local_pid, unsigned remote_pid, u32 type)
{
	int i;
	bool match = false;

	mutex_lock(&smp2p_array_lock);
	for(i = 0; i < SMP2P_STAT_ARRAY_SIZE; i++) {
	if (stats_info.stats[i].local_pid == local_pid
			&& stats_info.stats[i].remote_pid == remote_pid
			&& stats_info.stats[i].type == type) {
			stats_info.stats[i].cnt++;
			match = true;
			break;
		}
	}

	if (!match) {
		if (stats_info.array_size == SMP2P_STAT_ARRAY_SIZE) {
			pr_info("[oplus_smp2p_stats] smp2p stats info array full.");
			return false;
		}

		stats_info.stats[stats_info.array_size].local_pid = local_pid;
		stats_info.stats[stats_info.array_size].remote_pid = remote_pid;
		stats_info.stats[stats_info.array_size].cnt = 1;
		stats_info.stats[stats_info.array_size].type = type;
		stats_info.array_size++;
	}
	mutex_unlock(&smp2p_array_lock);

	return true;
}

static void smp2p_stats_info_reset(void)
{
	mutex_lock(&smp2p_array_lock);
	memset(stats_info.stats, 0, (sizeof(struct smp2p_stats) * SMP2P_STAT_ARRAY_SIZE));
	stats_info.array_size = 0;
	mutex_unlock(&smp2p_array_lock);
}

static int oplus_smp2p_stats_show(struct seq_file *seq_filp, void *v)
{
	int i;

	if (!oplus_smp2p_stats_on) {
		seq_printf(seq_filp, "<smp2p statistics disabled>\n");
		return 0;
	}

	seq_printf(seq_filp, "<smp2p statistics>\n");
	seq_printf(seq_filp, "<set 0 to clear history  data>\n");
	seq_printf(seq_filp, "local_pid  remote_pid  type  cnt\n");

	mutex_lock(&smp2p_array_lock);
	for(i = 0; i < stats_info.array_size; i++) {
		seq_printf(seq_filp, "%-10d %-10d  %-5x  %d\n",
			stats_info.stats[i].local_pid,
			stats_info.stats[i].remote_pid,
			stats_info.stats[i].type,
			stats_info.stats[i].cnt);
	}
	mutex_unlock(&smp2p_array_lock);

	return 0;
}

static int oplus_smp2p_stats_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = single_open(file, oplus_smp2p_stats_show, NULL);

	return ret;
}

static ssize_t oplus_smp2p_stats_write(struct file *file,
		const char __user *buff, size_t len, loff_t *data)
{
	char buf[10] = {0};
	unsigned int val = 0;
	bool reset_state = false;

	if (len > sizeof(buf))
		return -EFAULT;

	if (copy_from_user((char *)buf, buff, len))
		return -EFAULT;

	if (kstrtouint(buf, sizeof(buf), &val))
		return -EINVAL;

	reset_state = !(val);
	if (oplus_smp2p_stats_on && reset_state) {
		smp2p_stats_info_reset();
	}

	return len;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static const struct proc_ops oplus_smp2p_stats_fops = {
	.proc_open		= oplus_smp2p_stats_open,
	.proc_write		= oplus_smp2p_stats_write,
	.proc_read		= seq_read,
	.proc_lseek 	= default_llseek,
	.proc_release	= seq_release,
};
#else
static const struct file_operations oplus_smp2p_stats_fops = {
	.open		= oplus_smp2p_stats_open,
	.write		= oplus_smp2p_stats_write,
	.read			= seq_read,
	.proc_lseek		= seq_lseek;
	.proc_release	= single_release,
}
#endif

static int oplus_smp2p_stats_switch_show(struct seq_file *seq_filp, void *v)
{
	seq_printf(seq_filp, "%d\n", oplus_smp2p_stats_on);

	return 0;
}

static int oplus_smp2p_stats_switch_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = single_open(file, oplus_smp2p_stats_switch_show, NULL);

	return ret;
}

static ssize_t oplus_smp2p_stats_switch_write(struct file *file,
		const char __user *buff, size_t len, loff_t *data)
{
	char buf[10] = {0};
	unsigned int val = 0;

	if (len > sizeof(buf))
		return -EFAULT;

	if (copy_from_user((char *)buf, buff, len))
		return -EFAULT;

	if (kstrtouint(buf, sizeof(buf), &val))
		return -EINVAL;

	oplus_smp2p_stats_on = !!(val);
	if (!oplus_smp2p_stats_on) {
		smp2p_stats_info_reset();
	}

	return len;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static const struct proc_ops oplus_smp2p_stats_switch_fops = {
	.proc_open		= oplus_smp2p_stats_switch_open,
	.proc_write		= oplus_smp2p_stats_switch_write,
	.proc_read		= seq_read,
	.proc_lseek 	= default_llseek,
	.proc_release	= seq_release,
};
#else
static const struct file_operations oplus_smp2p_stats_switch_fops = {
	.open		= oplus_smp2p_stats_switch_open,
	.write		= oplus_smp2p_stats_switch_write,
	.read			= seq_read,
	.proc_lseek		= seq_lseek;
	.proc_release	= single_release,
}
#endif

int create_oplus_smp2p_node(void)
{
	pr_info("[oplus_smp2p_stats] create_oplus_smp2p_node");
	oplus_lpm_smp2p_proc = proc_mkdir(OPLUS_LPM_SMP2P_DIR_NAME, NULL);
	if (!oplus_lpm_smp2p_proc) {
		pr_err("[oplus_smp2p_stats] Failed to create /proc/oplus_lpm_smp2p dir.");
		return -ENOMEM;
	}

	oplus_smp2p_stats_entry = proc_create(OPLUS_SMP2P_STATS_PROC, 0666,
								oplus_lpm_smp2p_proc, &oplus_smp2p_stats_fops);
	if (!oplus_smp2p_stats_entry) {
		pr_err("[oplus_smp2p_stats] Failed to create /proc/oplus_lpm_smp2p/%s\n", OPLUS_SMP2P_STATS_PROC);
	}

	oplus_smp2p_stats_switch_entry = proc_create(OPLUS_SMP2P_STATS_SWITCH_PROC, 0666,
								oplus_lpm_smp2p_proc, &oplus_smp2p_stats_switch_fops);
	if (!oplus_smp2p_stats_switch_entry) {
		pr_err("[oplus_smp2p_stats] Failed to create /proc/oplus_lpm_smp2p/%s\n", OPLUS_SMP2P_STATS_SWITCH_PROC);
	}

	return 0;
}
