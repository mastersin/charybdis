/*
 *  charybdis: A slightly useful ircd.
 *  ircd.c: Starts up and runs the ircd.
 *
 *  Copyright (C) 1990 Jarkko Oikarinen and University of Oulu, Co Center
 *  Copyright (C) 1996-2002 Hybrid Development Team
 *  Copyright (C) 2002-2008 ircd-ratbox development team
 *  Copyright (C) 2005-2013 charybdis development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 */

#include <boost/filesystem.hpp>

namespace ircd {
namespace fs   {

using namespace boost::filesystem;

enum
{
	NAME  = 0,
	PATH  = 1,
};

using ent = std::pair<std::string, std::string>;

std::array<ent, num_of<index>()> paths
{{
	{ "prefix",            DPATH         },
	{ "binary dir",        BINPATH       },
	{ "config",            ETCPATH       },
	{ "log",               LOGPATH       },
	{ "libexec dir",       PKGLIBEXECDIR },
	{ "modules",           MODPATH       },
	{ "user help",         UHPATH        },
	{ "oper help",         HPATH         },
	{ "ircd.conf",         CPATH         },
	{ "ircd binary",       SPATH         },
	{ "ircd.motd",         MPATH         },
	{ "ircd.log",          LPATH         },
	{ "oper motd",         OPATH         },
	{ "bandb",             BDBPATH       },
	{ "db",                DBPATH        },
}};

} // namespace fs
} // namespace ircd

std::string
ircd::fs::read(const std::string &path)
{
	std::ifstream file{path};
	std::noskipws(file);
	std::istream_iterator<char> b{file};
	std::istream_iterator<char> e{};
	return std::string{b, e};
}

void
ircd::fs::chdir(const std::string &path)
try
{
	fs::current_path(path);
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

std::string
ircd::fs::cwd()
try
{
	return fs::current_path().string();
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

std::vector<std::string>
ircd::fs::ls_recursive(const std::string &path)
try
{
	const fs::recursive_directory_iterator end;
	fs::recursive_directory_iterator it(path);

	std::vector<std::string> ret;
	std::transform(it, end, std::back_inserter(ret), []
	(const auto &ent)
	{
		return ent.path().string();
	});

	return ret;
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

std::vector<std::string>
ircd::fs::ls(const std::string &path)
try
{
	static const fs::directory_iterator end;
	fs::directory_iterator it(path);

	std::vector<std::string> ret;
	std::transform(it, end, std::back_inserter(ret), []
	(const auto &ent)
	{
		return ent.path().string();
	});

	return ret;
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

bool
ircd::fs::is_reg(const std::string &path)
try
{
	return fs::is_regular_file(path);
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

bool
ircd::fs::is_dir(const std::string &path)
try
{
	return fs::is_directory(path);
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

bool
ircd::fs::exists(const std::string &path)
try
{
	return fs::exists(path);
}
catch(const fs::filesystem_error &e)
{
	throw filesystem_error("%s", e.what());
}

std::string
ircd::fs::make_path(const std::initializer_list<std::string> &list)
{
	fs::path ret;
	for(const auto &s : list)
		ret /= fs::path(s);

	return ret.string();
}

const char *
ircd::fs::get(index index)
noexcept try
{
	return std::get<PATH>(paths.at(index)).c_str();
}
catch(const std::out_of_range &e)
{
	return nullptr;
}

const char *
ircd::fs::name(index index)
noexcept try
{
	return std::get<NAME>(paths.at(index)).c_str();
}
catch(const std::out_of_range &e)
{
	return nullptr;
}
