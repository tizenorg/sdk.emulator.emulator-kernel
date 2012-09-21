#ifndef _YAGL_VERSION_H_
#define _YAGL_VERSION_H_

#include <linux/ioctl.h>

/*
 * Version number.
 */
#define YAGL_VERSION 11

/*
 * Device control codes magic.
 */
#define YAGL_IOC_MAGIC 'Y'

/*
 * Get version number.
 */
#define YAGL_IOC_GET_VERSION _IOR(YAGL_IOC_MAGIC, 0, unsigned int)

#endif
