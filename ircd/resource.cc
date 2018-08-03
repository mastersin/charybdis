// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

decltype(ircd::resource::resources)
ircd::resource::resources
{};

ircd::resource &
ircd::resource::find(const string_view &path_)
{
	const string_view path
	{
		rstrip(path_, '/')
	};

	auto it
	{
		resources.lower_bound(path)
	};

	if(it == end(resources)) try
	{
		--it;
		if(it == begin(resources) || !startswith(path, it->first))
			return *resources.at("/");
	}
	catch(const std::out_of_range &e)
	{
		throw http::error
		{
			http::code::NOT_FOUND
		};
	}

	auto rpath{it->first};
	//assert(!endswith(rpath, '/'));

	// Exact file or directory match
	if(path == rpath)
		return *it->second;

	// Directories handle all paths under them.
	if(!startswith(path, rpath))
	{
		// Walk the iterator back to find if there is a directory prefixing this path.
		if(it == begin(resources))
			throw http::error
			{
				http::code::NOT_FOUND
			};

		--it;
		rpath = it->first;
		if(!startswith(path, rpath))
			throw http::error
			{
				http::code::NOT_FOUND
			};
	}

	// Check if the resource is a directory; if not, it can only
	// handle exact path matches.
	if(~it->second->flags & it->second->DIRECTORY && path != rpath)
		throw http::error
		{
			http::code::NOT_FOUND
		};

	if(it->second->flags & it->second->DIRECTORY)
	{
		const auto rem(lstrip(path, rpath));
		if(!empty(rem) && !startswith(rem, '/'))
			throw http::error
			{
				http::code::NOT_FOUND
			};
	}

	return *it->second;
}

//
// resource
//

ircd::resource::resource(const string_view &path)
:resource
{
	path, opts{}
}
{
}

ircd::resource::resource(const string_view &path,
                         const opts &opts)
:path
{
	rstrip(path, '/')
}
,description{opts.description}
,flags{opts.flags}
,resources_it{[this]
{
	const auto iit
	{
		resources.emplace(this->path, this)
	};

	if(!iit.second)
		throw error
		{
			"resource \"%s\" already registered", this->path
		};

	return unique_const_iterator<decltype(resources)>
	{
		resources, iit.first
	};
}()}
{
	log::debug
	{
		"Registered resource \"%s\"", path.empty()? string_view{"/"} : this->path
	};
}

ircd::resource::~resource()
noexcept
{
	log::debug
	{
		"Unregistered resource \"%s\"", path.empty()? string_view{"/"} : path
	};
}

void
ircd::resource::operator()(client &client,
                           const http::request::head &head,
                           const string_view &content_partial)
{
	// Find the method or METHOD_NOT_ALLOWED
	auto &method
	{
		operator[](head.method)
	};

	// Bail out if the method limited the amount of content and it was exceeded.
	if(head.content_length > method.opts.payload_max)
		throw http::error
		{
			http::PAYLOAD_TOO_LARGE
		};

	// Check if the resource method wants a specific MIME type. If no option
	// is given by the resource then any Content-Type by the client will pass.
	if(method.opts.mime.first)
	{
		const auto &ct(split(head.content_type, ';'));
		const auto &supplied(split(ct.first, '/'));
		const auto &charset(ct.second);
		const auto &required(method.opts.mime);
		if(required.first != supplied.first ||
		   (required.second && required.second != supplied.second))
			throw http::error
			{
				http::UNSUPPORTED_MEDIA_TYPE
			};
	}

	// This timer will keep the request from hanging forever for whatever
	// reason. The resource method may want to do its own timing and can
	// disable this in its options structure.
	const net::scope_timeout timeout
	{
		*client.sock, method.opts.timeout, [&client, &head]
		(const bool &timed_out)
		{
			if(!timed_out)
				return;

			log::derror
			{
				"socket(%p) local[%s] remote[%s] Timed out in %s `%s'",
				client.sock.get(),
				string(local(client)),
				string(remote(client)),
				head.method,
				head.path
			};

			// The interrupt is effective when the socket has already been
			// closed and/or the client is still stuck in a request for
			// some reason.
			if(client.reqctx)
				ctx::interrupt(*client.reqctx);

			//TODO: If we know that no response has been sent yet
			//TODO: we can respond with http::REQUEST_TIMEOUT instead.
			client.close(net::dc::RST, net::close_ignore);
		}
	};

	// Content that hasn't yet arrived is remaining
	const size_t content_remain
	{
		head.content_length - client.content_consumed
	};

	// View of the content that will be passed to the resource handler. Starts
	// with the content received so far which is actually in the head's buffer.
	// One of three things can happen now:
	//
	// - There is no more content so we pass this as-is right to the resource.
	// - There is more content, so we allocate a content buffer, copy what we
	// have to it, read the rest off the socket, and then reassign this view.
	// - There is more content, but the resource wants to read it off the
	// socket on its own terms, so we pass this as-is.
	string_view content
	{
		content_partial
	};

	if(content_remain && ~method.opts.flags & method.CONTENT_DISCRETION)
	{
		// Copy any partial content to the final contiguous allocated buffer;
		client.content_buffer = unique_buffer<mutable_buffer>{head.content_length};
		memcpy(data(client.content_buffer), data(content_partial), size(content_partial));

		// Setup a window inside the buffer for the remaining socket read.
		const mutable_buffer content_remain_buffer
		{
			data(client.content_buffer) + size(content_partial), content_remain
		};

		// Read the remaining content off the socket.
		client.content_consumed += read_all(*client.sock, content_remain_buffer);
		assert(client.content_consumed == head.content_length);
		content = string_view
		{
			data(client.content_buffer), head.content_length
		};
	}

	client.request = resource::request
	{
		head, content
	};

	// We take the extra step here to clear the assignment to client.request
	// when this request stack has finished for two reasons:
	// - It allows other ctxs to peep at the client::list to see what this
	//   client/ctx/request is currently working on with some more safety.
	// - It prevents an easy source for stale refs wrt the longpoll thing.
	const unwind clear_request{[&client]
	{
		client.request = {};
	}};

	const auto pathparm
	{
		lstrip(head.path, this->path)
	};

	client.request.parv =
	{
		client.request.param, tokens(pathparm, '/', client.request.param)
	};

	// Finally handle the request.
	handle_request(client, method, client.request);
}

void
ircd::resource::handle_request(client &client,
                               method &method,
                               resource::request &request)
try
{
	method(client, request);
}
catch(const json::not_found &e)
{
	throw http::error
	{
		http::NOT_FOUND, "Required JSON field: %s", e.what()
	};
}
catch(const json::print_error &e)
{
	throw http::error
	{
		http::INTERNAL_SERVER_ERROR, "Generator Protection: %s", e.what()
	};
}
catch(const json::error &e)
{
	throw http::error
	{
		http::BAD_REQUEST, "%s", e.what()
	};
}
catch(const std::out_of_range &e)
{
	throw http::error
	{
		http::NOT_FOUND, "%s", e.what()
	};
}
catch(const ctx::timeout &e)
{
	throw http::error
	{
		http::BAD_GATEWAY, "%s", e.what()
	};
}

ircd::resource::method &
ircd::resource::operator[](const string_view &name)
try
{
	return *methods.at(name);
}
catch(const std::out_of_range &e)
{
	char buf[128];
	const http::header headers[]
	{
		{ "Allow", allow_methods_list(buf) }
	};

	throw http::error
	{
		http::METHOD_NOT_ALLOWED, {}, headers
	};
}

ircd::string_view
ircd::resource::allow_methods_list(const mutable_buffer &buf)
{
	size_t len(0);
	if(likely(size(buf)))
		buf[len] = '\0';

	auto it(begin(methods));
	if(it != end(methods))
	{
		len = strlcat(buf, it->first);
		for(++it; it != end(methods); ++it)
		{
			len = strlcat(buf, " ");
			len = strlcat(buf, it->first);
		}
	}

	return { data(buf), len };
}

ircd::resource::method::method(struct resource &resource,
                               const string_view &name,
                               const handler &handler)
:method
{
	resource, name, handler, {}
}
{
}

ircd::resource::method::method(struct resource &resource,
                               const string_view &name,
                               const handler &handler,
                               const struct opts &opts)
:name{name}
,resource{&resource}
,function{handler}
,opts{opts}
,methods_it{[this, &name]
{
	const auto iit
	{
		this->resource->methods.emplace(this->name, this)
	};

	if(!iit.second)
		throw error
		{
			"resource \"%s\" already registered", name
		};

	return unique_const_iterator<decltype(resource::methods)>
	{
		this->resource->methods,
		iit.first
	};
}()}
{
}

ircd::resource::method::~method()
noexcept
{
}

ircd::resource::response
ircd::resource::method::operator()(client &client,
                                   request &request)
try
{
	return function(client, request);
}
catch(const std::bad_function_call &e)
{
	throw http::error
	{
		http::SERVICE_UNAVAILABLE
	};
}

//
// resource::response::chunked
//

ircd::resource::response::chunked::chunked(chunked &&other)
noexcept
:c{std::move(other.c)}
{
	other.c = nullptr;
}

ircd::resource::response::chunked::chunked(client &client,
                                           const http::code &code)
:chunked
{
	client, code, "application/json; charset=utf-8"_sv, string_view{}
}
{
}

ircd::resource::response::chunked::chunked(client &client,
                                           const http::code &code,
                                           const vector_view<const http::header> &headers)
:chunked
{
	client, code, "application/json; charset=utf-8"_sv, headers
}
{
}

ircd::resource::response::chunked::chunked(client &client,
                                           const http::code &code,
                                           const string_view &content_type,
                                           const vector_view<const http::header> &headers)
:c{&client}
{
	assert(!empty(content_type));

	thread_local char buffer[4_KiB];
	window_buffer sb{buffer};
	{
		const critical_assertion ca;
		http::write(sb, headers);
	}

	response
	{
		client, code, content_type, size_t(-1), string_view{sb.completed()}
	};
}

ircd::resource::response::chunked::chunked(client &client,
                                           const http::code &code,
                                           const string_view &content_type,
                                           const string_view &headers)
:c{&client}
{
	response
	{
		client, code, content_type, size_t(-1), headers
	};
}

ircd::resource::response::chunked::~chunked()
noexcept try
{
	if(!c)
		return;

	if(!std::uncaught_exceptions())
		finish();
	else
		c->close(net::dc::RST, net::close_ignore);
}
catch(...)
{
	return;
}

bool
ircd::resource::response::chunked::finish()
{
	if(!c)
		return false;

	write(const_buffer{});
	c = nullptr;
	return true;
}

size_t
ircd::resource::response::chunked::write(const const_buffer &chunk)
try
{
	size_t ret{0};

	if(!c)
		return ret;

	//TODO: bring iov from net::socket -> net::write_() -> client::write_()
	char headbuf[32];
	ret += c->write_all(http::writechunk(headbuf, size(chunk)));
	ret += size(chunk)? c->write_all(chunk) : 0UL;
	ret += c->write_all("\r\n"_sv);
	return ret;
}
catch(...)
{
	this->c = nullptr;
	throw;
}

//
// resource::response
//

ircd::resource::response::response(client &client,
                                   const http::code &code)
:response{client, json::object{json::empty_object}, code}
{
}

ircd::resource::response::response(client &client,
                                   const http::code &code,
                                   const json::iov &members)
:response{client, members, code}
{
}

ircd::resource::response::response(client &client,
                                   const json::members &members,
                                   const http::code &code)
:response{client, code, members}
{
}

ircd::resource::response::response(client &client,
                                   const json::value &value,
                                   const http::code &code)
:response{client, code, value}
{
}

ircd::resource::response::response(client &client,
                                   const http::code &code,
                                   const json::value &value)
try
{
	const auto size
	{
		serialized(value)
	};

	const unique_buffer<mutable_buffer> buffer
	{
		size
	};

	switch(type(value))
	{
		case json::ARRAY:
		{
			response(client, json::array{stringify(mutable_buffer{buffer}, value)}, code);
			return;
		}

		case json::OBJECT:
		{
			response(client, json::object{stringify(mutable_buffer{buffer}, value)}, code);
			return;
		}

		default: throw http::error
		{
			http::INTERNAL_SERVER_ERROR, "Cannot send json::%s as response content", std::string{type(value)}
		};
	}
}
catch(const json::error &e)
{
	throw http::error
	{
		http::INTERNAL_SERVER_ERROR, "Generator Protection: %s", e.what()
	};
}

ircd::resource::response::response(client &client,
                                   const http::code &code,
                                   const json::members &members)
try
{
	const auto size
	{
		serialized(members)
	};

	const unique_buffer<mutable_buffer> buffer
	{
		size
	};

	const json::object object
	{
		stringify(mutable_buffer{buffer}, members)
	};

	response(client, object, code);
}
catch(const json::error &e)
{
	throw http::error
	{
		http::INTERNAL_SERVER_ERROR, "Generator Protection: %s", e.what()
	};
}

ircd::resource::response::response(client &client,
                                   const json::iov &members,
                                   const http::code &code)
try
{
	const auto size
	{
		serialized(members)
	};

	const unique_buffer<mutable_buffer> buffer
	{
		size
	};

	const json::object object
	{
		stringify(mutable_buffer{buffer}, members)
	};

	response(client, object, code);
}
catch(const json::error &e)
{
	throw http::error
	{
		http::INTERNAL_SERVER_ERROR, "Generator Protection: %s", e.what()
	};
}

ircd::resource::response::response(client &client,
                                   const json::object &object,
                                   const http::code &code)
{
	static const string_view content_type
	{
		"application/json; charset=utf-8"
	};

	assert(json::valid(object, std::nothrow));
	response(client, object, content_type, code);
}

ircd::resource::response::response(client &client,
                                   const json::array &array,
                                   const http::code &code)
{
	static const string_view content_type
	{
		"application/json; charset=utf-8"
	};

	assert(json::valid(array, std::nothrow));
	response(client, array, content_type, code);
}

ircd::resource::response::response(client &client,
                                   const string_view &content,
                                   const string_view &content_type,
                                   const http::code &code,
                                   const vector_view<const http::header> &headers)
{
	assert(empty(content) || !empty(content_type));

	// contents of this buffer get copied again when further passed to
	// response{}; we can get this off the stack if that remains true.
	thread_local char buffer[4_KiB];
	window_buffer sb{buffer};
	{
		const critical_assertion ca;
		http::write(sb, headers);
	}

	response
	{
		client, content, content_type, code, string_view{sb.completed()}
	};
}

ircd::resource::response::response(client &client,
                                   const string_view &content,
                                   const string_view &content_type,
                                   const http::code &code,
                                   const string_view &headers)
{
	assert(empty(content) || !empty(content_type));

	// Head gets sent
	response
	{
		client, code, content_type, size(content), headers
	};

	// All content gets sent
	const size_t written
	{
		client.write_all(content)
	};

	assert(written == size(content));
}

ircd::resource::response::response(client &client,
                                   const http::code &code,
                                   const string_view &content_type,
                                   const size_t &content_length,
                                   const string_view &headers)
{
	assert(!content_length || !empty(content_type));

	const auto request_time
	{
		client.timer.at<microseconds>().count()
	};

	const fmt::bsprintf<64> rtime
	{
		"%zd$us", request_time
	};

	// This buffer will be passed to the socket and sent out;
	// cannot be static/tls.
	char head_buf[4_KiB];
	window_buffer head{head_buf};
	http::response
	{
		head,
		code,
		content_length,
		content_type,
		headers,
		{
			{ "Access-Control-Allow-Origin",   "*"                  }, //TODO: XXX
			{ "X-IRCd-Request-Timer",          rtime,               },
		},
	};

	// Maximum size is is realistically ok but ideally a small
	// maximum; this exception should hit the developer in testing.
	if(unlikely(!head.remaining()))
		throw assertive
		{
			"HTTP headers too large for buffer of %zu", sizeof(head_buf)
		};

	const size_t written
	{
		client.write_all(head.completed())
	};

	#ifdef RB_DEBUG
	const log::facility facility
	{
		ushort(code) >= 200 && ushort(code) < 300?
			log::facility::DEBUG:
		ushort(code) >= 300 && ushort(code) < 400?
			log::facility::DWARNING:
		ushort(code) >= 400 && ushort(code) < 500?
			log::facility::DERROR:
			log::facility::ERROR
	};

	log::logf
	{
		log::general, facility,
		"socket(%p) local[%s] remote[%s] HTTP %d %s in %ld$us; %s %zd content",
		client.sock.get(),
		string(local(client)),
		string(remote(client)),
		uint(code),
		http::status(code),
		request_time,
		content_type,
		ssize_t(content_length),
	};
	#endif

	assert(written == size(head.completed()));
}
