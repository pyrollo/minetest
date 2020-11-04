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

void ScriptApiMemoryStoredCode::addSourceCode(const std::string &mod, const std::string &path, const std::string &source_code)
{
	std::string key = mod + ":" + path;
	m_source_code[key] = source_code;
}

bool ScriptApiMemoryStoredCode::getSourceCode(lua_State *L, const char *path, std::string &source_code, std::string &chunk_name)
{
	// Find current mod name
	std::string mod_name = "*unknown*";
	lua_rawgeti(L, LUA_REGISTRYINDEX, CUSTOM_RIDX_CURRENT_MOD_NAME);
	if (lua_isstring(L, -1))
		mod_name = readParam<std::string>(L, -1);
	lua_pop(L, 1);  // Pop mod name

	const std::string key = mod_name + ":" + path;
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

bool ScriptApiFileStoredCode::getSourceCode(lua_State *L, const char *path, std::string &source_code, std::string &chunk_name)
{
	FILE *fp;
	if (!path) {
		fp = stdin;
		chunk_name = "=stdin";
	} else {
		CHECK_SECURE_PATH_INTERNAL(L, path, false, NULL);

		fp = fopen(path, "rb");
		if (!fp) {
			lua_pushfstring(L, "%s: %s", path, strerror(errno));
			return false;
		}
		chunk_name = "@";
		chunk_name += '\0';
		chunk_name += path;
	}

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
		if (path)
			std::fclose(fp);
		return false;
	}

	size_t size = std::ftell(fp) - start;
	source_code = "";
	source_code.resize (size, '\0');
	ret = std::fseek(fp, start, SEEK_SET);
	if (ret) {
		lua_pushfstring(L, "%s: %s", path, strerror(errno));
		if (path)
			std::fclose(fp);
		return false;
	}
// PB HERE NO ?
	size_t num_read = std::fread(&source_code[0], 1, size, fp);
	if (path)
		std::fclose(fp);
	if (num_read != size) {
		lua_pushliteral(L, "Error reading file to load.");
		return false;
	}

	return true;
}
