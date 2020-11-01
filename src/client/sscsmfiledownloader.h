
#pragma once

#include <vector>
#include <queue>
#include "util/string.h"
#include "util/numeric.h"
#include "zlib.h"

class SSCSMFileDownloader
{
public:
	SSCSMFileDownloader(u32 bunches_count, StringMap *vfs);

	~SSCSMFileDownloader();

	/*
	 * Returns true when finished
	 */
	bool addBunch(u32 i, u8 *buffer, u32 size);

private:
	struct bunch
	{
		bunch(u32 ai, u8 *abuffer, u32 asize) :
			i(ai), buffer(abuffer), size(asize)
		{
		}

		u32 i;
		u8 *buffer;
		u32 size;
	};

	class comp
	{
	public:
		bool operator()(bunch const &a, bunch const &b) {
			return a.i > b.i;
		}
	};

	void readBunches();

	enum phase {
		mod_len,
		mod_string,
		path_len,
		path_string,
		file_size,
		file_content
	};

	std::priority_queue<bunch, std::vector<bunch>, comp> m_bunches;
	u32 m_bunches_count;
	u32 m_next_bunch_index;

	StringMap *m_script_vfs; // Where to store received scripts

	z_stream m_zstream;

	u8 *m_buffer;
	const u32 m_buffer_size = 1024; // this has to be at least 256 to be able to hold the file path

	std::stringstream m_current_file_content;
	std::string m_current_file_path;
	std::string m_current_mod_name;
	u32 m_read_length;

	u64 m_remaining_disk_space;

	phase m_phase;
};
