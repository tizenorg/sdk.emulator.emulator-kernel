#ifndef _YAGL_MARSHAL_H_
#define _YAGL_MARSHAL_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/byteorder.h>

/*
 * All marshalling/unmarshalling must be done with 8-byte alignment,
 * since this is the maximum alignment possible. This way we can
 * just do assignments without "memcpy" calls and can be sure that
 * the code won't fail on architectures that don't support unaligned
 * memory access.
 */

static __inline void yagl_marshal_put_uint32(u8** buff, u32 value)
{
    *(u32*)(*buff) = cpu_to_le32(value);
    *buff += 8;
}

static __inline u32 yagl_marshal_get_uint32(u8** buff)
{
    u32 tmp = le32_to_cpu(*(u32*)*buff);
    *buff += 8;
    return tmp;
}

#define yagl_marshal_put_pid(buff, value) yagl_marshal_put_uint32(buff, value)
#define yagl_marshal_put_tid(buff, value) yagl_marshal_put_uint32(buff, value)

#endif
