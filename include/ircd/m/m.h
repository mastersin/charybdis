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
#define HAVE_IRCD_M_H

/// Matrix Protocol System
namespace ircd::m
{
	using ircd::hash;

	struct init;

	IRCD_OVERLOAD(generate)

	extern struct log::log log;
}

namespace ircd::m::vm
{
	struct opts;
}

#include "name.h"
#include "error.h"
#include "import.h"
#include "self.h"
#include "id.h"
#include "event.h"
#include "dbs.h"
#include "state.h"
#include "vm.h"
#include "room.h"
#include "user.h"
#include "rooms.h"
#include "filter.h"
#include "events.h"
#include "node.h"
#include "login.h"
#include "register.h"
#include "invite_3pid.h"
#include "createroom.h"
#include "request.h"
#include "v1/v1.h"
#include "keys.h"
#include "edu.h"
#include "presence.h"
#include "typing.h"
#include "receipt.h"
#include "txn.h"
#include "hook.h"
#include "visible.h"

struct ircd::m::init
{
	struct listeners;

	self::init _self;
	dbs::init _dbs;

	static void bootstrap();
	void init_imports();
	void init_keys();
	void close();

  public:
	init(const string_view &origin);
	~init() noexcept;
};
