/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include "leds-aw2023.h"
#include <linux/workqueue.h>

#define AW2023_DRIVER_VERSION "V1.0.2"

/* register address */
#define AW2023_REG_RESET					0x00
#define AW2023_REG_GCR1						0x01
#define AW2023_REG_STATUS					0x02
#define AW2023_REG_PATST					0x03
#define AW2023_REG_GCR2						0x04
#define AW2023_REG_LEDEN					0x30
#define AW2023_REG_LCFG0					0x31
#define AW2023_REG_LCFG1					0x32
#define AW2023_REG_LCFG2					0x33
#define AW2023_REG_PWM0						0x34
#define AW2023_REG_PWM1						0x35
#define AW2023_REG_PWM2						0x36
#define AW2023_REG_LED0T0					0x37
#define AW2023_REG_LED0T1					0x38
#define AW2023_REG_LED0T2					0x39
#define AW2023_REG_LED1T0					0x3A
#define AW2023_REG_LED1T1					0x3B
#define AW2023_REG_LED1T2					0x3C
#define AW2023_REG_LED2T0					0x3D
#define AW2023_REG_LED2T1					0x3E
#define AW2023_REG_LED2T2					0x3F

/* register bits */
#define AW2023_CHIPID						0x09
#define AW2023_RESET_MASK					0x55
#define AW2023_CHIP_DISABLE_MASK			0x00
#define AW2023_CHIP_ENABLE_MASK				0x01
#define AW2023_LEN_ENABLE_MASK				0x01
#define AW2023_LED_ENABLE_MASK				0x07
#define AW2023_LED_DISABLE_MASK				0x00
#define AW2023_LED_CURRENT_MASK				0x0F
#define AW2023_LED_SYNC_MODE_MASK			0x80
#define AW2023_LED_BREATH_MODE_MASK			0x10
#define AW2023_LED_MANUAL_MODE_MASK			0x00
#define AW2023_LED_PWM_MASK					0xFF
#define AW2023_LED_TIME_HIGH_MASK			0xF0
#define AW2023_LED_TIME_LOW_MASK			0x0F
#define AW2023_LED_FADEIN_MODE_MASK			0x20
#define AW2023_LED_FADEOUT_MODE_MASK		0x40

/* aw2023 config */
#define AW_LED_RESET_DELAY					8
#define AW_LED_POWER_ON_DELAY 				2
#define AW_LED_POWER_OFF_DELAY				2
#define AW2023_VDD_MIN_UV					2600000
#define AW2023_VDD_MAX_UV					3300000
#define AW2023_VI2C_MIN_UV					1800000
#define AW2023_VI2C_MAX_UV					1800000

#define MAX_RISE_TIME_MS					15
#define MAX_HOLD_TIME_MS					15
#define MAX_FALL_TIME_MS					15
#define MAX_OFF_TIME_MS						15

/* aw2023 register read/write access*/
#define REG_NONE_ACCESS						0
#define REG_RD_ACCESS						1 << 0
#define REG_WR_ACCESS						1 << 1

#define AW2023_LED_TIME_SHIFT_MASK			4
#define AW2023_REG_MAX						0x7F
#define LED_MAX_NUM							3
#define LED_BRIGHTNESS_MAX					15
#define LED_SUPPORT_TYPE					"support"
#define LED_ESD_WORK_TIME					3

const unsigned char aw2023_reg_access[AW2023_REG_MAX] = {
	[AW2023_REG_RESET]  = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_GCR1]   = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_STATUS] = REG_RD_ACCESS,
	[AW2023_REG_PATST]  = REG_RD_ACCESS,
	[AW2023_REG_GCR2]   = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_LEDEN]  = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_LCFG0]  = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_LCFG1]  = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_LCFG2]  = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_PWM0]   = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_PWM1]   = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_PWM2]   = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_LED0T0] = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_LED0T1] = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_LED0T2] = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_LED1T0] = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_LED1T1] = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_LED1T2] = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_LED2T0] = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_LED2T1] = REG_RD_ACCESS|REG_WR_ACCESS,
	[AW2023_REG_LED2T2] = REG_RD_ACCESS|REG_WR_ACCESS,
};

enum AW2023_LED_MODE{
	AW2023_LED_NONE = 0,
	AW2023_LED_CCMODE,
	AW2023_LED_BLINKMODE,
	AW2023_LED_BREATHMODE,
	AW2023_LED_MAXMODE,
};

enum AW2023_LED_TYPE {
	AW2023_LED_RED = 0,
	AW2023_LED_GREEN,
	AW2023_LED_BLUE,
	AW2023_LED_ON,
	AW2023_LED_OFF,
};

struct aw2023_led {
	struct i2c_client *client;
	struct led_classdev cdev;
	struct aw2023_platform_data *pdata;
	struct work_struct brightness_work;
	struct mutex lock;
	struct regulator *vdd;
	struct regulator *vcc;
	int num_leds;
	int id;
	bool poweron;
	bool esd_flag;
	struct delayed_work   aw2023_led_work;
	struct workqueue_struct *aw2023_led_wq;
};

struct aw2023_led *led_default;
unsigned int aw2023_debug = 2;

static int aw2023_write(struct aw2023_led *led, u8 reg, u8 val)
{
	int ret = -EINVAL, retry_times = 0;

	do {
		ret = i2c_smbus_write_byte_data(led->client, reg, val);
		retry_times ++;
		if(retry_times == 5)
			break;
	} while (ret < 0);

	return ret;
}

static int aw2023_read(struct aw2023_led *led, u8 reg, u8 *val)
{
	int ret = -EINVAL, retry_times = 0;

	do {
		ret = i2c_smbus_read_byte_data(led->client, reg);
		retry_times ++;
		if(retry_times == 5)
			break;
	} while (ret < 0);
	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}

static int aw2023_power_on(struct aw2023_led *led, bool on)
{
	int rc = 0;

	if (on) {
		if (!IS_ERR_OR_NULL(led->vdd)){
			rc = regulator_enable(led->vdd);
			if (rc) {
				dev_err(&led->client->dev,
					"Regulator vdd enable failed rc=%d\n", rc);
				return rc;
			}
		}
		if (!IS_ERR_OR_NULL(led->vcc)){
			rc = regulator_enable(led->vcc);
			if (rc) {
				dev_err(&led->client->dev,
					"Regulator vcc enable failed rc=%d\n", rc);
				goto fail_enable_vdd;
			}
		}
		led->poweron = true;
		msleep(AW_LED_POWER_ON_DELAY);
	} else {
		if (!IS_ERR_OR_NULL(led->vdd)){
			rc = regulator_disable(led->vdd);
			if (rc) {
				dev_err(&led->client->dev,
					"Regulator vdd disable failed rc=%d\n", rc);
				return rc;
			}
		}
		if (!IS_ERR_OR_NULL(led->vcc)){
			rc = regulator_disable(led->vcc);
			if (rc) {
				dev_err(&led->client->dev,
					"Regulator vcc disable failed rc=%d\n", rc);
				goto fail_disable_vdd;
			}
		}
		led->poweron = false;
		msleep(AW_LED_POWER_OFF_DELAY);
	}
	return rc;
fail_enable_vdd:
	rc = regulator_disable(led->vdd);
	if (rc)
		dev_err(&led->client->dev,
			"Regulator vdd disable failed rc=%d\n", rc);
	return rc;

fail_disable_vdd:
	rc = regulator_enable(led->vdd);
	if (rc)
		dev_err(&led->client->dev,
			"Regulator vdd enable failed rc=%d\n", rc);
	return rc;
}

static int aw2023_power_init(struct aw2023_led *led, bool on)
{
	int rc = 0;

	if (on) {
		led->vdd = regulator_get(&led->client->dev, "vdd");
		if (IS_ERR(led->vdd)) {
			rc = PTR_ERR(led->vdd);
			dev_err(&led->client->dev,
				"Regulator get failed vdd rc=%d\n", rc);
			return rc;
		}

		if (regulator_count_voltages(led->vdd) > 0) {
			rc = regulator_set_voltage(led->vdd, AW2023_VDD_MIN_UV,
						AW2023_VDD_MAX_UV);
			if (rc) {
				dev_err(&led->client->dev,
					"Regulator set_vtg failed vdd rc=%d\n",
					rc);
				goto reg_vdd_put;
			}
		}

		led->vcc = regulator_get(&led->client->dev, "vcc");
		if (IS_ERR(led->vcc)) {
			rc = PTR_ERR(led->vcc);
			dev_err(&led->client->dev,
				"Regulator get failed vcc rc=%d\n", rc);
			goto reg_vdd_set_vtg;
		}

		if (regulator_count_voltages(led->vcc) > 0) {
			rc = regulator_set_voltage(led->vcc, AW2023_VI2C_MIN_UV,
						AW2023_VI2C_MAX_UV);
			if (rc) {
				dev_err(&led->client->dev,
				"Regulator set_vtg failed vcc rc=%d\n", rc);
				goto reg_vcc_put;
			}
		}
	} else {
		if (regulator_count_voltages(led->vdd) > 0)
			regulator_set_voltage(led->vdd, 0, AW2023_VDD_MAX_UV);

		regulator_put(led->vdd);

		if (regulator_count_voltages(led->vcc) > 0)
			regulator_set_voltage(led->vcc, 0, AW2023_VI2C_MAX_UV);

		regulator_put(led->vcc);
	}
	return 0;

reg_vcc_put:
	regulator_put(led->vcc);
reg_vdd_set_vtg:
	if (regulator_count_voltages(led->vdd) > 0)
		regulator_set_voltage(led->vdd, 0, AW2023_VDD_MAX_UV);
reg_vdd_put:
	regulator_put(led->vdd);
	return rc;
}

static int aw2023_led_init_default(struct aw2023_led *led)
{
	int ret = 0;
	u8 enable = 0;
	u8 val = 0;
	AW2023_DEBUG("%s: brightness = %d, imax = %d, rise_time_ms = %d, hold_time_ms = %d, fall_time_ms = %d, off_time_ms= %d\n", __func__,
		led->cdev.brightness, led->pdata->imax, led->pdata->rise_time_ms,
		led->pdata->hold_time_ms, led->pdata->fall_time_ms, led->pdata->off_time_ms);

	/* enable aw2023 if disabled */
	aw2023_read(led, AW2023_REG_GCR1, &val);
	enable = val & AW2023_CHIP_ENABLE_MASK;

	if (enable == AW2023_CHIP_DISABLE_MASK) {
		ret = aw2023_write(led, AW2023_REG_GCR1, AW2023_CHIP_ENABLE_MASK);
		if (ret) {
			dev_err(&led->client->dev, "write AW2023_REG_GCR1 error = %d\n", ret);
			return ret;
		}
		msleep(2);
	}

	/* aw2023 led imax*/
	ret = aw2023_write(led, AW2023_REG_GCR2, led->pdata->imax);
	if (ret) {
		dev_err(&led->client->dev, "write AW2023_REG_GCR2 error = %d\n", ret);
		return ret;
	}

	/* aw2023 led pwm 255 */
	ret = aw2023_write(led, AW2023_REG_PWM0 + led->id, AW2023_LED_PWM_MASK);
	if (ret) {
		dev_err(&led->client->dev, "write AW2023_REG_PWM0 error = %d\n", ret);
		return ret;
	}

	/* aw2023 led breath time tr1 & ton */
	ret = aw2023_write(led, AW2023_REG_LED0T0 + led->id*LED_MAX_NUM,
			(led->pdata->rise_time_ms << AW2023_LED_TIME_SHIFT_MASK | led->pdata->hold_time_ms));
	if (ret) {
		dev_err(&led->client->dev, "breath time tr1 & ton error = %d\n", ret);
		return ret;
	}

	/* aw2023 led breath time tf1 & toff */
	ret = aw2023_write(led, AW2023_REG_LED0T1 + led->id*LED_MAX_NUM,
		(led->pdata->fall_time_ms << AW2023_LED_TIME_SHIFT_MASK | led->pdata->off_time_ms));
	if (ret) {
		dev_err(&led->client->dev, "breath time tf1 & toff error = %d\n", ret);
		return ret;
	}

	/* aw2023 set led_mdoe*/
	led->pdata->led_mode = AW2023_LED_BREATH_MODE_MASK;

	return ret;
}

static int aw2023_led_change_mode(struct aw2023_led *led, enum AW2023_LED_MODE mode)
{
	int ret = 0;
	switch(mode) {
		case AW2023_LED_CCMODE:
			led->pdata->led_mode = AW2023_LED_MANUAL_MODE_MASK;
			break;
		case AW2023_LED_BLINKMODE:
			led->pdata->hold_time_ms = 4;
			led->pdata->off_time_ms =  4;
			led->pdata->rise_time_ms = 0;
			led->pdata->fall_time_ms = 0;
			led->pdata->led_mode = AW2023_LED_BREATH_MODE_MASK;
			break;
		case AW2023_LED_BREATHMODE:
			led->pdata->hold_time_ms = 0;
			led->pdata->off_time_ms =  0;
			led->pdata->rise_time_ms = 6;
			led->pdata->fall_time_ms = 6;
			led->pdata->led_mode = AW2023_LED_BREATH_MODE_MASK;
			break;
		default:
			led->pdata->led_mode = AW2023_LED_MANUAL_MODE_MASK;
			break;
	}
	return ret;
}

static void aw2023_brightness(struct aw2023_led *led)
{
	u8 i = 0;
	u8 val = 0;
	u8 enable = 0;
	u8 state = AW2023_LED_OFF;

	mutex_lock(&led->pdata->led->lock);

	if (!led->pdata->led->poweron) {
		if (aw2023_power_on(led->pdata->led, true)) {
			dev_err(&led->pdata->led->client->dev, "power on failed");
			mutex_unlock(&led->pdata->led->lock);
			return;
		}
	}

	/* enable aw2023 if disabled */
	aw2023_read(led, AW2023_REG_GCR1, &val);
	enable = val & AW2023_CHIP_ENABLE_MASK;

	if (enable == AW2023_CHIP_DISABLE_MASK) {
		aw2023_write(led, AW2023_REG_GCR1, AW2023_CHIP_ENABLE_MASK);
		msleep(2);
	} else {
		enable =0;
	}

	/*aw2023 led breath time*/
	if(led->pdata->led_mode & AW2023_LED_BREATH_MODE_MASK) {
		/* aw2023 led breath time tr1 & ton */
		aw2023_write(led, AW2023_REG_LED0T0 + led->id*LED_MAX_NUM,
			(led->pdata->rise_time_ms << AW2023_LED_TIME_SHIFT_MASK | led->pdata->hold_time_ms));
		aw2023_write(led, AW2023_REG_LED0T1 + led->id*LED_MAX_NUM,
			(led->pdata->fall_time_ms << AW2023_LED_TIME_SHIFT_MASK | led->pdata->off_time_ms));
	}

	/* aw2023 led mode & current */
	if (led->cdev.brightness > 0) {
		state = AW2023_LED_ON;
		if (led->cdev.brightness > LED_BRIGHTNESS_MAX)
			led->cdev.brightness = LED_BRIGHTNESS_MAX;
		/* aw2023 led chanel enable*/
		aw2023_write(led, AW2023_REG_PWM0 + led->id, AW2023_LED_PWM_MASK);
		aw2023_write(led, AW2023_REG_LEDEN, AW2023_LED_ENABLE_MASK);
	}
	aw2023_write(led, AW2023_REG_LCFG0 + led->id,(led->pdata->led_mode | led->cdev.brightness));

	/*aw2023 led sync mode*/
	if(led->id == AW2023_LED_BLUE) {
		aw2023_read(led, AW2023_REG_LCFG0 + i, &val);
		if((led_default[AW2023_LED_RED].pdata->led_mode == led_default[AW2023_LED_GREEN].pdata->led_mode)
			&& (led_default[AW2023_LED_RED].pdata->led_mode == led_default[AW2023_LED_BLUE].pdata->led_mode))
			aw2023_write(led, AW2023_REG_LCFG0, AW2023_LED_SYNC_MODE_MASK | val);
		else
			aw2023_write(led, AW2023_REG_LCFG0, val & (~AW2023_LED_SYNC_MODE_MASK));

	/*  all led disabled, set led chip disabled */
		for(i=0;i<LED_MAX_NUM;i++) {
			aw2023_read(led, AW2023_REG_LCFG0 + i, &val);
			enable |= val & AW2023_LED_CURRENT_MASK;
		}
		if(enable == 0) {
			aw2023_write(led, AW2023_REG_GCR1, AW2023_CHIP_DISABLE_MASK);
		}
	}

	AW2023_DEBUG("%s: brightness[%d] = %x led_mode[%d]=%x enable= %d\n", __func__, led->id, led->cdev.brightness, led->id, led->pdata->led_mode, enable);
	mutex_unlock(&led->pdata->led->lock);
}

static void aw2023_brightness_work(struct work_struct *work)
{
	struct aw2023_led *led = container_of(work, struct aw2023_led,
					brightness_work);
	aw2023_brightness(led);
}

static void aw2023_set_brightness(struct led_classdev *cdev,
				enum led_brightness brightness)
{
	struct aw2023_led *led = container_of(cdev, struct aw2023_led, cdev);
	led->cdev.brightness = brightness;

	if(led->cdev.trigger != NULL)
	{
		if(strcmp(led->cdev.trigger->name, "timer") == 0)
		{
			aw2023_led_change_mode(led, AW2023_LED_BLINKMODE);
			AW2023_DEBUG("%s[%d]: trigger = %s\n", __func__, led->id, led->cdev.trigger->name);
		}
	}
	schedule_work(&led->brightness_work);
}

static ssize_t aw2023_led_support_attr_show (struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);

	return snprintf(buf, PAGE_SIZE, "%s-%d\n", LED_SUPPORT_TYPE, led->cdev.max_brightness);
}

static ssize_t aw2023_led_breath_on_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);

	return snprintf(buf, PAGE_SIZE, "%d (max:15)\n", led->pdata->rise_time_ms);
}

static ssize_t aw2023_led_breath_on_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);
	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &data);
	if (ret)
		return ret;

	mutex_lock(&led->pdata->led->lock);
	led->pdata->rise_time_ms = data;
	mutex_unlock(&led->pdata->led->lock);

	AW2023_DEBUG("%s[%d]: rise_time_ms = %d (max:15)\n", __func__, led->id, led->pdata->rise_time_ms);

	return cnt;
}

static ssize_t aw2023_led_breath_off_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);

	return snprintf(buf, PAGE_SIZE, "%d (max:15)\n", led->pdata->fall_time_ms);
}

static ssize_t aw2023_led_breath_off_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);
	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &data);
	if (ret)
		return ret;

	mutex_lock(&led->pdata->led->lock);
	led->pdata->fall_time_ms = data;
	mutex_unlock(&led->pdata->led->lock);

	AW2023_DEBUG("%s[%d]: fall_time_ms = %d (max:15)\n", __func__, led->id, led->pdata->fall_time_ms);
	return cnt;
}

static ssize_t aw2023_led_ton_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);

	return snprintf(buf, PAGE_SIZE, "%d (max:15)\n", led->pdata->hold_time_ms);
}

static ssize_t aw2023_led_ton_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);

	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &data);
	if (ret)
		return ret;

	mutex_lock(&led->pdata->led->lock);
	led->pdata->hold_time_ms = data;
	mutex_unlock(&led->pdata->led->lock);
	AW2023_DEBUG("%s[%d]: hold_time_ms = %d (max:15)\n", __func__, led->id, led->pdata->hold_time_ms);

	return cnt;
}

static ssize_t aw2023_led_tr1_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);

	return snprintf(buf, PAGE_SIZE, "%d (max:15)\n", led->pdata->rise_time_ms);
}

static ssize_t aw2023_led_tr1_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);

	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &data);
	if (ret)
		return ret;

	mutex_lock(&led->pdata->led->lock);
	led->pdata->rise_time_ms = data;
	mutex_unlock(&led->pdata->led->lock);

	AW2023_DEBUG("%s[%d]: rise_time_ms = %d (max:15)\n", __func__, led->id, led->pdata->rise_time_ms);

	return cnt;
}

static ssize_t aw2023_led_tf1_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);

	return snprintf(buf, PAGE_SIZE, "%d (max:15)\n", led->pdata->fall_time_ms);

}

static ssize_t aw2023_led_tf1_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);

	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &data);
	if (ret)
		return ret;

	mutex_lock(&led->pdata->led->lock);
	led->pdata->fall_time_ms = data;
	mutex_unlock(&led->pdata->led->lock);

	AW2023_DEBUG("%s[%d]: fall_time_ms = %d (max:15)\n", __func__, led->id, led->pdata->fall_time_ms);

	return cnt;
}

static ssize_t aw2023_led_toff_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);

	return snprintf(buf, PAGE_SIZE, "%d (max:15)\n", led->pdata->off_time_ms);
}

static ssize_t aw2023_led_toff_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);

	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &data);
	if (ret)
		return ret;

	mutex_lock(&led->pdata->led->lock);
	led->pdata->off_time_ms = data;
	mutex_unlock(&led->pdata->led->lock);
	AW2023_DEBUG("%s[%d]: off_time_ms = %d (max:15)\n", __func__, led->id, led->pdata->off_time_ms);

	return cnt;
}

static ssize_t aw2023_reg_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);

	unsigned char i, reg_val;
	ssize_t len = 0;

	for(i=0; i < AW2023_REG_MAX; i++) {
		if(!(aw2023_reg_access[i] & REG_RD_ACCESS))
		continue;
		aw2023_read(led, i, &reg_val);
		len += snprintf(buf+len, PAGE_SIZE-len, "reg : 0x%02x = 0x%02x\n", i, reg_val);
	}

	return len;
}

static ssize_t aw2023_reg_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led =container_of(led_cdev, struct aw2023_led, cdev);

	unsigned int databuf[2];

	if(2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1]))
	{
		aw2023_write(led, (unsigned char)databuf[0], (unsigned char)databuf[1]);
	}

	return len;
}

static ssize_t aw2023_led_debug_attr_show (struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u (max:1)\n", aw2023_debug);
}

static ssize_t aw2023_led_debug_attr_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t cnt)
{
	unsigned long data = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2023_led *led = container_of(led_cdev, struct aw2023_led, cdev);
	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &data);
	if (ret)
		return ret;

	mutex_lock(&led->pdata->led->lock);
	aw2023_debug = data;
	mutex_unlock(&led->pdata->led->lock);

	AW2023_DEBUG("%s: aw2023_debug = %u (max:15)\n", __func__, aw2023_debug);

	return cnt;
}

static DEVICE_ATTR(support, 0664, aw2023_led_support_attr_show, NULL);
static DEVICE_ATTR(breath_on, 0664, aw2023_led_breath_on_attr_show,  aw2023_led_breath_on_attr_store);
static DEVICE_ATTR(breath_off, 0664, aw2023_led_breath_off_attr_show,  aw2023_led_breath_off_attr_store);
static DEVICE_ATTR(ton, 0664, aw2023_led_ton_attr_show, aw2023_led_ton_attr_store);
static DEVICE_ATTR(toff, 0664, aw2023_led_toff_attr_show, aw2023_led_toff_attr_store);
static DEVICE_ATTR(tr1, 0664, aw2023_led_tr1_attr_show, aw2023_led_tr1_attr_store);
static DEVICE_ATTR(tf1, 0664, aw2023_led_tf1_attr_show, aw2023_led_tf1_attr_store);
static DEVICE_ATTR(reg, 0664, aw2023_reg_show, aw2023_reg_store);
static DEVICE_ATTR(debug, 0664, aw2023_led_debug_attr_show, aw2023_led_debug_attr_store);

static struct attribute *aw2023_led_attributes[] = {
	&dev_attr_support.attr,
	&dev_attr_ton.attr,
	&dev_attr_toff.attr,
	&dev_attr_tr1.attr,
	&dev_attr_tf1.attr,
	&dev_attr_reg.attr,
	&dev_attr_debug.attr,
	NULL
};

static struct attribute_group aw2023_led_attr_group = {
	.attrs = aw2023_led_attributes,
};

static struct attribute *aw2023_led_breath_mode_attrs[] = {
	&dev_attr_breath_on.attr,
	&dev_attr_breath_off.attr,
	NULL
};

ATTRIBUTE_GROUPS(aw2023_led_breath_mode);

static int  aw2023_led_cc_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw2023_led *led = container_of(cdev, struct aw2023_led, cdev);

	AW2023_DEBUG("%s[%d]: activate", __func__, led->id);

	ret = aw2023_led_change_mode(led, AW2023_LED_CCMODE);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw2023_led_change_mode fail\n", __func__);
		return ret;
	}

	return ret;
}

static void  aw2023_led_cc_deactivate(struct led_classdev *cdev)
{
	struct aw2023_led *led = container_of(cdev, struct aw2023_led, cdev);

	AW2023_DEBUG("%s[%d]: deactivate", __func__, led->id);
}

static int  aw2023_led_blink_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw2023_led *led = container_of(cdev, struct aw2023_led, cdev);

	AW2023_DEBUG("%s[%d]: activate", __func__, led->id);

	ret = aw2023_led_change_mode(led, AW2023_LED_BLINKMODE);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw2023_led_change_mode fail\n", __func__);
		return ret;
	}

	return ret;
}

static void aw2023_led_blink_deactivate(struct led_classdev *cdev)
{
	struct aw2023_led *led = container_of(cdev, struct aw2023_led, cdev);

	AW2023_DEBUG("%s[%d]: deactivate", __func__, led->id);
}

static int aw2023_led_breath_activate(struct led_classdev *cdev)
{
	int ret = 0;
	struct aw2023_led *led = container_of(cdev, struct aw2023_led, cdev);

	AW2023_DEBUG("%s[%d]: activate", __func__, led->id);

	ret = aw2023_led_change_mode(led, AW2023_LED_BREATHMODE);
	if (ret < 0) {
		dev_err(led->cdev.dev, "%s: aw2023_led_change_mode fail\n", __func__);
		return ret;
	}
	return ret;
}

static void aw2023_led_breath_deactivate(struct led_classdev *cdev)
{
	struct aw2023_led *led = container_of(cdev, struct aw2023_led, cdev);

	AW2023_DEBUG("%s[%d]: deactivate", __func__, led->id);
}

static struct led_trigger aw2023_led_trigger[LED_MAX_NUM] = {
	{
		.name = "cc_mode",
		.activate = aw2023_led_cc_activate,
		.deactivate = aw2023_led_cc_deactivate,
	},
	{
		.name = "blink_mode",
		.activate = aw2023_led_blink_activate,
		.deactivate = aw2023_led_blink_deactivate,
	},
	{
		.name = "breath_mode",
		.activate = aw2023_led_breath_activate,
		.deactivate = aw2023_led_breath_deactivate,
		.groups = aw2023_led_breath_mode_groups,
	},
};


static int aw2023_check_chipid(struct aw2023_led *led)
{
	u8 val = 0;
	u8 cnt = 0;

	for(cnt = 5; cnt > 0; cnt --)
	{
		aw2023_read(led, AW2023_REG_RESET, &val);
		dev_notice(&led->client->dev,"aw2023 chip id %0x", val);
		if (val == AW2023_CHIPID)
			return 0;
	}
	return -EINVAL;
}

static int aw2023_init(struct aw2023_led *led)
{
	int ret = 0;
	int i = 0;

	for(i = 0; i < LED_MAX_NUM; i++) {
		ret = aw2023_led_init_default(&led[i]);
		if (ret) {
			dev_err(&led->client->dev, "aw2023_led_init_default : led[%d] error\n", i);
			return ret;
		}
		if(led_default->esd_flag)
		{
			aw2023_brightness(&led[i]);
		}
	}
	return ret;
}

static int aw2023_led_err_handle(struct aw2023_led *led_array,
				int parsed_leds)
{
	int i = 0;
	/*
	* If probe fails, cannot free resource of all LEDs, only free
	* resources of LEDs which have allocated these resource really.
	*/
	for (i = 0; i < parsed_leds; i++) {
		sysfs_remove_group(&led_array[i].cdev.dev->kobj,
				&aw2023_led_attr_group);
		led_classdev_unregister(&led_array[i].cdev);
		cancel_work_sync(&led_array[i].brightness_work);
		devm_kfree(&led_array->client->dev, led_array[i].pdata);
		led_array[i].pdata = NULL;
	}
	return i;
}

static void aw2023_work_func(struct work_struct *aw2023_work)
{
	u8 ret = 0;
	u8 val = 0;
	struct aw2023_led *led = container_of(aw2023_work, struct aw2023_led, aw2023_led_work.work);

	ret = aw2023_read(led, AW2023_REG_STATUS, &val);
	if(val) {
		dev_notice(&led->client->dev, "%s AW2023_REG_STATUS[%d] : [0x%x]\n", __func__, led->id, val);
		led_default->esd_flag = true;
		ret = aw2023_init(led_default);
		if (ret) {
			dev_err(&led->client->dev, "%s reset failed : [%d]\n", __func__, ret);
		}
	} else {
		led_default->esd_flag = false;
	}
	queue_delayed_work(led->aw2023_led_wq, &led->aw2023_led_work, 3 * HZ);
}

static int aw2023_led_parse_child_node(struct aw2023_led *led_array,
				struct device_node *node)
{
	struct aw2023_led *led;
	struct device_node *temp;
	struct aw2023_platform_data *pdata;
	int rc = 0;
	int parsed_leds = 0;
	int i = 0;

	for_each_child_of_node(node, temp) {
		led = &led_array[parsed_leds];
		led->client = led_array->client;

		pdata = devm_kzalloc(&led->client->dev,
				sizeof(struct aw2023_platform_data),
				GFP_KERNEL);
		if (!pdata) {
			dev_err(&led->client->dev,
				"Failed to allocate memory\n");
			parsed_leds--;
			goto free_pdata;
		}
		pdata->led = led_array;
		led->pdata = pdata;

		rc = of_property_read_string(temp, "aw2023,name",
			&led->cdev.name);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading led name, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw2023,id",
			&led->id);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading id, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw2023,imax",
			&led->pdata->imax);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading id, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_string(temp, "aw2023,led_default_trigger",
									&led->pdata->led_default_trigger);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure led_default_trigger, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw2023,brightness",
			&led->cdev.brightness);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading brightness, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw2023,max-brightness",
			&led->cdev.max_brightness);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading max-brightness, rc = %d\n",
				rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw2023,rise-time-ms",
			&led->pdata->rise_time_ms);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading rise-time-ms, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw2023,hold-time-ms",
			&led->pdata->hold_time_ms);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading hold-time-ms, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw2023,fall-time-ms",
			&led->pdata->fall_time_ms);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading fall-time-ms, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "aw2023,off-time-ms",
			&led->pdata->off_time_ms);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading off-time-ms, rc = %d\n", rc);
			goto free_pdata;
		}
		parsed_leds++;
	}
	return 0;

free_pdata:
	for (i = 0; i <= parsed_leds; i++){
		devm_kfree(&led_array->client->dev, led_array[i].pdata);
	}
	return rc;
}

static int aw2023_led_register(struct aw2023_led *led_array)
{
	struct aw2023_led *led;
	int i = 0;
	int rc = 0;

	for (i = 0; i < led_array->num_leds; i++) {
		led = &led_array[i];

		INIT_WORK(&led->brightness_work, aw2023_brightness_work);
		led->cdev.brightness_set = aw2023_set_brightness;
		led->cdev.default_trigger = led->pdata->led_default_trigger;

		rc = led_classdev_register(&led->client->dev, &led->cdev);
		if (rc) {
			dev_err(&led->client->dev,
				"unable to register led %d,rc=%d\n",
				led->id, rc);
			goto free_sync;
		}

		rc = sysfs_create_group(&led->cdev.dev->kobj,
				&aw2023_led_attr_group);
		if (rc) {
			dev_err(&led->client->dev, "led sysfs rc: %d\n", rc);
			goto free_dev;
		}
	}

	return 0;
free_dev:
	led_classdev_unregister(&led_array[i].cdev);
free_sync:
	cancel_work_sync(&led_array[i].brightness_work);
	aw2023_led_err_handle(led_array, i);
	return rc;
}

int aw2023_led_trigger_register(struct aw2023_led *led_array)
{
	struct aw2023_led *led;
	int i = 0;
	int ret = 0;

	for (i = 0; i < LED_MAX_NUM; i++) {
		led = &led_array[i];
		ret = led_trigger_register(&aw2023_led_trigger[i]);
		if (ret < 0) {
			dev_err(&led->client->dev, "register %d trigger fail\n", i);
			goto fail_led_trigger;
		}
	}

	return 0;
fail_led_trigger:
	while (--i >= 0)
	led_trigger_unregister(&aw2023_led_trigger[i]);
	return ret;
}

static int aw2023_led_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct aw2023_led *led_array;
	struct device_node *node;
	int ret = 0;
	int num_leds = 0;
	int i = 0;
	node = client->dev.of_node;
	if (node == NULL)
		return -EINVAL;

	num_leds = of_get_child_count(node);
	if (!num_leds)
		return -EINVAL;

	led_array = devm_kzalloc(&client->dev,
			(sizeof(struct aw2023_led) * num_leds), GFP_KERNEL);
	if (!led_array)
		return -ENOMEM;

	led_default = led_array;

	led_array->client = client;
	led_array->num_leds = num_leds;

	mutex_init(&led_array->lock);

	ret = aw2023_led_parse_child_node(led_array, node);
	if (ret) {
		dev_err(&client->dev, "parsed node error\n");
		goto free_led_arry;
	}

	i2c_set_clientdata(client, led_array);

	ret = aw2023_power_init(led_array, true);
	if (ret) {
		dev_err(&client->dev, "power init failed");
		goto free_led_arry;
	}

	led_array->poweron = false;
	if(!led_array->poweron)
	{
		ret = aw2023_power_on(led_array->pdata->led, true);
		if(ret) {
			if(aw2023_power_on(led_array->pdata->led, true)) {
			    dev_err(&client->dev, "AW2023 Probe power on fail\n");
			}
		}
	}

	ret = aw2023_check_chipid(led_array);
	if (ret) {
		dev_err(&client->dev, "Check chip id error\n");
		goto free_led_arry;
	}

	ret = aw2023_led_register(led_array);
	if (ret) {
		dev_err(&client->dev, "led register error\n");
		goto free_led_arry;
	}

	/* aw2023 led trigger register */
	ret = aw2023_led_trigger_register(led_array);
	if (ret) {
		dev_err(&client->dev, "led trigger register error\n");
		goto free_led_register;
	}

	/* aw2023 led init */
	ret = aw2023_init(led_array);
	if (ret) {
		dev_err(&client->dev, "aw2023_led_init_default: led[%d] error\n", i);
		goto free_led_register;
	}

	led_default->aw2023_led_wq = create_singlethread_workqueue("aw2023_led_workqueue");
	if (!led_default->aw2023_led_wq) {
		dev_err(&client->dev, "aw2023_led_workqueue error\n");
		goto free_led_register;
	}
	INIT_DELAYED_WORK(&led_default->aw2023_led_work, aw2023_work_func);
	queue_delayed_work(led_default->aw2023_led_wq, &led_default->aw2023_led_work,LED_ESD_WORK_TIME * HZ);

	return 0;

free_led_register:
	aw2023_led_err_handle(led_array, num_leds);
free_led_arry:
	mutex_destroy(&led_array->lock);
	devm_kfree(&client->dev, led_array);
	led_array = NULL;
	return ret;
}

static void aw2023_led_remove(struct i2c_client *client)
{
	struct aw2023_led *led_array = i2c_get_clientdata(client);
	int i = 0;
	int parsed_leds = led_array->num_leds;

	for (i = 0; i < parsed_leds; i++) {
		sysfs_remove_group(&led_array[i].cdev.dev->kobj,
				&aw2023_led_attr_group);
		led_classdev_unregister(&led_array[i].cdev);
		cancel_work_sync(&led_array[i].brightness_work);
		devm_kfree(&client->dev, led_array[i].pdata);
		led_array[i].pdata = NULL;
	}
	mutex_destroy(&led_array->lock);
	devm_kfree(&client->dev, led_array);
	led_array = NULL;
}

static void aw2023_led_shutdown(struct i2c_client *client)
{
	struct aw2023_led *led_array = i2c_get_clientdata(client);

	pr_err("aw2023 shutdown start.\n");
	aw2023_write(led_array, AW2023_REG_GCR1, AW2023_CHIP_DISABLE_MASK);
	pr_err("aw2023 shutdown done.\n");
}

static const struct i2c_device_id aw2023_led_id[] = {
	{"aw2023_led", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, aw2023_led_id);

static struct of_device_id aw2023_match_table[] = {
	{ .compatible = "awinic,aw2023_led",},
	{ },
};

static struct i2c_driver aw2023_led_driver = {
	.probe = aw2023_led_probe,
	.remove = aw2023_led_remove,
	.shutdown = aw2023_led_shutdown,
	.driver = {
		.name = "aw2023_led",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(aw2023_match_table),
	},
	.id_table = aw2023_led_id,
};

static int __init aw2023_led_init(void)
{
	pr_err("%s: driver version: %s\n", __func__, AW2023_DRIVER_VERSION);
	return i2c_add_driver(&aw2023_led_driver);
}

module_init(aw2023_led_init);

static void __exit aw2023_led_exit(void)
{
	i2c_del_driver(&aw2023_led_driver);
}
module_exit(aw2023_led_exit);

MODULE_AUTHOR("<liweilei@awinic.com.cn>");
MODULE_DESCRIPTION("AWINIC AW2023 LED driver");
MODULE_LICENSE("GPL v2");
