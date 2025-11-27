/*
 *	This include file is intended for oplus power that to notify and receive
 *	blank/unblank event.
 *
 */
#ifndef _OPLUS_POWER_NOTIFIER_H_
#define _OPLUS_POWER_NOTIFIER_H_

#include <linux/notifier.h>

#define		OPLUS_POWER_EVENT_PON   0x01

enum {
	OPLUS_POWER_NONE,
	/* power: kpdpwr and resin long press , s1 timer triggered */
	OPLUS_PON_KPDPWR_RESIN_BARK,
	/* power: kpdpwr and resin long press then release, s1 timer not trigger */
	OPLUS_PON_KPDPWR_RESIN_RELEASE,
};

struct oplus_power_notify_data {
	int pon_status;
	void *data; //not use, reserved
};

extern int oplus_power_notifier_register_client(struct notifier_block *nb);
extern int oplus_power_notifier_unregister_client(struct notifier_block *nb);
extern int oplus_power_notifier_call_chain(unsigned long val, void *v);
#endif
