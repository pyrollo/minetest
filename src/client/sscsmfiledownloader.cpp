
#include "sscsmfiledownloader.h"
#include "porting.h"
#include "util/serialize.h"
#include "filesys.h"
#include <fstream>
#include "settings.h"
#include "exceptions.h"

/*
 * data in decompressed buffers:
 * for each file {
 *     u8 mod_length
 *     chars mod
 *     u8 path_length
 *     chars path
 *     u32 file size
 *     chars file
 * }
 */


SSCSMFileDownloader::SSCSMFileDownloader(u32 bunches_count, StringMap *vfs) :
	m_bunches(), m_bunches_count(bunches_count), m_next_bunch_index(0),
	m_script_vfs(vfs), m_current_file_path(""), m_read_length(0)
{
	m_remaining_disk_space = g_settings->getU64("sscsm_file_size_limit");

	m_buffer = new u8[m_buffer_size];
	m_phase = mod_len;

	// initaialize the z_stream
	m_zstream.zalloc = Z_NULL;
	m_zstream.zfree = Z_NULL;
	m_zstream.opaque = Z_NULL;

	int ret = inflateInit(&m_zstream);
	if (ret < 0)
		throw SerializationError("SSCSMFileDownloader: inflateInit failed");

	m_zstream.avail_in = 0;

	fs::CreateAllDirs(porting::path_cache + DIR_DELIM + "sscsm");
}

SSCSMFileDownloader::~SSCSMFileDownloader()
{
	delete[] m_buffer;
}

bool SSCSMFileDownloader::addBunch(u32 i, u8 *buffer, u32 size)
{
	// emplace into priority queue to ensure that the bunches are read in the
	// correct order
	m_bunches.emplace(i, buffer, size);

	readBunches();

	if (m_next_bunch_index < m_bunches_count)
		return false;

	// all bunches are added

	// todo: verbosestream
	actionstream << "[Client] finished downloading sscsm files" << std::endl;

	// todo: get last buffer? (no, it will be empty)

	// end z_stream
	int ret = inflateEnd(&m_zstream);
	if (ret < 0) {
		throw SerializationError("SSCSMFileDownloader: inflateEnd failed");
	}

	return true;
}

void SSCSMFileDownloader::readBunches()
{
	while (!m_bunches.empty() && m_bunches.top().i <= m_next_bunch_index) {
		// Read a single bunch
		const bunch &b = m_bunches.top();

		int ret = 0;

		m_zstream.next_in = b.buffer;
		m_zstream.avail_in = b.size;

		while (m_zstream.avail_in > 0) {
			m_zstream.next_out = m_buffer;

			switch(m_phase) {
			case mod_len:
			case path_len:
				m_zstream.avail_out = 1;
				ret = inflate(&m_zstream, Z_SYNC_FLUSH);
				if (ret < 0)
					throw SerializationError("SSCSMFileDownloader: inflate failed (phase len)");

				if (m_zstream.avail_out)
					break;

				m_zstream.avail_out = readU8(m_buffer);
				if (m_phase == mod_len)
					m_current_mod_name = "";
				else
					m_current_file_path = "";

				m_phase = (phase)(m_phase + 1);
				break;

			case mod_string:
			case path_string:
			{
				u32 previous_out = m_zstream.avail_out;
				ret = inflate(&m_zstream, Z_SYNC_FLUSH);
				if (ret < 0)
					throw SerializationError("SSCSMFileDownloader: inflate failed (phase string)");

				if (m_phase == mod_string)
					m_current_mod_name.append((char *)m_buffer, previous_out - m_zstream.avail_out);
				else
					m_current_file_path.append((char *)m_buffer, previous_out - m_zstream.avail_out);

				if (m_zstream.avail_out)
					break;

				m_phase = (phase)(m_phase + 1);
				break;
			}

			case file_size:
				m_zstream.avail_out = 4; // u32
				// decompress the file length into buffer
				ret = inflate(&m_zstream, Z_SYNC_FLUSH);
				if (ret < 0)
					throw SerializationError("SSCSMFileDownloader: inflate failed (phase file_size)");

				if (m_zstream.avail_out == 0) {
					// the file length was read
					printf("Creating server sent script \"%s\" for mod \"%s\"\n", m_current_file_path.c_str(), m_current_mod_name.c_str());
					(*m_script_vfs)[m_current_mod_name + ":" +
							m_current_file_path] = "";

					m_read_length = readU32(m_buffer);
					if (m_read_length == 0) {
						// empty file, next file
						m_phase = mod_len;
						continue;
					}
				} else {
					 throw SerializationError("SSCSMFileDownloader: could not get 4 bytes file size");
				}
				m_phase = file_content;
				break;

			case file_content:
				m_zstream.avail_out = MYMIN(m_read_length, m_buffer_size);
				ret = inflate(&m_zstream, Z_SYNC_FLUSH);
				if (ret < 0)
					throw SerializationError("SSCSMFileDownloader: inflate failed (phase file_content)");

				u32 readc = MYMIN(m_read_length, m_buffer_size) - m_zstream.avail_out;

				if (readc) {
					// append to the file
					if (m_remaining_disk_space < readc) {
						// todo: SerializationError is probably not correct
						// todo: give more information (newlines?)
						throw SerializationError("There was too much SSCSM file data.");
					}

					m_remaining_disk_space -= readc;
					(*m_script_vfs)[m_current_mod_name + ":" +
							m_current_file_path].append((char *)(m_buffer), readc);
					m_read_length -= readc;
				}
				if (m_read_length)
					break;

				// Next file
				m_phase = mod_len;
				break;
			}
		} // while z data

		delete[] b.buffer;
		m_bunches.pop();

		// Read the next bunch
		m_next_bunch_index++;

	} // while bunches
}
