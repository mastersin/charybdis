/************************************************************************
 *   IRC - Internet Relay Chat, extensions/spamfilter.c
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
#include "numeric.h"
#include "modules.h"
#include "hook.h"
#include "client.h"
#include "ircd.h"
#include "send.h"
#include "chmode.h"
#include "s_serv.h"
#include "logger.h"

#include "spamfilter.h"
#define CHM_PRIVS chm_staff

int chm_spamfilter;
int h_spamfilter_query;
int h_spamfilter_reject;
int h_spamfilter_reconfig;
char reject_msg[BUFSIZE - CHANNELLEN - 32];
cfg_t cfg;


static
void hook_privmsg_channel(hook_data_privmsg_channel *const hook)
{
	if(hook->approved != 0 || EmptyString(hook->text))
		return;

	if(hook->msgtype != MESSAGE_TYPE_NOTICE && hook->msgtype != MESSAGE_TYPE_PRIVMSG)
		return;

	if(~hook->chptr->mode.mode & chm_spamfilter)
		return;

	call_hook(h_spamfilter_query,hook);
	if(hook->approved == 0)
		return;

	call_hook(h_spamfilter_reject,hook);
	sendto_realops_snomask(SNO_REJ|SNO_BOTS,L_NETWIDE,
	                       "spamfilter: REJECT %s[%s@%s] on %s to %s (%s)",
	                       hook->source_p->name,
	                       hook->source_p->username,
	                       hook->source_p->orighost,
	                       hook->source_p->servptr->name,
	                       hook->chptr->chname,
	                       hook->reason?: "filter gave no reason");

	hook->reason = cfg[CFG_REJECT_MSG]? reject_msg : NULL;
}


static
int spamfilter_sync(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(!IsOper(source_p) && !IsServer(source_p))
	{
		sendto_one(source_p, form_str(ERR_NOPRIVS), me.name, source_p->name, "SPAMFILTER SYNC");
		return 0;
	}

	for(size_t i = 0; i < CFG_SIZE; i++)
		sendto_server(client_p, NULL, CAP_ENCAP, NOCAPS,
		              ":%s ENCAP * SPAMFILTER SET %s %ld",
		              source_p->id,
		              cfg_str[i],
		              cfg[i]);
	return 0;
}


static
int spamfilter_set(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(!IsOper(source_p) && !IsServer(source_p))
	{
		sendto_one(source_p, form_str(ERR_NOPRIVS), me.name, source_p->name, "SPAMFILTER SET");
		return 0;
	}

	if(parc < 2)
	{
		sendto_one_notice(source_p, ":Usage: SPAMFILTER SET <key> <val>");
		return 0;
	}

	const size_t idx = cfg_idx(parv[0]);
	if(idx >= CFG_SIZE)
	{
		if(IsPerson(source_p))
			sendto_one_notice(source_p, ":Failed to find specified key.");

		return 0;
	}

	cfg[idx] = atol(parv[1]);
	call_hook(h_spamfilter_reconfig,&cfg);

	if(MyClient(source_p) && IsPerson(source_p))
	{
		sendto_server(client_p, NULL, CAP_ENCAP, NOCAPS,
		              ":%s ENCAP * SPAMFILTER SET %s %ld",
		              source_p->id,
		              parv[0],
		              cfg[idx]);

		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE,
		                       "spamfilter: Updated configuration [%s] => %lu",
		                       parv[0],
		                       cfg[idx]);

		sendto_one_notice(source_p, ":Updated: %s = %ld",parv[0],cfg[idx]);
	}

	return 0;
}


static
int spamfilter_show(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(parc <= 0)
	{
		for(size_t i = 0; i < CFG_SIZE; i++)
			sendto_one_notice(source_p,":\2%-20s\2 : %ld",cfg_str[i],cfg[i]);

		return 0;
	}

	const size_t idx = cfg_idx(parv[0]);
	if(idx >= CFG_SIZE)
	{
		sendto_one_notice(source_p,":Failed to find specified key.");
		return 0;
	}

	sendto_one_notice(source_p,"%s %ld",parv[0],cfg[idx]);
	return 0;
}


static
int spamfilter(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(parc <= 1)
	{
		sendto_one_notice(source_p, ":Insufficient parameters.");
		return 0;
	}

	if(irccmp(parv[1],"SET") == 0)
		return spamfilter_set(client_p,source_p,parc-2,parv+2);

	if(irccmp(parv[1],"SHOW") == 0)
		return spamfilter_show(client_p,source_p,parc-2,parv+2);

	if(irccmp(parv[1],"SYNC") == 0)
		return spamfilter_sync(client_p,source_p,parc-2,parv+2);

	return 0;
}


struct Message msgtab =\
{
	"SPAMFILTER", 0, 0, 0, MFLG_SLOW,
	{
		mg_ignore,                   /* unregistered clients   */
		{ spamfilter, 0   },         /* local clients          */
		{ spamfilter, 0   },         /* remote clients         */
		mg_ignore,                   /* servers                */
		{ spamfilter, 0   },         /* ENCAP                  */
		{ spamfilter, 0   }          /* ircops                 */
	}
};


static
void hook_server_introduced(hook_data_client *const data)
{
	spamfilter_sync(&me,&me,0,NULL);
}


static
void init_reject_msg()
{
	if(ServerInfo.network_name == NULL || AdminInfo.email == NULL)
		cfg[CFG_REJECT_MSG] = 0;

	rb_sprintf(reject_msg,
	           "Spam is off-topic on %s. If this is an error, contact %s",
	           ServerInfo.network_name?: "<network>",
	           AdminInfo.email?: "<email>");

	if(!cfg[CFG_REJECT_MSG])
		ilog(L_MAIN,"spamfilter: Module init before the network name and admin email are read from the conf disables the rejection message.");
}


static
int modinit(void)
{
	chm_spamfilter = cflag_add(MODE_SPAMFILTER, CHM_PRIVS);
	if(!chm_spamfilter)
		return -1;

	init_reject_msg();
	return 0;
}


static
void modfini(void)
{
	cflag_orphan(MODE_SPAMFILTER);
}


mapi_clist_av1 clist[] =\
{
	&msgtab,
	NULL
};


mapi_hlist_av1 hlist[] =\
{
	{ "spamfilter_query", &h_spamfilter_query,        },
	{ "spamfilter_reject", &h_spamfilter_reject,      },
	{ "spamfilter_reconfig", &h_spamfilter_reconfig,  },
	{ NULL, NULL                                      }
};


mapi_hfn_list_av1 hfnlist[] =\
{
	{ "privmsg_channel", (hookfn)hook_privmsg_channel      },
	{ "server_introduced", (hookfn)hook_server_introduced  },
	{ NULL, NULL                                           }
};


DECLARE_MODULE_AV1
(
	spamfilter,
	modinit,
	modfini,
	clist,
	hlist,
	hfnlist,
	"$Revision: 0 $"
);
