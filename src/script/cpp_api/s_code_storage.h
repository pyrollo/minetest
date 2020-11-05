/*
Minetest
Copyright (C) 2020 pyrollo, Pierre-Yves Rollo <dev@pyrollo.com>
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.
You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#include "util/string.h"
#include "cpp_api/s_base.h"

class ScriptApiMemoryStoredCode : virtual public ScriptApiBase
{
public:
	bool getSourceCode(lua_State *L, const std::string &path,
			std::string &source_code, std::string &chunk_name);

	void addSourceCode(const std::string &mod, const std::string &path,
			const std::string &source_code);
private:
	StringMap m_source_code;
};

class ScriptApiFileStoredCode : virtual public ScriptApiBase
{
public:
	bool getSourceCode(lua_State *L, const std::string &path,
			std::string &source_code, std::string &chunk_name);
};
