#ifndef LINUX_INPUT_SEC_INPUT_BRIDGE_H
#define LINUX_INPUT_SEC_INPUT_BRIDGE_H

#include <linux/kobject.h>

enum mkey_check_option {
	MKEY_CHECK_AUTO,
	MKEY_CHECK_AWAYS
};

struct sec_input_bridge_mkey {
	unsigned int type;
	unsigned int code;
	enum mkey_check_option option;
};

struct sec_input_bridge_platform_data {
	void (*pre_event_func)(void *event_data);
	void *event_data;

	const struct sec_input_bridge_mkey *mkey_map;	
	unsigned int num_mkey;

	unsigned char send_uevent;
	enum kobject_action uevent_action;
	const char *uevent_env_str;
	const char *uevent_env_value;
};

#endif /* LINUX_INPUT_SEC_INPUT_BRIDGE_H */

