/*
 * smb380.h - SMB380 Tri-axis accelerometer driver
 *
 * Copyright (c) 2009 Samsung Eletronics
 * Kyungmin Park <kyungmin.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Datasheet: BMA150_DataSheet_Rev.1.5_30May2008.pdf at
 * http://www.bosch-sensortec.com/content/language4/downloads/
 *
 */

#ifndef _SMB380_H_
#define _SMB380_H_

struct smb380_platform_data {
	int trans_matrix[3][3];
};

#endif /* _SMB380_H_ */
