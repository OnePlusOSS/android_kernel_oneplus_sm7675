// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 TsingTeng MicroSystem Co., Ltd.
 */

#include "tms_common.h"

/*********** PART0: Global Variables Area ***********/
struct tms_info *tms = NULL;
unsigned int tms_debug = LEVEL_INFO;

/*********** PART1: Declare Area ***********/

/*********** PART2: Function Area ***********/
void tms_buffer_dump(const char *tag, const uint8_t *src, int16_t len)
{
    uint16_t buf_len = (len > PAGESIZE) ? PAGESIZE : len;
    uint16_t index = 0;
    uint16_t i;

    if (tms_debug < LEVEL_DUMP) {
        TMS_DEBUG("%s[%d] bytes", tag, len);
    } else {
        char buf[PAGESIZE * 2 + 1];
        do {
            memset(buf, 0, sizeof(buf));

            for (i = 0; i < buf_len; i++) {
                snprintf(&buf[i * 2], 3, "%02X", src[index++]);
            }

            TMS_ERR("%s[%d] %s", tag, buf_len, buf);
            len = len - buf_len;
            buf_len = (len > PAGESIZE) ? PAGESIZE : len;
        } while (len > 0);
    }
}

struct tms_info *tms_common_data_binding(void)
{
    return tms;
}

void tms_gpio_set(unsigned int gpio, bool state, unsigned long predelay,
                  unsigned long postdelay)
{
    if (!gpio_is_valid(gpio)) {
        TMS_ERR("Gpio %d is invalid\n", gpio);
        return;
    }

    if (predelay) {
        usleep_range(predelay, predelay + 100);
    }

    gpio_set_value(gpio, state);
    TMS_DEBUG("Set gpio %d state is %d\n", gpio, state);

    if (postdelay) {
        usleep_range(postdelay, postdelay + 100);
    }
}

void tms_device_unregister(struct dev_register *dev)
{
    device_destroy(tms->class, dev->devno);
    cdev_del(&dev->chrdev);
    unregister_chrdev_region(dev->devno, dev->count);
    TMS_DEBUG("Unregister device\n");
}

int tms_device_register(struct dev_register *dev, void *data)
{
    int ret;

    dev->class = tms->class;
    ret = alloc_chrdev_region(&dev->devno, 0, dev->count, dev->name);

    if (ret < 0) {
        TMS_ERR("Alloc chrdev region failed, ret = %d\n", ret);
        return ret;
    }

    cdev_init(&dev->chrdev, dev->fops);
    ret = cdev_add(&dev->chrdev, dev->devno, dev->count);

    if (ret < 0) {
        TMS_ERR("Add char device failed, ret = %d\n", ret);
        goto err_free_devno;
    }

    dev->creation = device_create(tms->class, NULL, dev->devno, data, dev->name);

    if (IS_ERR(dev->creation)) {
        ret = PTR_ERR(dev->creation);
        TMS_ERR("Create the device failed, ret = %d\n", ret);
        goto err_delete_cdev;
    }

    TMS_DEBUG("Register device success\n");
    return SUCCESS;
err_delete_cdev:
    cdev_del(&dev->chrdev);
err_free_devno:
    unregister_chrdev_region(dev->devno, dev->count);
    return ret;
}

static ssize_t proc_debug_control_read(struct file *file, char __user *buf,
                                       size_t count, loff_t *ppos)
{
    char page[PAGESIZE] = {0};
    snprintf(page, PAGESIZE - 1, "%u\n", tms_debug);
    return simple_read_from_buffer(buf, count, ppos, page, strlen(page));
}

static ssize_t proc_debug_control_write(struct file *file,
                                        const char __user *buf, size_t count, loff_t *lo)
{
    int tmp = 0;
    char buffer[4] = {0};

    if ((count > 2) || copy_from_user(buffer, buf, count)) {
        return -EPERM;
    }

    if (sscanf(buffer, "%d", &tmp) == 1) {
        tms_debug = tmp;
    } else {
        return -EPERM;
    }

    return count;
}

DECLARE_PROC_OPS(proc_debug_control_ops, simple_open, proc_debug_control_read, proc_debug_control_write, NULL);

/*********** PART3: TMS Common Start Area ***********/
static int tms_proc_init(void)
{
    int ret;
    struct proc_dir_entry *prEntry_tmp = NULL;

    tms->prEntry = proc_mkdir("tmsdev", NULL);

    if (tms->prEntry == NULL) {
        TMS_ERR("Couldn't create tmsdev proc entry\n");
        return -ENOMEM;
    }

    prEntry_tmp = proc_create_data("debug_level", 0644, tms->prEntry,
                                   &proc_debug_control_ops, NULL);

    if (prEntry_tmp == NULL) {
        ret = -ENODEV;
        TMS_ERR("Couldn't create debug_level proc entry\n");
        goto err_remove_proc;
    }

    return SUCCESS;
err_remove_proc:
    remove_proc_subtree("tmsdev", NULL);
    return ret;
}

static int tms_common_probe(void)
{
    int ret;
    TMS_INFO("Enter\n");

    /* step1 : alloc tms common data */
    tms = kzalloc(sizeof(struct tms_info), GFP_KERNEL);

    if (tms == NULL) {
        TMS_ERR("TMS info alloc memory error\n");
        return -ENOMEM;
    }

    memset(tms, 0, sizeof(*tms));
    /* step2 : tms class register */
    tms->class = class_create(THIS_MODULE, DEVICES_CLASS_NAME);

    if (IS_ERR(tms->class)) {
        ret = PTR_ERR(tms->class);
        TMS_ERR("Failed to register device class\n");
        goto err_free_tms_info;
    }

    /* step3 : binding common data and function */
    tms->nfc_name         = "thn31";
    tms->registe_device   = tms_device_register;
    tms->unregiste_device = tms_device_unregister;
    tms->set_gpio         = tms_gpio_set;
    /* step4 : init proc */
    ret = tms_proc_init();

    if (ret) {
        TMS_ERR("NFC device proc create failed.\n");
        goto err_destroy_class;
    }

    TMS_INFO("Successfully\n");
    return SUCCESS;
err_destroy_class:
    class_destroy(tms->class);
err_free_tms_info:
    kfree(tms);
    tms = NULL;
    TMS_ERR("Failed, ret = %d\n", ret);
    return ret;
}

/*********** PART4: TMS Module Init Area ***********/
static int __init tms_driver_init(void)
{
    int ret = 0;

    ret = tms_common_probe();
    if (ret) {
        TMS_ERR("Unable to init TMS Common, ret = %d\n", ret);
        goto err;
    }

#if IS_ENABLED(CONFIG_TMS_GUIDE_DEVICE)
    ret = tms_guide_init();
#else
#if IS_ENABLED(CONFIG_TMS_NFC_DEVICE)
    ret = nfc_driver_init();
    if (ret) {
        goto err;
    }
#endif
#if IS_ENABLED(CONFIG_TMS_ESE_DEVICE)
    ret = ese_driver_init();
#endif
#endif
err:
    return ret;
}

static void __exit tms_driver_exit(void)
{
#if IS_ENABLED(CONFIG_TMS_GUIDE_DEVICE)
    tms_guide_exit();
#else
#if IS_ENABLED(CONFIG_TMS_NFC_DEVICE)
    nfc_driver_exit();
#endif
#if IS_ENABLED(CONFIG_TMS_ESE_DEVICE)
    ese_driver_exit();
#endif
#endif

    remove_proc_subtree("tmsdev", NULL);
    if (!IS_ERR(tms->class)) {
        class_destroy(tms->class);
    }

    if (tms) {
        kfree(tms);
        tms = NULL;
    }
}

module_init(tms_driver_init);
module_exit(tms_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Guoliang Wu <wugl@tsinghuaic.com>");
MODULE_DESCRIPTION("Board Support Driver for TMS Chip");