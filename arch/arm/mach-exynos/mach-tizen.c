/*
 * linux/arch/arm/mach-exynos4/mach-tizen.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/i2c/atmel_mxt_ts.h>
#include <linux/i2c-gpio.h>
#include <linux/gpio_keys.h>
#include <linux/gpio.h>
#include <linux/power/max8903_charger.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/mfd/max8997.h>
#include <linux/mfd/max8997-private.h>
#include <linux/mmc/host.h>
#include <linux/fb.h>
#include <linux/pwm_backlight.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/regulator/machine.h>
#include <linux/dma-mapping.h>
#include <linux/amba/bus.h>

#include <video/platform_lcd.h>
#include <media/m5mols.h>
#include <media/s5k6aa.h>
#include <media/s5p_fimc.h>
#include <media/v4l2-mediabus.h>

#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/mach-types.h>

#include <plat/adc.h>
#include <plat/regs-fb-v4.h>
#include <plat/regs-serial.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/fb.h>
#include <plat/sdhci.h>
#include <plat/ehci.h>
#include <plat/clock.h>
#include <plat/gpio-cfg.h>
#include <plat/iic.h>
#include <plat/mfc.h>
#include <plat/pd.h>
#include <plat/fimc-core.h>
#include <plat/camport.h>
#include <plat/mipi_csis.h>

#include <mach/map.h>
#include <mach/regs-pmu.h>

#include "common.h"

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define TIZEN_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define TIZEN_ULCON_DEFAULT	S3C2410_LCON_CS8

#define TIZEN_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG256 |	\
				 S5PV210_UFCON_RXTRIG256)

enum fixed_regulator_id {
	FIXED_REG_ID_MMC = 0,
	FIXED_REG_ID_MAX8903,
	FIXED_REG_ID_CAM_A28V,
	FIXED_REG_ID_CAM_12V,
	FIXED_REG_ID_CAM_VT_15V,
};

static struct s3c2410_uartcfg tizen_uartcfgs[] __initdata = {
	{
		.hwport		= 0,
		.ucon		= TIZEN_UCON_DEFAULT,
		.ulcon		= TIZEN_ULCON_DEFAULT,
		.ufcon		= TIZEN_UFCON_DEFAULT,
	},
	{
		.hwport		= 1,
		.ucon		= TIZEN_UCON_DEFAULT,
		.ulcon		= TIZEN_ULCON_DEFAULT,
		.ufcon		= TIZEN_UFCON_DEFAULT,
	},
	{
		.hwport		= 2,
		.ucon		= TIZEN_UCON_DEFAULT,
		.ulcon		= TIZEN_ULCON_DEFAULT,
		.ufcon		= TIZEN_UFCON_DEFAULT,
	},
	{
		.hwport		= 3,
		.ucon		= TIZEN_UCON_DEFAULT,
		.ulcon		= TIZEN_ULCON_DEFAULT,
		.ufcon		= TIZEN_UFCON_DEFAULT,
	},
};

/* eMMC */
static struct s3c_sdhci_platdata tizen_hsmmc0_data __initdata = {
	.max_width		= 8,
	.host_caps		= (MMC_CAP_8_BIT_DATA | MMC_CAP_4_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED |
				MMC_CAP_ERASE),
	.cd_type		= S3C_SDHCI_CD_PERMANENT,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
};

static struct regulator_consumer_supply emmc_supplies[] = {
	REGULATOR_SUPPLY("vmmc", "exynos4-sdhci.0"),
	REGULATOR_SUPPLY("vmmc", "dw_mmc"),
};

static struct regulator_init_data emmc_fixed_voltage_init_data = {
	.constraints		= {
		.name		= "VMEM_VDD_2.8V",
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(emmc_supplies),
	.consumer_supplies	= emmc_supplies,
};

static struct fixed_voltage_config emmc_fixed_voltage_config = {
	.supply_name		= "MASSMEMORY_EN (inverted)",
	.microvolts		= 2800000,
	.gpio			= EXYNOS4_GPL1(1),
	.enable_high		= false,
	.init_data		= &emmc_fixed_voltage_init_data,
};

static struct platform_device emmc_fixed_voltage = {
	.name			= "reg-fixed-voltage",
	.id			= FIXED_REG_ID_MMC,
	.dev			= {
		.platform_data	= &emmc_fixed_voltage_config,
	},
};

/* SD */
static struct s3c_sdhci_platdata tizen_hsmmc2_data __initdata = {
	.max_width		= 4,
	.host_caps		= MMC_CAP_4_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
	.ext_cd_gpio		= EXYNOS4_GPX3(3),	/* XEINT_27 */
	.ext_cd_gpio_invert	= 1,
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
};

/* WLAN */
static struct s3c_sdhci_platdata tizen_hsmmc3_data __initdata = {
	.max_width		= 4,
	.host_caps		= MMC_CAP_4_BIT_DATA |
				MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
	.cd_type		= S3C_SDHCI_CD_EXTERNAL,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
};

static void __init tizen_sdhci_init(void)
{
	s3c_sdhci0_set_platdata(&tizen_hsmmc0_data);
	s3c_sdhci2_set_platdata(&tizen_hsmmc2_data);
	s3c_sdhci3_set_platdata(&tizen_hsmmc3_data);
}

#define IRQ_PL050    { EXYNOS4_IRQ_SPI2 }
static AMBA_APB_DEVICE(kmi0,  "kmi-pl050", 0x41050, 0x12E30000,
        IRQ_PL050, NULL);

/* Frame Buffer */
static struct s3c_fb_pd_win tizen_fb_win0 = {
	.win_mode = {
		.left_margin	= 64,
		.right_margin	= 16,
		.upper_margin	= 64,
		.lower_margin	= 1,
		.hsync_len	= 48,
		.vsync_len	= 3,
		.xres		= 480,
		.yres		= 800,
		.refresh	= 60,
	},
	.max_bpp	= 32,
	.default_bpp	= 24,
	.virtual_x	= 720,
	.virtual_y	= 1280,
};

static struct s3c_fb_platdata tizen_fb_pdata __initdata = {
	.win[0]		= &tizen_fb_win0,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB |
			  VIDCON0_CLKSEL_LCD,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.setup_gpio	= exynos4_fimd0_gpio_setup_24bpp,
};

#ifdef CONFIG_TIZEN_FGLES
static struct resource tizen_fgles_resources[] = {
	[0] = DEFINE_RES_MEM(0xcf000000, 0x100000),
};

struct platform_device tizen_fgles_device = {
	.name		= "kfgles2",
	.id		= 0,
	.resource	= tizen_fgles_resources,
	.num_resources	= ARRAY_SIZE(tizen_fgles_resources),
};
#endif /* CONFIG_TIZEN_FGLES */

static void tizen_lcd_power_on(struct plat_lcd_data *pd, unsigned int power)
{
	int gpio = EXYNOS4_GPE1(5);

	gpio_request(gpio, "LVDS_nSHDN");
	gpio_direction_output(gpio, power);
	gpio_free(gpio);
}

static int tizen_bl_init(struct device *dev)
{
	return gpio_request_one(EXYNOS4_GPE2(3), GPIOF_OUT_INIT_LOW,
				"LCD_LD0_EN");
}

static int tizen_bl_notify(struct device *dev, int brightness)
{
	if (brightness < 1)
		brightness = 0;

	gpio_set_value(EXYNOS4_GPE2(3), 1);

	return brightness;
}

static void tizen_bl_exit(struct device *dev)
{
	gpio_free(EXYNOS4_GPE2(3));
}

/* tizen pwm backlight */
static struct platform_pwm_backlight_data tizen_backlight_data = {
	.pwm_id			= 0,
	.pwm_period_ns		= 30000,
	.max_brightness		= 100,
	.dft_brightness		= 50,
	.init			= tizen_bl_init,
	.notify			= tizen_bl_notify,
	.exit			= tizen_bl_exit,
};

static struct platform_device tizen_backlight_device = {
	.name			= "pwm-backlight",
	.id			= -1,
	.dev			= {
		.parent		= &s3c_device_timer[0].dev,
		.platform_data	= &tizen_backlight_data,
	},
};

static struct plat_lcd_data tizen_lcd_platform_data = {
	.set_power		= tizen_lcd_power_on,
};

static struct platform_device tizen_lcd_device = {
	.name			= "platform-lcd",
	.id			= -1,
	.dev			= {
		.platform_data	= &tizen_lcd_platform_data,
	},
};

static struct regulator_init_data wm8994_ldo1_data = {
	.constraints	= {
		.name		= "AVDD1_3.0V",
	},
};

static struct regulator_init_data wm8994_ldo2_data = {
	.constraints	= {
		.name		= "DCVDD_1.0V",
	},
};

static struct wm8994_pdata wm8994_platform_data = {
	.ldo[0]	= { 0, &wm8994_ldo1_data },
	.ldo[1]	= { 0, &wm8994_ldo2_data },
};

/* I2C1 */
static struct i2c_board_info i2c1_devs[] __initdata = {
	{
		I2C_BOARD_INFO("wm8994", 0x1a),
		.platform_data	= &wm8994_platform_data,
	},
};

static struct regulator_consumer_supply __initdata max8997_ldo1_[] = {
	REGULATOR_SUPPLY("vdd", "s5p-adc"), /* Used by CPU's ADC drv */
};
static struct regulator_consumer_supply __initdata max8997_ldo3_[] = {
	REGULATOR_SUPPLY("vdd11", "s5p-mipi-csis.0"), /* MIPI */
};
static struct regulator_consumer_supply __initdata max8997_ldo4_[] = {
	REGULATOR_SUPPLY("vdd18", "s5p-mipi-csis.0"), /* MIPI */
};
static struct regulator_consumer_supply __initdata max8997_ldo5_[] = {
	REGULATOR_SUPPLY("vhsic", "modemctl"), /* MODEM */
};
static struct regulator_consumer_supply tizen_max8997_ldo6_consumer[] = {
	REGULATOR_SUPPLY("vdd_reg", "6-003c"), /* S5K6AA camera */
};
static struct regulator_consumer_supply __initdata max8997_ldo7_[] = {
	REGULATOR_SUPPLY("dig_18", "0-001f"), /* HCD803 */
};
static struct regulator_consumer_supply __initdata max8997_ldo8_[] = {
	REGULATOR_SUPPLY("vusb_d", NULL), /* Used by CPU */
	REGULATOR_SUPPLY("vdac", NULL), /* Used by CPU */
};
static struct regulator_consumer_supply __initdata max8997_ldo11_[] = {
	REGULATOR_SUPPLY("vcc", "platform-lcd"), /* U804 LVDS */
};
static struct regulator_consumer_supply __initdata max8997_ldo12_[] = {
	REGULATOR_SUPPLY("vddio", "6-003c"), /* HDC802 */
};
static struct regulator_consumer_supply __initdata max8997_ldo13_[] = {
	REGULATOR_SUPPLY("vmmc", "exynos4-sdhci.2"), /* TFLASH */
};
static struct regulator_consumer_supply __initdata max8997_ldo14_[] = {
	REGULATOR_SUPPLY("inmotor", "max8997-haptic"),
};
static struct regulator_consumer_supply __initdata max8997_ldo15_[] = {
	REGULATOR_SUPPLY("avdd", "3-004a"), /* Touch Screen */
};
static struct regulator_consumer_supply __initdata max8997_ldo16_[] = {
	REGULATOR_SUPPLY("d_sensor", "0-001f"), /* HDC803 */
};
static struct regulator_consumer_supply __initdata max8997_ldo18_[] = {
	REGULATOR_SUPPLY("vdd", "3-004a"), /* Touch Screen */
};
static struct regulator_consumer_supply __initdata max8997_buck1_[] = {
	REGULATOR_SUPPLY("vdd_arm", NULL), /* CPUFREQ */
};
static struct regulator_consumer_supply __initdata max8997_buck2_[] = {
	REGULATOR_SUPPLY("vdd_int", "exynos4210-busfreq.0"), /* CPUFREQ */
};
static struct regulator_consumer_supply __initdata max8997_buck3_[] = {
	REGULATOR_SUPPLY("vdd", "mali_dev.0"), /* G3D of Exynos 4 */
};
static struct regulator_consumer_supply __initdata max8997_buck4_[] = {
	REGULATOR_SUPPLY("core", "0-001f"), /* HDC803 */
};
static struct regulator_consumer_supply __initdata max8997_buck6_[] = {
	REGULATOR_SUPPLY("dig_28", "0-001f"), /* pin "7" of HDC803 */
};
static struct regulator_consumer_supply __initdata max8997_esafeout1_[] = {
	REGULATOR_SUPPLY("usb_vbus", NULL), /* CPU's USB OTG */
};
static struct regulator_consumer_supply __initdata max8997_esafeout2_[] = {
	REGULATOR_SUPPLY("usb_vbus", "modemctl"), /* VBUS of Modem */
};

static struct regulator_consumer_supply __initdata max8997_charger_[] = {
	REGULATOR_SUPPLY("vinchg1", "charger-manager.0"),
};
static struct regulator_consumer_supply __initdata max8997_chg_toff_[] = {
	REGULATOR_SUPPLY("vinchg_stop", NULL), /* for jack interrupt handlers */
};

static struct regulator_consumer_supply __initdata max8997_32khz_ap_[] = {
	REGULATOR_SUPPLY("gps_clk", "bcm4751"),
	REGULATOR_SUPPLY("bt_clk", "bcm4330-b1"),
	REGULATOR_SUPPLY("wifi_clk", "bcm433-b1"),
};

static struct regulator_init_data __initdata max8997_ldo1_data = {
	.constraints	= {
		.name		= "VADC_3.3V_C210",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo1_),
	.consumer_supplies	= max8997_ldo1_,
};

static struct regulator_init_data __initdata max8997_ldo2_data = {
	.constraints	= {
		.name		= "VALIVE_1.1V_C210",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_ldo3_data = {
	.constraints	= {
		.name		= "VUSB_1.1V_C210",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo3_),
	.consumer_supplies	= max8997_ldo3_,
};

static struct regulator_init_data __initdata max8997_ldo4_data = {
	.constraints	= {
		.name		= "VMIPI_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo4_),
	.consumer_supplies	= max8997_ldo4_,
};

static struct regulator_init_data __initdata max8997_ldo5_data = {
	.constraints	= {
		.name		= "VHSIC_1.2V_C210",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo5_),
	.consumer_supplies	= max8997_ldo5_,
};

static struct regulator_init_data __initdata max8997_ldo6_data = {
	.constraints	= {
		.name		= "VCC_1.8V_PDA",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(tizen_max8997_ldo6_consumer),
	.consumer_supplies	= tizen_max8997_ldo6_consumer,
};

static struct regulator_init_data __initdata max8997_ldo7_data = {
	.constraints	= {
		.name		= "CAM_ISP_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo7_),
	.consumer_supplies	= max8997_ldo7_,
};

static struct regulator_init_data __initdata max8997_ldo8_data = {
	.constraints	= {
		.name		= "VUSB+VDAC_3.3V_C210",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo8_),
	.consumer_supplies	= max8997_ldo8_,
};

static struct regulator_init_data __initdata max8997_ldo9_data = {
	.constraints	= {
		.name		= "VCC_2.8V_PDA",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_ldo10_data = {
	.constraints	= {
		.name		= "VPLL_1.1V_C210",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_ldo11_data = {
	.constraints	= {
		.name		= "LVDS_VDD3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.boot_on	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo11_),
	.consumer_supplies	= max8997_ldo11_,
};

static struct regulator_init_data __initdata max8997_ldo12_data = {
	.constraints	= {
		.name		= "VT_CAM_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo12_),
	.consumer_supplies	= max8997_ldo12_,
};

static struct regulator_init_data __initdata max8997_ldo13_data = {
	.constraints	= {
		.name		= "VTF_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo13_),
	.consumer_supplies	= max8997_ldo13_,
};

static struct regulator_init_data __initdata max8997_ldo14_data = {
	.constraints	= {
		.name		= "VCC_3.0V_MOTOR",
		.min_uV		= 3000000,
		.max_uV		= 3000000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo14_),
	.consumer_supplies	= max8997_ldo14_,
};

static struct regulator_init_data __initdata max8997_ldo15_data = {
	.constraints	= {
		.name		= "VTOUCH_ADVV2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo15_),
	.consumer_supplies	= max8997_ldo15_,
};

static struct regulator_init_data __initdata max8997_ldo16_data = {
	.constraints	= {
		.name		= "CAM_SENSOR_IO_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo16_),
	.consumer_supplies	= max8997_ldo16_,
};

static struct regulator_init_data __initdata max8997_ldo18_data = {
	.constraints	= {
		.name		= "VTOUCH_VDD2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.apply_uV	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_ldo18_),
	.consumer_supplies	= max8997_ldo18_,
};

static struct regulator_init_data __initdata max8997_ldo21_data = {
	.constraints	= {
		.name		= "VDDQ_M1M2_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_buck1_data = {
	.constraints	= {
		.name		= "VARM_1.2V_C210",
		.min_uV		= 900000,
		.max_uV		= 1350000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE,
		.always_on	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max8997_buck1_),
	.consumer_supplies = max8997_buck1_,
};

static struct regulator_init_data __initdata max8997_buck2_data = {
	.constraints	= {
		.name		= "VINT_1.1V_C210",
		.min_uV		= 900000,
		.max_uV		= 1200000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE,
		.always_on	= 1,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max8997_buck2_),
	.consumer_supplies = max8997_buck2_,
};

static struct regulator_init_data __initdata max8997_buck3_data = {
	.constraints	= {
		.name		= "VG3D_1.1V_C210",
		.min_uV		= 900000,
		.max_uV		= 1100000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max8997_buck3_),
	.consumer_supplies = max8997_buck3_,
};

static struct regulator_init_data __initdata max8997_buck4_data = {
	.constraints	= {
		.name		= "CAM_ISP_CORE_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max8997_buck4_),
	.consumer_supplies = max8997_buck4_,
};

static struct regulator_init_data __initdata max8997_buck5_data = {
	.constraints	= {
		.name		= "VMEM_1.2V_C210",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_buck6_data = {
	.constraints	= {
		.name		= "CAM_AF_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max8997_buck6_),
	.consumer_supplies = max8997_buck6_,
};

static struct regulator_init_data __initdata max8997_buck7_data = {
	.constraints	= {
		.name		= "VCC_SUB_2.0V",
		.min_uV		= 2000000,
		.max_uV		= 2000000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_32khz_ap_data = {
	.constraints	= {
		.name		= "32KHz AP",
		.always_on	= 1,
		.state_mem	= {
			.enabled	= 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max8997_32khz_ap_),
	.consumer_supplies = max8997_32khz_ap_,
};

static struct regulator_init_data __initdata max8997_32khz_cp_data = {
	.constraints	= {
		.name		= "32KHz CP",
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_vichg_data = {
	.constraints	= {
		.name		= "VICHG",
		.state_mem	= {
			.disabled	= 1,
		},
	},
};

static struct regulator_init_data __initdata max8997_esafeout1_data = {
	.constraints	= {
		.name		= "SAFEOUT1",
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_esafeout1_),
	.consumer_supplies	= max8997_esafeout1_,
};

static struct regulator_init_data __initdata max8997_esafeout2_data = {
	.constraints	= {
		.name		= "SAFEOUT2",
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_esafeout2_),
	.consumer_supplies	= max8997_esafeout2_,
};

static struct regulator_init_data __initdata max8997_charger_cv_data = {
	.constraints	= {
		.name		= "CHARGER_CV",
		.min_uV		= 4200000,
		.max_uV		= 4200000,
		.apply_uV	= 1,
	},
};

static struct regulator_init_data __initdata max8997_charger_data = {
	.constraints	= {
		.name		= "CHARGER",
		.min_uA		= 200000,
		.max_uA		= 950000,
		.boot_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS |
				REGULATOR_CHANGE_CURRENT,
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_charger_),
	.consumer_supplies	= max8997_charger_,
};

static struct regulator_init_data __initdata max8997_charger_topoff_data = {
	.constraints	= {
		.name		= "CHARGER TOPOFF",
		.min_uA		= 50000,
		.max_uA		= 200000,
		.valid_ops_mask = REGULATOR_CHANGE_CURRENT,
	},
	.num_consumer_supplies	= ARRAY_SIZE(max8997_chg_toff_),
	.consumer_supplies	= max8997_chg_toff_,
};

static struct max8997_regulator_data __initdata tizen_max8997_regulators[] = {
	{ MAX8997_LDO1, &max8997_ldo1_data },
	{ MAX8997_LDO2, &max8997_ldo2_data },
	{ MAX8997_LDO3, &max8997_ldo3_data },
	{ MAX8997_LDO4, &max8997_ldo4_data },
	{ MAX8997_LDO5, &max8997_ldo5_data },
	{ MAX8997_LDO6, &max8997_ldo6_data },
	{ MAX8997_LDO7, &max8997_ldo7_data },
	{ MAX8997_LDO8, &max8997_ldo8_data },
	{ MAX8997_LDO9, &max8997_ldo9_data },
	{ MAX8997_LDO10, &max8997_ldo10_data },
	{ MAX8997_LDO11, &max8997_ldo11_data },
	{ MAX8997_LDO12, &max8997_ldo12_data },
	{ MAX8997_LDO13, &max8997_ldo13_data },
	{ MAX8997_LDO14, &max8997_ldo14_data },
	{ MAX8997_LDO15, &max8997_ldo15_data },
	{ MAX8997_LDO16, &max8997_ldo16_data },

	{ MAX8997_LDO18, &max8997_ldo18_data },
	{ MAX8997_LDO21, &max8997_ldo21_data },

	{ MAX8997_BUCK1, &max8997_buck1_data },
	{ MAX8997_BUCK2, &max8997_buck2_data },
	{ MAX8997_BUCK3, &max8997_buck3_data },
	{ MAX8997_BUCK4, &max8997_buck4_data },
	{ MAX8997_BUCK5, &max8997_buck5_data },
	{ MAX8997_BUCK6, &max8997_buck6_data },
	{ MAX8997_BUCK7, &max8997_buck7_data },

	{ MAX8997_EN32KHZ_AP, &max8997_32khz_ap_data },
	{ MAX8997_EN32KHZ_CP, &max8997_32khz_cp_data },

	{ MAX8997_ENVICHG, &max8997_vichg_data },
	{ MAX8997_ESAFEOUT1, &max8997_esafeout1_data },
	{ MAX8997_ESAFEOUT2, &max8997_esafeout2_data },
	{ MAX8997_CHARGER_CV, &max8997_charger_cv_data },
	{ MAX8997_CHARGER, &max8997_charger_data },
	{ MAX8997_CHARGER_TOPOFF, &max8997_charger_topoff_data },
};

static struct max8997_platform_data __initdata tizen_max8997_pdata = {
	.wakeup			= 1,

	.num_regulators		= ARRAY_SIZE(tizen_max8997_regulators),
	.regulators		= tizen_max8997_regulators,

	.buck125_gpios = { EXYNOS4_GPX0(5), EXYNOS4_GPX0(6), EXYNOS4_GPL0(0) },

	.buck1_voltage[0] = 1350000, /* 1.35V */
	.buck1_voltage[1] = 1300000, /* 1.3V */
	.buck1_voltage[2] = 1250000, /* 1.25V */
	.buck1_voltage[3] = 1200000, /* 1.2V */
	.buck1_voltage[4] = 1150000, /* 1.15V */
	.buck1_voltage[5] = 1100000, /* 1.1V */
	.buck1_voltage[6] = 1000000, /* 1.0V */
	.buck1_voltage[7] = 950000, /* 0.95V */

	.buck2_voltage[0] = 1100000, /* 1.1V */
	.buck2_voltage[1] = 1000000, /* 1.0V */
	.buck2_voltage[2] = 950000, /* 0.95V */
	.buck2_voltage[3] = 900000, /* 0.9V */
	.buck2_voltage[4] = 1100000, /* 1.1V */
	.buck2_voltage[5] = 1000000, /* 1.0V */
	.buck2_voltage[6] = 950000, /* 0.95V */
	.buck2_voltage[7] = 900000, /* 0.9V */

	.buck5_voltage[0] = 1200000, /* 1.2V */
	.buck5_voltage[1] = 1200000, /* 1.2V */
	.buck5_voltage[2] = 1200000, /* 1.2V */
	.buck5_voltage[3] = 1200000, /* 1.2V */
	.buck5_voltage[4] = 1200000, /* 1.2V */
	.buck5_voltage[5] = 1200000, /* 1.2V */
	.buck5_voltage[6] = 1200000, /* 1.2V */
	.buck5_voltage[7] = 1200000, /* 1.2V */
};

/* GPIO I2C 5 (PMIC) */
enum { I2C5_MAX8997 };
static struct i2c_board_info i2c5_devs[] __initdata = {
	[I2C5_MAX8997] = {
		I2C_BOARD_INFO("max8997", 0xCC >> 1),
		.platform_data	= &tizen_max8997_pdata,
	},
};

/* MAX8903 Secondary Charger */
static struct regulator_consumer_supply supplies_max8903[] = {
	REGULATOR_SUPPLY("vinchg2", "charger-manager.0"),
};

static struct regulator_init_data max8903_charger_en_data = {
	.constraints = {
		.name		= "VOUT_CHARGER",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.boot_on	= 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(supplies_max8903),
	.consumer_supplies = supplies_max8903,
};

static struct fixed_voltage_config max8903_charger_en = {
	.supply_name = "VOUT_CHARGER",
	.microvolts = 5000000, /* Assume 5VDC */
	.gpio = EXYNOS4_GPY4(5), /* TA_EN negaged */
	.enable_high = 0, /* Enable = Low */
	.enabled_at_boot = 1,
	.init_data = &max8903_charger_en_data,
};

static struct platform_device max8903_fixed_reg_dev = {
	.name = "reg-fixed-voltage",
	.id = FIXED_REG_ID_MAX8903,
	.dev = { .platform_data	= &max8903_charger_en },
};

static struct max8903_pdata tizen_max8903 = {
	/*
	 * cen: don't control with the driver, let it be
	 * controlled by regulator above
	 */
	.dok = EXYNOS4_GPX1(4), /* TA_nCONNECTED */
	/* uok, usus: not connected */
	.chg = EXYNOS4_GPE2(0), /* TA_nCHG */
	/* flt: vcc_1.8V_pda */
	.dcm = EXYNOS4_GPL0(1), /* CURR_ADJ */

	.dc_valid = true,
	.usb_valid = false, /* USB is not wired to MAX8903 */
};

static struct platform_device tizen_max8903_device = {
	.name			= "max8903-charger",
	.dev			= {
		.platform_data	= &tizen_max8903,
	},
};

static void __init tizen_power_init(void)
{
	int gpio;
	int irq_base = IRQ_GPIO_END + 1;
	int ta_en = 0;

	tizen_max8997_pdata.irq_base = irq_base;
	irq_base += MAX8997_IRQ_NR;

	gpio = EXYNOS4_GPX0(7);
	gpio_request(gpio, "AP_PMIC_IRQ");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);

	gpio = EXYNOS4_GPX2(3);
	gpio_request(gpio, "FUEL_ALERT");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);

	gpio = tizen_max8903.dok;
	gpio_request(gpio, "TA_nCONNECTED");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	ta_en = gpio_get_value(gpio) ? 0 : 1;

	gpio = tizen_max8903.chg;
	gpio_request(gpio, "TA_nCHG");
	gpio_direction_input(gpio);

	gpio = tizen_max8903.dcm;
	gpio_request(gpio, "CURR_ADJ");
	gpio_direction_output(gpio, ta_en);
}

/* USB EHCI */
static struct s5p_ehci_platdata tizen_ehci_pdata;

static void __init tizen_ehci_init(void)
{
	struct s5p_ehci_platdata *pdata = &tizen_ehci_pdata;

	s5p_ehci_set_platdata(pdata);
}

/* CAMERA */
static struct regulator_consumer_supply cam_vt_cam15_supply =
	REGULATOR_SUPPLY("vdd_core", "6-003c");

static struct regulator_init_data cam_vt_cam15_reg_init_data = {
	.constraints = { .valid_ops_mask = REGULATOR_CHANGE_STATUS },
	.num_consumer_supplies = 1,
	.consumer_supplies = &cam_vt_cam15_supply,
};

static struct fixed_voltage_config cam_vt_cam15_fixed_voltage_cfg = {
	.supply_name	= "VT_CAM_1.5V",
	.microvolts	= 1500000,
	.gpio		= EXYNOS4_GPE2(2), /* VT_CAM_1.5V_EN */
	.enable_high	= 1,
	.init_data	= &cam_vt_cam15_reg_init_data,
};

static struct platform_device cam_vt_cam15_fixed_rdev = {
	.name = "reg-fixed-voltage", .id = FIXED_REG_ID_CAM_VT_15V,
	.dev = { .platform_data	= &cam_vt_cam15_fixed_voltage_cfg },
};

static struct regulator_consumer_supply cam_vdda_supply[] = {
	REGULATOR_SUPPLY("vdda", "6-003c"),
	REGULATOR_SUPPLY("a_sensor", "0-001f"),
};

static struct regulator_init_data cam_vdda_reg_init_data = {
	.constraints = { .valid_ops_mask = REGULATOR_CHANGE_STATUS },
	.num_consumer_supplies = ARRAY_SIZE(cam_vdda_supply),
	.consumer_supplies = cam_vdda_supply,
};

static struct fixed_voltage_config cam_vdda_fixed_voltage_cfg = {
	.supply_name	= "CAM_IO_EN",
	.microvolts	= 2800000,
	.gpio		= EXYNOS4_GPE2(1), /* CAM_IO_EN */
	.enable_high	= 1,
	.init_data	= &cam_vdda_reg_init_data,
};

static struct platform_device cam_vdda_fixed_rdev = {
	.name = "reg-fixed-voltage", .id = FIXED_REG_ID_CAM_A28V,
	.dev = { .platform_data	= &cam_vdda_fixed_voltage_cfg },
};

static struct regulator_consumer_supply camera_8m_12v_supply =
	REGULATOR_SUPPLY("dig_12", "0-001f");

static struct regulator_init_data cam_8m_12v_reg_init_data = {
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &camera_8m_12v_supply,
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS
	},
};

static struct fixed_voltage_config cam_8m_12v_fixed_voltage_cfg = {
	.supply_name	= "8M_1.2V",
	.microvolts	= 1200000,
	.gpio		= EXYNOS4_GPE2(5), /* 8M_1.2V_EN */
	.enable_high	= 1,
	.init_data	= &cam_8m_12v_reg_init_data,
};

static struct platform_device cam_8m_12v_fixed_rdev = {
	.name = "reg-fixed-voltage", .id = FIXED_REG_ID_CAM_12V,
	.dev = { .platform_data = &cam_8m_12v_fixed_voltage_cfg },
};

static struct s5p_platform_mipi_csis mipi_csis_platdata = {
	.clk_rate	= 166000000UL,
	.lanes		= 2,
	.alignment	= 32,
	.hs_settle	= 12,
	.phy_enable	= s5p_csis_phy_enable,
};

#define GPIO_CAM_MEGA_RST	EXYNOS4_GPY3(7) /* ISP_RESET */
#define GPIO_CAM_8M_ISP_INT	EXYNOS4_GPL2(5)
#define GPIO_CAM_VT_NSTBY	EXYNOS4_GPL2(0)
#define GPIO_CAM_VT_NRST	EXYNOS4_GPL2(1)

static struct s5k6aa_platform_data s5k6aa_pldata = {
	.mclk_frequency	= 24000000UL,
	.gpio_reset	= { GPIO_CAM_VT_NRST, 0 },
	.gpio_stby	= { GPIO_CAM_VT_NSTBY, 0 },
	.bus_type	= V4L2_MBUS_PARALLEL,
	.horiz_flip	= 1,
};

static struct i2c_board_info s5k6aa_board_info = {
	I2C_BOARD_INFO("S5K6AA", 0x3c),
	.platform_data = &s5k6aa_pldata,
};

static struct m5mols_platform_data m5mols_platdata = {
	.gpio_reset = GPIO_CAM_MEGA_RST,
};

static struct i2c_board_info m5mols_board_info = {
	I2C_BOARD_INFO("M5MOLS", 0x1F),
	.platform_data	= &m5mols_platdata,
};

static struct s5p_fimc_isp_info tizen_camera_sensors[] = {
	{
		.flags		= V4L2_MBUS_PCLK_SAMPLE_RISING |
				  V4L2_MBUS_VSYNC_ACTIVE_LOW,
		.bus_type	= FIMC_ITU_601,
		.board_info	= &s5k6aa_board_info,
		.clk_frequency	= 24000000UL,
		.i2c_bus_num	= 6,
	}, {
		.flags		= V4L2_MBUS_PCLK_SAMPLE_FALLING |
				  V4L2_MBUS_VSYNC_ACTIVE_LOW,
		.bus_type	= FIMC_MIPI_CSI2,
		.board_info	= &m5mols_board_info,
		.clk_frequency	= 24000000UL,
		.csi_data_align	= 32,
	},
};

static struct s5p_platform_fimc fimc_md_platdata = {
	.isp_info	= tizen_camera_sensors,
	.num_clients	= ARRAY_SIZE(tizen_camera_sensors),
};

static struct gpio tizen_camera_gpios[] = {
	{ GPIO_CAM_VT_NSTBY,	GPIOF_OUT_INIT_LOW, "CAM_VGA_NSTBY" },
	{ GPIO_CAM_VT_NRST,	GPIOF_OUT_INIT_LOW, "CAM_VGA_NRST"  },
	{ GPIO_CAM_8M_ISP_INT,	GPIOF_IN,           "8M_ISP_INT"  },
	{ GPIO_CAM_MEGA_RST,	GPIOF_OUT_INIT_LOW, "CAM_8M_NRST" },
};

static void __init tizen_camera_init(void)
{
	s3c_set_platdata(&mipi_csis_platdata, sizeof(mipi_csis_platdata),
			 &s5p_device_mipi_csis0);
	s3c_set_platdata(&fimc_md_platdata,  sizeof(fimc_md_platdata),
			 &s5p_device_fimc_md);

	if (gpio_request_array(tizen_camera_gpios,
			       ARRAY_SIZE(tizen_camera_gpios))) {
		pr_err("%s: GPIO request failed\n", __func__);
		return;
	}

	m5mols_board_info.irq = s5p_register_gpio_interrupt(GPIO_CAM_8M_ISP_INT);
	if (!IS_ERR_VALUE(m5mols_board_info.irq))
		s3c_gpio_cfgpin(GPIO_CAM_8M_ISP_INT, S3C_GPIO_SFN(0xF));
	else
		pr_err("%s: Failed to configure 8M_ISP_INT GPIO\n", __func__);

	/* Free GPIOs controlled directly by the sensor drivers. */
	gpio_free(GPIO_CAM_VT_NRST);
	gpio_free(GPIO_CAM_VT_NSTBY);
	gpio_free(GPIO_CAM_MEGA_RST);

	if (exynos4_fimc_setup_gpio(S5P_CAMPORT_A)) {
		pr_err("%s: Camera port A setup failed\n", __func__);
		return;
	}
	/* Increase drive strength of the sensor clock output */
	s5p_gpio_set_drvstr(EXYNOS4_GPJ1(3), S5P_GPIO_DRVSTR_LV4);
}

static struct s3c2410_platform_i2c tizen_i2c6_platdata __initdata = {
	.frequency	= 400000U,
	.sda_delay	= 200,
	.bus_num	= 6,
};

static struct s3c2410_platform_i2c tizen_i2c0_platdata __initdata = {
	.frequency	= 400000U,
	.sda_delay	= 200,
};

/* DEVFREQ controlling memory/bus */
static struct platform_device exynos4_bus_devfreq = {
	.name			= "exynos4210-busfreq",
};

static struct platform_device *tizen_devices[] __initdata = {
	/* Samsung Platform Devices */
	&s5p_device_mipi_csis0,
	&s5p_device_fimd0,
	&s3c_device_timer[0],
	&s5p_device_ehci,
	&s5p_device_g2d,
	&s5p_device_jpeg,
	&s3c_device_rtc,
	&s5p_device_mfc,
	&s5p_device_mfc_l,
	&s5p_device_mfc_r,

	/* Tizen Devices */
	&tizen_lcd_device,
	&tizen_backlight_device,
#ifdef CONFIG_TIZEN_FGLES
	&tizen_fgles_device,
#endif
	&max8903_fixed_reg_dev,
	&tizen_max8903_device,
	&cam_vt_cam15_fixed_rdev,
	&cam_vdda_fixed_rdev,
	&cam_8m_12v_fixed_rdev,
	&exynos4_bus_devfreq,
	&s3c_device_i2c1,
	&exynos4_device_i2s0,
	&samsung_asoc_idma,
};

static void __init tizen_map_io(void)
{
	init_consistent_dma_size(SZ_4M);
	clk_xusbxti.rate = 24000000;
	exynos_init_io(NULL, 0);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(tizen_uartcfgs, ARRAY_SIZE(tizen_uartcfgs));
}

static void __init tizen_reserve(void)
{
	s5p_mfc_reserve_mem(0x43000000, 8 << 20, 0x51000000, 8 << 20);
}

static void  __init s3c_fb_setup(char *options, struct s3c_fb_pd_win *windata)
{
    int i;
    unsigned int optlen;
    unsigned short bpp = windata->default_bpp;
    u32 xres = windata->win_mode.xres, yres = windata->win_mode.yres;
    bool bpp_specified = false;
    bool yres_specified = false;
    bool xres_specified = false;

    if (!options || !*options) {
        return;
    }

    optlen = strlen(options);
    for (i = optlen - 1; i >= 0; i--) {
        switch (options[i]) {
        case '-':
            optlen = i;
            if (!bpp_specified && !yres_specified) {
                bpp = simple_strtoul(&options[i + 1], NULL, 10);
                bpp_specified = true;
            } else {
                goto done;
            }
            break;
        case 'x':
            if (!yres_specified) {
                yres = simple_strtoul(&options[i + 1], NULL, 10);
                yres_specified = true;
            } else {
                goto done;
            }
            break;
        case '0' ... '9':
            break;
        default:
            goto done;
        }
    }

    if (i < 0 && yres_specified) {
        xres = simple_strtoul(options, NULL, 10);
        xres_specified = true;
    }

done:
    if (bpp_specified) {
        if (bpp > windata->max_bpp) {
            printk(KERN_WARNING "Specified fb bpp mode %u is too big,"
                    "setting to %u bpp\n", bpp, windata->max_bpp);
            bpp = windata->max_bpp;
        }
        windata->default_bpp = bpp;
    }

    if (yres_specified) {
        if (yres > windata->virtual_y) {
            printk(KERN_WARNING "Specified fb Yres %u is too big,"
                    "setting to %u pixels\n", yres, windata->virtual_y);
            yres = windata->virtual_y;
        }
        windata->win_mode.yres = yres;
    }

    if (xres_specified) {
        if (xres > windata->virtual_x) {
            printk(KERN_WARNING "Specified fb Xres %u is too big,"
                    "setting to %u pixels\n", xres, windata->virtual_x);
            xres = windata->virtual_x;
        }
        windata->win_mode.xres = xres;
    }

    printk(KERN_DEBUG "Framebuffer mode %ux%u - %u bpp set",
        windata->win_mode.xres, windata->win_mode.yres, windata->default_bpp);
}

static void __init tizen_parse_videomode(void)
{
    char *cmdline_opt;

    if (fb_get_options("s3cfb", &cmdline_opt) == 0) {
        s3c_fb_setup(cmdline_opt, &tizen_fb_win0);
    }
}

void tizen_power_off(char mode, const char *cmd)
{
	unsigned long tmp;

	/* Setting Central Sequence Register for power down mode */

	tmp = __raw_readl(S5P_CENTRAL_SEQ_CONFIGURATION);
	tmp &= ~S5P_CENTRAL_LOWPWR_CFG;
	__raw_writel(tmp, S5P_CENTRAL_SEQ_CONFIGURATION);

	return 0;
}

void tizen_restart(char mode, const char *cmd)
{
	__raw_writel(0x1, S5P_SWRESET);
}

static void __init tizen_machine_init(void)
{
    tizen_parse_videomode();
	amba_device_register(&kmi0_device, &iomem_resource);

	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, i2c1_devs, ARRAY_SIZE(i2c1_devs));
	s5p_fimd0_set_platdata(&tizen_fb_pdata);

	tizen_camera_init();
	tizen_ehci_init();

	pm_power_off = tizen_power_off;

	/* Last */
	platform_add_devices(tizen_devices, ARRAY_SIZE(tizen_devices));
}

MACHINE_START(TIZEN, "Tizen")
	.atag_offset	= 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= tizen_map_io,
	.handle_irq	= gic_handle_irq,
	.init_machine	= tizen_machine_init,
	.timer		= &tizen_timer,
	.reserve        = &tizen_reserve,
	.restart	= tizen_restart,
MACHINE_END
