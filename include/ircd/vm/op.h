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
#define HAVE_IRCD_VM_OP_H

namespace ircd::vm::op
{
	IRCD_EXCEPTION(vm::error, error)

	using code = uint8_t;

	constexpr const size_t &tabsz {256};
	extern const std::array<struct info, tabsz> info;
	extern const std::array<struct wtab, tabsz> wtab;

	size_t len(const code &);
	size_t psh(const code &);
	size_t pop(const code &);
	string_view name(const code &);

	int32_t operand(const const_buffer &);
};

struct ircd::vm::op::info
{
	op::code code {0};
	string_view name;
	uint8_t len {0};
	uint8_t psh {0};
	uint8_t pop {0};

	info() = default;
};

struct ircd::vm::op::wtab
{
	uint16_t len     : 3;
	uint16_t psh     : 3;
	uint16_t pop     : 3;
	uint16_t _mbz_   : 7;

	wtab(const struct info &);
	wtab() = default;
};

static_assert
(
	sizeof(struct ircd::vm::op::wtab) == 2
);
