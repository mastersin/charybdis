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
#define HAVE_AIO_H

#include <linux/aio_abi.h>
#include <ircd/asio.h>

/// AIO context instance. Right now this is a singleton with an extern
/// instance at fs::aioctx.
struct ircd::fs::aio
{
	struct request;

	/// Maximum number of events we can submit to kernel
	static constexpr const size_t &MAX_EVENTS {512};

	/// Internal semaphore for synchronization of this object
	ctx::dock dock;

	/// The semaphore value for the eventfd which we keep here.
	uint64_t semval {0};

	/// An eventfd which will be notified by the kernel; we integrate this with
	/// the ircd io_service core epoll() event loop. The EFD_SEMAPHORE flag is
	/// not used to reduce the number of triggers. We can collect multiple AIO
	/// completions after a single trigger to this fd. Because EFD_SEMAPHORE is
	/// not set, the semval which is kept above will reflect a hint for how
	/// many AIO's are done.
	asio::posix::stream_descriptor resfd;

	/// Handler to the io context we submit requests to the kernel with
	aio_context_t idp {0};

	// Callback stack invoked when the sigfd is notified of completed events.
	void handle_event(const io_event &) noexcept;
	void handle_events() noexcept;
	void handle(const boost::system::error_code &, const size_t) noexcept;
	void set_handle();

	bool wait();
	bool interrupt();

	aio();
	~aio() noexcept;
};

/// Generic request control block.
struct ircd::fs::aio::request
:iocb
{
	struct read;
	struct write;

	ssize_t retval {std::numeric_limits<ssize_t>::min()};
	ssize_t errcode {0};
	ctx::ctx *waiter {ctx::current};

  public:
	size_t operator()();
	void cancel();

	request(const int &fd);
	~request() noexcept;
};

namespace ircd::fs
{
	const_buffer write__aio(const fd &, const const_buffer &, const write_opts &);
	const_buffer read__aio(const fd &, const mutable_buffer &, const read_opts &);
}

/// Read request control block
struct ircd::fs::aio::request::read
:request
{
	read(const int &fd, const mutable_buffer &, const read_opts &);
};

/// Write request control block
struct ircd::fs::aio::request::write
:request
{
	write(const int &fd, const const_buffer &, const write_opts &);
};
