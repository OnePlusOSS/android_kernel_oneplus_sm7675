// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 TsingTeng MicroSystem Co., Ltd.
 */

#include "ese_common.h"

/*********** PART0: Global Variables Area ***********/

/*********** PART1: Function Area ***********/
struct ese_info *ese_data_alloc(struct device *dev, struct ese_info *ese)
{
    ese = devm_kzalloc(dev, sizeof(struct ese_info), GFP_KERNEL);
    return ese;
}

void ese_data_free(struct device *dev, struct ese_info *ese)
{
    if (ese) {
        devm_kfree(dev, ese);
    }

    ese = NULL;
}

struct ese_info *ese_get_data(struct inode *inode)
{
    struct ese_info *ese;
    struct dev_register *char_dev;
    char_dev = container_of(inode->i_cdev, struct dev_register, chrdev);
    ese = container_of(char_dev, struct ese_info, dev);
    return ese;
}

void ese_hard_reset(struct ese_info *ese)
{
    if (!ese->tms->feature.indept_se_support) {
        return;
    }

    if (!ese->hw_res.rst_gpio) {
        TMS_ERR("ese rst_gpio is NULL\n");
        return;
    }

    ese->tms->set_gpio(ese->hw_res.rst_gpio, ON, WAIT_TIME_NONE, WAIT_TIME_500US);
    ese->tms->set_gpio(ese->hw_res.rst_gpio, OFF, WAIT_TIME_NONE, WAIT_TIME_500US);
    /* WAIT_TIME_5000US is BL wait time */
    ese->tms->set_gpio(ese->hw_res.rst_gpio, ON, WAIT_TIME_NONE, WAIT_TIME_5000US);
}

void ese_reset_control(struct ese_info *ese, bool state)
{
    if (!ese->tms->set_gpio) {
        TMS_ERR("set_gpio is NULL\n");
        return;
    }

    if (state == ON) {
        ese->tms->set_gpio(ese->hw_res.rst_gpio, ON, WAIT_TIME_NONE, WAIT_TIME_NONE);
    } else if (state == OFF) {
        ese->tms->set_gpio(ese->hw_res.rst_gpio, OFF, WAIT_TIME_NONE, WAIT_TIME_NONE);
    }
}

void ese_gpio_release(struct ese_info *ese)
{
    if (ese->tms->feature.indept_se_support) {
        gpio_free(ese->hw_res.rst_gpio);
    }
}

static int ese_gpio_configure_init(struct ese_info *ese)
{
    int ret;

    if (!ese->tms->feature.indept_se_support) {
        return SUCCESS;
    }

    if (gpio_is_valid(ese->hw_res.rst_gpio)) {
        ret = gpio_direction_output(ese->hw_res.rst_gpio, ese->hw_res.rst_flag);

        if (ret < 0) {
            TMS_ERR("Unable to set rst_gpio as output\n");
            return ret;
        }
    }

    return SUCCESS;
}

static int ese_parse_dts_init(struct ese_info *ese)
{
    int ret, rcv;
    struct device_node *np;

    np = ese->spi_dev->of_node;
    ese->tms->feature.indept_se_support = of_property_read_bool(np, "indept_se_support");
    TMS_DEBUG("indept_se_support = %d\n", ese->tms->feature.indept_se_support);
    rcv = of_property_read_string(np, "tms,device-name", &ese->dev.name);

    if (rcv < 0) {
        ese->dev.name = "tms_ese";
        TMS_WARN("device-name not specified, set default\n");
    }

    rcv = of_property_read_u32(np, "tms,device-count", &ese->dev.count);

    if (rcv < 0) {
        ese->dev.count = 1;
        TMS_WARN("device-count not specified, set default\n");
    }

    if (ese->tms->feature.indept_se_support) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,3,0)
        ese->hw_res.rst_gpio = of_get_named_gpio_flags(np, "tms,reset-gpio", 0,
                                                       &ese->hw_res.rst_flag);
#else
        ese->hw_res.rst_gpio = of_get_named_gpio(np, "tms,reset-gpio", 0);
        ese->hw_res.rst_flag = 1;
#endif
        if (gpio_is_valid(ese->hw_res.rst_gpio)) {
            rcv = gpio_request(ese->hw_res.rst_gpio, "ese_rst");

            if (rcv) {
                TMS_WARN("Unable to request gpio[%d] as RST\n",
                         ese->hw_res.rst_gpio);
            }
        } else {
            TMS_ERR("Reset gpio not specified\n");
            ret =  -EINVAL;
            goto err;
        }

        TMS_INFO("rst_gpio = %d\n", ese->hw_res.rst_gpio);
    }

    TMS_DEBUG("ese device name is %s, count = %d\n", ese->dev.name,
              ese->dev.count);
    return SUCCESS;
err:
    TMS_ERR("Failed, ret = %d\n", ret);
    return ret;
}

int ese_common_info_init(struct ese_info *ese)
{
    int ret;
    TMS_INFO("Enter\n");
    /* step1 : binding tms common data */
    ese->tms = tms_common_data_binding();

    if (ese->tms == NULL) {
        TMS_ERR("Get tms common info  error\n");
        return -ENOMEM;
    }

    /* step2 : dts parse */
    ret = ese_parse_dts_init(ese);

    if (ret) {
        TMS_ERR("Parse dts failed.\n");
        return ret;
    }

    /* step3 : set gpio work mode */
    ret = ese_gpio_configure_init(ese);

    if (ret) {
        TMS_ERR("Init gpio control failed.\n");
        goto err_free_gpio;
    }

    TMS_INFO("Successfully\n");
    return ret;
err_free_gpio:
    ese_gpio_release(ese);
    TMS_DEBUG("Failed, ret = %d\n", ret);
    return ret;
}
