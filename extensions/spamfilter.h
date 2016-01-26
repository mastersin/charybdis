/************************************************************************
 *   IRC - Internet Relay Chat, extensions/spamfilter.h
 *   Copyright (C) 2016 Jason Volk
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 */

#define MODE_SPAMFILTER 'Y'


enum Cfg
{
	CFG_REJECT_MSG = 0,
	CFG_NICKS_LIMIT,
	CFG_NICKS_BLOOM_SIZE,
	CFG_NICKS_BLOOM_BITS,
	CFG_NICKS_REFRESH,

	CFG_SIZE
};

const char *cfg_str[] =\
{
	"cfg_reject_msg",
	"cfg_nicks_limit",
	"cfg_nicks_bloom_size",
	"cfg_nicks_bloom_bits",
	"cfg_nicks_refresh",
};

typedef long cfg_t[CFG_SIZE];


static inline
size_t cfg_idx(const char *const str)
{
	size_t i = 0;
	for(; i < CFG_SIZE; i++)
		if(irccmp(cfg_str[i],str) == 0)
			break;

	return i;
}
