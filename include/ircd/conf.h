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
#define HAVE_IRCD_CONF_H

/// Configuration system.
///
/// This system disseminates mutable runtime values throughout IRCd. All users
/// that integrate a configurable value will create a [static] conf::item<>
/// instantiated with one of the explicit types available; also a name and
/// default value.
///
/// All conf::items are collected by this system. Users that administrate
/// configuration will push values to the conf::item's. The various items have
/// O(1) access to the value contained in their item instance. Administrators
/// have logarithmic access through this interface using the items map by name.
///
namespace ircd::conf
{
	template<class T = void> struct item;  // doesn't exist
	template<> struct item<void>;          // base class of all conf items
	template<> struct item<std::string>;
	template<> struct item<bool>;
	template<> struct item<uint64_t>;
	template<> struct item<int64_t>;
	template<> struct item<hours>;
	template<> struct item<seconds>;
	template<> struct item<milliseconds>;
	template<> struct item<microseconds>;

	template<class T> struct value;        // abstraction for carrying item value
	template<class T> struct lex_castable; // abstraction for lex_cast compatible

	IRCD_EXCEPTION(ircd::error, error)
	IRCD_EXCEPTION(error, not_found)
	IRCD_EXCEPTION(error, bad_value)

	extern std::map<string_view, item<> *> items;

	bool exists(const string_view &key);
	string_view get(const string_view &key, const mutable_buffer &out);
	bool set(const string_view &key, const string_view &value);
	bool set(std::nothrow_t, const string_view &key, const string_view &value);
}

/// Conf item base class. You don't create this directly; use one of the
/// derived templates instead.
template<>
struct ircd::conf::item<void>
{
	json::strung feature_;
	json::object feature;
	string_view name;

	virtual string_view get(const mutable_buffer &) const;
	virtual bool set(const string_view &);

	item(const json::members &);
	item(item &&) = delete;
	item(const item &) = delete;
	virtual ~item() noexcept;
};

/// Conf item value abstraction. If possible, the conf item will also
/// inherit from this template to deduplicate functionality between
/// conf items which contain similar classes of values.
template<class T>
struct ircd::conf::value
{
	using value_type = T;

	T _value;

	operator const T &() const
	{
		return _value;
	}

	template<class... A>
	value(A&&... a)
	:_value(std::forward<A>(a)...)
	{}
};

template<class T>
struct ircd::conf::lex_castable
:conf::item<>
,conf::value<T>
{
	string_view get(const mutable_buffer &out) const override
	{
		return lex_cast(this->_value, out);
	}

	bool set(const string_view &s) override
	{
		this->_value = lex_cast<T>(s);
		return true;
	}

	lex_castable(const json::members &members)
	:conf::item<>{members}
	,conf::value<T>(feature.get("default", long(0)))
	{}
};

template<>
struct ircd::conf::item<std::string>
:conf::item<>
,conf::value<std::string>
{
	operator string_view() const
	{
		return _value;
	}

	string_view get(const mutable_buffer &out) const override;
	bool set(const string_view &s) override;

	item(const json::members &members);
};

template<>
struct ircd::conf::item<bool>
:conf::item<>
,conf::value<bool>
{
	string_view get(const mutable_buffer &out) const override;
	bool set(const string_view &s) override;

	item(const json::members &members);
};

template<>
struct ircd::conf::item<uint64_t>
:lex_castable<uint64_t>
{
	using lex_castable::lex_castable;
};

template<>
struct ircd::conf::item<int64_t>
:lex_castable<int64_t>
{
	using lex_castable::lex_castable;
};

template<>
struct ircd::conf::item<ircd::hours>
:lex_castable<ircd::hours>
{
	using lex_castable::lex_castable;
};

template<>
struct ircd::conf::item<ircd::seconds>
:lex_castable<ircd::seconds>
{
	using lex_castable::lex_castable;
};

template<>
struct ircd::conf::item<ircd::milliseconds>
:lex_castable<ircd::milliseconds>
{
	using lex_castable::lex_castable;
};

template<>
struct ircd::conf::item<ircd::microseconds>
:lex_castable<ircd::microseconds>
{
	using lex_castable::lex_castable;
};
