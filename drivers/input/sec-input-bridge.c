/*
 *  sec-input-bridge.c - Specific control inpt event bridge for Samsung Electronics
 *
 *  Copyright (C) 2010 Samsung Electronics
 *  Yongsul Oh <yongsul96.oh@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <linux/workqueue.h>
#include <linux/mutex.h>


#include <linux/input/sec-input-bridge.h>

extern int bkl_warning_operation(int on_off, int gamma);

#ifdef	CONFIG_KERNEL_LOGGING
int rb_dump_ram_log_extract_to_file(char* file_path);
#ifdef	CONFIG_RB_DUMP_USE_NAND
int rb_dump_nand_log_extract_to_file(char* file_path);
#endif
#endif	// CONFIG_RB_DUMP_USE_NAND

struct sec_input_bridge {
	struct sec_input_bridge_platform_data *pdata;
	struct work_struct work;
	struct mutex	lock;
	struct platform_device *dev;
		
	u8 check_index;
};

static void input_bridge_set_ids(struct input_device_id *ids, unsigned int type, unsigned int code)
{
	switch (type) {
	case EV_KEY:
		ids->flags = INPUT_DEVICE_ID_MATCH_KEYBIT;
		__set_bit(code, ids->keybit);
		break;

	case EV_REL:
		ids->flags = INPUT_DEVICE_ID_MATCH_RELBIT;
		__set_bit(code, ids->relbit);
		break;

	case EV_ABS:
		ids->flags = INPUT_DEVICE_ID_MATCH_ABSBIT;
		__set_bit(code, ids->absbit);
		break;

	case EV_MSC:
		ids->flags = INPUT_DEVICE_ID_MATCH_MSCIT;
		__set_bit(code, ids->mscbit);
		break;

	case EV_SW:
		ids->flags = INPUT_DEVICE_ID_MATCH_SWBIT;
		__set_bit(code, ids->swbit);
		break;

	case EV_LED:
		ids->flags = INPUT_DEVICE_ID_MATCH_LEDBIT;
		__set_bit(code, ids->ledbit);
		break;

	case EV_SND:
		ids->flags = INPUT_DEVICE_ID_MATCH_SNDBIT;
		__set_bit(code, ids->sndbit);
		break;

	case EV_FF:
		ids->flags = INPUT_DEVICE_ID_MATCH_FFBIT;
		__set_bit(code, ids->ffbit);
		break;

	case EV_PWR:
		/* do nothing */
		break;

	default:
		printk(KERN_ERR
			"input_bridge_set_ids: unknown type %u (code %u)\n",
			type, code);
		return;
	}
	
	ids->flags |= INPUT_DEVICE_ID_MATCH_EVBIT;
	__set_bit(type, ids->evbit);
}

static void input_bridge_work(struct work_struct *work) {
	struct sec_input_bridge *bridge = container_of(work,
			struct sec_input_bridge, work);
	int state = -EINVAL;
	char env_str[16];
	char *envp[] = { env_str, NULL };
	
#ifdef	CONFIG_KERNEL_LOGGING
	rb_dump_ram_log_extract_to_file(NULL);
#ifdef CONFIG_RB_DUMP_USE_NAND
	rb_dump_nand_log_extract_to_file(NULL);
#endif
#endif
	
	mutex_lock(&bridge->lock);

	if (bridge->pdata->send_uevent) {
		sprintf(env_str, "%s=%s", bridge->pdata->uevent_env_str , bridge->pdata->uevent_env_value);
		state = kobject_uevent_env(&bridge->dev->dev.kobj, bridge->pdata->uevent_action, envp);
//		state = kobject_uevent(&bridge->dev->dev.kobj, bridge->pdata->uevent_action);
		if (state != 0) 
			printk(KERN_ERR
				"<error , kobject_uevent_env fail>  with action : %d\n",bridge->pdata->uevent_action);		
	}

	if (bridge->pdata->pre_event_func && (state == 0)) {
		bridge->pdata->pre_event_func(bridge->pdata->event_data);
	}

	/* LCD on/off to confirm action*/
#if 0 // emulator
	ssleep(2);

	for (i=0 ; i<8 ; i++) {
		bkl_warning_operation(1, 0);
		msleep(500);

		bkl_warning_operation(1, 10);
		msleep(500);
	}

	/* recovery first state*/
	bkl_warning_operation(0, 0);
#endif

	mutex_unlock(&bridge->lock);
}

static void input_bridge_event(struct input_handle *handle, unsigned int type,
		        unsigned int code, int value)
{
	int rep_check;
	
	struct input_handler *sec_bridge_handler = handle->handler;
	struct sec_input_bridge *sec_bridge = sec_bridge_handler->private;
	
	rep_check = test_bit(EV_REP,sec_bridge_handler->id_table->evbit);
	rep_check = (rep_check << 1) | 1;
	
	switch (type) {
	case EV_KEY:
		if ( value & rep_check ) {
			printk(KERN_INFO
				"sec-input-bridge: KEY input intercepted, type : %d , code : %d , value %d, index : %d, mkey_max : %d \n",
				type,code,value, sec_bridge->check_index, sec_bridge->pdata->num_mkey);
			if ( sec_bridge->pdata->mkey_map[sec_bridge->check_index].code == code) {
				sec_bridge->check_index++;
				printk(KERN_INFO "sec-input-bridge: code ok!\n");
				if ( (sec_bridge->check_index) >= (sec_bridge->pdata->num_mkey) ) {
					printk(KERN_INFO "sec-input-bridge: index is max \n");
					schedule_work(&sec_bridge->work);
					sec_bridge->check_index = 0;					
				}
			} else {
				sec_bridge->check_index = 0;
			}			
		}		
		break;

	default:
		break;
	}

}

static int input_bridge_connect(struct input_handler *handler,
					  struct input_dev *dev,
					  const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "sec-input-bridge";

	error = input_register_handle(handle);
	if (error) {
		printk(KERN_ERR
			"sec-input-bridge: Failed to register input bridge handler, "
			"error %d\n", error);
		kfree(handle);
		return error;
	}

	error = input_open_device(handle);
	if (error) {
		printk(KERN_ERR
			"sec-input-bridge: Failed to open input bridge device, "
			"error %d\n", error);
		input_unregister_handle(handle);
		kfree(handle);
		return error;
	}

	return 0;
}

static void input_bridge_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static struct input_handler input_bridge_handler = {
	.event =		input_bridge_event,
	.connect =	input_bridge_connect,
	.disconnect =	input_bridge_disconnect,
	.name =		"sec-input-bridge",
};

static int __devinit sec_input_bridge_probe(struct platform_device *dev)
{	
	struct sec_input_bridge_platform_data *pdata;	
	struct sec_input_bridge *bridge;
	struct input_device_id *input_bridge_ids;

	int state;
	int i;

	pdata = dev->dev.platform_data;
	if (!pdata) {
		dev_err(&dev->dev, "No samsung input bridge platform data.\n");
		return -EINVAL;
	}

	if (pdata->num_mkey == 0) {
		dev_err(&dev->dev, "No samsung input bridge platform data. num_mkey == 0\n");
		return -EINVAL;
	}

	bridge = kzalloc(sizeof(struct sec_input_bridge), GFP_KERNEL);
	if (!bridge)
		return -ENOMEM;

	input_bridge_ids = kzalloc(sizeof(struct input_device_id[(pdata->num_mkey +1)]), GFP_KERNEL);
	if(!input_bridge_ids) {
		kfree(bridge);
		return -ENOMEM;
	}
	memset(input_bridge_ids, 0x00, sizeof(input_bridge_ids));

	for( i=0 ; i < pdata->num_mkey ; i++ ) {
		input_bridge_set_ids(&input_bridge_ids[i], pdata->mkey_map[i].type, pdata->mkey_map[i].code);
	}
	
	input_bridge_handler.private = bridge;
	input_bridge_handler.id_table = input_bridge_ids;
	
	state = input_register_handler(&input_bridge_handler);
	if (state)
		goto input_register_fail;

	bridge->dev = dev;
	bridge->pdata = pdata;

	INIT_WORK(&bridge->work, input_bridge_work);
	mutex_init(&bridge->lock);

	platform_set_drvdata(dev, bridge);

	return 0;

input_register_fail:
	cancel_work_sync(&bridge->work);
	mutex_destroy(&bridge->lock);
	kfree(bridge);
	kfree(input_bridge_ids);
	
	return state;	

}

static int __devexit sec_input_bridge_remove(struct platform_device *dev)	
{
	struct sec_input_bridge *bridge = platform_get_drvdata(dev);
		
	cancel_work_sync(&bridge->work);
	mutex_destroy(&bridge->lock);
	kfree(input_bridge_handler.id_table);
	input_unregister_handler(&input_bridge_handler);
	kfree(bridge);
	platform_set_drvdata(dev, NULL);

	return 0;
}


#ifdef CONFIG_PM
static int sec_input_bridge_suspend(struct platform_device *dev, pm_message_t state)
{
	return 0;
}

static int sec_input_bridge_resume(struct platform_device *dev)
{
	return 0;
}
#else
#define sec_input_bridge_suspend		NULL
#define sec_input_bridge_resume		NULL
#endif

static struct platform_driver sec_input_bridge_driver = {
	.probe = sec_input_bridge_probe,
	.remove = __devexit_p(sec_input_bridge_remove),
	.suspend = sec_input_bridge_suspend,
	.resume = sec_input_bridge_resume,
	.driver = {
		.name = "samsung_input_bridge",
	},
};

static const struct sec_input_bridge_mkey emul_mkey_map[] = {
	{ .type = EV_KEY , .code = KEY_VOLUMEUP 	 },
	{ .type = EV_KEY , .code = KEY_VOLUMEDOWN	 },
	{ .type = EV_KEY , .code = KEY_MENU 		 },
	{ .type = EV_KEY , .code = KEY_MENU 		 },
	{ .type = EV_KEY , .code = KEY_VOLUMEUP 	 },
	{ .type = EV_KEY , .code = KEY_VOLUMEDOWN	},
	{ .type = EV_KEY , .code = KEY_VOLUMEUP 	 },
	{ .type = EV_KEY , .code = KEY_MENU 		 },
};

static struct sec_input_bridge_platform_data emul_input_bridge_data = {

    .mkey_map = emul_mkey_map ,
    .num_mkey = ARRAY_SIZE(emul_mkey_map),

    .send_uevent = 1,
    .uevent_action = KOBJ_CHANGE,
    .uevent_env_str = "RB_DUMP",
    .uevent_env_value = "ON",
};

static struct platform_device emul_input_bridge = {
    .name   = "samsung_input_bridge",
    .id     = -1,
    .dev    = {
        .platform_data = &emul_input_bridge_data,
    },
};

//static struct platform_device *sec_input_bridge_device;

static int __init sec_input_bridge_init(void)
{
	int err = 0;

	platform_device_register(&emul_input_bridge);

	err = platform_driver_register(&sec_input_bridge_driver);

	/*
	if (!err) {
		sec_input_bridge_device = platform_device_alloc("samsung_input_bridge", 0);
		if (sec_input_bridge_device)
			err = platform_device_add(sec_input_bridge_device);
		else
			err = -ENOMEM;

		if (err) {
			platform_device_put(sec_input_bridge_device);
			platform_driver_unregister(&sec_input_bridge_driver);
			return err;
		}
	}
	*/
	return err;
}

static void __exit sec_input_bridge_exit(void)
{
	platform_driver_unregister(&sec_input_bridge_driver);
}


module_init(sec_input_bridge_init);

#ifdef CONFIG_CHARGER_DETECT_BOOT
charger_module_init(sec_input_bridge_init);
#endif

module_exit(sec_input_bridge_exit);

MODULE_AUTHOR("Yongsul Oh <yongsul96.oh@samsung.com>");
MODULE_DESCRIPTION("Input Event -> Specific Control Bridge");
MODULE_LICENSE("GPL");
