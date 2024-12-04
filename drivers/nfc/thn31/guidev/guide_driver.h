/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2022 TsingTeng MicroSystem Co., Ltd.
 */

#ifndef _TMS_GUIDEV_H_
#define _TMS_GUIDEV_H_
/*********** PART0: Head files ***********/
//#include "../tms_common.h"
#include "../nfc/nfc_driver.h"

/*********** PART1: Define Area ***********/
#define GUIDEDEV_NAME         "tms,nfc"
#ifdef TMS_MOUDLE
#undef TMS_MOUDLE
#define TMS_MOUDLE            "Guidev"
#endif

#define MAX_MAJOR_VERSION_NUM (10)
#define MAX_CMD_LEN           (50)
#define TMS_CMD_HEAD_LEN      (3)
#define TMS_FW_CMP_BYTE       (3)
#define TMS_BL_CMP_BYTE       (4)
#define TMS_VERSION_MASK      (0xF0)
/*********** PART2: Struct Area ***********/
typedef enum {
    TMS_THN31,
    SAMPLE_DEV_1,
    SAMPLE_DEV_2,
    UNMATCH,
} chip_t;

typedef enum {
    TMS_FW,
    TMS_BL,
    SAMPLE_MATCH_1,
    SAMPLE_MATCH_2,
    UNKNOW,
} match_t;

struct match_info {
    int          sum;
    int          write_len;
    int          check_sum;
    int          ver_num;
    const int    read_retry;
    const int    write_retry;
    uint8_t      cmp;
    uint8_t      major_ver[MAX_MAJOR_VERSION_NUM];
    uint8_t      cmd[MAX_CMD_LEN];
    char         *name;
    chip_t       type;
    match_t      pattern;
};

struct guide_dev {
    bool                  dlpin_flag;
    struct i2c_client     *client;
    struct device         *dev;
    struct hw_resource    hw_res;
    struct tms_info       *tms;          /* tms common data */
};

/*********** PART3: Function or variables for other files ***********/
#endif /* _TMS_GUIDEV_H_ */