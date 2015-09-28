/* 
 *  mod-pquota: quota module with punish period for valvula
 *  Copyright (C) 2015 Wouter Paesen <wouter@blue-gate.be>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2.1 of
 *  the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307 USA
 *  
 *  You may find a copy of the license under this software is released
 *  at COPYING file. 
 *
 */
#ifndef __MOD_PQUOTA_H__
#define __MOD_PQUOTA_H__

#include <valvulad.h>

typedef struct _ModPQuotaBucketConfig {
	axl_bool enabled;
	axl_bool domain;
	int      duration;
	int      size;
	int      punish;
} ModPQuotaBucketConfig;

#endif
