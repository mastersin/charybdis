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

void _init();
void _fini();

mapi::header
IRCD_MODULE
{
	"Matrix Virtual Machine: fetch unit",
	_init, _fini
};

namespace ircd::m::vm
{
	static void fetch_one(m::vm::eval &, const m::event::id &event_id);
	static void enter(m::vm::eval &);
	extern "C" phase fetch_phase;
}

//
// init
//

void
_init()
{
	log::debug
	{
		m::vm::log, "Fetch unit ready"
	};
}

void
_fini()
{
	log::debug
	{
		m::vm::log, "Shutting down fetch unit..."
	};
}

//
// fetch phase
//

decltype(m::vm::fetch_phase)
m::vm::fetch_phase
{
	"fetch", enter
};

void
m::vm::enter(eval &eval)
{
	assert(eval.event_);
	assert(eval.opts);

	const event::prev prev
	{
		*eval.event_
	};

	const auto &prev_events
	{
		json::get<"prev_events"_>(prev)
	};

	const size_t &prev_count
	{
		size(prev_events)
	};

	for(size_t i(0); i < prev_count; ++i)
	{
		const auto &prev_id{prev.prev_event(i)};
		if(!exists(prev_id))
		{
			log::warning
			{
				log, "Missing prev %s in %s",
				string_view{prev_id},
				json::get<"event_id"_>(*eval.event_)
			};

			//fetch_one(eval, prev_id);
		}

		if(eval.opts->prev_check_exists && !exists(prev_id))
			throw error
			{
				fault::EVENT, "Missing prev event %s", prev_id
			};
	}
}

void
m::vm::fetch_one(eval &child,
                 const m::event::id &event_id)
{
	const auto &origin
	{
		at<"origin"_>(*child.event_)
	};

	m::v1::event::opts opts;
	opts.remote = origin;
	const unique_buffer<mutable_buffer> buf
	{
		96_KiB
	};

	m::v1::event request
	{
		event_id, buf, std::move(opts)
	};

	request.wait(seconds(10));
	request.get();

	const json::object &response
	{
		request
	};

	const m::event event
	{
		response
	};

	m::vm::opts vmopts;
	vmopts.non_conform.set(m::event::conforms::MISSING_PREV_STATE);
	vmopts.non_conform.set(m::event::conforms::MISSING_MEMBERSHIP);
	vmopts.verify = true;
	vmopts.prev_check_exists = false;
	vmopts.nothrows = -1U;
	vmopts.infolog_accept = true;
	vmopts.warnlog |= m::vm::fault::STATE;
	vmopts.errorlog &= ~m::vm::fault::STATE;
	m::vm::eval eval
	{
		event, vmopts
	};
}
