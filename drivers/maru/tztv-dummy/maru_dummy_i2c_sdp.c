/*
 * MARU Dummy I2C SDP driver
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *					Boston, MA  02110-1301, USA.
 *
 * Contributors:
 * - S-Core Co., Ltd
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <linux/semaphore.h>

#include "maru_dummy.h"

/* i2c controller state */
enum sdp_i2c_state {
	STATE_IDLE,
	STATE_START,
	STATE_READ,
	STATE_WRITE,
	STATE_STOP
};

struct sdp_platform_i2c {
	int		bus_num;
	unsigned int	flags;
	unsigned int	slave_addr;
	unsigned long	frequency;
	unsigned int	sda_delay;
	unsigned int	irq_reg;
};

struct sdp_i2c {
	wait_queue_head_t	wait;

	struct i2c_msg		*msg;
	unsigned int		msg_num;
	unsigned int		msg_idx;
	unsigned int		msg_ptr;

	unsigned int		tx_setup;
	unsigned int		irq;

	enum sdp_i2c_state	state;
	unsigned long		clkrate;

	void __iomem		*regs;
	void __iomem		*irq_reg;
	struct clk		*clk;
	struct device		*dev;
	struct i2c_adapter	adap;

	struct sdp_platform_i2c	*pdata;

};

static spinlock_t		lock_pend;
static spinlock_t 		lock_int;

static int sdp_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msgs, int num)
{
	maru_device_dbg(1, "sdp_i2c_xfer");
	return 0;
}

static u32 sdp_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_NOSTART |
		I2C_FUNC_PROTOCOL_MANGLING;
}

static const struct i2c_algorithm sdp_i2c_algorithm = {
	.master_xfer		= sdp_i2c_xfer,
	.functionality		= sdp_i2c_func,
};

static int sdp_i2c_get_irq_reg(struct sdp_i2c *i2c)
{
	struct sdp_platform_i2c *pdata;
	pdata = i2c->pdata;

	i2c->irq_reg = ioremap(pdata->irq_reg, 8);

	if (!i2c->irq_reg) {
		maru_device_err("Can't get interrupts status register\n");
		return -ENXIO;
	}

	return 0;
}

static int sdp_i2c_probe(struct platform_device *pdev)
{
	struct sdp_i2c *i2c;
	struct sdp_platform_i2c *pdata = NULL;
	int ret;

	i2c = devm_kzalloc(&pdev->dev, sizeof(struct sdp_i2c), GFP_KERNEL);
	if (!i2c) {
		maru_device_err("no memory for state\n");
		return -ENOMEM;
	}

	i2c->pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!i2c->pdata) {
		ret = -ENOMEM;
		goto err_noclk;
	}

	spin_lock_init(&lock_pend);
	spin_lock_init(&lock_int);

	strlcpy(i2c->adap.name, "sdp-i2c", sizeof(i2c->adap.name));
	i2c->adap.owner   = THIS_MODULE;
	i2c->adap.algo    = &sdp_i2c_algorithm;
	i2c->adap.retries = 2;
	i2c->adap.class   = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	i2c->tx_setup     = 50;

	init_waitqueue_head(&i2c->wait);

	i2c->dev = &pdev->dev;
	i2c->clk = clk_get(&pdev->dev, "rstn_i2c");
	if (IS_ERR(i2c->clk)) {
		maru_device_err("cannot get clock\n");
		ret = -ENOENT;
		goto err_noclk;
	}

	clk_prepare_enable(i2c->clk);

	i2c->adap.algo_data = i2c;
	i2c->adap.dev.parent = &pdev->dev;

	ret = sdp_i2c_get_irq_reg(i2c);
	if (ret != 0)
		goto err_clk;

	i2c->adap.nr = 6;
	ret = i2c_add_numbered_adapter(&i2c->adap);
	if (ret < 0) {
		maru_device_err("failed to add bus to i2c core\n");
		goto err_irq;
	}

	platform_set_drvdata(pdev, i2c);

	return 0;

 err_irq:
	free_irq(i2c->irq, i2c);

 err_clk:
	clk_disable_unprepare(i2c->clk);
	clk_put(i2c->clk);

 err_noclk:
	return ret;
}

static int sdp_i2c_remove(struct platform_device *pdev)
{
	struct sdp_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);
	free_irq(i2c->irq, i2c);

	clk_disable_unprepare(i2c->clk);
	clk_put(i2c->clk);

	return 0;
}

static struct platform_driver sdp_i2c_driver = {
	.probe		= sdp_i2c_probe,
	.remove		= sdp_i2c_remove,
	.driver = {
		.name	= "sdp-i2c",
		.owner	= THIS_MODULE,
	},
};

struct maru_i2c_dev {
	int addr;
};

static struct platform_device *maru_i2c_pdev;

static int __init maru_i2c_device_add(const struct maru_i2c_dev *dev)
{
	int err;

	maru_i2c_pdev = platform_device_alloc("sdp-i2c", -1);
	if (!maru_i2c_pdev)
		return -ENOMEM;

	err = platform_device_add_data(maru_i2c_pdev, dev, sizeof(*dev));
	if (err) {
		maru_device_err("Platform data allocation failed\n");
		goto err;
	}

	err = platform_device_add(maru_i2c_pdev);
	if (err) {
		maru_device_err("Device addition failed\n");
		goto err;
	}

	maru_device_info("maru_i2c_device is added.\n");
	return 0;
err:
	platform_device_put(maru_i2c_pdev);

	return err;
}

static int __init sdp_i2c_init(void)
{
	int err;

	struct maru_i2c_dev dev;

	err = platform_driver_register(&sdp_i2c_driver);
	if (!err) {
		err = maru_i2c_device_add(&dev);
		if (err)
			platform_driver_unregister(&sdp_i2c_driver);
	}

	return err;
}
subsys_initcall(sdp_i2c_init);

static void __exit sdp_i2c_exit(void)
{
	platform_driver_unregister(&sdp_i2c_driver);
}
module_exit(sdp_i2c_exit);

MODULE_AUTHOR("Jinhyung Choi");
MODULE_DESCRIPTION("Maru Dummy I2C SDP driver");
MODULE_LICENSE("GPL");