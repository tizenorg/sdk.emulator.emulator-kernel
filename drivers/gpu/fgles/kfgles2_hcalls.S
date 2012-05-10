/* Copyright (c) 2009 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) any later version of the License.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

.text

.extern kfgles2_base_egl;

/*
 * Cause an interrupt at the host machine by writing
 * to a special area.
 */
.macro HCALL NAME, BASE, NUM
.global \NAME
.func \NAME
\NAME:
	push {r4,fp}
	ldr r4,=\BASE
	ldr r4,[r4]
	str r4,[r4, #\NUM*0x04]
	pop {r4,fp}
	bx lr
	.size \NAME, . - \NAME
.endfunc
.endm

HCALL kfgles2_host_init_egl,  kfgles2_base_egl,  0
HCALL kfgles2_host_exit_egl,  kfgles2_base_egl,  1

.end

