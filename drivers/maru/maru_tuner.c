/*
 * MARU Virtual Tuner Driver
 *
 * Copyright (c) 2011 - 2013 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * Byeongki Shin <bk0121.shin@samsung.com>
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

#include <linux/module.h>
//#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>

#include "../drivers/media/dvb/dvb-core/demux.h"
#include "../drivers/media/dvb/dvb-core/dmxdev.h"
#include "../drivers/media/dvb/dvb-core/dvb_demux.h"
#include "../drivers/media/dvb/dvb-core/dvb_frontend.h"
#include "../drivers/media/dvb/dvb-core/dvbdev.h"
//#include "../drivers/media/dvb/dvb-core/dvb_net.h"

#include "../drivers/media/dvb/dvb-core/dvb_frontend.h"

#define MARUTUNER_DEBUG_LEVEL	0

static unsigned debug = 0;

#define print_err(fmt, arg...) \
	printk(KERN_ERR "[marutuner](%s): " fmt, __func__, ##arg)

#define print_warn(fmt, arg...) \
	printk(KERN_WARNING "[marutuner](%s): " fmt, __func__, ##arg)

#define print_info(fmt, arg...) \
	printk(KERN_INFO "[marutuner](%s): " fmt, __func__, ##arg)

#define print_dbg(level, fmt, arg...) \
	do { \
		if (debug >= (level)) { \
			printk(KERN_DEBUG "[marutuner](%s:%d): " fmt, \
					__func__, __LINE__, ##arg); \
		} \
	} while (0)


DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define MARUTUNER_MODULE_NAME	"marutuner"
#define DRIVER_NAME		"marutuner"
#define MARUTUNER_TS_PACKETS	1024
//#define MARUTUNER_MEM_SIZE	(188 * MARUTUNER_TS_PACKETS)    /* size of a TS packet is 188 byte */
#define MARUTUNER_MEM_SIZE	0x2000

/*
 * PCI register definition
 * must sync with GUEST tuner driver
 */
enum marutuner_cmd_reg {
    MARUTUNER_FE_STATUS = 0x00,
    MARUTUNER_FE_FREQ	= 0x04,
    MARUTUNER_FE_MOD	= 0x08,
    MARUTUNER_FE_TUNE   = 0x0C,

    MARUTUNER_SETDMA	= 0x10,
    MARUTUNER_START	= 0x14,
    MARUTUNER_INT	= 0x18,
    MARUTUNER_HWPTR	= 0x1C,
};

/* frontend status */
#define	MARUTUNER_FE_TUNE_FAILED	0x00
#define MARUTUNER_FE_HAS_ONLY_PARAM	0x01
#define	MARUTUNER_FE_HAS_TS		0x02

/* demux interrupt */
#define MARUTUNER_DMA_COMPLETE_INT	0x01
#define MARUTUNER_ERR_INT		0x08

struct marutuner_fe_state {
	struct dvb_frontend frontend;
	struct marutuner_dev *dev;

	u32 current_frequency;
	fe_modulation_t current_modulation;
};

struct marutuner_dev {
	/* pci */
	struct pci_dev *pdev;
	//resource_size_t io_base;
	//resource_size_t io_size;
	void __iomem *io_mem;
	//resource_size_t mem_base;	/* virtual dma address*/
	//resource_size_t mem_size;
	//void __iomem *data_mem;

	/* dvb */
	struct dmx_frontend hw_frontend;
	struct dmx_frontend mem_frontend;
	struct dmxdev dmxdev;
	struct dvb_adapter dvb_adapter;
	struct dvb_demux demux;
	struct dvb_frontend *fe;
	//struct dvb_net dvbnet;
	unsigned int full_ts_users;

	/* irq */
	unsigned int eos;		//end-of-stream

	/* dma */
	dma_addr_t dma_addr;
	unsigned char *ts_buf;
};

/*****************************************************************************
 * frontend
 *****************************************************************************/

static int marutuner_fe_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	*ber = 1;	//usually how?
	return 0;
}

static int marutuner_fe_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	*strength = 1;	//usually how?
	return 0;
}

static int marutuner_fe_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	*snr = 1;	//usually how?
	return 0;
}

static int marutuner_fe_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	*ucblocks = 0;	//usually how?
	return 0;
}

static int marutuner_fe_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct marutuner_fe_state *state = fe->demodulator_priv;
	struct marutuner_dev *marutuner = state->dev;
	unsigned int st = 0;
	fe_status_t tmp = FE_HAS_SIGNAL
			| FE_HAS_CARRIER
			| FE_HAS_VITERBI
			| FE_HAS_SYNC
			| FE_HAS_LOCK;

	st = ioread32(marutuner->io_mem + MARUTUNER_FE_STATUS);
	//print_info("tmp = 0x%02x, fe status = %d\n", tmp, st);

	switch (st & 0xFF) {
	case MARUTUNER_FE_TUNE_FAILED:
		tmp = 0;
		break;

	case MARUTUNER_FE_HAS_ONLY_PARAM:
		tmp &= ~FE_HAS_LOCK;
		break;

	case MARUTUNER_FE_HAS_TS:
		break;

	default:
		print_err("unknown status\n");
		break;
	}

	*status = tmp;
	print_info("status = %x\n", *status);

	return 0;
}

/*
 * Only needed if it actually reads something from the hardware
 */
static int marutuner_fe_get_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct marutuner_fe_state *state = fe->demodulator_priv;

	c->frequency = state->current_frequency;
	c->modulation = state->current_modulation;

	return 0;
}

static int marutuner_fe_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct marutuner_fe_state *state = fe->demodulator_priv;
	struct marutuner_dev *marutuner = state->dev;
	//int ret = -EINVAL;

	print_dbg(0, "freq = %d, mod = %d\n", c->frequency, c->modulation);

	if ((c->frequency < fe->ops.info.frequency_min)
	    || (c->frequency > fe->ops.info.frequency_max))
		return -EINVAL;

	/* check supported modulation. used SWITCH-CASE for extension */
	switch (c->modulation) {
	case VSB_8:
	case QAM_64:
	case QAM_256:
		/* Nothing to do */
		break;
	default:
		return -EINVAL;
	}

	//marutuner frontend setting
	iowrite32(c->frequency, marutuner->io_mem + MARUTUNER_FE_FREQ);
	iowrite32(c->modulation, marutuner->io_mem + MARUTUNER_FE_MOD);
	iowrite32(1, marutuner->io_mem + MARUTUNER_FE_TUNE);

	state->current_frequency = c->frequency;
	state->current_modulation = c->modulation;

	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	return 0;
}

static int marutuner_fe_sleep(struct dvb_frontend* fe)
{
	return 0;
}

static void marutuner_fe_release(struct dvb_frontend* fe)
{
	struct marutuner_fe_state* state = fe->demodulator_priv;

	kfree(state);

	print_info("\n");
}

static int marutuner_fe_init(struct dvb_frontend* fe)
{
	return 0;
}

static struct dvb_frontend_ops marutuner_fe_ops = {
	.delsys = { SYS_ATSC },
	.info = {
		.name			= "Marutuner ATSC",
		.frequency_min		= 54000000,
		.frequency_max		= 858000000,
		.frequency_stepsize	= 62500,
#if 0 /* maybe not need */
		.symbol_rate_min	=
		.symbol_rate_max	=
#endif
		.caps = FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_8VSB
#if 0 /* maybe full feature lists are same below */
		.caps =
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_8VSB | FE_CAN_16VSB |
			FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_128 | FE_CAN_QAM_256
#endif
	},

	.release = marutuner_fe_release,

	.init = marutuner_fe_init,
	.sleep = marutuner_fe_sleep,

	.set_frontend = marutuner_fe_set_frontend,
	.get_frontend = marutuner_fe_get_frontend,

	.read_status = marutuner_fe_read_status,
	.read_ber = marutuner_fe_read_ber,
	.read_signal_strength = marutuner_fe_read_signal_strength,
	.read_snr = marutuner_fe_read_snr,
	.read_ucblocks = marutuner_fe_read_ucblocks,
};

struct dvb_frontend *marutuner_fe_attach(struct marutuner_dev *marutuner)
{
	struct marutuner_fe_state *state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct marutuner_fe_state), GFP_KERNEL);
	if (!state) {
		print_err("Can't allocate memory for marutuner frontend state\n");
		return NULL;
	}

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &marutuner_fe_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	state->dev = marutuner;

	return &state->frontend;
}

/*****************************************************************************
 * adapter
 *****************************************************************************/

static inline struct marutuner_dev *feed_to_marutuner(struct dvb_demux_feed *feed)
{
	return container_of(feed->demux, struct marutuner_dev, demux);
}

static int __devinit marutuner_dma_map(struct marutuner_dev *marutuner)
{
	int ret = 0;

	marutuner->ts_buf = pci_alloc_consistent(marutuner->pdev,
						MARUTUNER_MEM_SIZE,
						&marutuner->dma_addr);
	if (!marutuner->ts_buf)
		ret = -ENOMEM;

	return ret;
}

static void marutuner_dma_unmap(struct marutuner_dev *marutuner)
{
	pci_free_consistent(marutuner->pdev,
			MARUTUNER_MEM_SIZE,
			marutuner->ts_buf,
			marutuner->dma_addr);
}

static void marutuner_set_dma_addr(struct marutuner_dev *marutuner)
{
	iowrite32(marutuner->dma_addr, marutuner->io_mem + MARUTUNER_SETDMA);
}

static irqreturn_t marutuner_isr(int irq, void *dev_id)
{
	struct marutuner_dev *marutuner = dev_id;
	unsigned int intr = 0;
	unsigned int status = 0;
	unsigned int count = 0;

	intr = ioread32(marutuner->io_mem + MARUTUNER_INT);
	if (!intr) {
		//print_info("This irq is not for this module\n");
		return IRQ_NONE;
	}

	print_info("This irq is mine\n");

	/* TODO : error handling need? */
	if (intr & MARUTUNER_ERR_INT) {
		print_err("tuner device has errors(intr = %u)\n", intr);
		//increase error count
		return IRQ_HANDLED;
	}

	count = ioread32(marutuner->io_mem + MARUTUNER_HWPTR);
	print_dbg(0, "dma read count = %d\n", count);
	if (count > MARUTUNER_MEM_SIZE) {
		print_err("DMA size overflow\n");
		/* TODO : handle */
	} else if (count < MARUTUNER_MEM_SIZE) {
		marutuner->eos = 1;
	}

#if 0
	u8 *buf = marutuner->ts_buf;
	print_dbg(0, "ts_buf(%p) = %02x %02x %02x %02x\n", buf,
				buf[0], buf[1], buf[2], buf[3]);
#endif
	dvb_dmx_swfilter(&marutuner->demux, marutuner->ts_buf, count);

	/* if the dma isn't working, should not request for dma start */
	status = ioread32(marutuner->io_mem + MARUTUNER_START);
	print_dbg(0, "dma status register : %d\n", status);
	if (status)
		iowrite32(1, marutuner->io_mem + MARUTUNER_START);

	return IRQ_HANDLED;
}

static int marutuner_start_feed(struct dvb_demux_feed *f)
{
	struct marutuner_dev *marutuner = feed_to_marutuner(f);

	print_dbg(0, "full_ts_users = %d\n", marutuner->full_ts_users);
	if (marutuner->full_ts_users++ == 0)
		iowrite32(1, marutuner->io_mem + MARUTUNER_START);

	return 0;
}

static int marutuner_stop_feed(struct dvb_demux_feed *f)
{
	struct marutuner_dev *marutuner = feed_to_marutuner(f);

	if (--marutuner->full_ts_users == 0)
		iowrite32(0, marutuner->io_mem + MARUTUNER_START);

	return 0;
}
static int __devinit marutuner_hw_init(struct marutuner_dev *marutuner)
{
	/* clear tsfile mapping table */
	//marutuner_reset_frontend(marutuner);

	/* map DMA and set address */
	marutuner_dma_map(marutuner);
	marutuner_set_dma_addr(marutuner);

	return 0;
}

static void marutuner_hw_exit(struct marutuner_dev *marutuner)
{
	marutuner_dma_unmap(marutuner);
}

static int __devinit marutuner_frontend_init(struct marutuner_dev *marutuner)
{
	int ret;

	marutuner->fe = marutuner_fe_attach(marutuner);
	if (!marutuner->fe) {
		print_err("could not attach frontend\n");
		return -ENODEV;
	}

	ret = dvb_register_frontend(&marutuner->dvb_adapter, marutuner->fe);
	if (ret < 0) {
		if (marutuner->fe->ops.release)
			marutuner->fe->ops.release(marutuner->fe);
		return ret;
	}

	return 0;
}

static int __devinit marutuner_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	struct marutuner_dev *marutuner;
	struct dvb_adapter *dvb_adapter;
	struct dvb_demux *dvbdemux;
	struct dmx_demux *dmx;
	int ret = -ENOMEM;


	marutuner = kzalloc(sizeof(struct marutuner_dev), GFP_KERNEL);
	if (!marutuner) {
		print_err("no memory\n");
		goto out;
	}

	marutuner->pdev = pdev;

	ret = pci_enable_device(pdev);
	if (ret < 0) {
		print_dbg(0, "failed pci_enable_device\n");
		goto err_kfree;
	}

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret < 0) {
		print_dbg(0, "failed pci_set_dma_mask\n");
		goto err_pci_disable_device;
	}

	pci_set_master(pdev);

	ret = pci_request_region(pdev, 0, DRIVER_NAME);
	if (ret < 0) {
		print_dbg(0, "failed pci_request_region\n");
		goto err_pci_disable_device;
	}

	marutuner->io_mem = pci_iomap(pdev, 0, pci_resource_len(pdev, 0));
	if (!marutuner->io_mem) {
		ret = -EIO;
		print_dbg(0, "failed pci_iomap\n");
		goto err_pci_release_regions;
	}

	pci_set_drvdata(pdev, marutuner);

	ret = request_irq(pdev->irq, marutuner_isr, IRQF_SHARED, DRIVER_NAME, marutuner);
	if (ret < 0) {
		print_dbg(0, "failed request_irq\n");
		goto err_pci_iounmap;
	}

	ret = marutuner_hw_init(marutuner);
	if (ret < 0) {
		print_dbg(0, "failed marutuner_hw_init\n");
		goto err_free_irq;
	}

	/* dvb */
	ret = dvb_register_adapter(&marutuner->dvb_adapter, DRIVER_NAME,
				   THIS_MODULE, &pdev->dev, adapter_nr);
	if (ret < 0) {
		print_dbg(0, "failed dvb_register_adapter\n");
		goto err_marutuner_hw_exit;
	}

	dvb_adapter = &marutuner->dvb_adapter;

	dvbdemux = &marutuner->demux;
	dvbdemux->filternum = 256;
	dvbdemux->feednum = 256;
	dvbdemux->start_feed = marutuner_start_feed;
	dvbdemux->stop_feed = marutuner_stop_feed;
	dvbdemux->dmx.capabilities = 0;		/* I can't find when this is used */

	ret = dvb_dmx_init(dvbdemux);
	if (ret < 0) {
		print_dbg(0, "failed dvb_dmx_init\n");
		goto err_dvb_unregister_adapter;
	}

	dmx = &dvbdemux->dmx;
	marutuner->dmxdev.filternum = 256;
	marutuner->dmxdev.demux = dmx;

	ret = dvb_dmxdev_init(&marutuner->dmxdev, dvb_adapter);
	if (ret < 0) {
		print_dbg(0, "failed dvb_dmxdev_init\n");
		goto err_dvb_dmx_release;
	}

	marutuner->hw_frontend.source = DMX_FRONTEND_0;
	ret = dmx->add_frontend(dmx, &marutuner->hw_frontend);
	if (ret < 0) {
		print_dbg(0, "failed add_frontend hw\n");
		goto err_dvb_dmxdev_release;
	}

	marutuner->mem_frontend.source = DMX_MEMORY_FE;
	ret = dmx->add_frontend(dmx, &marutuner->mem_frontend);
	if (ret < 0) {
		print_dbg(0, "failed add_frontend mem\n");
		goto err_remove_hw_frontend;
	}

	ret = dmx->connect_frontend(dmx, &marutuner->hw_frontend);
	if (ret < 0) {
		print_dbg(0, "failed connect_frontend hw\n");
		goto err_remove_mem_frontend;
	}

	ret = marutuner_frontend_init(marutuner);
	if (ret < 0) {
		print_dbg(0, "failed marutuner_frontend_init\n");
		goto err_disconnect_frontend;
	}

out:
	return ret;

err_disconnect_frontend:
	dmx->disconnect_frontend(dmx);
err_remove_mem_frontend:
	dmx->remove_frontend(dmx, &marutuner->mem_frontend);
err_remove_hw_frontend:
	dmx->remove_frontend(dmx, &marutuner->hw_frontend);
err_dvb_dmxdev_release:
	dvb_dmxdev_release(&marutuner->dmxdev);
err_dvb_dmx_release:
	dvb_dmx_release(dvbdemux);
err_dvb_unregister_adapter:
	dvb_unregister_adapter(dvb_adapter);
err_marutuner_hw_exit:
	marutuner_hw_exit(marutuner);
err_free_irq:
	free_irq(pdev->irq, marutuner);
err_pci_iounmap:
	pci_iounmap(pdev, marutuner->io_mem);
err_pci_release_regions:
	pci_release_regions(pdev);
err_pci_disable_device:
	pci_disable_device(pdev);
err_kfree:
	pci_set_drvdata(pdev, NULL);
	kfree(marutuner);
	goto out;

	return 0;
}

static void marutuner_remove(struct pci_dev *pdev)
{
	struct marutuner_dev *marutuner = pci_get_drvdata(pdev);

	if (marutuner == NULL) {
		print_warn("pci_remove on unknown pdev %p.\n", pdev);
		return ;
	}

}

static struct pci_device_id marutuner_id_table[] __devinitdata = {
	{
		.vendor = PCI_VENDOR_ID_TIZEN,
		.device = PCI_DEVICE_ID_VIRTUAL_TUNER,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
	},
	{},
};

MODULE_DEVICE_TABLE(pci, marutuner_id_table);

static struct pci_driver marutuner_driver = {
	.name		= MARUTUNER_MODULE_NAME,
	.id_table	= marutuner_id_table,
	.probe		= marutuner_probe,
	.remove		= marutuner_remove,
};

static int __init marutuner_init(void)
{
	int retv = 0;

	retv = pci_register_driver(&marutuner_driver);
	if (retv < 0)
		print_err("module loading fail");

	return retv;
}

static void __exit marutuner_exit(void)
{
	pci_unregister_driver(&marutuner_driver);
}

module_init(marutuner_init);
module_exit(marutuner_exit);

MODULE_DESCRIPTION("MARU Virtual Tuner Driver");
MODULE_AUTHOR("Byeongki Shin <bk0121.shin@samsung.com>");
MODULE_LICENSE("GPL");
