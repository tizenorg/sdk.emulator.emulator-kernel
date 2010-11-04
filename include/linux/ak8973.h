/*
 * ak8973.h - AK8973 magnetic sensors driver
 *
 * Copyright (c) 2009 Samsung Eletronics
 * Authors:
 *	Kim Kyuwon <q1.kim@samsung.com>
 *	Kyungmin Park <kyungmin.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _AK8973_H_
#define _AK8973_H_

struct ak8973_platform_data {
	unsigned int poll_interval;
	void (*reset)(void);
};

#endif
