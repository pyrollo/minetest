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

#include "cpp_api/s_code_storage.h"
#include "cpp_api/s_security.h" // For CHECK_SECURE_PATH_INTERNAL

void ScriptApiMemoryStoredCode::addSourceCode(const std::string &mod,
		const std::string &path, const std::string &source_code)
{
	std::string key = mod + ":" + path;
	m_source_code[key] = source_code;
}

bool ScriptApiMemoryStoredCode::getSourceCode(lua_State *L,
		const std::string &path, std::string &source_code,
		std::string &chunk_name)
{
	std::string current_mod_name = "*unknown*";

	// Use current mod name if any
	lua_rawgeti(L, LUA_REGISTRYINDEX, CUSTOM_RIDX_CURRENT_MOD_NAME);
	if (lua_isstring(L, -1))
		current_mod_name = readParam<std::string>(L, -1);
	lua_pop(L, 1);  // Pop mod name

	std::string key = path;
	std::string mod_name = current_mod_name;

	// Check if mod name in path
	auto pos = key.find_first_of(":");
	if (pos == std::string::npos) {
		key = mod_name + ":" + key;
	} else {
		std::string mod_name = path.substr(0, pos);
		if (current_mod_name != mod_name) {
			printf("Mod %s is reading %s\n", current_mod_name.c_str(), key.c_str());
			// TODO test current_mod_name can read mod_name
		}
	}

	chunk_name = "@" + key;

	// Try to find code in memory
	auto it = m_source_code.find(key);
	if (it == m_source_code.end()) {
		lua_pushfstring(L, "%s: not found for mod %s", path, mod_name.c_str());
		return false;
	}
	source_code = it->second;

	return true;
}

bool ScriptApiFileStoredCode::getSourceCode(lua_State *L,
		const std::string &path, std::string &source_code,
		std::string &chunk_name)
{
	CHECK_SECURE_PATH_INTERNAL(L, path.c_str(), false, NULL);

	FILE *fp = fopen(path.c_str(), "rb");
	if (!fp) {
		lua_pushfstring(L, "%s: %s", path, strerror(errno));
		return false;
	}
	chunk_name = "@";
	chunk_name += '\0';
	chunk_name += path;

	size_t start = 0;
	int c = std::getc(fp);
	if (c == '#') {
		// Skip the first line
		while ((c = std::getc(fp)) != EOF && c != '\n') {}
		if (c == '\n')
			std::getc(fp);
		start = std::ftell(fp);
	}

	// Read the file
	int ret = std::fseek(fp, 0, SEEK_END);
	if (ret) {
		lua_pushfstring(L, "%s: %s", path, strerror(errno));
		std::fclose(fp);
		return false;
	}

	size_t size = std::ftell(fp) - start;
	source_code = "";
	source_code.resize (size, '\0');
	ret = std::fseek(fp, start, SEEK_SET);
	if (ret) {
		lua_pushfstring(L, "%s: %s", path, strerror(errno));
		std::fclose(fp);
		return false;
	}

	size_t num_read = std::fread(&source_code[0], 1, size, fp);
	std::fclose(fp);
	if (num_read != size) {
		lua_pushliteral(L, "Error reading file to load.");
		return false;
	}

	return true;
}

