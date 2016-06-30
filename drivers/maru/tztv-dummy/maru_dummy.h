/*
 * MARU dummy driver
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * Jinhyung Choi <jinh0.choi@samsung.com>
 * Hyunjin Lee <hyunjin816.lee@samsung.com>
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

#ifndef _MARU_DUMMY_H
#define _MARU_DUMMY_H

extern int dummy_driver_debug;

#define maru_device_err(fmt, arg...) \
	printk(KERN_ERR "[ERR][%s]: " fmt, __func__, ##arg)

#define maru_device_warn(fmt, arg...) \
	printk(KERN_WARNING "[WARN][%s]: " fmt, __func__, ##arg)

#define maru_device_info(fmt, arg...) \
	printk(KERN_INFO "[INFO][%s]: " fmt, __func__, ##arg)

#define maru_device_dbg(log_level, fmt, arg...) \
	do {	\
		if (dummy_driver_debug >= (log_level)) {	\
			printk(KERN_INFO "[DEBUG][%s]: " fmt,  __func__, ##arg);	\
		}	\
	} while (0)

#endif