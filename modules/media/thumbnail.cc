// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#include <boost/gil/image.hpp>
#include <boost/gil/typedefs.hpp>
#include <boost/gil/extension/io/jpeg_io.hpp>
// #include <boost/gil/extension/numeric/sampler.hpp>
// #include <boost/gil/extension/numeric/resample.hpp>

#include "media.h"

mapi::header
IRCD_MODULE
{
	"Client-Sever 11.7.1.4 :Media thumbnails"
};

resource
thumbnail_resource__legacy
{
	"/_matrix/media/v1/thumbnail/",
	{
		"Media thumbnails (legacy version)",
		resource::DIRECTORY,
	}
};

resource
thumbnail_resource
{
	"/_matrix/media/r0/thumbnail/",
	{
		"Media thumbnails",
		resource::DIRECTORY,
	}
};

static resource::response
get__thumbnail_remote(client &client,
                      const resource::request &request,
                      const string_view &server,
                      const string_view &file,
                      const m::room &room);

static resource::response
get__thumbnail_local(client &client,
                     const resource::request &request,
                     const string_view &server,
                     const string_view &file,
                     const m::room &room);

resource::response
get__thumbnail(client &client,
               const resource::request &request)
{
	if(request.parv.size() < 1)
		throw http::error
		{
			http::MULTIPLE_CHOICES, "Server name parameter required"
		};

	if(request.parv.size() < 2)
		throw http::error
		{
			http::MULTIPLE_CHOICES, "Media ID parameter required"
		};

	const auto &server
	{
		request.parv[0]
	};

	const auto &file
	{
		request.parv[1]
	};

	const m::room::id::buf room_id
	{
		file_room_id(server, file)
	};

	const m::room room
	{
		room_id
	};

	if(m::exists(room))
		return get__thumbnail_local(client, request, server, file, room);

	if(!my_host(server))
		return get__thumbnail_remote(client, request, server, file, room);

	throw m::NOT_FOUND
	{
		"Media not found"
	};
}

resource::method
method_get__legacy
{
	thumbnail_resource__legacy, "GET", get__thumbnail
};

resource::method
method_get
{
	thumbnail_resource, "GET", get__thumbnail
};

static resource::response
get__thumbnail_remote(client &client,
                      const resource::request &request,
                      const string_view &hostname,
                      const string_view &mediaid,
                      const m::room &room)
{
	const net::hostport remote
	{
		hostname
	};

	char buf[6_KiB];
	window_buffer wb{buf};
	thread_local char uri[2_KiB];
	http::request
	{
		wb, hostname, "GET", fmt::sprintf
		{
			uri, "/_matrix/media/r0/download/%s/%s", hostname, mediaid
		}
	};

	const const_buffer out_head
	{
		wb.completed()
	};

	// Remaining space in buffer is used for received head
	const mutable_buffer in_head
	{
		buf + size(out_head), sizeof(buf) - size(out_head)
	};

	//TODO: --- This should use the progress callback to build blocks

	// Null content buffer will cause dynamic allocation internally.
	const mutable_buffer in_content{};

	struct server::request::opts opts;
	server::request remote_request
	{
		remote, { out_head }, { in_head, in_content }, &opts
	};

	if(remote_request.wait(seconds(10)) == ctx::future_status::timeout)
		throw http::error
		{
			http::REQUEST_TIMEOUT
		};

	//TODO: ---

	const auto &code
	{
		remote_request.get()
	};

	char mime_type_buf[64];
	const string_view content_type
	{
		magic::mime(mime_type_buf, remote_request.in.content)
	};

	const size_t file_size
	{
		size(remote_request.in.content)
	};

	parse::buffer pb{remote_request.in.head};
	parse::capstan pc{pb};
	pc.read += size(remote_request.in.head);
	const http::response::head head{pc};
	if(content_type != head.content_type)
		log::warning
		{
			"Server %s claims thumbnail %s is '%s' but we think it is '%s'",
			hostname,
			mediaid,
			head.content_type,
			content_type
		};

	// Send it off to user first
	resource::response
	{
		client, remote_request.in.content, content_type
	};

	//TODO: TXN
	create(room, m::me.user_id, "file");

	//TODO: TXN
	send(room, m::me.user_id, "ircd.file.stat", "size",
	{
		{ "value", long(file_size) }
	});

	//TODO: TXN
	send(room, m::me.user_id, "ircd.file.stat", "type",
	{
		{ "value", content_type }
	});

	const auto lpath{fs::make_path({fs::DPATH, "media"})};
	thread_local char pathbuf[1024];
	size_t pathlen{0};
	pathlen = strlcpy(pathbuf, lpath);
	pathlen = strlcat(pathbuf, "/"_sv); //TODO: fs utils
	const mutable_buffer pathpart
	{
		pathbuf + pathlen, sizeof(pathbuf) - pathlen
	};

	size_t off{0}, wrote{0};
	while(off < file_size)
	{
		const size_t blksz
		{
			std::min(file_size - off, size_t(32_KiB))
		};

		const const_buffer &block
		{
			data(remote_request.in.content) + off, blksz
		};

		const sha256::buf hash_
		{
			sha256{block}
		};

		char b58buf[hash_.size() * 2];
		const string_view hash
		{
			b58encode(b58buf, hash_)
		};

		send(room, m::me.user_id, "ircd.file.block",
		{
			{ "size",  long(blksz) },
			{ "hash",  hash        }
		});

		const string_view path
		{
			pathbuf, pathlen + copy(pathpart, hash)
		};

		if(!fs::exists(path))
			wrote += size(fs::write(path, block));

		off += blksz;
	}

	assert(off == file_size);
	assert(wrote <= off);
	return {};
}

static resource::response
get__thumbnail_local(client &client,
                     const resource::request &request,
                     const string_view &hostname,
                     const string_view &mediaid,
                     const m::room &room)
{
	// Get the file's total size
	size_t file_size{0};
	room.get("ircd.file.stat", "size", [&file_size]
	(const m::event &event)
	{
		file_size = at<"content"_>(event).get<size_t>("value");
	});

	// Get the MIME type
	char type_buf[64];
	string_view content_type;
	room.get("ircd.file.stat", "type", [&type_buf, &content_type]
	(const m::event &event)
	{
		const auto &value
		{
			unquote(at<"content"_>(event).at("value"))
		};

		content_type =
		{
			type_buf, copy(type_buf, value)
		};
	});

	// Send HTTP head to client
	resource::response
	{
		client, http::OK, content_type, file_size
	};

	const auto lpath{fs::make_path({fs::DPATH, "media"})};
	thread_local char pathbuf[1024];
	size_t pathlen{0};
	pathlen = strlcpy(pathbuf, lpath);
	pathlen = strlcat(pathbuf, "/"_sv); //TODO: fs utils
	const mutable_buffer pathpart
	{
		pathbuf + pathlen, sizeof(pathbuf) - pathlen
	};

	// Block buffer
	const unique_buffer<mutable_buffer> buf
	{
		64_KiB
	};

	// Spool content to client
	size_t sent{0}, read{0};
	m::room::messages it{room, 1};
	for(; bool(it); ++it)
	{
		const m::event &event{*it};
		if(at<"type"_>(event) != "ircd.file.block")
			continue;

		const auto &hash
		{
			unquote(at<"content"_>(event).at("hash"))
		};

		const auto &blksz
		{
			at<"content"_>(event).get<size_t>("size")
		};

		const string_view path
		{
			pathbuf, pathlen + copy(pathpart, hash)
		};

		const string_view &block
		{
			fs::read(path, buf)
		};

		assert(size(block) == blksz);
		read += size(block);
		sent += write_all(*client.sock, block);
	}

	assert(read == file_size);
	assert(sent == read);
	return {};
}
