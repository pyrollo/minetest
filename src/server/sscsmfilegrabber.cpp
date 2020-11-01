#include "sscsmfilegrabber.h"
#include "porting.h"
#include "util/serialize.h"
#include "log.h"
#include <fstream>
#include "filesys.h"
#include "string.h"
#include "server/mods.h"
#include "exceptions.h"
#include "script/cpp_api/s_base.h" // for BUILTIN_MOD_NAME

SSCSMFileGrabber::SSCSMFileGrabber(std::vector<std::string> *mods,
	std::vector<std::pair<u8 *, u32>> *sscsm_files,
	const std::unique_ptr<ServerModManager> &modmgr) :
	m_mods(mods), m_sscsm_files(sscsm_files), m_modmgr(modmgr), m_buffer_queue()
{
	m_out_buffer = new u8[m_buffer_size];
	m_zstream.zalloc = Z_NULL;
	m_zstream.zfree = Z_NULL;
	m_zstream.opaque = Z_NULL;
}

SSCSMFileGrabber::~SSCSMFileGrabber()
{
	delete[] m_out_buffer;
}

void SSCSMFileGrabber::parseMods()
{
	// initialize the z_stream
	int ret = deflateInit(&m_zstream, Z_DEFAULT_COMPRESSION);
	if (ret < 0)
		throw SerializationError("SSCSMFileGrabber: deflateInit failed");

	m_zstream.next_out = m_out_buffer;
	m_zstream.avail_out = m_buffer_size;

	// create first buffer
	m_buffer_offset = 0;
	m_buffer = new u8[m_buffer_size];

	// add builtin
	std::string builtin_path = porting::path_share + DIR_DELIM + "builtin" + DIR_DELIM;
	addDir(builtin_path + "sscsm", BUILTIN_MOD_NAME, "sscsm");
	addDir(builtin_path + "common", BUILTIN_MOD_NAME, "common");

	// parse all mods
	std::vector<std::string> modnames;
	m_modmgr->getModNames(modnames);

	for (const std::string &modname : modnames) {
		std::string path = m_modmgr->getModSpec(modname)->path;
		path = path + DIR_DELIM + "client";
		if (!fs::IsDir(path))
			// mod does not have a sscsm
			continue;
		verbosestream << "[Server] found sscsm for mod \"" << modname << "\"" << std::endl;
		m_mods->emplace_back(modname);
		addDir(path, modname, "");
	}

	// flush the z_stream (and add last buffer)
	m_zstream.avail_in = 0;
	do {
		ret = deflate(&m_zstream, Z_FINISH);
		if (ret < 0)
			throw SerializationError("SSCSMFileGrabber: deflate failed");

		if (m_zstream.avail_out == m_buffer_size)
			break;

		m_sscsm_files->emplace_back(m_out_buffer, m_buffer_size - m_zstream.avail_out);
		m_out_buffer = new u8[m_buffer_size];
		m_zstream.next_out = m_out_buffer;
		m_zstream.avail_out = m_buffer_size;

		if (ret == Z_STREAM_END)
			break;
	} while (true);

	// end the z_stream
	ret = deflateEnd(&m_zstream);
	if (ret < 0)
		throw SerializationError("SSCSMFileGrabber: deflateEnd failed");

	delete[] m_buffer;
}

void SSCSMFileGrabber::addDir(const std::string &server_path,
	const std::string &mod_name,
	const std::string &client_path)
{
	// get all subpaths (ignore if folder-/filename begins with '.')
	std::vector<std::string> subpaths;
	fs::GetRecursiveSubPaths(server_path, subpaths, true, {'.'});

	// add all files
	for (const std::string &file_path : subpaths) {
		if (fs::IsDir(file_path))
			continue;
#ifndef _WIN32 // DIR_DELIM is "/"
		std::string path = client_path +
			file_path.substr(server_path.length());
#else // DIR_DELIM is not "/"
		std::string path = client_path +
			str_replace(file_path.substr(server_path.length()), DIR_DELIM, "/");
#endif
		addFile(file_path, mod_name, path.erase(0, path.find_first_not_of("/")));
	}
}

void SSCSMFileGrabber::compressBuffer(u8 *buffer, u32 size)
{
	if (!size)
		return;

	m_zstream.next_in = buffer;
	m_zstream.avail_in = size;

	do {
		if (deflate(&m_zstream, Z_NO_FLUSH) < 0)
			throw SerializationError("SSCSMFileGrabber: deflate failed");

		if (m_zstream.avail_out > 0)
			break;

		// m_out_buffer is full, save it and continue compressing
		m_sscsm_files->emplace_back(m_out_buffer, m_buffer_size);
		m_out_buffer = new u8[m_buffer_size];
		m_zstream.next_out = m_out_buffer;
		m_zstream.avail_out = m_buffer_size;

	} while (m_zstream.avail_in > 0);
}

void SSCSMFileGrabber::addFile(const std::string &server_path,
	const std::string &mod_name,
	const std::string &client_path)
{
	verbosestream << "[Server] adding sscsm-file from " << server_path << " to " <<
			client_path << std::endl;
printf("Server adds mod \"%s\", file \"%s\"\n", mod_name.c_str(), client_path.c_str());
	// Read the file
	std::ifstream file(server_path, std::ios::binary | std::ios::ate);
	std::streamsize content_size = file.tellg();
	file.seekg(0, std::ios::beg);
	u8 *content_buffer = new u8[content_size];
	file.read((char*)content_buffer, content_size);
	file.close();

	// Write file Header
	u8 mod_length = mod_name.length();
	u8 path_length = client_path.length();
	u8 *header_buffer = new u8[6 + mod_length + path_length];

	writeU8(header_buffer, mod_length);
	const char *mod_name_c = mod_name.c_str();
	for (u8 i = 0; i < mod_length; i++)
		header_buffer[i + 1] = mod_name_c[i];

	writeU8(header_buffer + 1 + mod_length, path_length);
	const char *client_path_c = client_path.c_str();
	for (u8 i = 0; i < path_length; i++)
		header_buffer[i + 2 + mod_length] = client_path_c[i];
	writeU32(header_buffer + 2 + mod_length + path_length, content_size);
	compressBuffer(header_buffer, 6 + mod_length + path_length);

	delete[] header_buffer;

	// Write file content
	compressBuffer(content_buffer, content_size);

	delete[] content_buffer;
}
