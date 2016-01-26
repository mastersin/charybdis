/************************************************************************
 *   IRC - Internet Relay Chat, extensions/spamfilter_nicks.c
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

#include "stdinc.h"
#include "s_conf.h"
#include "modules.h"
#include "hook.h"
#include "client.h"
#include "ircd.h"
#include "send.h"
#include "hash.h"

#include "spamfilter.h"

#define DEFAULT_LIMIT        5
#define DEFAULT_BLOOM_SIZE   65536
#define DEFAULT_BLOOM_BITS   16
#define DEFAULT_REFRESH      3600 * 8

#define MAXCHANS 2048      /* for now this is the limit for channels with MODE_SPAMFILER */


cfg_t cfg;
uint8_t *bloom;
size_t bloom_size;
size_t bloom_members;
time_t bloom_flushed;
char chans[MAXCHANS][CHANNELLEN];
size_t chans_used;


static
void bloom_flush()
{
	memset(bloom,0x0,bloom_size);
	bloom_flushed = rb_current_time();
	bloom_members = 0;
}

static
void bloom_destroy()
{
	rb_free(bloom);
	bloom = NULL;
	bloom_members = 0;
}

static
void bloom_create(const size_t size)
{
	if(!size)
		return;

	bloom = rb_malloc(size);
	bloom_size = size;
	bloom_flush();
}

static
void bloom_add(long hash)
{
	hash %= bloom_size * 8L;
	bloom[hash / 8L] |= (1U << (hash % 8L));
	bloom_members++;
}

static
int bloom_test(long hash)
{
	hash %= bloom_size * 8L;
	return bloom[hash / 8L] & (1U << (hash % 8L));
}

static
void bloom_add_str(const char *const str)
{
	const unsigned hash = fnv_hash_upper((const unsigned char *)str,cfg[CFG_NICKS_BLOOM_BITS]);
	bloom_add(hash);
}

static
int bloom_test_str(const char *const str)
{
	const unsigned hash = fnv_hash_upper((const unsigned char *)str,cfg[CFG_NICKS_BLOOM_BITS]);
	return bloom_test(hash);
}


static
void chans_clear()
{
	chans_used = 0;
}

static
int chans_has(const struct Channel *const chptr)
{
	for(size_t i = 0; i < chans_used; i++)
		if(strncmp(chans[i],chptr->chname,CHANNELLEN) == 0)
			return 1;

	return 0;
}

static
int chans_add(const struct Channel *const chptr)
{
	if(chans_used >= MAXCHANS)
		return 0;

	rb_dlink_node *ptr;
	RB_DLINK_FOREACH(ptr,chptr->members.head)
	{
		const struct membership *const msptr = ptr->data;
		bloom_add_str(msptr->client_p->name);
	}

	rb_strlcpy(chans[chans_used],chptr->chname,sizeof(chans[chans_used]));
	chans_used++;
	return 1;
}


static
int expired()
{
	return bloom_flushed + cfg[CFG_NICKS_REFRESH] < rb_current_time();
}

static
void clear()
{
	chans_clear();
	bloom_flush();
}

static
void resize(const size_t size)
{
	chans_clear();
	bloom_destroy();
	bloom_create(size);
}


static
int prob_test_token(const char *const token)
{
	return bloom_test_str(token);
}


static
int real_test_token(const char *const token,
                    struct Channel *const chptr)
{
	struct Client *const client = find_named_client(token);
	return client && IsMember(client,chptr);
}


static
void false_positive_message()
{
	sendto_realops_snomask(SNO_GENERAL,L_ALL,
	                       "spamfilter: Nickname bloom filter false positive (size: %zu members: %zu channels: %zu flushed: %zu ago)",
	                       bloom_size,
	                       bloom_members,
	                       chans_used,
	                       rb_current_time() - bloom_flushed);
}


/*
 * Always find the length of any multibyte character to advance past.
 * The unicode space characters of concern are only of length 3.
 */
static
int is_delim(const uint8_t *const ptr,
             int *const adv)
{
	/* Some ascii ranges */
	if((ptr[0] >= 0x20 && ptr[0] <= 0x2F) ||
	   (ptr[0] >= 0x3A && ptr[0] <= 0x40) ||
	   (ptr[0] >= 0x5C && ptr[0] <= 0x60) ||
	   (ptr[0] >= 0x7B && ptr[0] <= 0x7F))
		return 1;

	/* Unicode below here */
	const int len = ((ptr[0] & 0x80) == 0x80)+
	                ((ptr[0] & 0xC0) == 0xC0)+
	                ((ptr[0] & 0xE0) == 0xE0)+
	                ((ptr[0] & 0xF0) == 0xF0)+
	                ((ptr[0] & 0xF8) == 0xF8)+
	                ((ptr[0] & 0xFC) == 0xFC);

	if(len)
		*adv += len - 1;

	if(len != 3)
		return 0;

	switch((htonl(*(const uint32_t *)ptr) & 0x1F7F7F00U) >> 8)
	{
		case 0x20000:
		case 0x20001:
		case 0x20002:
		case 0x20003:
		case 0x20004:
		case 0x20005:
		case 0x20006:
		case 0x20007:
		case 0x20008:
		case 0x20009:
		case 0x2000A:
		case 0x2000B:
		case 0x2002F:
		case 0x2005F:
		case 0x30000:
		case 0xf3b3f:
			return 1;

		default:
			return 0;
	}
}


static
uint count_nicks(const unsigned char *const text,
                 struct Channel *const chptr)
{
	uint ret = 0;
	const uint len = strlen(text);
	for(uint i = 0, t = 0; i + 6 < len; i++, t++)
	{
		if(!is_delim(text+i,&i))
			continue;

		if(t < 1 || t >= NICKLEN)
			continue;

		char token[NICKLEN];
		rb_strlcpy(token,(text+i)-t,t+1);
		if(prob_test_token(token))
		{
			if(rb_likely(real_test_token(token,chptr)))
				ret++;
			else
				false_positive_message();
		}

		t = -1;
	}

	return ret;
}


static
void hook_spamfilter_query(hook_data_privmsg_channel *const hook)
{
	if(hook->approved != 0)
		return;

	if(!bloom)
		return;

	const uint counted = count_nicks(hook->text,hook->chptr);
	if(counted < cfg[CFG_NICKS_LIMIT])
		return;

	static char reason[64];
	snprintf(reason,sizeof(reason),"nicks: counted %u names",counted);
	hook->reason = reason;
	hook->approved = -1;
}


static
void hook_channel_join(hook_data_channel_approval *const data)
{
	if(~data->chptr->mode.mode & chmode_table[(uint8_t)MODE_SPAMFILTER].mode_type)
		return;

	if(!bloom)
		return;

	if(expired())
		clear();

	if(!chans_has(data->chptr))
		chans_add(data->chptr);
	else
		bloom_add_str(data->client->name);
}


static
void hook_spamfilter_reconfig(cfg_t *const updated_cfg)
{
	if(updated_cfg)
		memcpy(cfg,updated_cfg,sizeof(cfg_t));

	if(!cfg[CFG_NICKS_LIMIT])
		cfg[CFG_NICKS_LIMIT] = DEFAULT_LIMIT;

	if(!cfg[CFG_NICKS_BLOOM_SIZE])
		cfg[CFG_NICKS_BLOOM_SIZE] = DEFAULT_BLOOM_SIZE;

	if(!cfg[CFG_NICKS_BLOOM_BITS])
		cfg[CFG_NICKS_BLOOM_BITS] = DEFAULT_BLOOM_BITS;

	if(!cfg[CFG_NICKS_REFRESH])
		cfg[CFG_NICKS_REFRESH] = DEFAULT_REFRESH;

	if(bloom_size != cfg[CFG_NICKS_BLOOM_SIZE])
		resize(cfg[CFG_NICKS_BLOOM_SIZE]);
}


static
int modinit(void)
{
	hook_spamfilter_reconfig(NULL);
	return 0;
}


static
void modfini(void)
{
	bloom_destroy();
}


mapi_clist_av1 clist[] =\
{
	NULL
};

mapi_hlist_av1 hlist[] =\
{
	{ NULL, NULL }
};

mapi_hfn_list_av1 hfnlist[] =\
{
	{ "spamfilter_reconfig", (hookfn)hook_spamfilter_reconfig  },
	{ "spamfilter_query", (hookfn)hook_spamfilter_query        },
	{ "channel_join", (hookfn)hook_channel_join                },
	{ NULL, NULL                                               }
};

DECLARE_MODULE_AV1
(
	spamfilter_nicks,
	modinit,
	modfini,
	clist,
	hlist,
	hfnlist,
	"$Revision: 0 $"
);
