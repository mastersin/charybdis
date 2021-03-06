// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

decltype(ircd::conf::items)
ircd::conf::items
{};

bool
ircd::conf::set(std::nothrow_t,
                const string_view &key,
                const string_view &value)
try
{
	return set(key, value);
}
catch(const std::exception &e)
{
	log::error
	{
		"%s", e.what()
	};

	return false;
}

bool
ircd::conf::set(const string_view &key,
                const string_view &value)
try
{
	auto &item(*items.at(key));
	return item.set(value);
}
catch(const bad_lex_cast &e)
{
	throw bad_value
	{
		"Conf item '%s' rejected value '%s'", key, value
	};
}
catch(const std::out_of_range &e)
{
	throw not_found
	{
		"Conf item '%s' is not available", key
	};
}

ircd::string_view
ircd::conf::get(const string_view &key,
                const mutable_buffer &out)
try
{
	const auto &item(*items.at(key));
	return item.get(out);
}
catch(const std::out_of_range &e)
{
	throw not_found
	{
		"Conf item '%s' is not available", key
	};
}

bool
ircd::conf::exists(const string_view &key)
{
	return items.count(key);
}

//
// item
//

/// Conf item abstract constructor.
ircd::conf::item<void>::item(const json::members &opts)
:feature_
{
	opts
}
,feature
{
	feature_
}
,name
{
	unquote(feature.at("name"))
}
{
	if(!items.emplace(name, this).second)
		throw error
		{
			"Conf item named '%s' already exists", name
		};
}

ircd::conf::item<void>::~item()
noexcept
{
	if(name)
	{
		const auto it{items.find(name)};
		assert(data(it->first) == data(name));
		items.erase(it);
	}
}

bool
ircd::conf::item<void>::set(const string_view &)
{
	return false;
}

ircd::string_view
ircd::conf::item<void>::get(const mutable_buffer &)
const
{
	return {};
}

//
// Non-inline template specialization definitions
//

//
// std::string
//

ircd::conf::item<std::string>::item(const json::members &members)
:conf::item<>{members}
,value{unquote(feature.get("default"))}
{
}

bool
ircd::conf::item<std::string>::set(const string_view &s)
{
	_value = std::string{s};
	return true;
}

ircd::string_view
ircd::conf::item<std::string>::get(const mutable_buffer &out)
const
{
	return { data(out), _value.copy(data(out), size(out)) };
}

//
// bool
//

ircd::conf::item<bool>::item(const json::members &members)
:conf::item<>{members}
,value{feature.get<bool>("default", false)}
{
}

bool
ircd::conf::item<bool>::set(const string_view &s)
{
	switch(hash(s))
	{
		case "true"_:
			_value = true;
			return true;

		case "false"_:
			_value = false;
			return true;

		default: throw bad_value
		{
			"Conf item '%s' not assigned a bool literal", name
		};
	}
}

ircd::string_view
ircd::conf::item<bool>::get(const mutable_buffer &out)
const
{
	return _value?
		strlcpy(out, "true"_sv):
		strlcpy(out, "false"_sv);
}
