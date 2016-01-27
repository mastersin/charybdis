/*
 * charybdis: an advanced ircd.
 * irc_btree23.c: A 2,3 b-tree - two-three bee-minus-tree (knuth order of 3).
 *
 * Copyright (C) 2016 Jason Volk
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice is present in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include "stdinc.h"
#include "match.h"
#include "client.h"
#include "setup.h"
#include "irc_btree23.h"
#include "s_assert.h"
#include "logger.h"


struct Node
{
	uint64_t key[2];
	uint64_t val[2];
	uint64_t ptr[3];
	uint64_t pad[1];
};


static
size_t num_keys(const struct Node *const node)
{
	return node->key[0] != 0+
	       node->key[1] != 0;
}


static
size_t num_ptrs(const struct Node *const node)
{
	return node->ptr[0] != 0+
	       node->ptr[1] != 0+
	       node->ptr[2] != 0;
}


static
size_t lower_bound(const struct Node *const node,
                   const uint64_t key)
{
	return !node->key[0] || node->key[0] >= key? 0:
	       !node->key[1] || node->key[1] >= key? 1:
	                                             2;
}


static
size_t upper_bound(const struct Node *const node,
                   const uint64_t key)
{
	return !node->key[0] || node->key[0] > key? 0:
	       !node->key[1] || node->key[1] > key? 1:
	                                            2;
}


static
size_t avail(const struct Node *const node)
{
	return sizeof(node->key) - num_keys(node);
}


static
int empty(const struct Node *const node)
{
	return num_keys(node) == 0;
}


static
int full(const struct Node *const node)
{
	return avail(node) == 0;
}


static
uint64_t node_index(struct irc_btree23 *const head,
                    struct Node *const node)
{
	struct Node *const first = (struct Node *)(head + 1);
	return (uintptr_t)(node - first + 1);
}


/* Never get(idx=0), that's the head! */
static
struct Node *get(struct irc_btree23 *const head,
                 const uint64_t idx)
{
	struct Node *const first = (struct Node *)(head + 1);
	return first + idx - 1;
}


static
struct Node *set(struct irc_btree23 *const head,
                 const uint64_t idx,
                 const struct Node *const node)
{
	struct Node *const dest = get(head,idx);
	*dest = *node;
	return dest;
}


static
struct Node *next(struct irc_btree23 *const head)
{
	return get(head,head->used++);
}


static
uint insert_into0(struct Node *const node,
                  const uint64_t key,
                  const uint64_t val)
{
	node->key[1] = node->key[0];
	node->val[1] = node->val[0];
	node->key[0] = key;
	node->val[0] = val;
	node->ptr[2] = node->ptr[1];
	node->ptr[1] = node->ptr[0];
	node->ptr[0] = 0;
	return 1;
}


static
uint insert_into1(struct Node *const node,
                  const uint64_t key,
                  const uint64_t val)
{
	node->key[1] = key;
	node->val[1] = val;
	return 1;
}


static
uint split_branch0(struct irc_btree23 *const head,
                   struct Node *const node,
                   struct Node *const push,
                   const uint64_t idx)
{
	struct Node *const cent = next(head);
	cent->key[0] = node->key[1];
	cent->val[0] = node->val[1];
	cent->ptr[0] = node->ptr[1];
	cent->ptr[1] = node->ptr[2];
	node->key[1] = push->key[0];
	node->val[1] = push->val[0];
	push->key[0] = node->key[1];
	push->val[0] = node->val[1];
	node->ptr[0] = push->ptr[0];
	node->ptr[1] = push->ptr[1];
	push->ptr[0] = idx;
	push->ptr[1] = node_index(head,cent);
	node->ptr[2] = 0;
	node->key[1] = 0;
	return 0;
}


static
uint split_branch1(struct irc_btree23 *const head,
                   struct Node *const node,
                   struct Node *const push,
                   const uint64_t idx)
{
	struct Node *const cent = next(head);
	cent->key[0] = node->key[1];
	cent->val[0] = node->val[1];
	cent->ptr[0] = push->ptr[1];
	cent->ptr[1] = node->ptr[2];
	node->ptr[1] = push->ptr[1];
	push->ptr[0] = idx;
	push->ptr[1] = node_index(head,cent);
	node->key[1] = 0;
	node->ptr[2] = 0;
	return 0;
}


static
uint pushed_from0(struct irc_btree23 *const head,
                  struct Node *const node,
                  struct Node *const push,
                  const uint64_t idx)
{
	node->key[1] = node->key[0];
	node->val[1] = node->val[0];
	node->key[0] = push->key[0];
	node->val[0] = push->val[0];
	node->ptr[2] = node->ptr[1];
	node->ptr[1] = node->ptr[0];
	node->ptr[0] = push->ptr[0];
	node->ptr[1] = push->ptr[1];
	return 1;
}


static
uint pushed_from1(struct irc_btree23 *const head,
                  struct Node *const node,
                  struct Node *const push,
                  const uint64_t idx)
{
	node->key[1] = push->key[0];
	node->val[1] = push->val[0];
	node->ptr[1] = push->ptr[0];
	node->ptr[2] = push->ptr[1];
	return 1;
}


static
uint pushed_from2(struct irc_btree23 *const head,
                  struct Node *const node,
                  struct Node *const push,
                  const uint64_t idx)
{
	struct Node *const left = next(head);
	left->key[0] = node->key[0];
	left->val[0] = node->val[0];
	left->ptr[0] = node->ptr[0];
	left->ptr[1] = node->ptr[1];
	node->key[0] = push->key[0];
	node->val[0] = push->val[0];
	node->ptr[0] = push->ptr[0];
	node->ptr[1] = push->ptr[1];
	push->key[0] = node->key[1];
	push->val[0] = node->val[1];
	push->ptr[0] = node_index(head,left);
	push->ptr[1] = idx;
	node->key[1] = 0;
	node->ptr[2] = 0;
	return 0;
}


static
uint split_leaf0(struct irc_btree23 *const head,
                 struct Node *const node,
                 struct Node *const push,
                 const uint64_t idx,
                 const uint64_t key,
                 const uint64_t val)
{
	struct Node *const cent = next(head);
	cent->key[0] = node->key[1];
	cent->val[0] = node->val[1];
	push->key[0] = node->key[0];
	push->val[0] = node->val[0];
	push->ptr[0] = idx;
	push->ptr[1] = node_index(head,cent);
	node->key[0] = key;
	node->val[0] = val;
	return 0;
}


static
uint split_leaf1(struct irc_btree23 *const head,
                 struct Node *const node,
                 struct Node *const push,
                 const uint64_t idx,
                 const uint64_t key,
                 const uint64_t val)
{
	struct Node *const cent = next(head);
	cent->key[0] = node->key[1];
	cent->val[0] = node->val[1];
	push->key[0] = key;
	push->val[0] = val;
	push->ptr[0] = idx;
	push->ptr[1] = node_index(head,cent);
	node->key[1] = 0;
	return 0;
}


static
uint split_leaf2(struct irc_btree23 *const head,
                 struct Node *const node,
                 struct Node *const push,
                 const uint64_t idx,
                 const uint64_t key,
                 const uint64_t val)
{
	struct Node *const left = next(head);
	left->key[0] = node->key[0];
	left->val[0] = node->val[0];
	push->key[0] = node->key[1];
	push->val[0] = node->val[1];
	push->ptr[0] = node_index(head,left);
	push->ptr[1] = idx;
	node->key[0] = key;
	node->val[0] = val;
	node->key[1] = 0;
	return 0;
}


static
int insert(struct irc_btree23 *const head,
           struct Node *const push,
           const uint64_t idx,
           const uint64_t key,
           const uint64_t val)
{
	struct Node *node = get(head,idx);

	if(empty(node))
	{
		node->key[0] = key;
		node->val[0] = val;
		return 1;
	}

	switch(lower_bound(node,key))
	{
		case 0:
		return node->ptr[0] && insert(head,push,node->ptr[0],key,val)?  1:
		       node->ptr[0] && avail(node) == 1?                        pushed_from0(head,node,push,idx):
		       node->ptr[0]?                                            split_branch0(head,node,push,idx):
		       full(node)?                                              split_leaf0(head,node,push,idx,key,val):
		                                                                insert_into0(node,key,val);

		case 1:
		return node->ptr[1] && insert(head,push,node->ptr[1],key,val)?  1:
		       node->ptr[1] && avail(node) == 1?                        pushed_from1(head,node,push,idx):
		       node->ptr[1]?                                            split_branch1(head,node,push,idx):
		       full(node)?                                              split_leaf1(head,node,push,idx,key,val):
		                                                                insert_into1(node,key,val);

		case 2:
		return !node->ptr[2]?                                           split_leaf2(head,node,push,idx,key,val):
		       insert(head,push,node->ptr[2],key,val)?                  1:
		                                                                pushed_from2(head,node,push,idx);

		default:
		assert(0);
		return -1;
	}
}


static
struct Node *find(struct irc_btree23 *const head,
                  const uint64_t key,
                  uint64_t *const pos)
{
	struct Node *node;
	uint64_t idx = head->root; do
	{
		node = get(head,idx);
		*pos = lower_bound(node,key);
		if(*pos < 2 && node->key[*pos] == key)
			break;
	}
	while((idx = node->ptr[*pos]));
	return node;
}


static
size_t debug_node(char *const buf,
                  const size_t size,
                  const struct Node *const node)
{
	return snprintf(buf,size,"key[%9lu][%9lu]  val[%9lu][%9lu]  ptr[%9lu][%9lu][%9lu]",
	                node->key[0],
	                node->key[1],
	                node->val[0],
	                node->val[1],
	                node->ptr[0],
	                node->ptr[1],
	                node->ptr[2]);
}


static
void debug_dump(struct irc_btree23 *const head)
{
	printf("size: %zu\n",head->size);
	printf("used: %zu\n",head->used);
	printf("root: %zu\n",head->root);

	char buf[BUFSIZE];
	for(size_t i = 1; i <= 8; i++)
	{
		struct Node *const node = get(head,i);
		debug_node(buf,sizeof(buf),node);
		printf("Node[%4zu]: %s\n",i,buf);
	}
}


int irc_btree23_set(struct irc_btree23 *const head,
                    const uint64_t key,
                    const uint64_t val)
{
	struct Node push = {0};
	if(!insert(head,&push,head->root,key,val))
	{
		head->root = node_index(head,next(head));
		set(head,head->root,&push);
	}

	return 0;
}


uint64_t irc_btree23_get(struct irc_btree23 *const head,
                         const uint64_t key)
{
	uint64_t pos;
	struct Node *const node = find(head,key,&pos);
	return pos < 2? node->val[pos] : 0;
}


int irc_btree23_init(struct irc_btree23 *const head,
                     const size_t size,
                     const uint flags)
{
	memset(head,0x0,sizeof(struct irc_btree23));
	head->size = (size - sizeof(struct irc_btree23)) / sizeof(struct Node);
	head->used = 2;
	head->root = 1;

	if(~flags & IRC_BTREE23_UNINITIALIZED)
		memset(head+1,0x0,head->size * sizeof(struct Node));

	return 0;
}


struct irc_btree23 *irc_btree23_new(const size_t size,
                                    const uint flags)
{
	struct irc_btree23 *const head = rb_malloc(size);

	if(rb_unlikely(!head))
		return NULL;

	if(rb_unlikely(irc_btree23_init(head,size,flags) != 0))
	{
		rb_free(head);
		return NULL;
	}

	return head;
}


void irc_btree23_free(struct irc_btree23 *const head)
{
	rb_free(head);
}
