// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 TsingTeng MicroSystem Co., Ltd.
 */

#include "ese_driver.h"

/*********** PART0: Global Variables Area ***********/

/*********** PART1: Callback Function Area ***********/
static int ese_set_power(struct ese_info *ese, unsigned long arg)
{
    int ret = SUCCESS;
    TMS_DEBUG("arg = %lu\n", arg);

    if (!ese->tms->set_gpio) {
        TMS_ERR("set_gpio is NULL");
        return ret;
    }

    switch (arg) {
    case ESE_POWER_ON:
        ese->tms->set_gpio(ese->tms->hw_res.ven_gpio, ON, WAIT_TIME_NONE, WAIT_TIME_NONE);
        break;

    case ESE_POWER_OFF:
        ese->tms->set_gpio(ese->tms->hw_res.ven_gpio, OFF, WAIT_TIME_NONE, WAIT_TIME_NONE);
        break;

    default:
        TMS_ERR("Bad control arg %lu\n", arg);
        ret = -ENOIOCTLCMD;
        break;
    }

    return ret;
}

static int ese_spi_clk_control(struct ese_info *ese, unsigned long arg)
{
    int ret = SUCCESS;
    struct mtk_spi *mt_spi = spi_master_get_devdata(ese->client->master);

    TMS_DEBUG("arg = %lu\n", arg);

    switch (arg) {
    case ON:
        //mt_spi_enable_master_clk(ese->client);
        ret = clk_prepare_enable(mt_spi->spi_clk);
        break;

    case OFF:
        //mt_spi_disable_master_clk(ese->client);
        clk_disable_unprepare(mt_spi->spi_clk);
        break;

    default:
        TMS_ERR("Bad control arg %lu\n", arg);
        ret = -ENOIOCTLCMD;
        break;
    }

    return ret;
}

/*********** PART2: Operation Function Area ***********/
static long ese_device_ioctl(struct file *file, unsigned int cmd,
                             unsigned long arg)
{
    int ret;
    struct ese_info *ese;
    TMS_DEBUG("cmd = %x arg = %zx\n", cmd, arg);
    ese = file->private_data;

    if (!ese) {
        TMS_ERR("eSE device no longer exists\n");
        return -ENODEV;
    }

    switch (cmd) {
    case ESE_SET_POWER:
        ret = ese_set_power(ese, arg);
        break;

    case ESE_HARD_RESET:
        ese_hard_reset(ese);
        ret = SUCCESS;
        break;

    case ESE_SPI_CLK_CONTROL:
        ret = ese_spi_clk_control(ese, arg);
        break;

    default:
        TMS_ERR("Unknow control cmd[%x]\n", cmd);
        ret = -ENOIOCTLCMD;
    };

    return ret;
}

static ssize_t ese_device_write(struct file *file, const char *buf,
                                size_t count, loff_t *offset)
{
    int ret = -EIO;
    uint8_t *write_buf;
    struct ese_info *ese;
    ese = file->private_data;

    if (!ese) {
        TMS_ERR("eSE device no longer exists\n");
        return -ENODEV;
    }

    if (count > 0 && count < ESE_MAX_BUFFER_SIZE) {
    } else if (count > ESE_MAX_BUFFER_SIZE) {
        TMS_WARN("The write bytes[%zu] exceeded the buffer max size, count = %d\n",
                 count, ESE_MAX_BUFFER_SIZE);
        count = ESE_MAX_BUFFER_SIZE;
    } else {
        TMS_ERR("Write error,count = %zu\n", count);
        return -EPERM;
    }

    /* malloc write buffer */
    write_buf = devm_kzalloc(ese->spi_dev, count, GFP_DMA | GFP_KERNEL);

    if (!write_buf) {
        return -ENOMEM;
    }

    memset(write_buf, 0x00, count);
    mutex_lock(&ese->write_mutex);

    if (copy_from_user(write_buf, buf, count)) {
        TMS_ERR("Copy from user space failed\n");
        ret = -EFAULT;
        goto err_release_write;
    }

    /* Write data */
    ret = spi_write(ese->client, write_buf, count);

    if (ret == 0) {
        ret = count;
    } else {
        TMS_ERR("SPI writer error = %d\n", ret);
        ret = -EIO;
        goto err_release_write;
    }

    tms_buffer_dump("Tx ->", write_buf, count);
err_release_write:
    mutex_unlock(&ese->write_mutex);
    devm_kfree(ese->spi_dev, write_buf);
    return ret;
}

static ssize_t ese_device_read(struct file *file, char *buf, size_t count,
                               loff_t *offset)
{
    int ret = -EIO;
    uint8_t *read_buf;
    struct ese_info *ese;
    ese = file->private_data;

    if (!ese) {
        TMS_ERR("eSE device no longer exists\n");
        return -ENODEV;
    }

    if (count > 0 && count < ESE_MAX_BUFFER_SIZE) {
    } else if (count > ESE_MAX_BUFFER_SIZE) {
        TMS_WARN("The read bytes[%zu] exceeded the buffer max size, count = %d\n",
                 count, ESE_MAX_BUFFER_SIZE);
        count = ESE_MAX_BUFFER_SIZE;
    } else {
        TMS_ERR("read error,count = %zu\n", count);
        return -EPERM;
    }

    /* malloc read buffer */
    read_buf = devm_kzalloc(ese->spi_dev, count, GFP_DMA | GFP_KERNEL);

    if (!read_buf) {
        return -ENOMEM;
    }

    memset(read_buf, 0x00, count);
    mutex_lock(&ese->read_mutex);

    ret = spi_read(ese->client, read_buf, count);

    if (ret == 0) {
        ret = count;
    } else {
        TMS_ERR("SPI read failed ret = %d\n", ret);
        ret = -EFAULT;
        goto err_release_read;
    }

    if (copy_to_user(buf, read_buf, count)) {
        TMS_ERR("Copy to user space failed\n");
        ret = -EFAULT;
        goto err_release_read;
    }

    tms_buffer_dump("Rx <-", read_buf, count);
err_release_read:
    mutex_unlock(&ese->read_mutex);
    devm_kfree(ese->spi_dev, read_buf);
    return ret;
}

static int ese_device_close(struct inode *inode, struct file *file)
{
    struct ese_info *ese = NULL;
    TMS_INFO("Close eSE device[%d-%d]\n", imajor(inode),
              iminor(inode));
    ese = ese_get_data(inode);

    if (!ese) {
        TMS_ERR("eSE device not exist\n");
        return -ENODEV;
    }

    file->private_data = NULL;
    return SUCCESS;
}

static int ese_device_open(struct inode *inode, struct file *file)
{
    struct ese_info *ese = NULL;

    TMS_INFO("eSE device number is %d-%d\n", imajor(inode),
              iminor(inode));
    ese = ese_get_data(inode);

    if (!ese) {
        TMS_ERR("eSE device not exist\n");
        return -ENODEV;
    }

    file->private_data = ese;
    return SUCCESS;
}

static const struct file_operations ese_fops = {
    .owner          = THIS_MODULE,
    .open           = ese_device_open,
    .release        = ese_device_close,
    .read           = ese_device_read,
    .write          = ese_device_write,
    .unlocked_ioctl = ese_device_ioctl,
};

/*********** PART3: eSE Driver Start Area ***********/
static int ese_device_probe(struct spi_device *client)
{
    int ret;
    struct ese_info *ese = NULL;
    TMS_INFO("Enter\n");

    TMS_DEBUG("chip select = %d , bus number = %d \n",
              client->chip_select, client->master->bus_num);
    /* step1 : alloc ese_info */
    ese = ese_data_alloc(&client->dev, ese);

    if (ese == NULL) {
        TMS_ERR("eSE info alloc memory error\n");
        return -ENOMEM;
    }

    /* step2 : init and binding parameters for easy operate */
    ese->client                = client;
    ese->spi_dev               = &client->dev;
    ese->client->mode          = SPI_MODE_0;
    ese->client->bits_per_word = 8;
    ese->dev.fops              = &ese_fops;
    /* step3 : register common ese */
    ret = ese_common_info_init(ese);

    if (ret) {
        TMS_ERR("Init common eSE device failed\n");
        goto err_free_ese_malloc;
    }

    /* step4 : setup spi */
    ret = spi_setup(ese->client);

    if (ret < 0) {
        TMS_ERR("Failed to perform SPI setup\n");
        goto err_free_ese_info;
    }

    /* step5 : init mutex and queues */
    init_waitqueue_head(&ese->read_wq);
    mutex_init(&ese->read_mutex);
    mutex_init(&ese->write_mutex);

    /* step6 : register ese device */
    if (!ese->tms->registe_device) {
        TMS_ERR("ese->tms->registe_device is NULL\n");
        ret = -ERROR;
        goto err_destroy_mutex;
    }
    ret = ese->tms->registe_device(&ese->dev, ese);

    if (ret) {
        TMS_ERR("eSE device register failed\n");
        goto err_destroy_mutex;
    }

    spi_set_drvdata(client, ese);
    TMS_INFO("Successfully\n");
    return SUCCESS;
err_destroy_mutex:
    mutex_destroy(&ese->read_mutex);
    mutex_destroy(&ese->write_mutex);
err_free_ese_info:
    ese_gpio_release(ese);
err_free_ese_malloc:
    ese_data_free(&client->dev, ese);
    TMS_ERR("Failed, ret = %d\n", ret);
    return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,18,0)
static int ese_device_remove(struct spi_device *client)
#else
static void ese_device_remove(struct spi_device *client)
#endif
{
    struct ese_info *ese;

    ese = spi_get_drvdata(client);

    if (!ese) {
        TMS_ERR("eSE device no longer exists\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,18,0)
        return -ENODEV;
#else
        return;
#endif
    }

    mutex_destroy(&ese->read_mutex);
    mutex_destroy(&ese->write_mutex);
    ese_gpio_release(ese);
    if (ese->tms->unregiste_device) {
        ese->tms->unregiste_device(&ese->dev);
    }
    ese_data_free(&client->dev, ese);
    spi_set_drvdata(client, NULL);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,18,0)
    return SUCCESS;
#endif
}

static const struct spi_device_id ese_device_id[] = {
    {ESE_DEVICE, 0 },
    { }
};

static struct of_device_id ese_match_table[] = {
    { .compatible = ESE_DEVICE, },
    { }
};

static struct spi_driver ese_spi_driver = {
    .probe    = ese_device_probe,
    .remove   = ese_device_remove,
    .id_table = ese_device_id,
    .driver   = {
        .owner          = THIS_MODULE,
        .name           = ESE_DEVICE,
        .of_match_table = ese_match_table,
        .probe_type     = PROBE_PREFER_ASYNCHRONOUS,
    },
};

int ese_driver_init(void)
{
    int ret;
    TMS_INFO("Loading eSE driver\n");
    ret = spi_register_driver(&ese_spi_driver);

    if (ret) {
        TMS_ERR("Unable to register spi driver, ret = %d\n", ret);
    }

    return ret;
}

void ese_driver_exit(void)
{
    TMS_INFO("Unloading eSE driver\n");
    spi_unregister_driver(&ese_spi_driver);
}

MODULE_DESCRIPTION("TMS eSE Driver");
MODULE_LICENSE("GPL");
