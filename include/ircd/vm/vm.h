// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#pragma once
#define HAVE_IRCD_VM_H

namespace ircd::vm
{
	IRCD_EXCEPTION(ircd::error, error)

	struct core;
	struct stack;
}

#include "op.h"

struct ircd::vm::stack
{
	struct frame;
};

struct ircd::vm::stack::frame
{

};

struct ircd::vm::core
{
	struct port;
	struct inst;

	const_buffer cs;
	uint32_t sp {0};
	uint32_t ip {0};
	bool zf {false};
	bool hf {false};
	bool ff {false};

	bool operator()();

	core(const const_buffer &cs);

	friend std::string debug(const core &);
};

struct ircd::vm::core::inst
:const_buffer
{
	inst(const const_buffer &cs, const size_t &ip);
	inst() = default;
};

struct ircd::vm::core::port
{
	core::inst inst;
	uint32_t ip;
	uint32_t sp;
	bool zf {false};
	bool hf {false};
	bool ff {false};

	bool operator()();

	port(const core &);

	friend std::string debug(const port &);
};
