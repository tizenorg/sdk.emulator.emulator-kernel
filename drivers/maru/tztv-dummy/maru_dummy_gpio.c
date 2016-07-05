/*
 * MARU Dummy GPIO driver
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * Jinhyung Choi <jinh0.choi@samsung.com>
 * SangHo Park <sangho.p@samsung.com>
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
 * You should have received a copy of the GNU General Public License13
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *					Boston, MA  02110-1301, USA.
 *
 * Contributors:
 * - S-Core Co., Ltd
 *
 */

#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>

#include "maru_dummy.h"

#define DRVNAME		"maru_gpio"

struct maru_gpio_dev {
	int addr;
};

struct maru_gpio {
	struct gpio_chip chip;
	struct maru_gpio_dev *dev;
};

static int maru_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	maru_device_dbg(1, "gpio request offset : %u\n", offset);
	return 0;
}

static int maru_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	maru_device_dbg(1, "gpio direction in offset : %u\n", offset);
	return 0;
}

static int maru_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	maru_device_dbg(1, "gpio direction out offset : %u, value : %d\n", offset, value);
	return 0;
}

static int maru_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	maru_device_dbg(1, "gpio get offset : %u\n", offset);
	return 0;
}

static void maru_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	maru_device_dbg(1, "gpio set offset : %u, value : %d\n", offset, value);
}

static int maru_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	maru_device_dbg(1, "gpio to irq offset : %u\n", offset);
	return 0;
}

static struct gpio_chip maru_gpio_chip = {
	.label				= DRVNAME,
	.owner				= THIS_MODULE,
	.request			= maru_gpio_request,
	.direction_input	= maru_gpio_direction_in,
	.direction_output	= maru_gpio_direction_out,
	.get				= maru_gpio_get,
	.set				= maru_gpio_set,
	.to_irq				= maru_gpio_to_irq,
	.can_sleep			= 1,
	.ngpio				= 255,
	.base				= 1,
};

static int maru_gpio_probe(struct platform_device *pdev)
{
	struct maru_gpio *maru_gpio;
	int ret;

	maru_gpio = devm_kzalloc(&pdev->dev, sizeof(*maru_gpio),
				   GFP_KERNEL);
	if (maru_gpio == NULL){
		maru_device_err("Could not allocate maru_gpio\n");
		return -ENOMEM;
	}

	maru_gpio->chip = maru_gpio_chip;
	maru_gpio->chip.dev = &pdev->dev;

	ret = gpiochip_add(&maru_gpio->chip);
	if (ret < 0) {
		maru_device_err("Could not register maru gpiochip %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, maru_gpio);

	maru_device_info("maru gpio device probe done.\n");

	return ret;
}

void maru_gpio_remove(struct platform_device *pdev)
{
	struct maru_gpio *maru_gpio = platform_get_drvdata(pdev);

	gpiochip_remove(&maru_gpio->chip);
}

static struct platform_device *maru_gpio_pdev;

static int __init maru_gpio_device_add(const struct maru_gpio_dev *dev)
{
	int err;

	maru_gpio_pdev = platform_device_alloc(DRVNAME, -1);
	if (!maru_gpio_pdev)
		return -ENOMEM;

	err = platform_device_add_data(maru_gpio_pdev, dev, sizeof(*dev));
	if (err) {
		maru_device_err("Platform data allocation failed\n");
		goto err;
	}

	err = platform_device_add(maru_gpio_pdev);
	if (err) {
		maru_device_err("Device addition failed\n");
		goto err;
	}

	maru_device_info("maru_gpio_device is added.\n");
	return 0;
err:
	platform_device_put(maru_gpio_pdev);

	return err;
}

static struct platform_driver maru_gpio_driver = {
	.driver = {
		.name	= DRVNAME,
		.owner	= THIS_MODULE,
	},
	.probe		= maru_gpio_probe,
	.remove		= maru_gpio_remove,
};

static int __init maru_gpio_init(void)
{
	int err;
	struct maru_gpio_dev dev;

	err = platform_driver_register(&maru_gpio_driver);
	if (!err) {
		err = maru_gpio_device_add(&dev);
		if (err)
			platform_driver_unregister(&maru_gpio_driver);
	}

	return err;
}
subsys_initcall(maru_gpio_init);

static void __exit maru_gpio_exit(void)
{
	platform_driver_unregister(&maru_gpio_driver);
}
module_exit(maru_gpio_exit);

MODULE_AUTHOR("Jinhyung Choi");
MODULE_DESCRIPTION("Maru Dummy GPIO driver");
MODULE_LICENSE("GPL");
