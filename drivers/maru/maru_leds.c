/*
 * Virtual led device node
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * JinHyung Choi <jinhyung2.choi@samsung.com>
 * SooYoung Ha <yoosah.ha@samsung.com>
 * Sungmin Ha <sungmin82.ha@samsung.com>
 * YeongKyoon Lee <yeongkyoon.lee@samsung.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Contributors:
 * - S-Core Co., Ltd
 */

#include <linux/module.h>
#include <linux/leds.h>
#include <linux/platform_device.h>

#define MARU_DRVNAME 	"maru-leds"
#define LED_DEVICE_NAME	"leds-torch"

struct maru_leds_dev {
	int addr;
};

struct maru_led {
	struct maru_leds_dev *dev;
	struct led_classdev cdev;
	struct platform_device *pdev;
	bool enabled;
	int id;
};

static void maru_led_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
}

static int maru_led_probe(struct platform_device *pdev)
{
	int ret;
	struct maru_led *led;

	led = devm_kzalloc(&pdev->dev, sizeof(*led), GFP_KERNEL);
	if (led == NULL)
		return -ENOMEM;

	led->cdev.name = LED_DEVICE_NAME;
	led->cdev.brightness_set = maru_led_brightness_set;
	led->cdev.flags |= LED_CORE_SUSPENDRESUME;
	led->cdev.brightness = 0;

	platform_set_drvdata(pdev, led);

	ret = led_classdev_register(&pdev->dev, &led->cdev);
	if (ret < 0)
		return ret;

	return 0;
}

static int maru_led_remove(struct platform_device *pdev)
{
	struct maru_led *led = dev_get_platdata(&pdev->dev);

	led_classdev_unregister(&led->cdev);

	return 0;
}

static struct platform_device *pdev;

static int __init maru_leds_device_add(const struct maru_leds_dev *dev)
{
	int err;

	pdev = platform_device_alloc(MARU_DRVNAME, -1);
	if (!pdev)
		return -ENOMEM;

	err = platform_device_add_data(pdev, dev, sizeof(*dev));
	if (err) {
		dev_err(&pdev->dev, "Platform data allocation failed\n");
		goto err;
	}

	err = platform_device_add(pdev);
	if (err) {
		dev_err(&pdev->dev, "Device addition failed\n");
		goto err;
	}

	dev_err(&pdev->dev, "maru_led_device is added.\n");

	return 0;
err:
	platform_device_put(pdev);

	return err;
}

static struct platform_driver maru_led_driver = {
	.driver = {
		.name  = MARU_DRVNAME,
		.owner = THIS_MODULE,
	},
	.probe  = maru_led_probe,
	.remove = maru_led_remove,
};

static int __init maru_leds_init(void)
{
	int err;
	struct maru_leds_dev dev;

	err = platform_driver_register(&maru_led_driver);
	if (!err) {
		err = maru_leds_device_add(&dev);
		if (err)
			platform_driver_unregister(&maru_led_driver);
	}

	return err;
}
static void __exit maru_leds_exit(void)
{
	platform_driver_unregister(&maru_led_driver);
}

module_init(maru_leds_init);
module_exit(maru_leds_exit);

