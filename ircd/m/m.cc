// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#include <ircd/m/m.h>

namespace ircd::m
{
	struct log::log log
	{
		"matrix", 'm'
	};

	std::map<std::string, ircd::module> modules;
	std::list<ircd::net::listener> listeners;

	static void leave_ircd_room();
	static void join_ircd_room();
}

//
// my user
//

const ircd::m::user::id::buf
ircd_user_id
{
	"ircd", ircd::my_host()  //TODO: hostname
};

ircd::m::user
ircd::m::me
{
	ircd_user_id
};

//
// my room
//

const ircd::m::room::id::buf
ircd_room_id
{
	"ircd", ircd::my_host()
};

ircd::m::room
ircd::m::my_room
{
	ircd_room_id
};

//
// my node
//

const ircd::m::node::id::buf
ircd_node_id
{
	"", ircd::my_host()  //TODO: hostname
};

ircd::m::node
ircd::m::my_node
{
	ircd_node_id
};

//
// misc
//

const ircd::m::room::id::buf
control_room_id
{
	"control", ircd::my_host()
};

ircd::m::room
ircd::m::control
{
	control_room_id
};

//
// init
//

ircd::m::init::init()
try
:config
{
	ircd::conf::config //TODO: X
}
,_keys
{
	this->config
}
{
	modules();
	if(db::sequence(*dbs::events) == 0)
		bootstrap();

	join_ircd_room();
	listeners();
}
catch(const m::error &e)
{
	log.error("%s %s", e.what(), e.content);
	throw;
}
catch(const std::exception &e)
{
	log.error("%s", e.what());
	throw;
}

ircd::m::init::~init()
noexcept try
{
	leave_ircd_room();
	m::listeners.clear();
	m::modules.clear();
}
catch(const m::error &e)
{
	log.critical("%s %s", e.what(), e.content);
	ircd::terminate();
}

void
ircd::m::init::modules()
{
	const string_view prefixes[]
	{
		"s_", "m_", "client_", "key_", "federation_", "media_"
	};

	for(const auto &name : mods::available())
		if(startswith_any(name, std::begin(prefixes), std::end(prefixes)))
			m::modules.emplace(name, name);

	m::modules.emplace("root"s, "root"s);
}

namespace ircd::m
{
	static void init_listener(const json::object &config, const json::object &opts, const string_view &bindaddr);
	static void init_listener(const json::object &config, const json::object &opts);
}

void
ircd::m::init::listeners()
{
	const json::array listeners
	{
		config["listeners"]
	};

	if(m::listeners.empty())
		init_listener(config, {});
	else
		for(const json::object opts : listeners)
			init_listener(config, opts);
}

static void
ircd::m::init_listener(const json::object &config,
                       const json::object &opts)
{
	const json::array binds
	{
		opts["bind_addresses"]
	};

	if(binds.empty())
		init_listener(config, opts, "0.0.0.0");
	else
		for(const auto &bindaddr : binds)
			init_listener(config, opts, unquote(bindaddr));
}

static void
ircd::m::init_listener(const json::object &config,
                       const json::object &opts,
                       const string_view &host)
{
	const json::array resources
	{
		opts["resources"]
	};

	// resources has multiple names with different configs which are being
	// ignored :-/
	const std::string name{"Matrix"s};

	// Translate synapse options to our options (which reflect asio::ssl)
	const json::strung options{json::members
	{
		{ "name",                      name                            },
		{ "host",                      host                            },
		{ "port",                      opts.get("port", 8448L)         },
		{ "ssl_certificate_file_pem",  config["tls_certificate_path"]  },
		{ "ssl_private_key_file_pem",  config["tls_private_key_path"]  },
		{ "ssl_tmp_dh_file",           config["tls_dh_params_path"]    },
	}};

	m::listeners.emplace_back(options);
}

void
ircd::m::init::bootstrap()
{
	assert(dbs::events);
	assert(db::sequence(*dbs::events) == 0);

	ircd::log::notice
	(
		"This appears to be your first time running IRCd because the events "
		"database is empty. I will be bootstrapping it with initial events now..."
	);

	create(user::users, me.user_id);
	me.activate(
	{
		{ "active", true }
	});

	create(my_room, me.user_id);
	send(my_room, me.user_id, "m.room.name", "",
	{
		{ "name", "IRCd's Room" }
	});

	send(my_room, me.user_id, "m.room.topic", "",
	{
		{ "topic", "The daemon's den." }
	});

	send(user::users, me.user_id, "m.room.name", "",
	{
		{ "name", "Users" }
	});

	create(user::tokens, me.user_id);
	send(user::tokens, me.user_id, "m.room.name", "",
	{
		{ "name", "User Tokens" }
	});
}

bool
ircd::m::self::host(const string_view &s)
{
	return s == host();
}

ircd::string_view
ircd::m::self::host()
{
	return "zemos.net"; //me.user_id.host();
}

ircd::conf::item<std::string>
me_online_status_msg
{
	{ "name",     "ircd.me.online.status_msg"          },
	{ "default",  "Wanna chat? IRCd at your service!"  }
};

void
ircd::m::join_ircd_room()
try
{
	presence::set(me, "online", me_online_status_msg);
	join(my_room, me.user_id);
}
catch(const m::ALREADY_MEMBER &e)
{
	log.warning("IRCd did not shut down correctly...");
}

ircd::conf::item<std::string>
me_offline_status_msg
{
	{ "name",     "ircd.me.offline.status_msg"     },
	{ "default",  "Catch ya on the flip side..."   }
};

void
ircd::m::leave_ircd_room()
{
	leave(my_room, me.user_id);
	presence::set(me, "offline", me_offline_status_msg);
}

///////////////////////////////////////////////////////////////////////////////
//
// m/keys.h
//

ircd::ed25519::sk
ircd::m::self::secret_key
{};

ircd::ed25519::pk
ircd::m::self::public_key
{};

std::string
ircd::m::self::public_key_b64
{};

std::string
ircd::m::self::public_key_id
{};

std::string
ircd::m::self::tls_cert_der
{};

std::string
ircd::m::self::tls_cert_der_sha256_b64
{};

void
ircd::m::keys::get(const string_view &server_name,
                   const string_view &key_id,
                   const key_closure &closure)
try
{
	get(server_name, [&key_id, &closure](const keys &keys)
	{
		const json::object vks
		{
			at<"verify_keys"_>(keys)
		};

		const json::object vkk
		{
			vks.at(key_id)
		};

		const string_view &key
		{
			vkk.at("key")
		};

		closure(key);
	});
}
catch(const json::not_found &e)
{
	throw m::NOT_FOUND
	{
		"Failed to find key '%s' for '%s': %s",
		key_id,
		server_name,
		e.what()
	};
}

void
ircd::m::keys::get(const string_view &server_name,
                   const string_view &key_id,
                   const closure &closure)
{
	get(server_name, [&key_id, &closure](const keys &keys)
	{
		closure(keys);
	});
}

void
ircd::m::keys::get(const string_view &server_name,
                   const closure &closure_)
{
	using prototype = void (const string_view &, const closure &);

	static import<prototype> function
	{
		"key_keys", "get__keys"
	};

	return function(server_name, closure_);
}

void
ircd::m::keys::get(const string_view &server_name,
                   const string_view &key_id,
                   const string_view &query_server,
                   const closure &closure_)
{
	using prototype = void (const string_view &, const string_view &, const string_view &, const closure &);

	static import<prototype> function
	{
		"key_keys", "query__keys"
	};

	return function(server_name, key_id, query_server, closure_);
}

//
// init
//

ircd::m::keys::init::init(const json::object &config)
:config{config}
{
	certificate();
	signing();
}

ircd::m::keys::init::~init()
noexcept
{
}

void
ircd::m::keys::init::certificate()
{
	const std::string private_key_file
	{
		unquote(config.at("tls_private_key_path"))
	};

	const std::string public_key_file
	{
		unquote(config.get("tls_public_key_path", private_key_file + ".pub"))
	};

	if(!fs::exists(private_key_file))
	{
		log::warning("Failed to find certificate private key @ `%s'; creating...", private_key_file);
		openssl::genrsa(private_key_file, public_key_file);
	}

	const std::string cert_file
	{
		unquote(config.at("tls_certificate_path"))
	};

	if(!fs::exists(cert_file))
		throw fs::error("Failed to find SSL certificate @ `%s'", cert_file);

	const auto cert_pem
	{
		fs::read(cert_file)
	};

	const unique_buffer<mutable_buffer> der_buf
	{
		8_KiB
	};

	const auto cert_der
	{
		openssl::cert2d(der_buf, cert_pem)
	};

	const fixed_buffer<const_buffer, crh::sha256::digest_size> hash
	{
		sha256{cert_der}
	};

	self::tls_cert_der_sha256_b64 =
	{
		b64encode_unpadded(hash)
	};

	log.info("Certificate `%s' :PEM %zu bytes; DER %zu bytes; sha256b64 %s",
	         cert_file,
	         cert_pem.size(),
	         ircd::size(cert_der),
	         self::tls_cert_der_sha256_b64);

	thread_local char print_buf[8_KiB];
	log.info("Certificate `%s' :%s",
	         cert_file,
	         openssl::print_subject(print_buf, cert_pem));
}

void
ircd::m::keys::init::signing()
{
	const std::string sk_file
	{
		unquote(config.get("signing_key_path", "construct.sk"))
	};

	if(fs::exists(sk_file))
		log.info("Using ed25519 secret key @ `%s'", sk_file);
	else
		log.notice("Creating new ed25519 secret key @ `%s'", sk_file);

	self::secret_key = ed25519::sk
	{
		sk_file, &self::public_key
	};

	self::public_key_b64 = b64encode_unpadded(self::public_key);
	const fixed_buffer<const_buffer, sha256::digest_size> hash
	{
		sha256{self::public_key}
	};

	const auto public_key_hash_b58
	{
		b58encode(hash)
	};

	static const auto trunc_size{8};
	self::public_key_id = fmt::snstringf
	{
		BUFSIZE, "ed25519:%s", trunc(public_key_hash_b58, trunc_size)
	};

	log.info("Current key is '%s' and the public key is: %s",
	         self::public_key_id,
	         self::public_key_b64);
}

///////////////////////////////////////////////////////////////////////////////
//
// m/presence.h
//

ircd::m::presence::presence(const user &user,
                            const mutable_buffer &buf)
:presence
{
	get(user, buf)
}
{
}

ircd::m::event::id::buf
ircd::m::presence::set(const user &user,
                       const string_view &presence,
                       const string_view &status_msg)
{
	return set(m::presence
	{
		{ "user_id",     user.user_id  },
		{ "presence",    presence      },
		{ "status_msg",  status_msg    },
	});
}

ircd::m::event::id::buf
ircd::m::presence::set(const presence &object)
{
	using prototype = event::id::buf (const presence &);

	static import<prototype> function
	{
		"m_presence", "presence_set"
	};

	return function(object);
}

ircd::json::object
ircd::m::presence::get(const user &user,
                       const mutable_buffer &buffer)
{
	using prototype = json::object (const m::user &, const mutable_buffer &);

	static import<prototype> function
	{
		"m_presence", "presence_get"
	};

	return function(user, buffer);
}

bool
ircd::m::presence::valid_state(const string_view &state)
{
	using prototype = bool (const string_view &);

	static import<prototype> function
	{
		"m_presence", "presence_valid_state"
	};

	return function(state);
}

///////////////////////////////////////////////////////////////////////////////
//
// m/node.h
//

/// ID of the room which indexes all nodes (an instance of the room is
/// provided below).
const ircd::m::room::id::buf
nodes_room_id
{
	"nodes", ircd::my_host()
};

/// The nodes room is the database of all nodes. It primarily serves as an
/// indexing mechanism and for top-level node related keys.
///
const ircd::m::room
ircd::m::nodes
{
	nodes_room_id
};

ircd::m::node
ircd::m::create(const id::node &node_id,
                const json::members &args)
{
	using prototype = node (const id::node &, const json::members &);

	static import<prototype> function
	{
		"s_node", "create_node"
	};

	assert(node_id);
	return function(node_id, args);
}

bool
ircd::m::exists(const node::id &node_id)
{
	using prototype = bool (const node::id &);

	static import<prototype> function
	{
		"s_node", "exists__nodeid"
	};

	return function(node_id);
}

bool
ircd::m::my(const node &node)
{
	return my(node.node_id);
}

//
// node
//

/// Generates a node-room ID into buffer; see room_id() overload.
ircd::m::id::room::buf
ircd::m::node::room_id()
const
{
	ircd::m::id::room::buf buf;
	return buf.assigned(room_id(buf));
}

/// This generates a room mxid for the "node's room" essentially serving as
/// a database mechanism for this specific node. This room_id is a hash of
/// the node's full mxid.
///
ircd::m::id::room
ircd::m::node::room_id(const mutable_buffer &buf)
const
{
	assert(!empty(node_id));
	const sha256::buf hash
	{
		sha256{node_id}
	};

	char b58[size(hash) * 2];
	return
	{
		buf, b58encode(b58, hash), my_host()
	};
}

//
// node::room
//

ircd::m::node::room::room(const m::node::id &node_id)
:room{m::node{node_id}}
{}

ircd::m::node::room::room(const m::node &node)
:node{node}
,room_id{node.room_id()}
{
	static_cast<m::room &>(*this) = room_id;
}

///////////////////////////////////////////////////////////////////////////////
//
// m/user.h
//

/// ID of the room which indexes all users (an instance of the room is
/// provided below).
const ircd::m::room::id::buf
users_room_id
{
	"users", ircd::my_host()
};

/// The users room is the database of all users. It primarily serves as an
/// indexing mechanism and for top-level user related keys. Accounts
/// registered on this server will be among state events in this room.
/// Users do not have access to this room, it is used internally.
///
ircd::m::room
ircd::m::user::users
{
	users_room_id
};

/// ID of the room which stores ephemeral tokens (an instance of the room is
/// provided below).
const ircd::m::room::id::buf
tokens_room_id
{
	"tokens", ircd::my_host()
};

/// The tokens room serves as a key-value lookup for various tokens to
/// users, etc. It primarily serves to store access tokens for users. This
/// is a separate room from the users room because in the future it may
/// have an optimized configuration as well as being more easily cleared.
///
ircd::m::room
ircd::m::user::tokens
{
	tokens_room_id
};

bool
ircd::m::exists(const user::id &user_id)
{
	return user::users.has("ircd.user", user_id);
}

bool
ircd::m::my(const user &user)
{
	return my(user.user_id);
}

//
// user
//

/// Generates a user-room ID into buffer; see room_id() overload.
ircd::m::id::room::buf
ircd::m::user::room_id()
const
{
	ircd::m::id::room::buf buf;
	return buf.assigned(room_id(buf));
}

/// This generates a room mxid for the "user's room" essentially serving as
/// a database mechanism for this specific user. This room_id is a hash of
/// the user's full mxid.
///
ircd::m::id::room
ircd::m::user::room_id(const mutable_buffer &buf)
const
{
	assert(!empty(user_id));
	const sha256::buf hash
	{
		sha256{user_id}
	};

	char b58[size(hash) * 2];
	return
	{
		buf, b58encode(b58, hash), my_host()
	};
}

ircd::string_view
ircd::m::user::gen_access_token(const mutable_buffer &buf)
{
	static const size_t token_max{32};
	static const auto &token_dict{rand::dict::alpha};

	const mutable_buffer out
	{
		data(buf), std::min(token_max, size(buf))
	};

	return rand::string(token_dict, out);
}

ircd::m::event::id::buf
ircd::m::user::activate(const json::members &contents)
{
	using prototype = event::id::buf (const m::user &, const json::members &);

	static import<prototype> function
	{
		"client_account", "activate__user"
	};

	return function(*this, contents);
}

ircd::m::event::id::buf
ircd::m::user::deactivate(const json::members &contents)
{
	using prototype = event::id::buf (const m::user &, const json::members &);

	static import<prototype> function
	{
		"client_account", "deactivate__user"
	};

	return function(*this, contents);
}

bool
ircd::m::user::is_active()
const
{
	using prototype = bool (const m::user &);

	static import<prototype> function
	{
		"client_account", "is_active__user"
	};

	return function(*this);
}

ircd::m::event::id::buf
ircd::m::user::password(const string_view &password)
{
	using prototype = event::id::buf (const m::user::id &, const string_view &) noexcept;

	static import<prototype> function
	{
		"client_account", "set_password"
	};

	return function(user_id, password);
}

bool
ircd::m::user::is_password(const string_view &password)
const noexcept try
{
	using prototype = bool (const m::user::id &, const string_view &) noexcept;

	static import<prototype> function
	{
		"client_account", "is_password"
	};

	return function(user_id, password);
}
catch(const std::exception &e)
{
	log::critical
	{
		"user::is_password(): %s %s",
		string_view{user_id},
		e.what()
	};

	return false;
}

//
// user::room
//

ircd::m::user::room::room(const m::user::id &user_id)
:room{m::user{user_id}}
{}

ircd::m::user::room::room(const m::user &user)
:user{user}
,room_id{user.room_id()}
{
	static_cast<m::room &>(*this) = room_id;
}

///////////////////////////////////////////////////////////////////////////////
//
// m/room.h
//

ircd::m::room
ircd::m::create(const id::room &room_id,
                const id::user &creator,
                const string_view &type)
{
	using prototype = room (const id::room &, const id::user &, const string_view &);

	static import<prototype> function
	{
		"client_createroom", "createroom__type"
	};

	return function(room_id, creator, type);
}

ircd::m::room
ircd::m::create(const id::room &room_id,
                const id::user &creator,
                const id::room &parent,
                const string_view &type)
{
	using prototype = room (const id::room &, const id::user &, const id::room &, const string_view &);

	static import<prototype> function
	{
		"client_createroom", "createroom__parent_type"
	};

	return function(room_id, creator, parent, type);
}

ircd::m::event::id::buf
ircd::m::join(const room &room,
              const id::user &user_id)
{
	using prototype = event::id::buf (const m::room &, const id::user &);

	static import<prototype> function
	{
		"client_rooms", "join__room_user"
	};

	return function(room, user_id);
}

ircd::m::event::id::buf
ircd::m::leave(const room &room,
               const id::user &user_id)
{
	using prototype = event::id::buf (const m::room &, const id::user &);

	static import<prototype> function
	{
		"client_rooms", "leave__room_user"
	};

	return function(room, user_id);
}

ircd::m::event::id::buf
ircd::m::redact(const room &room,
                const id::user &sender,
                const id::event &event_id,
                const string_view &reason)
{
	using prototype = event::id::buf (const m::room &, const id::user &, const id::event &, const string_view &);

	static import<prototype> function
	{
		"client_rooms", "redact__"
	};

	return function(room, sender, event_id, reason);
}

ircd::m::event::id::buf
ircd::m::notice(const room &room,
                const string_view &body)
{
	return message(room, me.user_id, body, "m.notice");
}

ircd::m::event::id::buf
ircd::m::notice(const room &room,
                const m::id::user &sender,
                const string_view &body)
{
	return message(room, sender, body, "m.notice");
}

ircd::m::event::id::buf
ircd::m::msghtml(const room &room,
                 const m::id::user &sender,
                 const string_view &html,
                 const string_view &alt,
                 const string_view &msgtype)
{
	return message(room, sender,
	{
		{ "msgtype",         msgtype                       },
		{ "format",          "org.matrix.custom.html"      },
		{ "body",            { alt?: html, json::STRING }  },
		{ "formatted_body",  { html, json::STRING }        },
	});
}

ircd::m::event::id::buf
ircd::m::message(const room &room,
                 const m::id::user &sender,
                 const string_view &body,
                 const string_view &msgtype)
{
	return message(room, sender,
	{
		{ "body",     { body,    json::STRING } },
		{ "msgtype",  { msgtype, json::STRING } },
	});
}

ircd::m::event::id::buf
ircd::m::message(const room &room,
                 const m::id::user &sender,
                 const json::members &contents)
{
	return send(room, sender, "m.room.message", contents);
}

ircd::m::event::id::buf
ircd::m::send(const room &room,
              const m::id::user &sender,
              const string_view &type,
              const string_view &state_key,
              const json::members &contents)
{
	json::iov _content;
	json::iov::push content[contents.size()];
	return send(room, sender, type, state_key, make_iov(_content, content, contents.size(), contents));
}

ircd::m::event::id::buf
ircd::m::send(const room &room,
              const m::id::user &sender,
              const string_view &type,
              const string_view &state_key,
              const json::object &contents)
{
	json::iov _content;
	json::iov::push content[contents.size()];
	return send(room, sender, type, state_key, make_iov(_content, content, contents.size(), contents));
}

ircd::m::event::id::buf
ircd::m::send(const room &room,
              const m::id::user &sender,
              const string_view &type,
              const string_view &state_key,
              const json::iov &content)
{
	using prototype = event::id::buf (const m::room &, const id::user &, const string_view &, const string_view &, const json::iov &);

	static import<prototype> function
	{
		"client_rooms", "state__iov"
	};

	return function(room, sender, type, state_key, content);
}

ircd::m::event::id::buf
ircd::m::send(const room &room,
              const m::id::user &sender,
              const string_view &type,
              const json::members &contents)
{
	json::iov _content;
	json::iov::push content[contents.size()];
	return send(room, sender, type, make_iov(_content, content, contents.size(), contents));
}

ircd::m::event::id::buf
ircd::m::send(const room &room,
              const m::id::user &sender,
              const string_view &type,
              const json::object &contents)
{
	json::iov _content;
	json::iov::push content[contents.count()];
	return send(room, sender, type, make_iov(_content, content, contents.count(), contents));
}

ircd::m::event::id::buf
ircd::m::send(const room &room,
              const m::id::user &sender,
              const string_view &type,
              const json::iov &content)
{
	using prototype = event::id::buf (const m::room &, const id::user &, const string_view &, const json::iov &);

	static import<prototype> function
	{
		"client_rooms", "send__iov"
	};

	return function(room, sender, type, content);
}

ircd::m::event::id::buf
ircd::m::commit(const room &room,
                json::iov &event,
                const json::iov &contents)
{
	using prototype = event::id::buf (const m::room &, json::iov &, const json::iov &);

	static import<prototype> function
	{
		"client_rooms", "commit__iov_iov"
	};

	return function(room, event, contents);
}

ircd::m::id::room::buf
ircd::m::room_id(const id::room_alias &room_alias)
{
	char buf[256];
	return room_id(buf, room_alias);
}

ircd::m::id::room
ircd::m::room_id(const mutable_buffer &out,
                 const id::room_alias &room_alias)
{
	using prototype = id::room (const mutable_buffer &, const id::room_alias &);

	static import<prototype> function
	{
		"client_directory_room", "room_id__room_alias"
	};

	return function(out, room_alias);
}

bool
ircd::m::exists(const id::room_alias &room_alias,
                const bool &remote_query)
{
	using prototype = bool (const id::room_alias, const bool &);

	static import<prototype> function
	{
		"client_directory_room", "room_alias_exists"
	};

	return function(room_alias, remote_query);
}

///////////////////////////////////////////////////////////////////////////////
//
// m/hook.h
//

/// Alternative hook ctor simply allowing the the function argument
/// first and description after.
ircd::m::hook::hook(decltype(function) function,
                    const json::members &members)
:hook
{
	members, std::move(function)
}
{
}

/// Primary hook ctor
ircd::m::hook::hook(const json::members &members,
                    decltype(function) function)
try
:_feature{[&members]() -> json::strung
{
	const ctx::critical_assertion ca;
	std::vector<json::member> copy
	{
		begin(members), end(members)
	};

	for(auto &member : copy) switch(hash(member.first))
	{
		case hash("room_id"):
		{
			// Rewrite the room_id if the supplied input has no hostname
			if(valid_local_only(id::ROOM, member.second))
			{
				assert(my_host());
				thread_local char buf[256];
				member.second = id::room { buf, member.second, my_host() };
			}

			validate(id::ROOM, member.second);
			continue;
		}

		case hash("sender"):
		{
			// Rewrite the sender if the supplied input has no hostname
			if(valid_local_only(id::USER, member.second))
			{
				assert(my_host());
				thread_local char buf[256];
				member.second = id::user { buf, member.second, my_host() };
			}

			validate(id::USER, member.second);
			continue;
		}

		case hash("state_key"):
		{
			const bool is_member_event
			{
				end(members) != std::find_if(begin(members), end(members), [](const auto &member)
				{
					return member.first == "type" && member.second == "m.room.member";
				})
			};

			// Rewrite the sender if the supplied input has no hostname
			if(valid_local_only(id::USER, member.second))
			{
				assert(my_host());
				thread_local char buf[256];
				member.second = id::user { buf, member.second, my_host() };
			}

			validate(id::USER, member.second);
			continue;
		}
	}

	return { copy.data(), copy.data() + copy.size() };
}()}
,feature
{
	_feature
}
,matching
{
	feature
}
,function
{
	std::move(function)
}
,registered
{
	list.add(*this)
}
{
}
catch(...)
{
	if(registered)
		list.del(*this);
}

ircd::m::hook::~hook()
noexcept
{
	if(registered)
		list.del(*this);
}

ircd::string_view
ircd::m::hook::site_name()
const try
{
	return unquote(feature.at("_site"));
}
catch(const std::out_of_range &e)
{
	throw assertive
	{
		"Hook %p must name a '_site' to register with.", this
	};
}

bool
ircd::m::hook::match(const m::event &event)
const
{
	if(json::get<"origin"_>(matching))
		if(at<"origin"_>(matching) != json::get<"origin"_>(event))
			return false;

	if(json::get<"room_id"_>(matching))
		if(at<"room_id"_>(matching) != json::get<"room_id"_>(event))
			return false;

	if(json::get<"sender"_>(matching))
		if(at<"sender"_>(matching) != json::get<"sender"_>(event))
			return false;

	if(json::get<"type"_>(matching))
		if(at<"type"_>(matching) != json::get<"type"_>(event))
			return false;

	if(json::get<"state_key"_>(matching))
		if(at<"state_key"_>(matching) != json::get<"state_key"_>(event))
			return false;

	if(json::get<"membership"_>(matching))
		if(at<"membership"_>(matching) != json::get<"membership"_>(event))
			return false;

	if(json::get<"content"_>(matching))
		if(json::get<"type"_>(event) == "m.room.message")
			if(at<"content"_>(matching).has("msgtype"))
				if(at<"content"_>(matching).get("msgtype") != json::get<"content"_>(event).get("msgtype"))
					return false;

	return true;
}


//
// hook::site
//

ircd::m::hook::site::site(const json::members &members)
try
:_feature
{
	members
}
,feature
{
	_feature
}
,registered
{
	list.add(*this)
}
{
}
catch(...)
{
	if(registered)
		list.del(*this);
}

ircd::m::hook::site::~site()
noexcept
{
	if(registered)
		list.del(*this);
}

void
ircd::m::hook::site::operator()(const event &event)
{
	std::set<hook *> matching;     //TODO: allocator
	const auto site_match{[&matching]
	(auto &map, const string_view &key)
	{
		auto pit{map.equal_range(key)};
		for(; pit.first != pit.second; ++pit.first)
			matching.emplace(pit.first->second);
	}};

	if(json::get<"origin"_>(event))
		site_match(origin, at<"origin"_>(event));

	if(json::get<"room_id"_>(event))
		site_match(room_id, at<"room_id"_>(event));

	if(json::get<"sender"_>(event))
		site_match(sender, at<"sender"_>(event));

	if(json::get<"type"_>(event))
		site_match(type, at<"type"_>(event));

	if(json::get<"state_key"_>(event))
		site_match(state_key, at<"state_key"_>(event));

	auto it(begin(matching));
	while(it != end(matching))
	{
		const hook &hook(**it);
		if(!hook.match(event))
			it = matching.erase(it);
		else
			++it;
	}

	for(const auto &hook : matching)
		hook->function(event);
}

bool
ircd::m::hook::site::add(hook &hook)
{
	if(json::get<"origin"_>(hook.matching))
		origin.emplace(at<"origin"_>(hook.matching), &hook);

	if(json::get<"room_id"_>(hook.matching))
		room_id.emplace(at<"room_id"_>(hook.matching), &hook);

	if(json::get<"sender"_>(hook.matching))
		sender.emplace(at<"sender"_>(hook.matching), &hook);

	if(json::get<"state_key"_>(hook.matching))
		state_key.emplace(at<"state_key"_>(hook.matching), &hook);

	if(json::get<"type"_>(hook.matching))
		type.emplace(at<"type"_>(hook.matching), &hook);

	++count;
	return true;
}

bool
ircd::m::hook::site::del(hook &hook)
{
	const auto unmap{[&hook]
	(auto &map, const string_view &key)
	{
		auto pit{map.equal_range(key)};
		for(; pit.first != pit.second; ++pit.first)
			if(pit.first->second == &hook)
				return map.erase(pit.first);

		assert(0);
	}};

	if(json::get<"origin"_>(hook.matching))
		unmap(origin, at<"origin"_>(hook.matching));

	if(json::get<"room_id"_>(hook.matching))
		unmap(room_id, at<"room_id"_>(hook.matching));

	if(json::get<"sender"_>(hook.matching))
		unmap(sender, at<"sender"_>(hook.matching));

	if(json::get<"state_key"_>(hook.matching))
		unmap(state_key, at<"state_key"_>(hook.matching));

	if(json::get<"type"_>(hook.matching))
		unmap(type, at<"type"_>(hook.matching));

	--count;
	return true;
}

ircd::string_view
ircd::m::hook::site::name()
const try
{
	return unquote(feature.at("name"));
}
catch(const std::out_of_range &e)
{
	throw assertive
	{
		"Hook site %p requires a name", this
	};
}

//
// hook::list
//

decltype(ircd::m::hook::list)
ircd::m::hook::list
{};

bool
ircd::m::hook::list::del(hook &hook)
try
{
	const auto site(at(hook.site_name()));
	assert(site != nullptr);
	return site->del(hook);
}
catch(const std::out_of_range &e)
{
	log::critical
	{
		"Tried to unregister hook(%p) from missing hook::site '%s'",
		&hook,
		hook.site_name()
	};

	assert(0);
	return false;
}

bool
ircd::m::hook::list::add(hook &hook)
try
{
	const auto site(at(hook.site_name()));
	assert(site != nullptr);
	return site->add(hook);
}
catch(const std::out_of_range &e)
{
	throw error
	{
		"No hook::site named '%s' is registered...", hook.site_name()
	};
}

bool
ircd::m::hook::list::del(site &site)
{
	return erase(site.name());
}

bool
ircd::m::hook::list::add(site &site)
{
	const auto iit
	{
		emplace(site.name(), &site)
	};

	if(unlikely(!iit.second))
		throw error
		{
			"Hook site name '%s' already in use", site.name()
		};

	return true;
}

///////////////////////////////////////////////////////////////////////////////
//
// m/error.h
//

thread_local char
ircd::m::error::fmtbuf[768]
{};

ircd::m::error::error()
:http::error{http::INTERNAL_SERVER_ERROR}
{}

ircd::m::error::error(std::string c)
:http::error{http::INTERNAL_SERVER_ERROR, std::move(c)}
{}

ircd::m::error::error(const http::code &c)
:http::error{c, std::string{}}
{}

ircd::m::error::error(const http::code &c,
                      const json::members &members)
:http::error{c, json::strung{members}}
{}

ircd::m::error::error(const http::code &c,
                      const json::iov &iov)
:http::error{c, json::strung{iov}}
{}

ircd::m::error::error(const http::code &c,
                      const json::object &object)
:http::error{c, std::string{object}}
{}
