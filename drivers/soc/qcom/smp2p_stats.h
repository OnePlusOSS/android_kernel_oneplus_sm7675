/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */

#ifndef _OPLUS_SMP2P_STATS_H
#define _OPLUS_SMP2P_STATS_H

#define SENSOR_TYPE_SHIFT		24

int create_oplus_smp2p_node(void);
bool update_smp2p_stats_info(unsigned local_pid, unsigned remote_pid, u32 type);

#endif /* _OPLUS_SMP2P_STATS_H */
