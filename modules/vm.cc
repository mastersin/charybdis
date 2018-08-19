// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

namespace ircd::m::vm
{
	extern hook::site<> commit_hook;
	extern hook::site<> eval_hook;
	extern hook::site<> notify_hook;
	extern phase enter;
	extern phase leave;

	static void write_commit(eval &);
	static fault _eval_edu(eval &, const event &);
	static fault _eval_pdu(eval &, const event &);

	extern "C" fault eval__event(eval &, const event &);
	extern "C" fault eval__commit(eval &, json::iov &, const json::iov &);
	extern "C" fault eval__commit_room(eval &, const room &, json::iov &, const json::iov &);

	static void init();
	static void fini();
}

ircd::mapi::header
IRCD_MODULE
{
	"Matrix Virtual Machine",
	ircd::m::vm::init, ircd::m::vm::fini
};

decltype(ircd::m::vm::commit_hook)
ircd::m::vm::commit_hook
{
	{ "name", "vm.commit" }
};

decltype(ircd::m::vm::eval_hook)
ircd::m::vm::eval_hook
{
	{ "name", "vm.eval" }
};

decltype(ircd::m::vm::notify_hook)
ircd::m::vm::notify_hook
{
	{ "name", "vm.notify" }
};

//
// init
//

void
ircd::m::vm::init()
{
	id::event::buf event_id;
	current_sequence = retired_sequence(event_id);

	log::info
	{
		log, "BOOT %s @%lu [%s]",
		string_view{m::my_node.node_id},
		current_sequence,
		current_sequence? string_view{event_id} : "NO EVENTS"_sv
	};
}

void
ircd::m::vm::fini()
{
	assert(eval::list.empty());

	id::event::buf event_id;
	const auto current_sequence
	{
		retired_sequence(event_id)
	};

	log::info
	{
		log, "HLT '%s' @%lu [%s]",
		string_view{m::my_node.node_id},
		current_sequence,
		current_sequence? string_view{event_id} : "NO EVENTS"_sv
	};
}

//
// eval
//

enum ircd::m::vm::fault
ircd::m::vm::eval__commit_room(eval &eval,
                               const room &room,
                               json::iov &event,
                               const json::iov &contents)
{
	// This eval entry point is only used for commits. We try to find the
	// commit opts the user supplied directly to this eval or with the room.
	if(!eval.copts)
		eval.copts = room.copts;

	if(!eval.copts)
		eval.copts = &vm::default_copts;

	// Note that the regular opts is unconditionally overridden because the
	// user should have provided copts instead.
	assert(!eval.opts || eval.opts == eval.copts);
	eval.opts = eval.copts;

	// Set a member pointer to the json::iov currently being composed. This
	// allows other parallel evals to have deep access to exactly what this
	// eval is attempting to do.
	eval.issue = &event;
	eval.room_id = room.room_id;
	eval.phase = &vm::enter;
	const unwind deissue{[&eval]
	{
		eval.phase = &vm::leave;
		eval.room_id = {};
		eval.issue = nullptr;
	}};

	assert(eval.issue);
	assert(eval.room_id);
	assert(eval.copts);
	assert(eval.opts);
	assert(room.room_id);

	const auto &opts
	{
		*eval.copts
	};

	const json::iov::push room_id
	{
		event, { "room_id", room.room_id }
	};

	using prev_prototype = std::pair<json::array, int64_t> (const m::room &,
	                                                        const mutable_buffer &,
	                                                        const size_t &,
	                                                        const bool &);
	static m::import<prev_prototype> make_prev__buf
	{
		"m_room", "make_prev__buf"
	};

	const bool need_tophead{event.at("type") != "m.room.create"};
	const unique_buffer<mutable_buffer> prev_buf{8192};
	const auto prev
	{
		make_prev__buf(room, prev_buf, 16, need_tophead)
	};

	const auto &prev_events{prev.first};
	const auto &depth{prev.second};
	const json::iov::set depth_
	{
		event, !event.has("depth"),
		{
			"depth", [&depth]
			{
				return json::value
				{
					depth == std::numeric_limits<int64_t>::max()? depth : depth + 1
				};
			}
		}
	};

	using auth_prototype = json::array (const m::room &,
	                                    const mutable_buffer &,
	                                    const vector_view<const string_view> &,
	                                    const string_view &);

	static m::import<auth_prototype> make_auth__buf
	{
		"m_room", "make_auth__buf"
	};

	char ae_buf[512];
	json::array auth_events;
	if(depth != -1 && opts.add_auth_events)
	{
		static const string_view types[]
		{
			"m.room.create",
			"m.room.join_rules",
			"m.room.power_levels",
		};

		const auto member
		{
			event.at("type") != "m.room.member"?
				string_view{event.at("sender")}:
				string_view{}
		};

		auth_events = make_auth__buf(room, ae_buf, types, member);
	}

	const json::iov::add auth_events_
	{
		event, opts.add_auth_events,
		{
			"auth_events", [&auth_events]() -> json::value
			{
				return auth_events;
			}
		}
	};

	const json::iov::add prev_events_
	{
		event, opts.add_prev_events,
		{
			"prev_events", [&prev_events]() -> json::value
			{
				return prev_events;
			}
		}
	};

	const json::iov::add prev_state_
	{
		event, opts.add_prev_state,
		{
			"prev_state", []
			{
				return json::empty_array;
			}
		}
	};

	return eval(event, contents);
}

enum ircd::m::vm::fault
ircd::m::vm::eval__commit(eval &eval,
                          json::iov &event,
                          const json::iov &contents)
{
	// This eval entry point is only used for commits. If the user did not
	// supply commit opts we supply the default ones here.
	if(!eval.copts)
		eval.copts = &vm::default_copts;

	// Note that the regular opts is unconditionally overridden because the
	// user should have provided copts instead.
	assert(!eval.opts || eval.opts == eval.copts);
	eval.opts = eval.copts;

	// Set a member pointer to the json::iov currently being composed. This
	// allows other parallel evals to have deep access to exactly what this
	// eval is attempting to do.
	assert(!eval.room_id || eval.issue == &event);
	if(!eval.room_id)
	{
		eval.issue = &event;
		eval.phase = &enter;
	}

	const unwind deissue{[&eval]
	{
		// issue is untouched when room_id is set; that indicates it was set
		// and will be unset by another eval function (i.e above).
		if(!eval.room_id)
		{
			eval.phase = &leave;
			eval.issue = nullptr;
		}
	}};

	assert(eval.issue);
	assert(eval.copts);
	assert(eval.opts);
	assert(eval.copts);

	const auto &opts
	{
		*eval.copts
	};

	const json::iov::add origin_
	{
		event, opts.add_origin,
		{
			"origin", []() -> json::value
			{
				return my_host();
			}
		}
	};

	const json::iov::add origin_server_ts_
	{
		event, opts.add_origin_server_ts,
		{
			"origin_server_ts", []
			{
				return json::value{ircd::time<milliseconds>()};
			}
		}
	};

	const json::strung content
	{
		contents
	};

	// event_id

	sha256::buf event_id_hash;
	if(opts.add_event_id)
	{
		const json::iov::push _content
		{
			event, { "content", content },
		};

		thread_local char preimage_buf[64_KiB];
		event_id_hash = sha256
		{
			stringify(mutable_buffer{preimage_buf}, event)
		};
	}

	const string_view event_id
	{
		opts.add_event_id?
			make_id(event, eval.event_id, event_id_hash):
			string_view{}
	};

	const json::iov::add event_id_
	{
		event, opts.add_event_id,
		{
			"event_id", [&event_id]() -> json::value
			{
				return event_id;
			}
		}
	};

	// hashes

	char hashes_buf[128];
	const string_view hashes
	{
		opts.add_hash?
			m::event::hashes(hashes_buf, event, content):
			string_view{}
	};

	const json::iov::add hashes_
	{
		event, opts.add_hash,
		{
			"hashes", [&hashes]() -> json::value
			{
				return hashes;
			}
		}
	};

	// sigs

	char sigs_buf[384];
	const string_view sigs
	{
		opts.add_sig?
			m::event::signatures(sigs_buf, event, contents):
			string_view{}
	};

	const json::iov::add sigs_
	{
		event, opts.add_sig,
		{
			"signatures", [&sigs]() -> json::value
			{
				return sigs;
			}
		}
	};

	const json::iov::push content_
	{
		event, { "content", content },
	};

	return eval(event);
}

enum ircd::m::vm::fault
ircd::m::vm::eval__event(eval &eval,
                         const event &event)
try
{
	// Set a member pointer to the event currently being evaluated. This
	// allows other parallel evals to have deep access to exactly what this
	// eval is working on. The pointer must be nulled on the way out.
    eval.event_ = &event;
	if(!eval.issue)
		eval.phase = &enter;

	const unwind null_event{[&eval]
	{
		if(!eval.issue)
			eval.phase = &leave;

		eval.event_ = nullptr;
	}};

	assert(eval.opts);
	assert(eval.event_);
	assert(eval.id);
	assert(eval.ctx);

	const auto &opts
	{
		*eval.opts
	};

	if(eval.copts)
	{
		if(unlikely(!my_host(at<"origin"_>(event))))
			throw error
			{
				fault::GENERAL, "Committing event for origin: %s", at<"origin"_>(event)
			};

		if(eval.copts->debuglog_precommit)
			log::debug
			{
				log, "injecting event(mark +%ld) %s",
				vm::current_sequence,
				pretty_oneline(event)
			};

		check_size(event);
		commit_hook(event);
	}

	const event::conforms &report
	{
		opts.conforming && !opts.conformed?
			event::conforms{event, opts.non_conform.report}:
			opts.report
	};

	if(opts.conforming && !report.clean())
		throw error
		{
			fault::INVALID, "Non-conforming event: %s", string(report)
		};

	//ctx::sleep(milliseconds(250));

	// A conforming (with lots of masks) event without an event_id is an EDU.
	const fault ret
	{
		json::get<"event_id"_>(event)?
			_eval_pdu(eval, event):
			_eval_edu(eval, event)
	};

	if(ret != fault::ACCEPT)
		return ret;

	vm::accepted accepted
	{
		event, &opts, &report
	};

	if(opts.effects)
		notify_hook(event);

	if(opts.notify)
		vm::accept(accepted);

	if(opts.debuglog_accept)
		log::debug
		{
			log, "%s", pretty_oneline(event)
		};

	if(opts.infolog_accept)
		log::debug
		{
			log, "%s", pretty_oneline(event)
		};

	return ret;
}
catch(const ctx::interrupted &e) // INTERRUPTION
{
	if(eval.opts->errorlog & fault::INTERRUPT)
		log::error
		{
			log, "eval %s: #NMI: %s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			e.what()
		};

	if(eval.opts->warnlog & fault::INTERRUPT)
		log::warning
		{
			log, "eval %s: #NMI: %s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			e.what()
		};

	throw;
}
catch(const error &e) // VM FAULT CODE
{
	if(eval.opts->errorlog & e.code)
		log::error
		{
			log, "eval %s: %s: %s %s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			reflect(e.code),
			e.what(),
			e.content
		};

	if(eval.opts->warnlog & e.code)
		log::warning
		{
			log, "eval %s: %s: %s %s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			reflect(e.code),
			e.what(),
			e.content
		};

	if(eval.opts->nothrows & e.code)
		return e.code;

	throw;
}
catch(const m::error &e) // GENERAL MATRIX ERROR
{
	if(eval.opts->errorlog & fault::GENERAL)
		log::error
		{
			log, "eval %s: #GP: %s :%s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			e.what(),
			e.content
		};

	if(eval.opts->warnlog & fault::GENERAL)
		log::warning
		{
			log, "eval %s: #GP: %s :%s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			e.what(),
			e.content
		};

	if(eval.opts->nothrows & fault::GENERAL)
		return fault::GENERAL;

	throw;
}
catch(const std::exception &e) // ALL OTHER ERRORS
{
	if(eval.opts->errorlog & fault::GENERAL)
		log::error
		{
			log, "eval %s: #GP: %s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			e.what()
		};

	if(eval.opts->warnlog & fault::GENERAL)
		log::warning
		{
			log, "eval %s: #GP: %s",
			json::get<"event_id"_>(event)?: json::string{"<edu>"},
			e.what()
		};

	if(eval.opts->nothrows & fault::GENERAL)
		return fault::GENERAL;

	throw error
	{
		fault::GENERAL, "%s", e.what()
	};
}

enum ircd::m::vm::fault
ircd::m::vm::_eval_edu(eval &eval,
                       const event &event)
{
	eval_hook(event);
	return fault::ACCEPT;
}

enum ircd::m::vm::fault
ircd::m::vm::_eval_pdu(eval &eval,
                       const event &event)
{
	assert(eval.opts);
	const auto &opts
	{
		*eval.opts
	};

	const m::event::id &event_id
	{
		at<"event_id"_>(event)
	};

	const m::room::id &room_id
	{
		at<"room_id"_>(event)
	};

	const string_view &type
	{
		at<"type"_>(event)
	};

	if(!opts.replays && exists(event_id))  //TODO: exclusivity
		throw error
		{
			fault::EXISTS, "Event has already been evaluated."
		};

	if(opts.verify)
		if(!verify(event))
			throw m::BAD_SIGNATURE
			{
				"Signature verification failed"
			};

	const size_t reserve_bytes
	{
		opts.reserve_bytes == size_t(-1)?
			json::serialized(event):
			opts.reserve_bytes
	};

	// Obtain sequence number here
	eval.sequence = ++vm::current_sequence;

	eval_hook(event);

	int64_t top;
	id::event::buf head;
	std::tie(head, top, std::ignore) = m::top(std::nothrow, room_id);
	if(top < 0 && (opts.head_must_exist || opts.history))
		if(type != "m.room.create")
			throw error
			{
				fault::STATE, "Found nothing for room %s", string_view{room_id}
			};

	static m::import<m::vm::phase> fetch
	{
		"vm_fetch", "fetch_phase"
	};

	fetch(eval);

	if(!opts.write)
		return fault::ACCEPT;

	db::txn txn
	{
		*dbs::events, db::txn::opts
		{
			reserve_bytes + opts.reserve_index,   // reserve_bytes
			0,                                    // max_bytes (no max)
		}
	};

	// Expose to eval interface
	eval.txn = &txn;
	const unwind clear{[&eval]
	{
		eval.txn = nullptr;
	}};

	// Preliminary write_opts
	m::dbs::write_opts wopts;
	wopts.present = opts.present;
	wopts.history = opts.history;
	wopts.head = opts.head;
	wopts.refs = opts.refs;
	wopts.event_idx = eval.sequence;

	m::state::id_buffer new_root_buf;
	wopts.root_out = new_root_buf;
	string_view new_root;
	if(type != "m.room.create")
	{
		m::room room{room_id, head};
		m::room::state state{room};
		wopts.root_in = state.root_id;
		new_root = dbs::write(txn, event, wopts);
	}
	else
	{
		new_root = dbs::write(txn, event, wopts);
	}

	write_commit(eval);
	return fault::ACCEPT;
}

void
ircd::m::vm::write_commit(eval &eval)
{
	assert(eval.txn);
	auto &txn(*eval.txn);
	if(eval.opts->debuglog_accept)
		log::debug
		{
			log, "Committing %zu cells in %zu bytes to events database...",
			txn.size(),
			txn.bytes()
		};

	txn();
}

uint64_t
ircd::m::vm::retired_sequence()
{
	event::id::buf event_id;
	return retired_sequence(event_id);
}

uint64_t
ircd::m::vm::retired_sequence(event::id::buf &event_id)
{
	static constexpr auto column_idx
	{
		json::indexof<event, "event_id"_>()
	};

	auto &column
	{
		dbs::event_column.at(column_idx)
	};

	const auto it
	{
		column.rbegin()
	};

	if(!it)
	{
		// If this iterator is invalid the events db should
		// be completely fresh.
		assert(db::sequence(*dbs::events) == 0);
		return 0;
	}

	const auto &ret
	{
		byte_view<uint64_t>(it->first)
	};

	event_id = it->second;
	return ret;
}

//
// phase enter
//

decltype(ircd::m::vm::enter)
ircd::m::vm::enter
{
	"enter"
};

//
// phase leave
//

decltype(ircd::m::vm::leave)
ircd::m::vm::leave
{
	"leave"
};
