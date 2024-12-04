// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 TsingTeng MicroSystem Co., Ltd.
 */

#include "nfc_common.h"
#include "../../../../include/linux/pinctrl/qcom-pinctrl.h"

/*********** PART0: Global Variables Area ***********/

/*********** PART1: Function Area ***********/
struct nfc_info *nfc_data_alloc(struct device *dev, struct nfc_info *nfc)
{
    nfc = devm_kzalloc(dev, sizeof(struct nfc_info), GFP_KERNEL);
    return nfc;
}

void nfc_data_free(struct device *dev, struct nfc_info *nfc)
{
    if (nfc) {
        devm_kfree(dev, nfc);
    }

    nfc = NULL;
}

struct nfc_info *nfc_get_data(struct inode *inode)
{
    struct nfc_info *nfc;
    struct dev_register *char_dev;
    char_dev = container_of(inode->i_cdev, struct dev_register, chrdev);
    nfc = container_of(char_dev, struct nfc_info, dev);
    return nfc;
}

void nfc_hard_reset(struct nfc_info *nfc)
{
    if (!nfc->tms->set_gpio) {
        TMS_ERR("nfc->tms->set_gpio is NULL");
        return;
    }

    nfc->tms->set_gpio(nfc->hw_res.ven_gpio, OFF, WAIT_TIME_20000US, WAIT_TIME_20000US);
    nfc->tms->set_gpio(nfc->hw_res.ven_gpio, ON, WAIT_TIME_NONE, WAIT_TIME_20000US);
}
static bool nfc_write(struct i2c_client *client, const uint8_t *cmd, size_t len)
{
    int count;
    ssize_t ret;

    for (count = 0; count < RETRY_TIMES; count++) {
        ret = i2c_master_send(client, cmd, len);
        if (ret == len) {
            tms_buffer_dump("Tx ->", cmd, len);
            return true;
        }

        TMS_ERR("Error writting: %zd\n", ret);
    }

    return false;
}

static bool nfc_read_header(struct i2c_client *client, unsigned int irq_gpio,
                            uint8_t *header, size_t header_len)
{
    ssize_t ret;
    int retry = 10;

    do {
        if (gpio_get_value(irq_gpio)) {
            break;
        }

        TMS_DEBUG("Wait for data...\n");
        usleep_range(WAIT_TIME_1000US, WAIT_TIME_1000US + 100);
    } while (retry--);

    ret = i2c_master_recv(client, header, header_len);
    if (ret == header_len) {
        tms_buffer_dump("Rx <-", header, header_len);
        return true;
    }

    TMS_ERR("Error reading header: %zd\n", ret);
    return false;
}

static bool nfc_read_payload(struct i2c_client *client, unsigned int irq_gpio,
                             uint8_t *payload, size_t payload_len)
{
    ssize_t ret;
    int retry = 10;
    size_t read_len = (payload_len == 1) ? payload_len + 1 : payload_len;

    do {
        if (gpio_get_value(irq_gpio)) {
            break;
        }

        TMS_DEBUG("Wait for data...\n");
        usleep_range(WAIT_TIME_1000US, WAIT_TIME_1000US + 100);
    } while (retry--);

    ret = i2c_master_recv(client, payload, read_len);
    if (ret == read_len) {
        tms_buffer_dump("Rx <-", payload, payload_len);
        return true;
    }

    TMS_ERR("Error reading payload: %zd\n", ret);
    return false;
}

void nfc_jump_fw(struct i2c_client *client, unsigned int irq_gpio)
{
    const uint8_t core_reset[] = {0x20, 0x00, 0x01, 0x00};
    const uint8_t chk_rsp_hdr[] = {0x40, 0x00, 0x01};
    uint8_t rsp_hdr[NCI_HDR_LEN] = {0};
    uint8_t rsp_payload[MAX_NCI_PAYLOAD_LEN] = {0};
    /* It is possible to receive up to two times and redundant once */
    int retry = 2;

    if (!nfc_write(client, core_reset, sizeof(core_reset))) {
        TMS_ERR("send core_reset error\n");
        return;
    }

    do {
        if (!nfc_read_header(client, irq_gpio, rsp_hdr, NCI_HDR_LEN)) {
            return;
        }

        if (!nfc_read_payload(client, irq_gpio, rsp_payload, rsp_hdr[HEAD_PAYLOAD_BYTE])) {
            TMS_ERR("Read core_reset rsp payload error\n");
            return;
        }

        if ((!memcmp(rsp_hdr, chk_rsp_hdr, NCI_HDR_LEN)) && rsp_payload[0] == 0xFE) {
            usleep_range(WAIT_TIME_10000US, WAIT_TIME_10000US + 100);
            /* If an NTF is reported by the FW after hard reset, it needs to be discarded */
            retry = 1;
            continue;
        } else if ((!memcmp(rsp_hdr, chk_rsp_hdr, NCI_HDR_LEN)) && rsp_payload[0] == 0x00) {
            /* Core reset NTF needs to be received in FW */
            retry = 1;
            continue;
        } else if ((!memcmp(rsp_hdr, chk_rsp_hdr, NCI_HDR_LEN)) && rsp_payload[0] == 0xFF) {
            TMS_ERR("Failed, need upgrade the firmware\n");
            break;
        }
    } while (retry--);
}
void nfc_disable_irq(struct nfc_info *nfc)
{
    unsigned long flag;
    spin_lock_irqsave(&nfc->irq_enable_slock, flag);

    if (nfc->irq_enable) {
        disable_irq_nosync(nfc->client->irq);
        nfc->irq_enable = false;
    }

    spin_unlock_irqrestore(&nfc->irq_enable_slock, flag);
}

void nfc_enable_irq(struct nfc_info *nfc)
{
    unsigned long flag;
    spin_lock_irqsave(&nfc->irq_enable_slock, flag);

    if (!nfc->irq_enable) {
        enable_irq(nfc->client->irq);
        nfc->irq_enable = true;
    }

    spin_unlock_irqrestore(&nfc->irq_enable_slock, flag);
}

static irqreturn_t nfc_irq_handler(int irq, void *dev_id)
{
    struct nfc_info *nfc;
    nfc = dev_id;

    if (device_may_wakeup(nfc->i2c_dev)) {
        pm_wakeup_event(nfc->i2c_dev, WAKEUP_SRC_TIMEOUT);
    }

    nfc_disable_irq(nfc);
    wake_up(&nfc->read_wq);
    return IRQ_HANDLED;
}

int nfc_irq_register(struct nfc_info *nfc)
{
    int ret;

    nfc->client->irq = gpio_to_irq(nfc->hw_res.irq_gpio);

    if (nfc->client->irq < 0) {
        TMS_ERR("Get soft irq number failed");
        return -ERROR;
    }

    ret = devm_request_irq(nfc->i2c_dev, nfc->client->irq, nfc_irq_handler,
                           IRQF_TRIGGER_HIGH, nfc->dev.name, nfc);

    if (ret) {
        TMS_ERR("Register irq failed, ret = %d\n", ret);
        return ret;
    }

    TMS_INFO("Register NFC IRQ[%d]\n", nfc->client->irq);
    return SUCCESS;
}

void nfc_power_control(struct nfc_info *nfc, bool state)
{
    if (!nfc->tms->set_gpio) {
        TMS_ERR("nfc->tms->set_gpio is NULL");
        return;
    }

    if (state == ON) {
        nfc_enable_irq(nfc);
        nfc->tms->set_gpio(nfc->hw_res.ven_gpio, ON, WAIT_TIME_NONE, WAIT_TIME_NONE);
        nfc->tms->ven_enable = true;
    } else if (state == OFF) {
        nfc_disable_irq(nfc);
        nfc->tms->set_gpio(nfc->hw_res.ven_gpio, OFF, WAIT_TIME_NONE, WAIT_TIME_NONE);
        nfc->tms->ven_enable = false;
    }
}

void nfc_fw_download_control(struct nfc_info *nfc, bool state)
{
     if (!nfc->dlpin_flag) {
        return;
    }
    if (!nfc->tms->set_gpio) {
        TMS_ERR("nfc->tms->set_gpio is NULL");
        return;
    }

    if (state == ON) {
        nfc->tms->set_gpio(nfc->hw_res.download_gpio, ON, WAIT_TIME_NONE, WAIT_TIME_10000US);
    } else if (state == OFF) {
        nfc->tms->set_gpio(nfc->hw_res.download_gpio, OFF, WAIT_TIME_NONE, WAIT_TIME_10000US);
    }
}

static int ese_power_control(struct nfc_info *nfc, bool state)
{
    if (!nfc->tms->set_gpio) {
        TMS_ERR("nfc->tms->set_gpio is NULL");
        return -ERROR;
    }

    if (state == ON) {
        nfc->tms->ven_enable = gpio_get_value(nfc->hw_res.ven_gpio);

        if (!nfc->tms->ven_enable) {
            nfc->tms->set_gpio(nfc->hw_res.ven_gpio, ON, WAIT_TIME_NONE, WAIT_TIME_NONE);
        }
    } else if (state == OFF) {
        if (!nfc->tms->ven_enable) {
            nfc->tms->set_gpio(nfc->hw_res.ven_gpio, OFF, WAIT_TIME_NONE, WAIT_TIME_NONE);
        }
    }

    return SUCCESS;
}

int nfc_ioctl_set_ese_state(struct nfc_info *nfc, unsigned long arg)
{
    int ret;
    TMS_DEBUG("arg = %lu\n", arg);

    switch (arg) {
    case REQUEST_ESE_POWER_ON:
        ret = ese_power_control(nfc, ON);
        break;

    case REQUEST_ESE_POWER_OFF:
        ret = ese_power_control(nfc, OFF);
        break;

    default:
        TMS_ERR("Bad control arg %lu\n", arg);
        ret = -ENOIOCTLCMD;
        break;
    }

    return ret;
}

int nfc_ioctl_get_ese_state(struct nfc_info *nfc, unsigned long arg)
{
    int ret;
    TMS_DEBUG("arg = %lu\n", arg);

    switch (arg) {
    case REQUEST_ESE_POWER_STATE:
        ret = gpio_get_value(nfc->hw_res.ven_gpio);
        break;

    default:
        TMS_ERR("Bad control arg %lu\n", arg);
        ret = -ENOIOCTLCMD;
        break;
    }

    return ret;
}

void nfc_gpio_release(struct nfc_info *nfc)
{
    gpio_free(nfc->hw_res.irq_gpio);
    gpio_free(nfc->hw_res.ven_gpio);
    if (nfc->dlpin_flag) {
        gpio_free(nfc->hw_res.download_gpio);
    }
}

static int nfc_gpio_configure_init(struct nfc_info *nfc)
{
    int ret;

    if (gpio_is_valid(nfc->hw_res.irq_gpio)) {
        ret = gpio_direction_input(nfc->hw_res.irq_gpio);

        if (ret < 0) {
            TMS_ERR("Unable to set irq gpio as input\n");
            return ret;
        }
    }

    if (gpio_is_valid(nfc->hw_res.ven_gpio)) {
        ret = gpio_direction_output(nfc->hw_res.ven_gpio, nfc->hw_res.ven_flag);

        if (ret < 0) {
            TMS_ERR("Unable to set ven gpio as output\n");
            return ret;
        }
    }

    if (gpio_is_valid(nfc->hw_res.download_gpio)) {
        ret = gpio_direction_output(nfc->hw_res.download_gpio,
                                    nfc->hw_res.download_flag);

        if (ret < 0) {
            TMS_ERR("Unable to set download_gpio as output\n");
            return ret;
        }
    }

    return SUCCESS;
}

static int nfc_platform_clk_init(struct nfc_info *nfc)
{
    nfc->clk = devm_clk_get(nfc->i2c_dev, "clk_aux");

    if (IS_ERR(nfc->clk)) {
        TMS_ERR("Platform clock not specified\n");
        return -ERROR;
    }

    nfc->clk_parent = devm_clk_get(nfc->i2c_dev, "source");

    if (IS_ERR(nfc->clk_parent)) {
        TMS_ERR("Clock parent not specified\n");
        return -ERROR;
    }

    clk_set_parent(nfc->clk, nfc->clk_parent);
    clk_set_rate(nfc->clk, 26000000);
    nfc->clk_enable = devm_clk_get(nfc->i2c_dev, "enable");

    if (IS_ERR(nfc->clk_enable)) {
        TMS_ERR("Clock enable not specified\n");
        return -ERROR;
    }

    clk_prepare_enable(nfc->clk);
    clk_prepare_enable(nfc->clk_enable);
    return SUCCESS;
}

static int nfc_parse_dts_init(struct nfc_info *nfc)
{
    int ret, rcv;
    struct device_node *np;

    np = nfc->i2c_dev->of_node;
    rcv = of_property_read_string(np, "tms,device-name", &nfc->dev.name);

    if (rcv < 0) {
        nfc->dev.name = "tms_nfc";
        TMS_WARN("device-name not specified, set default\n");
    }

    rcv = of_property_read_u32(np, "tms,device-count", &nfc->dev.count);

    if (rcv < 0) {
        nfc->dev.count = 1;
        TMS_WARN("device-count not specified, set default\n");
    }

    nfc->hw_res.irq_gpio = of_get_named_gpio(np, "tms,irq-gpio", 0);

    if (gpio_is_valid(nfc->hw_res.irq_gpio)) {
        rcv = gpio_request(nfc->hw_res.irq_gpio, "nfc_int");

        if (rcv) {
            TMS_WARN("Unable to request gpio[%d] as IRQ\n",
                     nfc->hw_res.irq_gpio);
        }
    } else {
        TMS_ERR("Irq gpio not specified\n");
        return -EINVAL;
    }

    nfc->hw_res.ven_gpio = of_get_named_gpio_flags(np, "tms,ven-gpio", 0,
                           &nfc->hw_res.ven_flag);

    if (gpio_is_valid(nfc->hw_res.ven_gpio)) {
        rcv = gpio_request(nfc->hw_res.ven_gpio, "nfc_ven");

        if (rcv) {
            TMS_WARN("Unable to request gpio[%d] as VEN\n",
                     nfc->hw_res.ven_gpio);
        }
    } else {
        TMS_ERR("Ven gpio not specified\n");
        ret =  -EINVAL;
        goto err_free_irq;
    }

    nfc->hw_res.download_gpio = of_get_named_gpio_flags(np, "tms,download-gpio", 0,
                                &nfc->hw_res.download_flag);

    if (gpio_is_valid(nfc->hw_res.download_gpio)) {
        nfc->dlpin_flag = true;
        rcv = gpio_request(nfc->hw_res.download_gpio, "nfc_fw_download");

        if (rcv) {
            TMS_WARN("Unable to request gpio[%d] as FWDownLoad\n",
                     nfc->hw_res.download_gpio);
        }
    } else {
        nfc->dlpin_flag = true;
        TMS_ERR("FW-Download gpio not specified\n");
    }

    TMS_DEBUG("NFC device name is %s, count = %d\n", nfc->dev.name,
              nfc->dev.count);
    TMS_INFO("irq_gpio = %d, ven_gpio = %d, download_gpio = %d\n",
             nfc->hw_res.irq_gpio, nfc->hw_res.ven_gpio, nfc->hw_res.download_gpio);
    return SUCCESS;
err_free_irq:
    gpio_free(nfc->hw_res.irq_gpio);
    TMS_ERR("Failed, ret = %d\n", ret);
    return ret;
}

int nfc_common_info_init(struct nfc_info *nfc)
{
    int ret;
    TMS_INFO("Enter\n");
    /* step1 : binding tms common data */
    nfc->tms = tms_common_data_binding();

    if (nfc->tms == NULL) {
        TMS_ERR("Get tms common info  error\n");
        return -ENOMEM;
    }

    /* step2 : dts parse */
    ret = nfc_parse_dts_init(nfc);
    msm_gpio_mpm_wake_set(6,1);

    if (ret) {
        TMS_ERR("Parse dts failed.\n");
        return ret;
    }

    /* step3 : set platform clock */
    ret = nfc_platform_clk_init(nfc);

    if (ret) {
        TMS_WARN("Not set platform clock\n");
    }

    /* step4 : set gpio work mode */
    ret = nfc_gpio_configure_init(nfc);

    if (ret) {
        TMS_ERR("Init gpio control failed.\n");
        goto err_free_gpio;
    }

    /* step5 : binding common function */
    nfc->tms->hw_res.ven_gpio = nfc->hw_res.ven_gpio;
    nfc->tms->ven_enable      = false;
    TMS_INFO("Successfully\n");
    return SUCCESS;
err_free_gpio:
    nfc_gpio_release(nfc);
    TMS_ERR("Failed, ret = %d\n", ret);
    return ret;
}
