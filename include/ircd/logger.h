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
#define HAVE_IRCD_LOGGER_H

// windows.h #define conflicts with our facility
#ifdef HAVE_WINDOWS_H
#undef ERROR
#endif

namespace ircd
{
	const char *smalldate(const time_t &);
}

/// Logging system
namespace ircd::log
{
	enum facility :int;
	const char *reflect(const facility &);

	struct log;
	struct vlog;
	struct logf;
	struct mark;
	struct console_quiet;

	struct critical;
	struct error;
	struct derror;
	struct warning;
	struct dwarning;
	struct notice;
	struct info;
	struct debug;

	extern log star;     // "*", '*'
	extern log general;  // "ircd", 'G'

	// The mask is the list of named loggers to allow; an empty mask disallows
	// all loggers. An empty unmask allows all loggers. An unmask of a logger
	// that wasn't masked has no effect. Provided string_views don't have to
	// remain valid after call.
	void console_unmask(const vector_view<string_view> & = {});
	void console_mask(const vector_view<string_view> & = {});

	// This suite adjusts the output for an entire level.
	bool console_enabled(const facility &);
	void console_disable(const facility &);
	void console_enable(const facility &);

	void flush();
	void close();
	void open();

	void init();
	void fini();
}

enum ircd::log::facility
:int
{
	CRITICAL  = 0,  ///< Catastrophic/unrecoverable; program is in a compromised state.
	ERROR     = 1,  ///< Things that shouldn't happen; user impacted and should know.
	WARNING   = 2,  ///< Non-impacting undesirable behavior user should know about.
	NOTICE    = 3,  ///< An infrequent important message with neutral or positive news.
	INFO      = 4,  ///< A more frequent message with good news.
	DERROR    = 5,  ///< An error but only worthy of developers in debug mode.
	DWARNING  = 6,  ///< A warning but only for developers in debug mode.
	DEBUG     = 7,  ///< Maximum verbosity for developers.
	_NUM_
};

/// A named logger. Create an instance of this to help categorize log messages.
/// All messages sent to this logger will be prefixed with the given name.
/// Admins will use this to create masks to filter log messages. Instances
/// of this class are registered with instance_list for de-confliction and
/// iteration, so the recommended duration of this class is static.
struct ircd::log::log
:instance_list<log>
{
	string_view name;                  // name of this logger
	char snote;                        // snomask character
	bool cmasked {true};               // currently in the console mask (enabled)
	bool fmasked {true};               // currently in the file mask (enabled)

  public:
	template<class... args> void operator()(const facility &, const char *const &fmt, args&&...);

	#if RB_LOG_LEVEL >= 0
	template<class... args> void critical(const char *const &fmt, args&&...);
	#else // Required for DCE in gcc 6.3.0 20170519
	void critical(const char *const &fmt, ...);
	#endif

	#if RB_LOG_LEVEL >= 1
	template<class... args> void error(const char *const &fmt, args&&...);
	#else // Required for DCE in gcc 6.3.0 20170519
	void error(const char *const &fmt, ...);
	#endif

	#if RB_LOG_LEVEL >= 2
	template<class... args> void warning(const char *const &fmt, args&&...);
	#else // Required for DCE in gcc 6.3.0 20170519
	void warning(const char *const &fmt, ...);
	#endif

	#if RB_LOG_LEVEL >= 3
	template<class... args> void notice(const char *const &fmt, args&&...);
	#else // Required for DCE in gcc 6.3.0 20170519
	void notice(const char *const &fmt, ...);
	#endif

	#if RB_LOG_LEVEL >= 4
	template<class... args> void info(const char *const &fmt, args&&...);
	#else // Required for DCE in gcc 6.3.0 20170519
	void info(const char *const &fmt, ...);
	#endif

	#if RB_LOG_LEVEL >= 5
	template<class... args> void derror(const char *const &fmt, args&&...);
	#else // Required for DCE in gcc 6.3.0 20170519
	void derror(const char *const &fmt, ...);
	#endif

	#if RB_LOG_LEVEL >= 6
	template<class... args> void dwarning(const char *const &fmt, args&&...);
	#else // Required for DCE in gcc 6.3.0 20170519
	void dwarning(const char *const &fmt, ...);
	#endif

	#if RB_LOG_LEVEL >= 7
	template<class... args> void debug(const char *const &fmt, args&&...);
	#else // Required for DCE in gcc 6.3.0 20170519
	void debug(const char *const &fmt, ...);
	#endif

	log(const string_view &name, const char &snote = '\0');

	static bool exists(const log *const &ptr);
	static log *find(const string_view &name);
	static log *find(const char &snote);
};

struct ircd::log::vlog
{
	vlog(const log &log, const facility &, const char *const &fmt, const va_rtti &ap);
};

struct ircd::log::logf
{
	template<class... args>
	logf(const log &log, const facility &facility, const char *const &fmt, args&&... a)
	{
		vlog(log, facility, fmt, va_rtti{std::forward<args>(a)...});
	}
};

struct ircd::log::mark
{
	mark(const facility &, const string_view &msg = {});
	mark(const string_view &msg = {});
};

struct ircd::log::console_quiet
{
	console_quiet(const bool &showmsg = true);
	~console_quiet();
};

#if RB_LOG_LEVEL >= 7
struct ircd::log::debug
{
	template<class... args>
	debug(const log &log, const char *const &fmt, args&&... a)
	{
		vlog(log, facility::DEBUG, fmt, va_rtti{std::forward<args>(a)...});
	}

	template<class... args>
	debug(const char *const &fmt, args&&... a)
	{
		vlog(general, facility::DEBUG, fmt, va_rtti{std::forward<args>(a)...});
	}
};
#else
struct ircd::log::debug
{
	debug(const log &, const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}

	debug(const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}
};
#endif

#if RB_LOG_LEVEL >= 6
struct ircd::log::dwarning
{
	template<class... args>
	dwarning(const log &log, const char *const &fmt, args&&... a)
	{
		vlog(log, facility::DWARNING, fmt, va_rtti{std::forward<args>(a)...});
	}

	template<class... args>
	dwarning(const char *const &fmt, args&&... a)
	{
		vlog(general, facility::DWARNING, fmt, va_rtti{std::forward<args>(a)...});
	}
};
#else
struct ircd::log::dwarning
{
	dwarning(const log &, const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}

	dwarning(const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}
};
#endif

#if RB_LOG_LEVEL >= 5
struct ircd::log::derror
{
	template<class... args>
	derror(const log &log, const char *const &fmt, args&&... a)
	{
		vlog(log, facility::DERROR, fmt, va_rtti{std::forward<args>(a)...});
	}

	template<class... args>
	derror(const char *const &fmt, args&&... a)
	{
		vlog(general, facility::DERROR, fmt, va_rtti{std::forward<args>(a)...});
	}
};
#else
struct ircd::log::derror
{
	derror(const log &, const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}

	derror(const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}
};
#endif

#if RB_LOG_LEVEL >= 4
struct ircd::log::info
{
	template<class... args>
	info(const log &log, const char *const &fmt, args&&... a)
	{
		vlog(log, facility::INFO, fmt, va_rtti{std::forward<args>(a)...});
	}

	template<class... args>
	info(const char *const &fmt, args&&... a)
	{
		vlog(general, facility::INFO, fmt, va_rtti{std::forward<args>(a)...});
	}
};
#else
struct ircd::log::info
{
	info(const log &, const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}

	info(const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}
};
#endif

#if RB_LOG_LEVEL >= 3
struct ircd::log::notice
{
	template<class... args>
	notice(const log &log, const char *const &fmt, args&&... a)
	{
		vlog(log, facility::NOTICE, fmt, va_rtti{std::forward<args>(a)...});
	}

	template<class... args>
	notice(const char *const &fmt, args&&... a)
	{
		vlog(general, facility::NOTICE, fmt, va_rtti{std::forward<args>(a)...});
	}
};
#else
struct ircd::log::notice
{
	notice(const log &, const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}

	notice(const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}
};
#endif

#if RB_LOG_LEVEL >= 2
struct ircd::log::warning
{
	template<class... args>
	warning(const log &log, const char *const &fmt, args&&... a)
	{
		vlog(log, facility::WARNING, fmt, va_rtti{std::forward<args>(a)...});
	}

	template<class... args>
	warning(const char *const &fmt, args&&... a)
	{
		vlog(general, facility::WARNING, fmt, va_rtti{std::forward<args>(a)...});
	}
};
#else
struct ircd::log::warning
{
	warning(const log &, const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}

	warning(const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}
};
#endif

#if RB_LOG_LEVEL >= 1
struct ircd::log::error
{
	template<class... args>
	error(const log &log, const char *const &fmt, args&&... a)
	{
		vlog(log, facility::ERROR, fmt, va_rtti{std::forward<args>(a)...});
	}

	template<class... args>
	error(const char *const &fmt, args&&... a)
	{
		vlog(general, facility::ERROR, fmt, va_rtti{std::forward<args>(a)...});
	}
};
#else
struct ircd::log::error
{
	error(const log &, const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}

	error(const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}
};
#endif

#if RB_LOG_LEVEL >= 0
struct ircd::log::critical
{
	template<class... args>
	critical(const log &log, const char *const &fmt, args&&... a)
	{
		vlog(log, facility::CRITICAL, fmt, va_rtti{std::forward<args>(a)...});
	}

	template<class... args>
	critical(const char *const &fmt, args&&... a)
	{
		vlog(general, facility::CRITICAL, fmt, va_rtti{std::forward<args>(a)...});
	}
};
#else
struct ircd::log::critical
{
	critical(const log &, const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}

	critical(const char *const &, ...)
	{
		// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
	}
};
#endif

#if RB_LOG_LEVEL >= 7
template<class... args>
void
ircd::log::log::debug(const char *const &fmt,
                      args&&... a)
{
	operator()(facility::DEBUG, fmt, va_rtti{std::forward<args>(a)...});
}
#else
inline void
ircd::log::log::debug(const char *const &fmt,
                      ...)
{
	// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
}
#endif

#if RB_LOG_LEVEL >= 6
template<class... args>
void
ircd::log::log::dwarning(const char *const &fmt,
                         args&&... a)
{
	operator()(facility::DWARNING, fmt, va_rtti{std::forward<args>(a)...});
}
#else
inline void
ircd::log::log::dwarning(const char *const &fmt,
                         ...)
{
	// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
}
#endif

#if RB_LOG_LEVEL >= 5
template<class... args>
void
ircd::log::log::derror(const char *const &fmt,
                       args&&... a)
{
	operator()(facility::DERROR, fmt, va_rtti{std::forward<args>(a)...});
}
#else
inline void
ircd::log::log::derror(const char *const &fmt,
                       ...)
{
	// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
}
#endif

#if RB_LOG_LEVEL >= 4
template<class... args>
void
ircd::log::log::info(const char *const &fmt,
                     args&&... a)
{
	operator()(facility::INFO, fmt, va_rtti{std::forward<args>(a)...});
}
#else
inline void
ircd::log::log::info(const char *const &fmt,
                     ...)
{
	// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
}
#endif

#if RB_LOG_LEVEL >= 3
template<class... args>
void
ircd::log::log::notice(const char *const &fmt,
                       args&&... a)
{
	operator()(facility::NOTICE, fmt, va_rtti{std::forward<args>(a)...});
}
#else
inline void
ircd::log::log::notice(const char *const &fmt,
                       ...)
{
	// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
}
#endif

#if RB_LOG_LEVEL >= 2
template<class... args>
void
ircd::log::log::warning(const char *const &fmt,
                        args&&... a)
{
	operator()(facility::WARNING, fmt, va_rtti{std::forward<args>(a)...});
}
#else
inline void
ircd::log::log::warning(const char *const &fmt,
                        ...)
{
	// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
}
#endif

#if RB_LOG_LEVEL >= 1
template<class... args>
void
ircd::log::log::error(const char *const &fmt,
                      args&&... a)
{
	operator()(facility::ERROR, fmt, va_rtti{std::forward<args>(a)...});
}
#else
inline void
ircd::log::log::error(const char *const &fmt,
                      ...)
{
	// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
}
#endif

#if RB_LOG_LEVEL >= 0
template<class... args>
void
ircd::log::log::critical(const char *const &fmt,
                         args&&... a)
{
	operator()(facility::CRITICAL, fmt, va_rtti{std::forward<args>(a)...});
}
#else
inline void
ircd::log::log::critical(const char *const &fmt,
                         ...)
{
	// Required in gcc 6.3.0 20170519, template param packs are not DCE'ed
}
#endif

template<class... args>
void
ircd::log::log::operator()(const facility &f,
                           const char *const &fmt,
                           args&&... a)
{
	vlog(*this, f, fmt, va_rtti{std::forward<args>(a)...});
}
