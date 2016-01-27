/*
 * charybdis: an advanced ircd.
 * irc_btree23.h: A 2,3 b-tree - two-three bee-minus-tree (knuth order of 3).
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

#ifndef __IRC_BTREE23_H__
#define __IRC_BTREE23_H__


enum irc_btree23_flags
{
	IRC_BTREE23_UNINITIALIZED  = 1
};


struct irc_btree23
{
	uint64_t size;
	uint64_t used;
	uint64_t root;
	uint64_t :64;
	uint64_t :64;
	uint64_t :64;
	uint64_t :64;
	uint64_t :64;
};


/* Initialize a btree structure.
 *
 * !! This is a CONTIGUOUS STRUCTURE behind *head. You probably don't want this on your stack then.
 * If you mmap() anonymously, point *head at the front and use the UNINITIALIZED flag.
 *
 * !! The size is BYTES, not elements: this means the tree can store some fewer number of
 * key/values than that. Nodes in this tree are 64 bytes and each stores 2 key/value pairs, but
 * you should review the literature to know why the mental math you just did might still blow
 * past the end.
 */
int irc_btree23_init(struct irc_btree23 *head, const size_t size, const uint flags);


/* Allocates and initializes the btree.
 *
 * You'd do well to read the init comment.
 */
struct irc_btree23 *irc_btree23_new(const size_t size, const uint flags);


/* Free a tree returned by irc_btree23_new() */
void irc_btree23_free(struct irc_btree23 *head);


/* Set the value for a key.
 *
 * You cannot use 0 for your key. You can store val=0 but when you get your non-zero key
 * you won't know if the key just doesn't have a value either.
 * ((don't worry, this behavior will be replaced with some clever flags in the node))
 */
int irc_btree23_set(struct irc_btree23 *head, const uint64_t key, const uint64_t val);


/* Get the value of a key.
 *
 * This will return zero if not found.
 * You also can't find a key=0 as explained in the irc_btree23_set(), and if your key
 * set a val=0 you won't know if this function simply didn't find that key.
 * ((don't worry, this behavior will be replaced with some clever flags in the node))
 */
uint64_t irc_btree23_get(struct irc_btree23 *head, const uint64_t key);


#endif
