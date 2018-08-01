// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

namespace ircd::vm
{
	static void init();
	static void fini();
}

ircd::mapi::header
IRCD_MODULE
{
	"Matrix Virtual Machine",
	ircd::vm::init,
	ircd::vm::fini
};

//
// init
//

void
ircd::vm::init()
{
}

void
ircd::vm::fini()
{
}
