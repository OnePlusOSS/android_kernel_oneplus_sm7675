/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2022 TsingTeng MicroSystem Co., Ltd.
 */

#ifndef _TMS_COMMON_H_
#define _TMS_COMMON_H_

/*********** PART0: Head files ***********/
#include <linux/version.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/io.h>
#include <linux/clk.h>

/*********** PART1: LOG TAG Declear***********/
#define log_fmt(fmt) "[TMS-%s]%s: " fmt
#define TMS_ERR(a, arg...)\
    pr_err(log_fmt(a), TMS_MOUDLE, __func__, ##arg)

#define TMS_WARN(a, arg...)\
    do{\
        if (tms_debug >= LEVEL_WARN)\
            pr_err(log_fmt(a), TMS_MOUDLE, __func__, ##arg);\
    }while(0)

#define TMS_INFO(a, arg...)\
    do{\
        if (tms_debug >= LEVEL_INFO)\
            pr_err(log_fmt(a), TMS_MOUDLE, __func__, ##arg);\
    }while(0)

#define TMS_DEBUG(a, arg...)\
    do{\
        if (tms_debug >= LEVEL_DEBUG)\
            pr_err(log_fmt(a), TMS_MOUDLE, __func__, ##arg);\
    }while(0)

/*********** PART2: Define Area ***********/
#define TMS_MOUDLE                "Common"
#define TMS_VERSION               "010200"
#define DEVICES_CLASS_NAME        "tms"
#define OFF                       0    /* Device power off */
#define ON                        1    /* Device power on */
#define SUCCESS                   0
#define ERROR                     1
#define PAGESIZE                  512
#define WAIT_TIME_NONE            0
#define WAIT_TIME_1000US          1000
#define WAIT_TIME_10000US         10000
#define WAIT_TIME_10000US         10000
#define WAIT_TIME_20000US         20000

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
#define DECLARE_PROC_OPS(node_ops, open_func, read_func, write_func, release_func) \
static const struct proc_ops node_ops = { \
    .proc_open    = open_func,            \
    .proc_read    = read_func,            \
    .proc_write   = write_func,           \
    .proc_release = release_func,         \
    .proc_lseek   = default_llseek,       \
}
#else
#define DECLARE_PROC_OPS(node_ops, open_func, read_func, write_func, release_func) \
static const struct file_operations node_ops = { \
    .open    = open_func,                        \
    .read    = read_func,                        \
    .write   = write_func,                       \
    .release = release_func,                     \
    .owner   = THIS_MODULE,                      \
}
#endif
/*********** PART3: Struct Area ***********/
struct hw_resource {
    unsigned int    irq_gpio;
    unsigned int    rst_gpio;
    unsigned int    ven_gpio;
    unsigned int    download_gpio; /* nfc fw download control */
    uint32_t        ven_flag;      /* nfc ven setting flag */
    uint32_t        download_flag; /* nfc download setting flag */
    uint32_t        rst_flag;      /* ese reset setting flag */
};

struct dev_register {
    unsigned int                    count;     /* Number of devices */
    const char                      *name;     /* device name */
    dev_t                           devno;     /* request a device number */
    struct device                   *creation;
    struct cdev                     chrdev;    /* Used for char device */
    struct class                    *class;
    const struct file_operations    *fops;
};

struct tms_info {
    bool                        ven_enable; /* store VEN state */
    int                         dev_count;
    char                        *nfc_name;
    struct class                *class;
    struct hw_resource          hw_res;
    struct proc_dir_entry       *prEntry;
    int (*registe_device)       (struct dev_register *dev, void *data);
    void (*unregiste_device)    (struct dev_register *dev);
    void (*set_ven)             (struct hw_resource hw_res, bool state);
    void (*set_download)        (struct hw_resource hw_res, bool state);
    void (*set_reset)           (struct hw_resource hw_res, bool state);
    void (*set_gpio)            (unsigned int gpio, bool state,
                                 unsigned long predelay,
                                 unsigned long postdelay);
};

typedef enum {
    LEVEL_DEFAULT = 0, /* print dirver error info */
    LEVEL_WARN,        /* print driver warning info */
    LEVEL_INFO,        /* print basic debug info */
    LEVEL_DEBUG,       /* print all debug info */
    LEVEL_DUMP,        /* print I/O buffer info */
} tms_debug_level;

/*********** PART4: Function or variables for other files ***********/
extern unsigned int tms_debug;
struct tms_info *tms_common_data_binding(void);
void tms_buffer_dump(const char *tag, const uint8_t *src, int16_t len);
int nfc_driver_init(void);
void nfc_driver_exit(void);
int ese_driver_init(void);
void ese_driver_exit(void);
int tms_guide_init(void);
void tms_guide_exit(void);
#endif /* _TMS_COMMON_H_ */
