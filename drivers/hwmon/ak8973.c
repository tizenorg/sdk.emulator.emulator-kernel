/*
 *  ak8973.c - 3-axis Electronic Compass
 *
 *  Copyright (C) 2008 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/input-polldev.h>
#include <linux/completion.h>

#include <linux/ak8973.h>

#define AK8973_ST		0xC0
#define AK8973_TMPS		0xC1
#define AK8973_H1X		0xC2
#define AK8973_H1Y		0xC3
#define AK8973_H1Z		0xC4

#define AK8973_MS1		0xE0
#define AK8973_HXDA		0xE1
#define AK8973_HYDA		0xE2
#define AK8973_HZDA		0xE3
#define AK8973_HXGA		0xE4
#define AK8973_HYGA		0xE5
#define AK8973_HZGA		0xE6

#define AK8973_TS1		0x5D

#define AK8973_ETS		0x62
#define AK8973_EVIR		0x63
#define AK8973_EIHE		0x64
#define AK8973_ETST		0x65
#define AK8973_EHXGA		0x66
#define AK8973_EHYGA		0x67
#define AK8973_EHZGA		0x68
#define AK8973_WRAL1		0x60

#define SENSOR_MODE		(0x00)
#define EEPROM_MODE		(0x02)
#define POWERDOWN_MODE		(0x03)

#define ST_INT			(1 << 0)
#define ST_WEN			(1 << 1)

#define EEPROM_WRITE		(0x15 << 3)

#define MAX_8BIT		((1 << 8) - 1)

#define RESET_MAXCNT		3

#define MAX_MEASUREMENT_TIME		((HZ * 60) / 1000)

#define NUM_OF_SENSOR_DATA	4
#define NUM_OF_EEPROM_DATA	7
#define NUM_OF_HXDA_DATA	3
#define NUM_OF_ETC_DATA	1

struct ak8973_data {
	u8 x;
	u8 y;
	u8 z;
	int temp;
};

struct ak8973_chip {
	struct i2c_client *client;
	struct device *dev;
	struct work_struct work;
	struct mutex lock;
	struct input_polled_dev *ipdev;

	struct completion comp;

	int poll_interval;
	void (*reset) (void);

	/* EEPROM data */
	u8 eeprom[NUM_OF_EEPROM_DATA];

	struct ak8973_data data;
	int reset_cnt; /* workaround */
};

static int ak8973_write_reg(struct i2c_client *client, u8 *buffer, int length)
{
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = length,
			.buf = buffer,
		},
	};

	return i2c_transfer(client->adapter, msg, 1);
}

static int ak8973_read_reg(struct i2c_client *client, u8 *buffer, int length)
{
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = buffer,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = buffer,
		},
	};
	return i2c_transfer(client->adapter, msg, 2);
}

static int ak8973_to_temp(int value)
{
	int temp;

//	temp = 35 + ((120 - value) / 1.6);
	temp = value;

	return temp;
}

static void ak8973_update_data(struct ak8973_chip *ac)
{
	u8 buffer[NUM_OF_SENSOR_DATA];
	mutex_lock(&ac->lock);

	buffer[0] = AK8973_TMPS;
	ak8973_read_reg(ac->client, buffer, NUM_OF_SENSOR_DATA);
	ac->data.temp = ak8973_to_temp(buffer[0]);
	ac->data.x = buffer[1];
	ac->data.y = buffer[2];
	ac->data.z = buffer[3];

	mutex_unlock(&ac->lock);
}

static void ak8973_work(struct work_struct *work)
{
	struct ak8973_chip *ac = container_of(work, struct ak8973_chip, work);
	static int counter = -1;

	if (++counter == 0) {
		enable_irq(ac->client->irq);
		return;
	}

	ak8973_update_data(ac);
	complete(&ac->comp);

	if (ac->ipdev) {
		mutex_lock(&ac->lock);
		input_report_abs(ac->ipdev->input, ABS_X, ac->data.x);
		input_report_abs(ac->ipdev->input, ABS_Y, ac->data.y);
		input_report_abs(ac->ipdev->input, ABS_Z, ac->data.z);
		input_sync(ac->ipdev->input);
		mutex_unlock(&ac->lock);
	}

	enable_irq(ac->client->irq);
}

static irqreturn_t ak8973_irq(int irq, void *data)
{
	struct ak8973_chip *ac = data;

	if (!work_pending(&ac->work)) {
		disable_irq_nosync(irq);
		schedule_work(&ac->work);
	} else
		dev_err(&ac->client->dev, "work_pending\n");

	return IRQ_HANDLED;
}

static ssize_t ak8973_show_output(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ak8973_chip *ac = dev_get_drvdata(dev);
	u8 buffer[2];

	/*
	 * To execute the sensor measurement mode,
	 * INT pin must be low.
	 * Before executing this pattern, drive INT pin to low by
	 * performing data register read operation as dummy.
	 */
	buffer[0] = AK8973_H1X;
	ak8973_read_reg(ac->client, buffer, NUM_OF_ETC_DATA);

	buffer[0] = AK8973_MS1;
	buffer[1] = SENSOR_MODE;
	ak8973_write_reg(ac->client, buffer, NUM_OF_ETC_DATA + 1);
	wait_for_completion_timeout(&ac->comp, MAX_MEASUREMENT_TIME);
	return sprintf(buf, "%d %d %d %d\n",
			ac->data.x, ac->data.y, ac->data.z, ac->data.temp);
}
static SENSOR_DEVICE_ATTR(output, S_IRUGO, ak8973_show_output, NULL, 0);

#define AK8973_ADJUST(name) \
static ssize_t ak8973_show_##name(struct device *dev, \
		struct device_attribute *attr, char *buf) \
{ \
	struct ak8973_chip *ac = dev_get_drvdata(dev); \
	char *p = buf; \
	u8 buffer[NUM_OF_HXDA_DATA]; \
 \
	mutex_lock(&ac->lock); \
	buffer[0] = AK8973_##name; \
	ak8973_read_reg(ac->client, buffer, NUM_OF_HXDA_DATA); \
	p += sprintf(p, "%d %d %d \n", \
			buffer[0], buffer[1], buffer[2]); \
	mutex_unlock(&ac->lock); \
	return p - buf; \
} \
static ssize_t ak8973_set_##name(struct device *dev, \
		struct device_attribute *attr, const char *buf, size_t count) \
{ \
	struct ak8973_chip *ac = dev_get_drvdata(dev); \
	unsigned long val; \
	int size_of_buf = 50; \
	char *copied_buf = kzalloc(sizeof(char) * size_of_buf, GFP_KERNEL); \
	char **buffer_pointer = &copied_buf; \
	char *sub_string; \
	u8 buffer[NUM_OF_HXDA_DATA + 1]; \
	int i; \
 \
	strncpy(copied_buf, buf, 50); \
	mutex_lock(&ac->lock); \
	buffer[0] = AK8973_##name; \
	for (i = 0 ; i < NUM_OF_HXDA_DATA ; i++) { \
		sub_string = strsep(buffer_pointer, " "); \
		strict_strtoul(sub_string, 10, &val); \
		buffer[i + 1] = (u8) val; \
	} \
	ak8973_write_reg(ac->client, buffer, NUM_OF_HXDA_DATA + 1); \
	mutex_unlock(&ac->lock); \
	kfree(copied_buf); \
	return count; \
} \
static SENSOR_DEVICE_ATTR(name, S_IRUGO | S_IWUSR, \
		ak8973_show_##name, ak8973_set_##name, 0);

AK8973_ADJUST(HXDA);
AK8973_ADJUST(HXGA);

static ssize_t ak8973_set_reset(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct ak8973_chip *ac = dev_get_drvdata(dev);

	mutex_lock(&ac->lock);
	if (ac->reset)
		ac->reset();
	mutex_unlock(&ac->lock);

	return count;
}

static SENSOR_DEVICE_ATTR(reset, S_IWUSR, NULL, ak8973_set_reset, 0);

static ssize_t ak8973_show_eeprom(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct ak8973_chip *ac = dev_get_drvdata(dev);
	char *p = buf;
	u8 buffer[2];
	int i;

	mutex_lock(&ac->lock);

	buffer[0] = AK8973_MS1;
	buffer[1] = EEPROM_MODE;
	ak8973_write_reg(ac->client, buffer, NUM_OF_ETC_DATA + 1);

	ac->eeprom[0] = AK8973_ETS;
	ak8973_read_reg(ac->client, ac->eeprom, NUM_OF_EEPROM_DATA);
	for (i = 0; i < sizeof(ac->eeprom); i++) {
		p += sprintf(p, "%d ", ac->eeprom[i]);
	}
	p += sprintf(p, "\n");

	buffer[0] = AK8973_MS1;
	buffer[1] = POWERDOWN_MODE;
	ak8973_write_reg(ac->client, buffer, NUM_OF_ETC_DATA + 1);

	mutex_unlock(&ac->lock);
	return p - buf;
}

static ssize_t ak8973_set_eeprom(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	/*
	 * Never write in EEPROM
	 * Device-specific adjusted values are stored before shipment in AKM.
	 * Write operation is not needed in user operation.
	 * It may loose adjusted values.
	 * If these adjusted values are lost, AK8973 cannot operate normally
	 */
	return count;
}

static SENSOR_DEVICE_ATTR(eeprom, S_IRUGO | S_IWUSR,
			  ak8973_show_eeprom, ak8973_set_eeprom, 0);

static struct attribute *ak8973_attributes[] = {
	&sensor_dev_attr_output.dev_attr.attr,
	&sensor_dev_attr_eeprom.dev_attr.attr,
	&sensor_dev_attr_reset.dev_attr.attr,
	&sensor_dev_attr_HXDA.dev_attr.attr,
	&sensor_dev_attr_HXGA.dev_attr.attr,
	NULL
};

static const struct attribute_group ak8973_group = {
	.attrs = ak8973_attributes,
};

static void ak8973_initialize(struct ak8973_chip *ac)
{
	u8 buffer[2], hxga[1], hyga[1], hzga[1];

	/* Put the device in EEPROM access mode */
	buffer[0] = AK8973_MS1;
	buffer[1] = EEPROM_MODE;
	ak8973_write_reg(ac->client, buffer, NUM_OF_ETC_DATA + 1);

	msleep(1);

	hxga[0] = AK8973_EHXGA;
	ak8973_read_reg(ac->client, hxga, NUM_OF_ETC_DATA);
	hyga[0] = AK8973_EHYGA;
	ak8973_read_reg(ac->client, hyga, NUM_OF_ETC_DATA);
	hzga[0] = AK8973_EHZGA;
	ak8973_read_reg(ac->client, hzga, NUM_OF_ETC_DATA);

	/* Put the device in Power-down mode */
	buffer[0] = AK8973_MS1;
	buffer[1] = POWERDOWN_MODE;
	ak8973_write_reg(ac->client, buffer, NUM_OF_ETC_DATA + 1);

	msleep(1);

	buffer[0] = AK8973_HXGA;
	buffer[1] = hxga[0];
	ak8973_write_reg(ac->client, buffer, NUM_OF_ETC_DATA + 1);
	buffer[0] = AK8973_HYGA;
	buffer[1] = hyga[0];
	ak8973_write_reg(ac->client, buffer, NUM_OF_ETC_DATA + 1);
	buffer[0] = AK8973_HZGA;
	buffer[1] = hzga[0];
	ak8973_write_reg(ac->client, buffer, NUM_OF_ETC_DATA + 1);

	buffer[0] = AK8973_ST;
	ak8973_read_reg(ac->client, buffer, NUM_OF_ETC_DATA);
	dev_dbg(&ac->client->dev, "AK8973 Status : 0x%x\n", buffer[0]);

	/* After power-down mode is set, at least 100us is needed
	 * before setting another mode */
	udelay(100);
}

#ifdef CONFIG_INPUT_POLLDEV
static void ak8973_ipdev_poll(struct input_polled_dev *dev)
{
	struct ak8973_chip *ac = dev->private;
	int value;
	u8 buffer[2], buffer_r[1];

	mutex_lock(&ac->lock);

	buffer_r[0] = AK8973_ST;
	ak8973_read_reg(ac->client, buffer_r, NUM_OF_ETC_DATA);
	dev_dbg(&ac->client->dev, "%s: Status 0x%x\n", __func__, value);

	/* workaround */
	if ((value & ST_INT)) {
		ac->reset_cnt++;
		if (ac->reset_cnt > RESET_MAXCNT) {
			if (ac->reset)
				ac->reset();
			ac->reset_cnt = 0;
			value = 0;
		}
	}

	buffer[0] = AK8973_MS1;
	buffer[1] = SENSOR_MODE;
	if (!(value & ST_INT))
		ak8973_write_reg(ac->client, buffer, NUM_OF_ETC_DATA + 1);

	mutex_unlock(&ac->lock);
}

static int ak8973_register_input_dev(struct ak8973_chip *ac)
{
	struct i2c_client *client = ac->client;
	struct input_polled_dev *ipdev;
	int ret;

	ac->ipdev = ipdev = input_allocate_polled_device();
	if (!ipdev) {
		dev_err(&client->dev, "fail: allocate poll input device\n");
		ret = -ENOMEM;
		goto error_input;
	}

	ipdev->private = ac;
	ipdev->poll = ak8973_ipdev_poll;
	if (ac->poll_interval)
		ipdev->poll_interval = ac->poll_interval;
	ipdev->input->name = "ak8973 compass";
	ipdev->input->id.bustype = BUS_I2C;
	ipdev->input->dev.parent = &client->dev;
	ipdev->input->evbit[0] = BIT_MASK(EV_ABS);

	input_set_abs_params(ipdev->input, ABS_X, 0, MAX_8BIT, 0, 0);
	input_set_abs_params(ipdev->input, ABS_Y, 0, MAX_8BIT, 0, 0);
	input_set_abs_params(ipdev->input, ABS_Z, 0, MAX_8BIT, 0, 0);

	ret = input_register_polled_device(ipdev);
	if (ret) {
		dev_err(&client->dev, "fail: register to poll input device\n");
		goto error_reg_input;
	}

	return 0;

error_reg_input:
	input_unregister_polled_device(ipdev);
error_input:
	input_free_polled_device(ipdev);
	return ret;
}

static void ak8973_unregister_input_dev(struct ak8973_chip *ac)
{
	input_unregister_polled_device(ac->ipdev);
	input_free_polled_device(ac->ipdev);
}
#else
static int ak8973_register_input_dev(struct ak8973_chip *ac)
{
	return 0;
}

static void ak8973_unregister_input_dev(struct ak8973_chip *ac)
{

}
#endif

static int ak8973_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ak8973_chip *ac;
	struct ak8973_platform_data *pdata;
	int ret;

	ac = kzalloc(sizeof(struct ak8973_chip), GFP_KERNEL);
	if (!ac)
		return -ENOMEM;

	pdata = client->dev.platform_data;

	ac->reset = pdata->reset;
	ac->poll_interval = pdata->poll_interval;
	ac->client = client;
	i2c_set_clientdata(client, ac);

	if (ac->reset)
		ac->reset();

	INIT_WORK(&ac->work, ak8973_work);
	mutex_init(&ac->lock);
	init_completion(&ac->comp);

	if (client->irq > 0) {
		ret = request_irq(client->irq, ak8973_irq, IRQF_TRIGGER_RISING,
				  "ak8973 compass", ac);
		if (ret) {
			dev_err(&client->dev, "can't get IRQ %d, ret %d\n",
				client->irq, ret);
			goto error_irq;
		}
	}

	ac->dev = hwmon_device_register(&client->dev);
	if (IS_ERR(ac->dev)) {
		ret = PTR_ERR(ac->dev);
		goto error_hwmon;
	}

	ret = sysfs_create_group(&client->dev.kobj, &ak8973_group);
	if (ret) {
		dev_err(&client->dev, "Creating ak8973 attribute group failed");
		goto error_device;
	}

	ret = ak8973_register_input_dev(ac);
	if (ret) {
		dev_err(&client->dev, "Registering ak8973 input device failed");
		goto error_input;
	}

	ak8973_initialize(ac);

	printk(KERN_INFO "%s registered\n", id->name);
	return 0;

error_input:
	ak8973_unregister_input_dev(ac);
error_device:
	sysfs_remove_group(&client->dev.kobj, &ak8973_group);
error_hwmon:
	hwmon_device_unregister(ac->dev);
error_irq:
	free_irq(client->irq, ac);
	i2c_set_clientdata(client, NULL);
	kfree(ac);
	return ret;
}

static int __exit ak8973_remove(struct i2c_client *client)
{
	struct ak8973_chip *ac = i2c_get_clientdata(client);

	wait_for_completion(&ac->comp);
	ak8973_unregister_input_dev(ac);
	sysfs_remove_group(&client->dev.kobj, &ak8973_group);
	hwmon_device_unregister(ac->dev);
	free_irq(client->irq, ac);
	i2c_set_clientdata(client, NULL);
	kfree(ac);
	return 0;
}

static int ak8973_suspend(struct i2c_client *client, pm_message_t mesg)
{
	disable_irq(client->irq);
	return 0;
}

static int ak8973_resume(struct i2c_client *client)
{
	struct ak8973_chip *ac = i2c_get_clientdata(client);

	if (ac->reset)
		ac->reset();

	enable_irq(client->irq);

	ak8973_initialize(ac);
	return 0;
}

static const struct i2c_device_id ak8973_id[] = {
	{"ak8973", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, ak8973_id);

static struct i2c_driver ak8973_i2c_driver = {
	.driver = {
		   .name = "ak8973",
		   },
	.probe = ak8973_probe,
	.remove = __exit_p(ak8973_remove),
	.suspend = ak8973_suspend,
	.resume = ak8973_resume,
	.id_table = ak8973_id,
};

static int __init ak8973_init(void)
{
	return i2c_add_driver(&ak8973_i2c_driver);
}

module_init(ak8973_init);

static void __exit ak8973_exit(void)
{
	i2c_del_driver(&ak8973_i2c_driver);
}

module_exit(ak8973_exit);

MODULE_AUTHOR("Kyungmin Park <kyungmin.park@samsung.com>");
MODULE_DESCRIPTION("I2C interface for AK8973");
MODULE_LICENSE("GPL");
