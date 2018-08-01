// Matrix Construct
//
// Copyright (C) Matrix Construct Developers, Authors & Contributors
// Copyright (C) 2016-2018 Jason Volk <jason@zemos.net>
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice is present in all copies. The
// full license for this software is available in the LICENSE file.

#include <ircd/js/js.h>
#include <ircd/vm/vm.h>

std::string
ircd::vm::debug(const core &core)
{
	const core::port p{core};
	return debug(p);
}

ircd::vm::core::core(const const_buffer &cs)
:cs{cs}
,ip{0}
{
}

bool
ircd::vm::core::operator()()
{
	if(hf)
		return hf;

	port p{*this};
	p();
	ip = p.ip;
	sp = p.sp;
	zf = p.zf;
	ff = p.ff;
	hf = p.hf || ip >= size(cs);
	return hf;
}

//
// port
//

std::string
ircd::vm::debug(const core::port &port)
{
	std::stringstream ret;
	ret << "@[" << std::right << std::hex << std::setw(4) << std::setfill(' ') << port.ip << std::dec
	    << "] +[" << std::right << std::setw(4) << std::setfill(' ') << port.sp
	    << "]"
	    << "  " << std::right << std::setw(2) << std::hex << std::setfill('0') << uint(uint8_t(port.inst[0])) << std::dec
	    << " " << std::left << std::setw(8) << std::setfill(' ') << u2a(const_buffer(port.inst + 1))
	    << "  -" << std::setw(1) << op::pop(port.inst[0])
	    << " +" << std::setw(1) << op::psh(port.inst[0])
	    << "  " << std::left << std::setw(20) << std::setfill(' ') << op::name(port.inst[0])
	    << (port.zf? " ZERO" : "")
	    << (port.hf? " HALT" : "")
	    << (port.ff? " FAULT" : "")
	    ;

	return ret.str();
}

ircd::vm::core::port::port(const core &core)
:inst{core.cs, core.ip}
,ip{core.ip}
,sp{core.sp}
,zf{core.zf}
,hf{core.hf}
,ff{core.ff}
{
}

bool
ircd::vm::core::port::operator()()
{
	switch(uint8_t(inst[0]))
	{
		case 0xe6:
		case 0xe3:
		case 0x6d:
			// jump allowed
			break;

		default:
			// no jump allowed
			break;
	}

	sp += op::psh(inst[0]);
	sp -= op::pop(inst[0]);

	switch(uint8_t(inst[0]))
	{
		case 0x06:
		{
			ip += op::operand(inst);
			break;
		}

		case 0x07:
		{
			if(!zf)
			{
				ip += op::operand(inst);
			}
			else
			{
				ip += op::len(inst[0]);
			}

			break;
		}

		case 0x08:
		{
			if(!zf)
			{
				ip += op::operand(inst);
			}
			else
			{
				ip += op::len(inst[0]);
			}

			break;
		}

		case 0x3a:
		{
			assert(op::len(inst[0]) == 3);
			sp -= op::operand(inst);
			ip += op::len(inst[0]);
			break;
		}

		case 0x52:
		{
			assert(op::len(inst[0]) == 3);
			sp -= op::operand(inst);
			ip += op::len(inst[0]);
			break;
		}

		case 0x70:
		{
			ip += op::len(inst[0]);
			hf = true;
			ff = true;
			break;
		}

		case 0x99:
		{
			ip += op::len(inst[0]);
			break;
		}

		case 0x6d:
		{
			ip += op::len(inst[0]);
			break;
		}

		case 0xe3:
		{
			ip += op::len(inst[0]);
			break;
		}

		case 0xe6:
		{
			ip += op::len(inst[0]);
			break;
		}

		default:
			ip += op::len(inst[0]);
			break;
	}

	return true;
}

//
// inst
//

ircd::vm::core::inst::inst(const const_buffer &cs,
                           const size_t &ip)
:const_buffer{[&cs, &ip]
{
	if(unlikely(ip >= size(cs)))
		throw error
		{
			"Instruction Pointer (offset=%zu) beyond Code Segment (size=%zu)", ip, size(cs)
		};

	const op::code &bc(cs[ip]);
	if(unlikely(ip + op::len(bc) > size(cs)))
		throw error
		{
			"ip(%zu) + len(%zu) > cs(%zu)", ip, op::len(bc), size(cs)
		};

	return const_buffer
	{
		std::begin(cs) + ip, op::len(bc)
	};
}()}
{
}

///////////////////////////////////////////////////////////////////////////////
//
// vm/stack.h
//

///////////////////////////////////////////////////////////////////////////////
//
// vm/op.h
//

int32_t
ircd::vm::op::operand(const const_buffer &buf)
{
	if(unlikely(size(buf) < 1))
		throw error
		{
			"No bytecode supplied in buffer to op::operand()"
		};

	const code &bc(buf[0]);
	if(unlikely(size(buf) < len(bc)))
		throw error
		{
			"No operand data supplied in buffer to op::operand()"
		};

	const auto &operand_size
	{
		len(bc) - 1
	};

	switch(operand_size)
	{
		case 1:   return *reinterpret_cast<const int8_t *>(buf.begin() + 1);
		case 2:   return ntoh(*reinterpret_cast<const int16_t *>(buf.begin() + 1));
		case 4:   return ntoh(*reinterpret_cast<const int32_t *>(buf.begin() + 1));
		default:  throw error
		{
			"No operand allowed for op '%s'", name(bc)
		};
	}
}

ircd::string_view
ircd::vm::op::name(const code &code)
{
	return info[code].name;
}

size_t
ircd::vm::op::pop(const code &code)
{
	return wtab[code].pop;
}

size_t
ircd::vm::op::psh(const code &code)
{
	return wtab[code].psh;
}

size_t
ircd::vm::op::len(const code &code)
{
	return wtab[code].len;
}

//
// info tab
//

decltype(ircd::vm::op::info)
ircd::vm::op::info
{{
	{ 0x00, "nop",                          1, 0, 0    },
	{ 0x01, "jsundefined",                  1, 1, 0    },
	{ 0x02, "getrval",                      1, 1, 0    },
	{ 0x03, "enterwith",                    5, 0, 1    },
	{ 0x04, "leavewith",                    1, 0, 0    },
	{ 0x05, "return",                       1, 0, 1    },
	{ 0x06, "goto",                         5, 0, 0    },
	{ 0x07, "ifeq",                         5, 0, 1    },
	{ 0x08, "ifne",                         5, 0, 1    },
	{ 0x09, "arguments",                    1, 1, 0    },
	{ 0x0a, "swap",                         1, 2, 2    },
	{ 0x0b, "popn",                         3, 0, 0    },
	{ 0x0c, "dup",                          1, 2, 1    },
	{ 0x0d, "dup2",                         1, 4, 2    },
	{ 0x0e, "checkisobj",                   2, 1, 1    },
	{ 0x0f, "bitor",                        1, 1, 2    },
	{ 0x10, "bitxor",                       1, 1, 2    },
	{ 0x11, "bitand",                       1, 1, 2    },
	{ 0x12, "eq",                           1, 1, 2    },
	{ 0x13, "ne",                           1, 1, 2    },
	{ 0x14, "lt",                           1, 1, 2    },
	{ 0x15, "le",                           1, 1, 2    },
	{ 0x16, "gt",                           1, 1, 2    },
	{ 0x17, "ge",                           1, 1, 2    },
	{ 0x18, "lsh",                          1, 1, 2    },
	{ 0x19, "rsh",                          1, 1, 2    },
	{ 0x1a, "ursh",                         1, 1, 2    },
	{ 0x1b, "add",                          1, 1, 2    },
	{ 0x1c, "sub",                          1, 1, 2    },
	{ 0x1d, "mul",                          1, 1, 2    },
	{ 0x1e, "div",                          1, 1, 2    },
	{ 0x1f, "mod",                          1, 1, 2    },
	{ 0x20, "not",                          1, 1, 1    },
	{ 0x21, "bitnot",                       1, 1, 1    },
	{ 0x22, "neg",                          1, 1, 1    },
	{ 0x23, "pos",                          1, 1, 1    },
	{ 0x24, "delname",                      5, 1, 0    },
	{ 0x25, "delprop",                      5, 1, 1    },
	{ 0x26, "delelem",                      1, 1, 2    },
	{ 0x27, "typeOf",                       1, 1, 1    },
	{ 0x28, "void",                         1, 1, 1    },
	{ 0x29, "spreadcall",                   1, 1, 3    },
	{ 0x2a, "spreadnew",                    1, 1, 4    },
	{ 0x2b, "spreadeval",                   1, 1, 3    },
	{ 0x2c, "dupat",                        4, 1, 0    },
	{ 0x2d, "symbol",                       2, 1, 0    },
	{ 0x2e, "strict-delprop",               5, 1, 1    },
	{ 0x2f, "strict-delelem",               1, 1, 2    },
	{ 0x30, "strict-setprop",               5, 1, 2    },
	{ 0x31, "strict-setname",               5, 1, 2    },
	{ 0x32, "strict-spreadeval",            1, 1, 3    },
	{ 0x33, "classheritage",                1, 2, 1    },
	{ 0x34, "funwithproto",                 5, 1, 1    },
	{ 0x35, "getprop",                      5, 1, 1    },
	{ 0x36, "setprop",                      5, 1, 2    },
	{ 0x37, "getelem",                      1, 1, 2    },
	{ 0x38, "setelem",                      1, 1, 3    },
	{ 0x39, "strict-setelem",               1, 1, 3    },
	{ 0x3a, "call",                         3, 1, 2    },
	{ 0x3b, "getname",                      5, 1, 0    },
	{ 0x3c, "double",                       5, 1, 0    },
	{ 0x3d, "string",                       5, 1, 0    },
	{ 0x3e, "zero",                         1, 1, 0    },
	{ 0x3f, "one",                          1, 1, 0    },
	{ 0x40, "null",                         1, 1, 0    },
	{ 0x41, "is-constructing",              1, 1, 0    },
	{ 0x42, "false",                        1, 1, 0    },
	{ 0x43, "false",                        1, 1, 0    },
	{ 0x44, "or",                           5, 1, 1    },
	{ 0x45, "and",                          5, 1, 1    },
	{ 0x46, "tableswitch",                  0, 0, 1    },
	{ 0x47, "runonce",                      1, 0, 0    },
	{ 0x48, "stricteq",                     1, 1, 2    },
	{ 0x49, "strictne",                     1, 1, 2    },
	{ 0x4a, "throwmsg",                     3, 0, 0    },
	{ 0x4b, "iter",                         2, 1, 1    },
	{ 0x4c, "moreiter",                     1, 2, 1    },
	{ 0x4d, "isnoiter",                     1, 2, 1    },
	{ 0x4e, "enditer",                      1, 0, 1    },
	{ 0x4f, "funapply",                     3, 1, 0    },
	{ 0x50, "object",                       5, 1, 0    },
	{ 0x51, "pop",                          1, 0, 1    },
	{ 0x52, "new",                          3, 1, 2    },
	{ 0x53, "objwithproto",                 1, 1, 1    },
	{ 0x54, "getarg",                       3, 1, 0    },
	{ 0x55, "setarg",                       3, 1, 1    },
	{ 0x56, "getlocal",                     4, 1, 0    },
	{ 0x57, "setlocal",                     4, 1, 1    },
	{ 0x58, "uint16",                       3, 1, 0    },
	{ 0x59, "newinit",                      5, 1, 0    },
	{ 0x5a, "newarray",                     5, 1, 0    },
	{ 0x5b, "newobject",                    5, 1, 0    },
	{ 0x5c, "inithomeobject",               2, 2, 2    },
	{ 0x5d, "initprop",                     5, 1, 2    },
	{ 0x5e, "initelem",                     1, 1, 3    },
	{ 0x5f, "initelem_inc",                 1, 2, 3    },
	{ 0x60, "initelem_array",               5, 1, 2    },
	{ 0x61, "initprop_getter",              5, 1, 2    },
	{ 0x62, "initprop_setter",              5, 1, 2    },
	{ 0x63, "initelem_getter",              1, 1, 3    },
	{ 0x64, "initelem_setter",              1, 1, 3    },
	{ 0x65, "callsiteobj",                  5, 1, 0    },
	{ 0x66, "newarray_copyonwrite",         5, 1, 0    },
	{ 0x67, "superbase",                    1, 1, 0    },
	{ 0x68, "getprop-super",                5, 1, 2    },
	{ 0x69, "strictsetprop-super",          5, 1, 3    },
	{ 0x6a, "label",                        5, 0, 0    },
	{ 0x6b, "setprop-super",                5, 1, 3    },
	{ 0x6c, "funcall",                      3, 1, 0    },
	{ 0x6d, "loophead",                     1, 0, 0    },
	{ 0x6e, "bindname",                     5, 1, 0    },
	{ 0x6f, "setname",                      5, 1, 2    },
	{ 0x70, "throw",                        1, 0, 1    },
	{ 0x71, "in",                           1, 1, 2    },
	{ 0x72, "instanceOf",                   1, 1, 2    },
	{ 0x73, "debugger",                     1, 0, 0    },
	{ 0x74, "gosub",                        5, 0, 0    },
	{ 0x75, "retsub",                       1, 0, 2    },
	{ 0x76, "exception",                    1, 1, 0    },
	{ 0x77, "lineno",                       5, 0, 0    },
	{ 0x78, "condswitch",                   1, 0, 0    },
	{ 0x79, "case",                         5, 1, 2    },
	{ 0x7a, "default",                      5, 0, 1    },
	{ 0x7b, "eval",                         3, 1, 0    },
	{ 0x7c, "strict-eval",                  3, 1, 0    },
	{ 0x7d, "getelem-super",                1, 1, 3    },
	{ 0x7e, "spreadcallarray",              5, 1, 0    },
	{ 0x7f, "deffun",                       1, 0, 1    },
	{ 0x80, "defconst",                     5, 0, 0    },
	{ 0x81, "defvar",                       5, 0, 0    },
	{ 0x82, "lambda",                       5, 1, 0    },
	{ 0x83, "lambda_arrow",                 5, 1, 1    },
	{ 0x84, "callee",                       1, 1, 0    },
	{ 0x85, "pick",                         2, 0, 0    },
	{ 0x86, "try",                          1, 0, 0    },
	{ 0x87, "finally",                      1, 2, 0    },
	{ 0x88, "getaliasedvar",                5, 1, 0    },
	{ 0x89, "setaliasedvar",                5, 1, 1    },
	{ 0x8a, "checklexical",                 4, 0, 0    },
	{ 0x8b, "initlexical",                  4, 1, 1    },
	{ 0x8c, "checkaliasedlexical",          5, 0, 0    },
	{ 0x8d, "initaliasedlexical",           5, 1, 1    },
	{ 0x8e, "uninitialized",                1, 1, 0    },
	{ 0x8f, "getintrinsic",                 5, 1, 0    },
	{ 0x90, "setintrinsic",                 5, 1, 1    },
	{ 0x91, "calliter",                     3, 1, 0    },
	{ 0x92, "initlockedprop",               5, 1, 2    },
	{ 0x93, "inithiddenprop",               5, 1, 2    },
	{ 0x94, "newtarget",                    1, 1, 0    },
	{ 0x95, "toasync",                      1, 1, 1    },
	{ 0x96, "pow",                          1, 1, 2    },
	{ 0x97, "throwing",                     1, 0, 1    },
	{ 0x98, "setrval",                      1, 0, 1    },
	{ 0x99, "retrval",                      1, 0, 0    },
	{ 0x9a, "getgname",                     5, 1, 0    },
	{ 0x9b, "setgname",                     5, 1, 2    },
	{ 0x9c, "strict-setgname",              5, 1, 2    },
	{ 0x9d, "gimplicitthis",                5, 1, 0    },
	{ 0x9e, "setelem-super",                1, 1, 4    },
	{ 0x9f, "strict-setelem-super",         1, 1, 4    },
	{ 0xa0, "regexp",                       5, 1, 0    },
	{ 0xa1, "initglexical",                 5, 1, 1    },
	{ 0xa2, "deflet",                       5, 0, 0    },
	{ 0xa3, "checkobjcoercible",            1, 1, 1    },
	{ 0xa4, "superfun",                     1, 1, 0    },
	{ 0xa5, "supercall",                    3, 1, 0    },
	{ 0xa6, "spreadsupercall",              1, 1, 4    },
	{ 0xa7, "classconstructor",             5, 1, 0    },
	{ 0xa8, "derivedconstructor",           5, 1, 1    },
	{ 0xa9, "throwsetconst",                4, 1, 1    },
	{ 0xaa, "throwsetaliasedconst",         5, 1, 1    },
	{ 0xab, "inithiddenprop_getter",        5, 1, 2    },
	{ 0xac, "inithiddenprop_setter",        5, 1, 2    },
	{ 0xad, "inithiddenelem_getter",        1, 1, 3    },
	{ 0xae, "inithiddenelem_setter",        1, 1, 3    },
	{ 0xaf, "inithiddenelem",               1, 1, 3    },
	{ 0xb0, "getimport",                    5, 1, 0    },
	{ 0xb1, "debug-checkselfhosted",        1, 1, 1    },
	{ 0xb2, "optimize-spreadcall",          1, 2, 1    },
	{ 0xb3, "throwsetcallee",               1, 1, 1    },
	{ 0xb4, "pushvarenv",                   5, 0, 0    },
	{ 0xb5, "popvarenv",                    1, 0, 0    },
	{ 0xb6, "unused182",                    1, 0, 0    },
	{ 0xb7, "unused183",                    1, 0, 0    },
	{ 0xb8, "callprop",                     5, 1, 1    },
	{ 0xb9, "functionthis",                 1, 1, 0    },
	{ 0xba, "globalthis",                   1, 1, 0    },
	{ 0xbb, "unused187",                    1, 0, 0    },
	{ 0xbc, "uint24",                       4, 1, 0    },
	{ 0xbd, "checkthis",                    1, 1, 1    },
	{ 0xbe, "checkreturn",                  1, 0, 1    },
	{ 0xbf, "checkthisreinit",              1, 1, 1    },
	{ 0xc0, "unused192",                    1, 0, 0    },
	{ 0xc1, "callelem",                     1, 1, 2    },
	{ 0xc2, "mutateproto",                  1, 1, 2    },
	{ 0xc3, "getxprop",                     5, 1, 1    },
	{ 0xc4, "typeofexpr",                   1, 1, 1    },
	{ 0xc5, "freshenlexicalenv",            1, 0, 0    },
	{ 0xc6, "recreatelexicalenv",           1, 0, 0    },
	{ 0xc7, "pushlexicalenv",               5, 0, 0    },
	{ 0xc8, "poplexicalenv",                1, 0, 0    },
	{ 0xc9, "debugleavelexicalenv",         1, 0, 0    },
	{ 0xca, "initialyield",                 4, 1, 1    },
	{ 0xcb, "yield",                        4, 1, 2    },
	{ 0xcc, "finalyieldrval",               1, 0, 1    },
	{ 0xcd, "resume",                       3, 1, 2    },
	{ 0xce, "arraypush",                    1, 0, 2    },
	{ 0xcf, "forceinterpreter",             1, 0, 0    },
	{ 0xd0, "debugafteryield",              1, 0, 0    },
	{ 0xd1, "unused209",                    1, 0, 0    },
	{ 0xd2, "unused210",                    1, 0, 0    },
	{ 0xd3, "unused211",                    1, 0, 0    },
	{ 0xd4, "generator",                    1, 1, 0    },
	{ 0xd5, "bindvar",                      1, 1, 0    },
	{ 0xd6, "bindgname",                    5, 1, 0    },
	{ 0xd7, "int8",                         2, 1, 0    },
	{ 0xd8, "int32",                        5, 1, 0    },
	{ 0xd9, "length",                       5, 1, 1    },
	{ 0xda, "hole",                         1, 1, 0    },
	{ 0xdb, "unused219",                    1, 0, 0    },
	{ 0xdc, "unused220",                    1, 0, 0    },
	{ 0xdd, "unused221",                    1, 0, 0    },
	{ 0xde, "unused222",                    1, 0, 0    },
	{ 0xdf, "unused223",                    1, 0, 0    },
	{ 0xe0, "rest",                         1, 1, 0    },
	{ 0xe1, "toid",                         1, 1, 1    },
	{ 0xe2, "implicitthis",                 5, 1, 0    },
	{ 0xe3, "loopentry",                    2, 0, 0    },
	{ 0xe4, "tostring",                     1, 1, 1    },
	{ 0xe5, "nop-destructuring",            1, 0, 0    },
	{ 0xe6, "jumptarget",                   1, 0, 0    },
	{ 0xe7, {},                             0, 0, 0    },
	{ 0xe8, {},                             0, 0, 0    },
	{ 0xe9, {},                             0, 0, 0    },
	{ 0xea, {},                             0, 0, 0    },
	{ 0xeb, {},                             0, 0, 0    },
	{ 0xec, {},                             0, 0, 0    },
	{ 0xed, {},                             0, 0, 0    },
	{ 0xee, {},                             0, 0, 0    },
	{ 0xef, {},                             0, 0, 0    },
	{ 0xf0, {},                             0, 0, 0    },
	{ 0xf1, {},                             0, 0, 0    },
	{ 0xf2, {},                             0, 0, 0    },
	{ 0xf3, {},                             0, 0, 0    },
	{ 0xf4, {},                             0, 0, 0    },
	{ 0xf5, {},                             0, 0, 0    },
	{ 0xf6, {},                             0, 0, 0    },
	{ 0xf7, {},                             0, 0, 0    },
	{ 0xf8, {},                             0, 0, 0    },
	{ 0xf9, {},                             0, 0, 0    },
	{ 0xfa, {},                             0, 0, 0    },
	{ 0xfb, {},                             0, 0, 0    },
	{ 0xfc, {},                             0, 0, 0    },
	{ 0xfd, {},                             0, 0, 0    },
	{ 0xfe, {},                             0, 0, 0    },
	{ 0xff, {},                             0, 0, 0    },
}};

//
// wtab
//

decltype(ircd::vm::op::wtab)
ircd::vm::op::wtab{[]
{
	std::array<struct wtab, tabsz> ret;
	for(size_t i(0); i < tabsz; ++i)
		ret[i] = info[i];

	return ret;
}()};

ircd::vm::op::wtab::wtab(const struct info &info)
:len{info.len}
,psh{info.psh}
,pop{info.pop}
,_mbz_{0}
{
}
