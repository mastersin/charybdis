// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

using namespace ircd;

extern "C" void rehash_conf();

mapi::header
IRCD_MODULE
{
	"Server Configuration", []
	{
		rehash_conf();
	}
};

const m::room::id::buf
conf_room_id
{
	"conf", ircd::my_host()
};

m::room
conf_room
{
	conf_room_id
};

extern "C" m::event::id::buf
set_conf_item(const m::user::id &sender,
              const string_view &key,
              const string_view &val)
{
	return send(conf_room, sender, "ircd.conf.item", key,
	{
		{ "value", val }
	});
}

extern "C" void
get_conf_item(const string_view &key,
              const std::function<void (const string_view &)> &closure)
{
	conf_room.get("ircd.conf.item", key, [&closure]
	(const m::event &event)
	{
		const auto &value
		{
			unquote(at<"content"_>(event).at("value"))
		};

		closure(value);
	});
}

static void
conf_updated(const m::event &event)
noexcept try
{
	const auto &content
	{
		at<"content"_>(event)
	};

	const auto &key
	{
		at<"state_key"_>(event)
	};

	const string_view &value
	{
		unquote(content.at("value"))
	};

	log::debug
	{
		"Updating conf [%s] => %s", key, value
	};

	if(runlevel == runlevel::START && !conf::exists(key))
	{
		log::dwarning
		{
			"Cannot set conf item '%s'; does not exist or not loaded yet",
			key
		};

		return;
	}

	ircd::conf::set(key, value);
}
catch(const std::exception &e)
{
	log::error
	{
		"Failed to set conf item '%s' :%s",
		json::get<"state_key"_>(event),
		e.what()
	};
}

const m::hookfn<>
conf_updated_hook
{
	conf_updated,
	{
		{ "_site",       "vm.notify"       },
		{ "room_id",     "!conf"           },
		{ "type",        "ircd.conf.item"  },
	}
};

static void
init_conf_items(const m::event &)
{
	const m::room::state state
	{
		conf_room
	};

	state.for_each("ircd.conf.item", []
	(const m::event &event)
	{
		conf_updated(event);
	});
}

const m::hookfn<>
init_conf_items_hook
{
	init_conf_items,
	{
		{ "_site",       "vm.notify"      },
		{ "room_id",     "!ircd"          },
		{ "type",        "m.room.member"  },
		{ "membership",  "join"           },
		{ "state_key",   "@ircd"          },
	}
};

void
rehash_conf()
{
	init_conf_items(m::event{});
}

static void
create_conf_item(const string_view &key,
                 const conf::item<> &item)
{
	thread_local char vbuf[4_KiB];
	const string_view &val
	{
		item.get(vbuf)
	};

	send(conf_room, m::me.user_id, "ircd.conf.item", key,
	{
		{ "value", val }
	});
}

static void
create_conf_room(const m::event &)
{
	m::create(conf_room_id, m::me.user_id);

	for(const auto &p : conf::items)
	{
		const auto &key{p.first};
		const auto &item{p.second}; assert(item);
		create_conf_item(key, *item);
	}
}

const m::hookfn<>
create_conf_room_hook
{
	create_conf_room,
	{
		{ "_site",       "vm.notify"      },
		{ "room_id",     "!ircd"          },
		{ "type",        "m.room.create"  },
	}
};
