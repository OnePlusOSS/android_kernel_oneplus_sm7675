/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
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

#ifndef __LINUX_AW2023_LED_H__
#define __LINUX_AW2023_LED_H__

/* The definition of each time described as shown in figure.
 *        /-----------\
 *       /      |      \
 *      /|      |      |\
 *     / |      |      | \-----------
 *       |hold_time_ms |      |
 *       |             |      |
 * rise_time_ms  fall_time_ms |
 *                       off_time_ms
 */

struct aw2023_platform_data {
	int imax;
	int led_current;
	int rise_time_ms;
	int hold_time_ms;
	int fall_time_ms;
	int off_time_ms;
	int led_mode;
	const char *led_default_trigger;
	struct aw2023_led *led;
};

typedef enum debug_level {
	LEVEL_BASIC,    /*printk basic tp debug info*/
	LEVEL_DETAIL,   /*printk tp detail log for stress test*/
	LEVEL_DEBUG,    /*printk all tp debug info*/
	LEVEL_MAX,
} led_debug_level;

extern unsigned int aw2023_debug ;
#define AW2023_DEVICE  "aw2023"
#define AW2023_DEBUG(a, arg...)\
	do{\
		if (LEVEL_DEBUG == aw2023_debug)\
		pr_err("[LED]"AW2023_DEVICE ": " a, ##arg);\
	}while(0)
#endif
