/************************************************************************
 *   IRC - Internet Relay Chat, extensions/spamfilter_expr.c
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
#include "s_serv.h"
#include "hash.h"

#include "spamfilter.h"
#define JIT_STACK_STARTSIZE  1048576
#define JIT_STACK_MAXSIZE    (JIT_STACK_STARTSIZE * 8)
#define EXPR_HEAP_MAX        1024
#define PCRE_STATIC          1
#include <pcre.h>


struct Expr
{
	uint id;
	uint comp_opts;
	uint exec_opts;
	uint study_opts;
	char *patstr;
	pcre *expr;
	pcre_extra *extra;
	time_t added;
	time_t last;
	uint hits;
	rb_dlink_node node;
};

cfg_t cfg;
rb_bh *expr_heap;
int expr_heap_used;
rb_dlink_list exprs;
pcre_jit_stack *jstack;


static
void free_expr(struct Expr *const expr)
{
	pcre_free_study(expr->extra);
	pcre_free(expr->expr);
	free(expr->patstr);
	rb_bh_free(expr_heap,expr);
	expr_heap_used--;
}


static
struct Expr *new_expr(const char *const patstr,
                      const uint comp_opts,
                      const uint exec_opts,
                      const int study_opts,
                      int *const errcodeptr,
                      const char **const errptr,
                      int *const erroffset,
                      const unsigned char *const tableptr)
{
	if(expr_heap_used >= EXPR_HEAP_MAX)
	{
		*errcodeptr = -128;
		*erroffset = 0;
		*errptr = "The expression heap is full.";
		return NULL;
	}

	pcre *const expr = pcre_compile2(patstr,comp_opts,errcodeptr,errptr,erroffset,tableptr);
	if(!expr)
		return NULL;

	struct Expr *const ret = rb_bh_alloc(expr_heap);
	expr_heap_used++;
	ret->id = fnv_hash((const unsigned char *)patstr,16);
	ret->comp_opts = comp_opts;
	ret->exec_opts = exec_opts;
	ret->study_opts = study_opts;
	ret->patstr = strdup(patstr);
	ret->expr = expr;
	ret->extra = ret->study_opts >= 0? pcre_study(expr,ret->study_opts,errptr) : NULL;
	ret->added = 0;
	ret->last = 0;
	ret->hits = 0;
	return ret;
}


static
void remove_expr(struct Expr *const expr)
{
	rb_dlinkDelete(&expr->node,&exprs);
	free_expr(expr);
}


static
struct Expr *find_expr_by_id(const uint id)
{
	rb_dlink_node *ptr;
	RB_DLINK_FOREACH(ptr,exprs.head)
	{
		struct Expr *const expr = ptr->data;
		if(expr->id == id)
			return expr;
	}

	return NULL;
}


static
struct Expr *find_expr_by_str(const char *const pattern)
{
	rb_dlink_node *ptr;
	RB_DLINK_FOREACH(ptr,exprs.head)
	{
		struct Expr *const expr = ptr->data;
		if(strcmp(expr->patstr,pattern) == 0)
			return expr;
	}

	return NULL;
}


static
int remove_expr_by_id(const uint id)
{
	struct Expr *const expr = find_expr_by_id(id);
	if(!expr)
		return 0;

	remove_expr(expr);
	return 1;
}


static
void remove_exprs()
{
	rb_dlink_node *ptr, *nextptr;
	RB_DLINK_FOREACH_SAFE(ptr,nextptr,exprs.head)
	{
		struct Expr *const expr = ptr->data;
		free_expr(expr);
	}
}


static
struct Expr *add_expr(const char *const patstr,
                      const uint comp_opts,
                      const uint exec_opts,
                      const uint study_opts,
                      int *const errcodeptr,
                      const char **const errptr,
                      int *const erroffset,
                      const unsigned char *const tableptr)
{
	if(find_expr_by_str(patstr))
	{
		*errcodeptr = -129;
		*erroffset = 0;
		*errptr = "The pattern already exists.";
		return NULL;
	}

	struct Expr *const expr = new_expr(patstr,
	                                   comp_opts,
	                                   exec_opts,
	                                   study_opts,
	                                   errcodeptr,
	                                   errptr,
	                                   erroffset,
	                                   tableptr);
	if(expr)
	{
		expr->added = rb_current_time();
		rb_dlinkAdd(expr,&expr->node,&exprs);
	}

	return expr;
}


static
const char *strerror_pcre(const long val)
{
	switch(val)
	{
		case 1:
		case 0:                              return "SUCCESS";
		case PCRE_ERROR_NOMATCH:             return "NOMATCH";
		case PCRE_ERROR_NULL:                return "NULL";
		case PCRE_ERROR_BADOPTION:           return "BADOPTION";
		case PCRE_ERROR_BADMAGIC:            return "BADMAGIC";
		case PCRE_ERROR_UNKNOWN_OPCODE:      return "UNKNOWN_OPCODE";
		case PCRE_ERROR_NOMEMORY:            return "NOMEMORY";
		case PCRE_ERROR_NOSUBSTRING:         return "NOSUBSTRING";
		case PCRE_ERROR_MATCHLIMIT:          return "MATCHLIMIT";
		case PCRE_ERROR_CALLOUT:             return "CALLOUT";
		case PCRE_ERROR_BADUTF8:             return "BADUTF8";
		case PCRE_ERROR_BADUTF8_OFFSET:      return "BADUTF8_OFFSET";
		case PCRE_ERROR_PARTIAL:             return "PARTIAL";
		case PCRE_ERROR_BADPARTIAL:          return "BADPARTIAL";
		case PCRE_ERROR_INTERNAL:            return "INTERNAL";
		case PCRE_ERROR_BADCOUNT:            return "BADCOUNT";
		case PCRE_ERROR_RECURSIONLIMIT:      return "RECURSIONLIMIT";
		case PCRE_ERROR_BADNEWLINE:          return "BADNEWLINE";
		case PCRE_ERROR_BADOFFSET:           return "BADOFFSET";
		case PCRE_ERROR_SHORTUTF8:           return "SHORTUTF8";
		case PCRE_ERROR_RECURSELOOP:         return "RECURSELOOP";
		case PCRE_ERROR_JIT_STACKLIMIT:      return "JIT_STACKLIMIT";
		case PCRE_ERROR_BADMODE:             return "BADMODE";
		case PCRE_ERROR_BADENDIANNESS:       return "BADENDIANNESS";
		case PCRE_ERROR_JIT_BADOPTION:       return "JIT_BADOPTION";
		case PCRE_ERROR_BADLENGTH:           return "BADLENGTH";
		case PCRE_ERROR_UNSET:               return "UNSET";
		default:                             return "????????";
	}
}


static
const char *str_pcre_info(const long val)
{
	switch(val)
	{
		case PCRE_INFO_BACKREFMAX:           return "BACKREFMAX";
		case PCRE_INFO_CAPTURECOUNT:         return "CAPTURECOUNT";
		case PCRE_INFO_FIRSTBYTE:            return "FIRSTBYTE";
		case PCRE_INFO_HASCRORLF:            return "HASCRORLF";
		case PCRE_INFO_JCHANGED:             return "JCHANGED";
		case PCRE_INFO_JIT:                  return "JIT";
		case PCRE_INFO_LASTLITERAL:          return "LASTLITERAL";
		case PCRE_INFO_MINLENGTH:            return "MINLENGTH";
		case PCRE_INFO_MATCH_EMPTY:          return "MATCH_EMPTY";
		case PCRE_INFO_MAXLOOKBEHIND:        return "MAXLOOKBEHIND";
		case PCRE_INFO_NAMECOUNT:            return "NAMECOUNT";
		case PCRE_INFO_NAMETABLE:            return "NAMETABLE";
		case PCRE_INFO_NAMEENTRYSIZE:        return "NAMEENTRYSIZE";
		case PCRE_INFO_OKPARTIAL:            return "OKPARTIAL";
		case PCRE_INFO_FIRSTCHARACTERFLAGS:  return "FIRSTCHARACTERFLAGS";
		case PCRE_INFO_REQUIREDCHARFLAGS:    return "REQUIREDCHARFLAGS";
		case PCRE_INFO_FIRSTCHARACTER:       return "FIRSTCHARACTER";
		case PCRE_INFO_MATCHLIMIT:           return "MATCHLIMIT";
		case PCRE_INFO_RECURSIONLIMIT:       return "RECURSIONLIMIT";
		case PCRE_INFO_REQUIREDCHAR:         return "REQUIREDCHAR";
		case PCRE_INFO_OPTIONS:              return "OPTIONS";
		case PCRE_INFO_SIZE:                 return "SIZE";
		case PCRE_INFO_JITSIZE:              return "JITSIZE";
		case PCRE_INFO_STUDYSIZE:            return "STUDYSIZE";
		default:                             return "";
	}
}


static
const char *str_pcre_opt(const long val)
{
	switch(val)
	{
		case PCRE_CASELESS:                  return "CASELESS";
		case PCRE_MULTILINE:                 return "MULTILINE";
		case PCRE_DOTALL:                    return "DOTALL";
		case PCRE_EXTENDED:                  return "EXTENDED";
		case PCRE_ANCHORED:                  return "ANCHORED";
		case PCRE_DOLLAR_ENDONLY:            return "DOLLAR_ENDONLY";
		case PCRE_EXTRA:                     return "EXTRA";
		case PCRE_NOTBOL:                    return "NOTBOL";
		case PCRE_NOTEOL:                    return "NOTEOL";
		case PCRE_UNGREEDY:                  return "UNGREEDY";
		case PCRE_NOTEMPTY:                  return "NOTEMPTY";
		case PCRE_UTF8:                      return "UTF8";
		case PCRE_NO_AUTO_CAPTURE:           return "NO_AUTO_CAPTURE";
		case PCRE_NO_UTF8_CHECK:             return "NO_UTF8_CHECK";
		case PCRE_AUTO_CALLOUT:              return "AUTO_CALLOUT";
//		case PCRE_PARTIAL_SOFT:              return "PARTIAL_SOFT";
		case PCRE_PARTIAL:                   return "PARTIAL";
		case PCRE_NEVER_UTF:                 return "NEVER_UTF";
		case PCRE_NO_AUTO_POSSESS:           return "NO_AUTO_POSSESS";
		case PCRE_FIRSTLINE:                 return "FIRSTLINE";
		case PCRE_DUPNAMES:                  return "DUPNAMES";
		case PCRE_NEWLINE_CR:                return "NEWLINE_CR";
		case PCRE_NEWLINE_LF:                return "NEWLINE_LF";
		case PCRE_NEWLINE_CRLF:              return "NEWLINE_CRLF";
		case PCRE_NEWLINE_ANY:               return "NEWLINE_ANY";
		case PCRE_NEWLINE_ANYCRLF:           return "NEWLINE_ANYCRLF";
		case PCRE_BSR_ANYCRLF:               return "BSR_ANYCRLF";
		case PCRE_BSR_UNICODE:               return "BSR_UNICODE";
		case PCRE_JAVASCRIPT_COMPAT:         return "JAVASCRIPT_COMPAT";
		case PCRE_NO_START_OPTIMIZE:         return "NO_START_OPTIMIZE";
//		case PCRE_NO_START_OPTIMISE:         return "NO_START_OPTIMISE";
		case PCRE_PARTIAL_HARD:              return "PARTIAL_HARD";
		case PCRE_NOTEMPTY_ATSTART:          return "NOTEMPTY_ATSTART";
		case PCRE_UCP:                       return "UCP";
		default:                             return "";
	}
}


static
const char *str_pcre_extra(const long val)
{
	switch(val)
	{
		case PCRE_EXTRA_STUDY_DATA:              return "STUDY_DATA";
		case PCRE_EXTRA_MATCH_LIMIT:             return "MATCH_LIMIT";
		case PCRE_EXTRA_CALLOUT_DATA:            return "CALLOUT_DATA";
		case PCRE_EXTRA_TABLES:                  return "TABLES";
		case PCRE_EXTRA_MATCH_LIMIT_RECURSION:   return "MATCH_LIMIT_RECURSION";
		case PCRE_EXTRA_MARK:                    return "MARK";
		case PCRE_EXTRA_EXECUTABLE_JIT:          return "EXECUTABLE_JIT";
		default:                                 return "";
	}
}


static
const char *str_pcre_study(const long val)
{
	switch(val)
	{
		case PCRE_STUDY_JIT_COMPILE:                return "JIT_COMPILE";
		case PCRE_STUDY_JIT_PARTIAL_SOFT_COMPILE:   return "JIT_PARTIAL_SOFT_COMPILE";
		case PCRE_STUDY_JIT_PARTIAL_HARD_COMPILE:   return "JIT_PARTIAL_HARD_COMPILE";
		case PCRE_STUDY_EXTRA_NEEDED:               return "EXTRA_NEEDED";
		default:                                    return "";
	}
}


static
long reflect_pcre_info(const char *const str)
{
	for(int i = 0; i < 48; i++)
		if(irccmp(str_pcre_info(i),str) == 0)
			return i;

	return -1;
}


static
long reflect_pcre_opt(const char *const str)
{
	ulong i = 1;
	for(; i; i <<= 1)
		if(irccmp(str_pcre_opt(i),str) == 0)
			return i;

	return i;
}


static
long reflect_pcre_extra(const char *const str)
{
	ulong i = 1;
	for(; i; i <<= 1)
		if(irccmp(str_pcre_extra(i),str) == 0)
			return i;

	return i;
}


static
long reflect_pcre_study(const char *const str)
{
	ulong i = 1;
	for(; i; i <<= 1)
		if(irccmp(str_pcre_study(i),str) == 0)
			return i;

	return i;
}


static
long parse_pcre_opts(const char *const str,
                     long (*const reflector)(const char *))
{
	static char s[BUFSIZE];
	static const char *delim = "|";

	ulong ret = 0;
	rb_strlcpy(s,str,sizeof(s));
	char *ctx, *tok = strtok_r(s,delim,&ctx); do
	{
		ret |= reflector(tok);
	}
	while((tok = strtok_r(NULL,delim,&ctx)) != NULL);

	return ret;
}


static
int strlcat_pcre_opts(const long opts,
                      char *const buf,
                      const size_t max,
                      const char *(*const strfun)(const long))
{
	int ret = 0;
	ulong o = 1;
	for(; o; o <<= 1)
	{
		if(opts & o)
		{
			const char *const str = strfun(o);
			if(strlen(str))
			{
				ret = rb_strlcat(buf,str,max);
				ret = rb_strlcat(buf,"|",max);
			}
		}
	}

	if(ret && buf[ret-1] == '|')
		buf[ret-1] = '\0';

	if(!ret)
		ret = rb_strlcat(buf,"0",max);

	return ret;
}


static
size_t expr_info_val(struct Expr *const expr,
                     const int what,
                     char *const buf,
                     const size_t len)
{
	union
	{
		int v_int;
		uint v_uint;
		long v_long;
		ulong v_ulong;
		size_t v_size;
		uint8_t *v_uint8;
		unsigned char *v_uchar;
	} v;

	const int ret = pcre_fullinfo(expr->expr,expr->extra,what,&v);
	if(ret < 0)
		return snprintf(buf,len,"Error: %s",strerror_pcre(ret));

	switch(what)
	{
		case PCRE_INFO_BACKREFMAX:
		case PCRE_INFO_CAPTURECOUNT:
		case PCRE_INFO_FIRSTBYTE:
		case PCRE_INFO_HASCRORLF:
		case PCRE_INFO_JCHANGED:
		case PCRE_INFO_JIT:
		case PCRE_INFO_LASTLITERAL:
		case PCRE_INFO_MINLENGTH:
		case PCRE_INFO_MATCH_EMPTY:
		case PCRE_INFO_MAXLOOKBEHIND:
		case PCRE_INFO_NAMECOUNT:
		case PCRE_INFO_NAMETABLE:
		case PCRE_INFO_NAMEENTRYSIZE:
		case PCRE_INFO_OKPARTIAL:
		case PCRE_INFO_FIRSTCHARACTERFLAGS:
		case PCRE_INFO_REQUIREDCHARFLAGS:
			return snprintf(buf,len,"%d",v.v_int);

		case PCRE_INFO_FIRSTCHARACTER:
		case PCRE_INFO_MATCHLIMIT:
		case PCRE_INFO_RECURSIONLIMIT:
		case PCRE_INFO_REQUIREDCHAR:
			return snprintf(buf,len,"%u",v.v_uint);

		case PCRE_INFO_OPTIONS:
			return snprintf(buf,len,"0x%08lx",v.v_ulong);

		case PCRE_INFO_SIZE:
		case PCRE_INFO_JITSIZE:
		case PCRE_INFO_STUDYSIZE:
			return snprintf(buf,len,"%zu",v.v_size);

		default:
			return rb_strlcpy(buf,"Requested information unsupported.",len);
	}
}


static
size_t expr_info(struct Expr *const expr,
                 const int what[],
                 const size_t num_what,
                 char *const buf,
                 const ssize_t len)
{
	static char valbuf[BUFSIZE];

	ssize_t boff = 0;
	for(size_t i = 0; i < num_what && boff < len; i++)
	{
		expr_info_val(expr,what[i],valbuf,sizeof(valbuf));
		boff += snprintf(buf+boff,len-boff,"%s[%s] ",str_pcre_info(what[i]),valbuf);
	}

	return boff;
}


static
int match_expr(struct Expr *const expr,
               const char *const text,
               const int len,
               const int off,
               const uint options)
{
	static const int ovlen = 30;
	static int ovec[30];

	const uint opts = options | expr->exec_opts;
	const int jit = expr->extra && (expr->extra->flags & PCRE_EXTRA_EXECUTABLE_JIT);
	const int ret = jit? pcre_jit_exec(expr->expr,expr->extra,text,len,off,expr->exec_opts,ovec,ovlen,jstack):
	                     pcre_exec(expr->expr,expr->extra,text,len,off,opts,ovec,ovlen);
	if(ret > 0)
	{
		expr->hits++;
		expr->last = rb_current_time();
	}
	else if(rb_unlikely(ret < -1))
		sendto_realops_snomask(SNO_GENERAL,L_ALL,
		                       "spamfilter: Expression #%u error (%d): %s",
		                       expr->id,
		                       ret,
		                       strerror_pcre(ret));
	return ret;
}


static
struct Expr *match_any_expr(const char *const text,
                            const size_t len,
                            const size_t off,
                            const uint options)
{
	rb_dlink_node *ptr;
	RB_DLINK_FOREACH(ptr, exprs.head)
	{
		struct Expr *const expr = ptr->data;
		if(match_expr(expr,text,len,off,options) > 0)
			return expr;
	}

	return NULL;
}


static
void hook_spamfilter_query(hook_data_privmsg_channel *const hook)
{
	if(hook->approved != 0)
		return;

	const size_t len = strlen(hook->text);
	struct Expr *const expr = match_any_expr(hook->text,len,0,0);
	if(!expr)
		return;

	static char reason[64];
	snprintf(reason,sizeof(reason),"expr: matched #%ld",expr->id);
	hook->reason = reason;
	hook->approved = -1;
}


static
int dump_pcre_config(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	union
	{
		int v_int;
		ulong v_ulong;
		char *v_char;
	} v;

	sendto_one_notice(source_p,":\2%-30s\2: %s","PCRE VERSION",pcre_version());

	if(pcre_config(PCRE_CONFIG_JIT,&v) == 0)
		sendto_one_notice(source_p,":\2%-30s\2: %d (%s)","PCRE JIT",v.v_int,
		                  v.v_int == 0? "UNAVAILABLE":
		                  v.v_int == 1? "AVAILABLE":
		                                "???");

	if(pcre_config(PCRE_CONFIG_JITTARGET,&v) == 0)
		sendto_one_notice(source_p,":\2%-30s\2: %s","PCRE JITTARGET",v.v_char);

	if(pcre_config(PCRE_CONFIG_LINK_SIZE,&v) == 0)
		sendto_one_notice(source_p,":\2%-30s\2: %d","PCRE LINK_SIZE",v.v_int);

	if(pcre_config(PCRE_CONFIG_PARENS_LIMIT,&v) == 0)
		sendto_one_notice(source_p,":\2%-30s\2: %lu","PCRE PARENS_LIMIT",v.v_ulong);

	if(pcre_config(PCRE_CONFIG_MATCH_LIMIT,&v) == 0)
		sendto_one_notice(source_p,":\2%-30s\2: %lu","PCRE MATCH_LIMIT",v.v_ulong);

	if(pcre_config(PCRE_CONFIG_MATCH_LIMIT_RECURSION,&v) == 0)
		sendto_one_notice(source_p,":\2%-30s\2: %lu","PCRE MATCH_LIMIT_RECURSION",v.v_ulong);

	if(pcre_config(PCRE_CONFIG_NEWLINE,&v) == 0)
		sendto_one_notice(source_p,":\2%-30s\2: %d (%s)","PCRE NEWLINE",v.v_int,
		                  v.v_int == 13?    "CR":
		                  v.v_int == 10?    "LF":
		                  v.v_int == 3338?  "CRLF":
		                  v.v_int == -2?    "ANYCRLF":
		                  v.v_int == -1?    "ANY":
		                                    "???");

	if(pcre_config(PCRE_CONFIG_BSR,&v) == 0)
		sendto_one_notice(source_p,":\2%-30s\2: %d (%s)","PCRE BSR",v.v_int,
		                  v.v_int == 0? "all Unicode line endings":
		                  v.v_int == 1? "CR, LF, or CRLF only":
		                                 "???");

	if(pcre_config(PCRE_CONFIG_POSIX_MALLOC_THRESHOLD,&v) == 0)
		sendto_one_notice(source_p,":\2%-30s\2: %d","PCRE POSIX_MALLOC_THRESHOLD",v.v_int);

	if(pcre_config(PCRE_CONFIG_STACKRECURSE,&v) == 0)
		sendto_one_notice(source_p,":\2%-30s\2: %d","PCRE STACKRECURSE",v.v_int);

	if(pcre_config(PCRE_CONFIG_UTF16,&v) == 0)
		sendto_one_notice(source_p,":\2%-30s\2: %d","PCRE UTF16",v.v_int);

	if(pcre_config(PCRE_CONFIG_UTF32,&v) == 0)
		sendto_one_notice(source_p,":\2%-30s\2: %d","PCRE UTF32",v.v_int);

	if(pcre_config(PCRE_CONFIG_UNICODE_PROPERTIES,&v) == 0)
		sendto_one_notice(source_p,":\2%-30s\2: %d (%s)","PCRE UNICODE_PROPERTIES",v.v_int,
		                  v.v_int == 0? "UNAVAILABLE":
		                  v.v_int == 1? "AVAILABLE":
		                                "???");
	return 0;
}


static
int spamexpr_info(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(parc > 0 && !IsOper(source_p))
	{
		sendto_one(source_p, form_str(ERR_NOPRIVS), me.name, source_p->name, "SPAMEXPR INFO");
		sendto_one_notice(source_p,":Only operators can give arguments to this command.");
		return 0;
	}

	if(parc < 1 && IsOper(source_p))
	{
		dump_pcre_config(client_p,source_p,parc,parv);
		return 0;
	}

	const int id = atoi(parv[0]);
	struct Expr *const expr = find_expr_by_id(id);
	if(!expr)
	{
		sendto_one_notice(source_p,":Failed to find any expression with ID %u.",id);
		return 0;
	}

	const int num_what = parc - 1;
	int what[num_what];
	for(int i = 0; i < num_what; i++)
		what[i] = reflect_pcre_info(parv[i+1]);

	static char buf[BUFSIZE];
	expr_info(expr,what,num_what,buf,sizeof(buf));

	char comp_opts[128] = {0}, exec_opts[128] = {0}, study_opts[128] = {0};
	strlcat_pcre_opts(expr->comp_opts,comp_opts,sizeof(comp_opts),str_pcre_opt);
	strlcat_pcre_opts(expr->exec_opts,exec_opts,sizeof(exec_opts),str_pcre_opt);
	strlcat_pcre_opts(expr->study_opts,study_opts,sizeof(study_opts),str_pcre_study);

	sendto_one_notice(source_p,":#%u time[%lu] last[%lu] hits[%d] [%s][%s][%s] %s %s",
	                  expr->id,
	                  expr->added,
	                  expr->last,
	                  expr->hits,
	                  comp_opts,
	                  exec_opts,
	                  study_opts,
	                  buf,
	                  expr->patstr);
	return 0;
}


static
int spamexpr_list(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(parc > 0 && !IsOper(source_p))
	{
		sendto_one(source_p, form_str(ERR_NOPRIVS), me.name, source_p->name, "SPAMEXPR LIST");
		sendto_one_notice(source_p,":Only operators can give arguments to this command.");
		return 0;
	}

	const char *nparv[parc + 2];
	char id[16];
	nparv[0] = id;
	nparv[parc+1] = NULL;
	for(size_t i = 0; i < parc; i++)
		nparv[i+1] = parv[i];

	rb_dlink_node *ptr;
	RB_DLINK_FOREACH(ptr, exprs.head)
	{
		struct Expr *const expr = ptr->data;
		snprintf(id,sizeof(id),"%u",expr->id);
		spamexpr_info(client_p,source_p,parc+1,nparv);
	}

	sendto_one_notice(source_p,":End of expression list.");
	return 0;
}


/*
 * The option fields are string representations of the options or'ed together.
 * Use 0 for no option.
 * example:  CASELESS|ANCHORED|DOTALL
 */
static
int spamexpr_add(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(!IsOper(source_p) && !IsServer(source_p))
	{
		sendto_one(source_p, form_str(ERR_NOPRIVS), me.name, source_p->name, "SPAMEXPR ADD");
		return 0;
	}

	if(parc < 4)
	{
		sendto_one_notice(source_p, ":Usage: ADD <compile opts|0> <exec opts|0> <study opts|0> <expression>");
		return 0;
	}

	const long comp_opts = parse_pcre_opts(parv[0],reflect_pcre_opt);
	const long exec_opts = parse_pcre_opts(parv[1],reflect_pcre_opt);
	const long study_opts = parse_pcre_opts(parv[2],reflect_pcre_study);
	const char *const pattern = parv[3];

	/* not yet used */
	const unsigned char *tableptr = NULL;

	int erroff;
	int errcode;
	const char *errstr;
	struct Expr *const expr = add_expr(pattern,comp_opts,exec_opts,study_opts,&errcode,&errstr,&erroff,tableptr);
	if(!expr)
	{
		if(IsPerson(source_p))
			sendto_one_notice(source_p,":Invalid expression (%d): %s at %d.",errcode,errstr,erroff);

		return 0;
	}

	if(MyClient(source_p) && IsPerson(source_p))
	{
		sendto_server(client_p, NULL, CAP_ENCAP, NOCAPS,
		              ":%s ENCAP * SPAMEXPR ADD %s %s %s :%s",
		              client_p->id,
		              parv[0],
		              parv[1],
		              parv[2],
		              parv[3]);

		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE,
		                       "spamfilter: Expression #%u added: \"%s\".",
		                       expr->id,
		                       expr->patstr);

		sendto_one_notice(source_p,":Added expression #%u.",expr->id);
	}

	return 0;
}


static
int spamexpr_del(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(!IsOper(source_p) && !IsServer(source_p))
	{
		sendto_one(source_p, form_str(ERR_NOPRIVS), me.name, source_p->name, "SPAMEXPR DEL");
		return 0;
	}

	if(parc < 1)
	{
		sendto_one_notice(source_p, ":Must specify an expression id number.");
		return 0;
	}

	const int id = atoi(parv[0]);
	if(!remove_expr_by_id(id))
	{
		sendto_one_notice(source_p, ":Failed to remove any expression with ID #%u.",id);
		return 0;
	}

	if(MyClient(source_p) && IsPerson(source_p))
	{
		sendto_server(client_p, NULL, CAP_ENCAP, NOCAPS,
		              ":%s ENCAP * SPAMEXPR DEL %u",
		              client_p->id,
		              id);

		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE,
		                       "spamfilter: Expression #%u removed.",
		                       id);

		sendto_one_notice(source_p,":Removed expression #%u.",id);
	}

	return 0;
}


static
int spamexpr_test(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(!IsOper(source_p))
	{
		sendto_one(source_p, form_str(ERR_NOPRIVS), me.name, source_p->name, "SPAMEXPR TEST");
		return 0;
	}

	if(parc < 2)
	{
		sendto_one_notice(source_p, ":Specify an ID and text argument, or ID -1 for all.");
		return 0;
	}

	if(!rb_dlink_list_length(&exprs))
	{
		sendto_one_notice(source_p, ":No expressions have been added to test.");
		return 0;
	}

	const int id = atoi(parv[0]);
	const size_t len = strlen(parv[1]);

	if(id >= 0)
	{
		struct Expr *expr = find_expr_by_id(id);
		if(!expr)
		{
			sendto_one_notice(source_p, ":Failed to find expression with ID #%u",id);
			return 0;
		}

		const int ret = match_expr(expr,parv[1],len,0,0);
		sendto_one_notice(source_p, ":#%-2d: (%d) %s",id,ret,strerror_pcre(ret));
		return 0;
	}

	rb_dlink_node *ptr;
	RB_DLINK_FOREACH(ptr, exprs.head)
	{
		struct Expr *const expr = ptr->data;
		const int ret = match_expr(expr,parv[1],len,0,0);
		sendto_one_notice(source_p, ":#%-2u: (%d): %s",expr->id,ret,strerror_pcre(ret));
	}

	return 0;
}


static
int spamexpr_sync(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(!IsOper(source_p) && !IsServer(source_p))
	{
		sendto_one(source_p, form_str(ERR_NOPRIVS), me.name, source_p->name, "SPAMEXPR SYNC");
		return 0;
	}

	rb_dlink_node *ptr;
	RB_DLINK_FOREACH(ptr, exprs.head)
	{
		struct Expr *const expr = ptr->data;
		char comp_opts[128] = {0}, exec_opts[128] = {0}, study_opts[128] = {0};
		strlcat_pcre_opts(expr->comp_opts,comp_opts,sizeof(comp_opts),str_pcre_opt);
		strlcat_pcre_opts(expr->exec_opts,exec_opts,sizeof(exec_opts),str_pcre_opt);
		strlcat_pcre_opts(expr->study_opts,study_opts,sizeof(study_opts),str_pcre_study);
		sendto_server(client_p, NULL, CAP_ENCAP, NOCAPS,
		              ":%s ENCAP * SPAMEXPR ADD %s %s %s :%s",
		              source_p->id,
		              comp_opts,
		              exec_opts,
		              study_opts,
		              expr->patstr);
	}

	return 0;
}


static
int spamexpr(struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if(parc <= 1)
	{
		sendto_one_notice(source_p, ":Insufficient parameters.");
		return 0;
	}

	if(irccmp(parv[1],"LIST") == 0)
		return spamexpr_list(client_p,source_p,parc-2,parv+2);

	if(irccmp(parv[1],"INFO") == 0)
		return spamexpr_info(client_p,source_p,parc-2,parv+2);

	if(irccmp(parv[1],"ADD") == 0)
		return spamexpr_add(client_p,source_p,parc-2,parv+2);

	if(irccmp(parv[1],"DEL") == 0)
		return spamexpr_del(client_p,source_p,parc-2,parv+2);

	if(irccmp(parv[1],"TEST") == 0)
		return spamexpr_test(client_p,source_p,parc-2,parv+2);

	if(irccmp(parv[1],"SYNC") == 0)
		return spamexpr_sync(client_p,source_p,parc-2,parv+2);

	sendto_one_notice(source_p, ":Command not found.");
	return 0;
}


static
void hook_server_introduced(hook_data_client *const hdata)
{
	spamexpr_sync(&me,&me,0,NULL);
}


static
void hook_spamfilter_reconfig(cfg_t *const updated_cfg)
{
	memcpy(cfg,updated_cfg,sizeof(cfg_t));
}


static
int modinit(void)
{
	jstack = pcre_jit_stack_alloc(JIT_STACK_STARTSIZE,JIT_STACK_MAXSIZE);
	expr_heap = rb_bh_create(sizeof(struct Expr), EXPR_HEAP_MAX, "spamexpr_heap");

	sendto_server(&me, NULL, CAP_ENCAP, NOCAPS,
	              ":%s ENCAP * SPAMEXPR SYNC",
	              me.id);
	return 0;
}


static
void modfini(void)
{
	remove_exprs();
	rb_bh_destroy(expr_heap);
	pcre_jit_stack_free(jstack);
}


struct Message msgtab =\
{
	"SPAMEXPR", 0, 0, 0, MFLG_SLOW,
	{
		mg_ignore,               /* unregistered clients   */
		{ spamexpr, 0   },       /* local clients          */
		{ spamexpr, 0   },       /* remote clients         */
		mg_ignore,               /* servers                */
		{ spamexpr, 0    },      /* ENCAP                  */
		{ spamexpr, 0    }       /* ircops                 */
	}
};

mapi_clist_av1 clist[] =\
{
	&msgtab,
	NULL
};

mapi_hlist_av1 hlist[] =\
{
	{ NULL, NULL                                     }
};

mapi_hfn_list_av1 hfnlist[] =\
{
	{ "spamfilter_query", (hookfn)hook_spamfilter_query       },
	{ "spamfilter_reconfig", (hookfn)hook_spamfilter_reconfig },
	{ "server_introduced", (hookfn)hook_server_introduced     },
	{ NULL, NULL                                              }
};

DECLARE_MODULE_AV1
(
	spamfilter_expr,
	modinit,
	modfini,
	clist,
	hlist,
	hfnlist,
	"$Revision: 0 $"
);
