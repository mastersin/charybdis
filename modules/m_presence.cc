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

mapi::header
IRCD_MODULE
{
	"Matrix Presence"
};

const string_view
valid_states[]
{
	"online", "offline", "unavailable",
};

extern "C" bool
presence_valid_state(const string_view &state)
{
	return std::any_of(begin(valid_states), end(valid_states), [&state]
	(const string_view &valid)
	{
		return state == valid;
	});
}

static void handle_edu_m_presence_(const m::event &, const m::edu::m_presence &edu);
static void handle_edu_m_presence(const m::event &);

const m::hookfn<>
_m_presence_eval
{
	handle_edu_m_presence,
	{
		{ "_site",   "vm.eval"     },
		{ "type",    "m.presence"  },
	}
};

void
handle_edu_m_presence(const m::event &event)
try
{
	if(m::my_host(at<"origin"_>(event)))
		return;

	const json::object &content
	{
		at<"content"_>(event)
	};

	const json::array &push
	{
		content.get("push")
	};

	for(const json::object &presence : push)
		handle_edu_m_presence_(event, presence);
}
catch(const std::exception &e)
{
	log::derror
	{
		"Presence from %s :%s",
		json::get<"origin"_>(event),
		e.what(),
	};
}

void
handle_edu_m_presence_(const m::event &event,
                       const m::edu::m_presence &object)
try
{
	const m::user::id &user_id
	{
		at<"user_id"_>(object)
	};

	if(user_id.host() != at<"origin"_>(event))
	{
		log::dwarning
		{
			"Ignoring %s from %s for user %s",
			at<"type"_>(event),
			at<"origin"_>(event),
			string_view{user_id}
		};

		return;
	}

	bool useful{true};
	m::presence::get(std::nothrow, user_id, [&event, &object, &useful]
	(const m::event &existing_event, const json::object &existing_object)
	{
		if(json::get<"user_id"_>(object) != unquote(existing_object.get("user_id")))
		{
			//log::critical("%s != %s", json::get<"user_id"_>(object), unquote(existing_object.get("user_id")));
			return;
		}

		assert(json::get<"user_id"_>(object) == unquote(existing_object.get("user_id")));

		const auto &prev_active_ago
		{
			existing_object.get<time_t>("last_active_ago")
		};

		const time_t &now_active_ago
		{
			json::get<"last_active_ago"_>(object)
		};

		const time_t &prev_active_absolute
		{
			json::get<"origin_server_ts"_>(existing_event) - prev_active_ago
		};

		const time_t &now_active_absolute
		{
			json::get<"origin_server_ts"_>(event) - now_active_ago
		};

		// First way to filter out the synapse presence spam bug is seeing
		// if the update is older than the last update.
		if(now_active_absolute < prev_active_absolute)
			useful = false;
		else if(json::get<"presence"_>(object) != unquote(existing_object.get("presence")))
			useful = true;
		else if(json::get<"currently_active"_>(object) != existing_object.get<bool>("currently_active"))
			useful = true;
		else if(json::get<"currently_active"_>(object))
			useful = true;
		else
			useful = false;
	});

	if(!useful)
		return;

	const auto evid
	{
		m::presence::set(object)
	};

	log::info
	{
		"%s %s is %s and %s %zd seconds ago",
		at<"origin"_>(event),
		string_view{user_id},
		json::get<"currently_active"_>(object)? "active"_sv : "inactive"_sv,
		json::get<"presence"_>(object),
		json::get<"last_active_ago"_>(object) / 1000L
	};
}
catch(const m::error &e)
{
	log::error
	{
		"Presence from %s :%s :%s",
		json::get<"origin"_>(event),
		e.what(),
		e.content
	};
}

extern "C" bool
get__m_presence(const std::nothrow_t,
                const m::user &user,
                const m::presence::event_closure &closure)
{
	static const m::event::fetch::opts fopts
	{
		m::event::keys::include
		{
			"event_id",
			"content",
			"origin_server_ts",
		}
	};

	const m::user::room user_room
	{
		user, nullptr, &fopts
	};

	return user_room.get(std::nothrow, "ircd.presence", "", [&closure]
	(const m::event &event)
	{
		closure(event, json::get<"content"_>(event));
	});
}

extern "C" m::event::id::buf
commit__m_presence(const m::presence &content)
{
	const m::user user
	{
		at<"user_id"_>(content)
	};

	//TODO: ABA
	if(!exists(user))
		create(user.user_id);

	const m::user::room user_room
	{
		user
	};

	//TODO: ABA
	return send(user_room, user.user_id, "ircd.presence", "", json::strung{content});
}
