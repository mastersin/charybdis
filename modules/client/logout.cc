/* 
 * Copyright (C) 2017 Charybdis Development Team
 * Copyright (C) 2017 Jason Volk <jason@zemos.net>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice is present in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "account.h"

using namespace ircd;

using object = db::object<account>;
template<class T = string_view> using value = db::value<T, account>;

resource logout_resource
{
	"_matrix/client/r0/logout",
	"Invalidates an existing access token, so that it can no longer be used for "
	"authorization. (3.2.3)"
};

resource::response
logout(client &client, const resource::request &request)
{
	const auto &access_token(request.query.at("access_token"));
	const auto it(resource::tokens.find(access_token));
	if(unlikely(it == end(resource::tokens)))
		throw http::error{http::INTERNAL_SERVER_ERROR};

	resource::tokens.erase(it);
	return resource::response
	{
		client, json::obj
		{
			{   }
		}
	};
}

resource::method post
{
	logout_resource, "POST", logout,
	{
		post.REQUIRES_AUTH
	}
};

mapi::header IRCD_MODULE
{
	"registers the resource 'client/logout' to handle requests"
};