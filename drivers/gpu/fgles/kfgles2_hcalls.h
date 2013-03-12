/* Copyright (c) 2009-2010 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) any later version of the License.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
 
#ifndef HCALLS_H_
#define HCALLS_H_

/**
 * kfgles2_host_init_XX - create host handle
 *
 * Inform host machine that a create and return
 * new client handle, or 0 to indicate error.
 */
uint32_t kfgles2_host_init_egl(uint32_t abi);

/**
 * kfgles2_host_exit_XX - release host handle
 * @id ID of the client disconnected
 *
 * Inform host machine that a client is disconnected.
 */
void kfgles2_host_exit_egl(uint32_t id);

#endif /* HCALLS_H_ */

