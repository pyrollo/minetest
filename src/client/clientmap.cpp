/*
Minetest
Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

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

#include "clientmap.h"
#include "client.h"
#include "mapblock_mesh.h"
#include <IMaterialRenderer.h>
#include <matrix4.h>
#include "mapsector.h"
#include "mapblock.h"
#include "profiler.h"
#include "settings.h"
#include "camera.h"               // CameraModes
#include "util/basic_macros.h"
#include <algorithm>
#include "client/renderingengine.h"

ClientMap::ClientMap(
		Client *client,
		MapDrawControl &control,
		s32 id
):
	Map(client),
	scene::ISceneNode(RenderingEngine::get_scene_manager()->getRootSceneNode(),
		RenderingEngine::get_scene_manager(), id),
	m_client(client),
	m_control(control)
{
	m_box = aabb3f(-BS*1000000,-BS*1000000,-BS*1000000,
			BS*1000000,BS*1000000,BS*1000000);

	/* TODO: Add a callback function so these can be updated when a setting
	 *       changes.  At this point in time it doesn't matter (e.g. /set
	 *       is documented to change server settings only)
	 *
	 * TODO: Local caching of settings is not optimal and should at some stage
	 *       be updated to use a global settings object for getting thse values
	 *       (as opposed to the this local caching). This can be addressed in
	 *       a later release.
	 */
	m_cache_trilinear_filter  = g_settings->getBool("trilinear_filter");
	m_cache_bilinear_filter   = g_settings->getBool("bilinear_filter");
	m_cache_anistropic_filter = g_settings->getBool("anisotropic_filter");

}

MapSector * ClientMap::emergeSector(v2s16 p2d)
{
	// Check that it doesn't exist already
	MapSector *sector = getSectorNoGenerate(p2d);

	// Create it if it does not exist yet
	if (!sector) {
		sector = new MapSector(this, p2d, m_gamedef);
		m_sectors[p2d] = sector;
	}

	return sector;
}

void ClientMap::OnRegisterSceneNode()
{
	if(IsVisible)
	{
		SceneManager->registerNodeForRendering(this, scene::ESNRP_SOLID);
		SceneManager->registerNodeForRendering(this, scene::ESNRP_TRANSPARENT);
	}

	ISceneNode::OnRegisterSceneNode();
}

void ClientMap::getBlocksInViewRange(v3s16 cam_pos_nodes,
		v3s16 *p_blocks_min, v3s16 *p_blocks_max)
{
	v3s16 box_nodes_d = m_control.wanted_range * v3s16(1, 1, 1);
	// Define p_nodes_min/max as v3s32 because 'cam_pos_nodes -/+ box_nodes_d'
	// can exceed the range of v3s16 when a large view range is used near the
	// world edges.
	v3s32 p_nodes_min(
		cam_pos_nodes.X - box_nodes_d.X,
		cam_pos_nodes.Y - box_nodes_d.Y,
		cam_pos_nodes.Z - box_nodes_d.Z);
	v3s32 p_nodes_max(
		cam_pos_nodes.X + box_nodes_d.X,
		cam_pos_nodes.Y + box_nodes_d.Y,
		cam_pos_nodes.Z + box_nodes_d.Z);
	// Take a fair amount as we will be dropping more out later
	// Umm... these additions are a bit strange but they are needed.
	*p_blocks_min = v3s16(
			p_nodes_min.X / MAP_BLOCKSIZE - 3,
			p_nodes_min.Y / MAP_BLOCKSIZE - 3,
			p_nodes_min.Z / MAP_BLOCKSIZE - 3);
	*p_blocks_max = v3s16(
			p_nodes_max.X / MAP_BLOCKSIZE + 1,
			p_nodes_max.Y / MAP_BLOCKSIZE + 1,
			p_nodes_max.Z / MAP_BLOCKSIZE + 1);
}

void ClientMap::updateDrawList()
{
	ScopeProfiler sp(g_profiler, "CM::updateDrawList()", SPT_AVG);

	for (auto &i : m_drawlist) {
		MapBlock *block = i.second;
		block->refDrop();
	}
	m_drawlist.clear();

	const v3f camera_position = m_camera_position;
	const v3f camera_direction = m_camera_direction;
	const f32 camera_fov = m_camera_fov;

	v3s16 cam_pos_nodes = floatToInt(camera_position, BS);
	v3s16 p_blocks_min;
	v3s16 p_blocks_max;
	getBlocksInViewRange(cam_pos_nodes, &p_blocks_min, &p_blocks_max);

	// Number of blocks currently loaded by the client
	u32 blocks_loaded = 0;
	// Number of blocks with mesh in rendering range
	u32 blocks_in_range_with_mesh = 0;
	// Number of blocks occlusion culled
	u32 blocks_occlusion_culled = 0;

	// No occlusion culling when free_move is on and camera is
	// inside ground
	bool occlusion_culling_enabled = true;
	if (g_settings->getBool("free_move") && g_settings->getBool("noclip")) {
		MapNode n = getNode(cam_pos_nodes);
		if (n.getContent() == CONTENT_IGNORE ||
				m_nodedef->get(n).solidness == 2)
			occlusion_culling_enabled = false;
	}

	// Uncomment to debug occluded blocks in the wireframe mode
	// TODO: Include this as a flag for an extended debugging setting
	//if (occlusion_culling_enabled && m_control.show_wireframe)
	//    occlusion_culling_enabled = porting::getTimeS() & 1;

	for (const auto &sector_it : m_sectors) {
		MapSector *sector = sector_it.second;
		v2s16 sp = sector->getPos();

		blocks_loaded += sector->size();
		if (!m_control.range_all) {
			if (sp.X < p_blocks_min.X || sp.X > p_blocks_max.X ||
					sp.Y < p_blocks_min.Z || sp.Y > p_blocks_max.Z)
				continue;
		}

		MapBlockVect sectorblocks;
		sector->getBlocks(sectorblocks);

		/*
			Loop through blocks in sector
		*/

		u32 sector_blocks_drawn = 0;

		for (MapBlock *block : sectorblocks) {
			/*
				Compare block position to camera position, skip
				if not seen on display
			*/

			if (block->mesh) {
				block->mesh->updateCameraOffset(m_camera_offset);
			} else {
				// Ignore if mesh doesn't exist
				continue;
			}

			float range = 100000 * BS;
			if (!m_control.range_all)
				range = m_control.wanted_range * BS;

			float d = 0.0;
			if (!isBlockInSight(block->getPos(), camera_position,
					camera_direction, camera_fov, range, &d))
				continue;

			blocks_in_range_with_mesh++;

			/*
				Occlusion culling
			*/
			if ((!m_control.range_all && d > m_control.wanted_range * BS) ||
					(occlusion_culling_enabled && isBlockOccluded(block, cam_pos_nodes))) {
				blocks_occlusion_culled++;
				continue;
			}

			// This block is in range. Reset usage timer.
			block->resetUsageTimer();

			// Add to set
			block->refGrab();
			m_drawlist[block->getPos()] = block;

			sector_blocks_drawn++;
		} // foreach sectorblocks

		if (sector_blocks_drawn != 0)
			m_last_drawn_sectors.insert(sp);
	}

	g_profiler->avg("MapBlock meshes in range [#]", blocks_in_range_with_mesh);
	g_profiler->avg("MapBlocks occlusion culled [#]", blocks_occlusion_culled);
	g_profiler->avg("MapBlocks drawn [#]", m_drawlist.size());
	g_profiler->avg("MapBlocks loaded [#]", blocks_loaded);

// Impossible to resize texture. Beware, seems that texture size has to be a power of two.
// Plan A - Have enough size and/or limit size to a number of blocks.
//          Example : 1x262144 (eq 512x512) allows 1024 nodes square
//          Pros : only one texture to manage
//          Cons : limited range, many unused pixels (only few blocks in cube are visible)
//                 in shader, complex calculation to find data in texture
//
// Plan A+ - Same as plan A but blocks indexed by pos instead of being in a mattrix.
//           Pros : less data to store, less limitation on blocks quantity
//           Cons : very complex calculation to find data in texture

// Plan B - Have multiplke 9x9x9 textures, for each block, named (X,Y,Z)
//          Pros : every pixel is used, no waste, less memory
//                 very easy to retrieve data from shader
//          Cons : howto manage unused textures ?

// Nous ne sommes pas au bon endroit pour le plan B ! Les mesh sont déjà créées
/*
	IWritableTextureSource *tsrc = ((IWritableTextureSource *)m_client->getTextureSource());
	video::IVideoDriver *driver = RenderingEngine::get_video_driver();
	static u32 age = 0;

// 1 create texture
	video::ITexture *txt = tsrc->getTexture("__shaderPyrTexture");
	if (!txt) {
		printf("CREATE TEXTURE\n");

		video::IImage *image = driver->createImage(
			video::ECF_A8R8G8B8, core::dimension2d<u32>(1, 1));
		sanity_check(image != NULL);
		((IWritableTextureSource *)m_client->getTextureSource())->insertSourceImage("__shaderPyrTexture", image);
		image->drop();
		txt = tsrc->getTexture("__shaderPyrTexture");
	}
	sanity_check(txt != NULL);

// 2 update texture with lock/unlock

	u8 *pixels = (u8 *)txt->lock();
	pixels[0] = age;
	pixels[1] = age;
	pixels[2] = age;
	pixels[3] = age;

	txt->unlock();

	age+=32;
	if (age > 255) age = 0;
	printf("Age = %d\n", age);
*/
	if (g_settings->getBool("enable_shaders")) {
		IWritableTextureSource *tsrc = ((IWritableTextureSource *)m_client->getTextureSource());
		m_mapblock_texture_timestamp = porting::getTimeMs();
printf("new m_mapblock_texture_timestamp=%ld\n", m_mapblock_texture_timestamp);
		for (const auto &i : m_drawlist) {
			v3s16 pos = i.first;
			std::string tname = "__shaderMapblockTexture"
				+ std::to_string(pos.X) + "_"
				+ std::to_string(pos.Y) + "_"
				+ std::to_string(pos.Z);

			video::ITexture *txt = tsrc->getTexture(tname.c_str());
			sanity_check(txt != NULL);
			u64 age;
			if (i.second->m_display_timestamp) {
				age = m_mapblock_texture_timestamp - i.second->m_display_timestamp;
				if (age > 255)
					age = 255;
			} else {
				age = 1;
				i.second->m_display_timestamp = m_mapblock_texture_timestamp;
			}

if (pos.X == 0 && pos.Z == 0) {
	printf("clientmap blockage (y=%d)= %d\n", pos.Y, age);
}

			u8 *pixels = (u8 *)txt->lock();
			pixels[0] = age;
			pixels[1] = age;
			pixels[2] = age;
			pixels[3] = age;
			txt->unlock();
		}
	}
/*	{
		v3s16 min = v3s16 (S16_MAX, S16_MAX, S16_MAX);
		v3s16 max = v3s16 (S16_MIN, S16_MIN, S16_MIN);
		u64 now = porting::getTimeMs();

		// Compute offset and size
		for (const auto &i : m_drawlist) {
			v3s16 pos = i.first;
			if (pos.X > max.X) max.X = pos.X;
			if (pos.Y > max.Y) max.Y = pos.Y;
			if (pos.Z > max.Z) max.Z = pos.Z;
			if (pos.X < min.X) min.X = pos.X;
			if (pos.Y < min.Y) min.Y = pos.Y;
			if (pos.Z < min.Z) min.Z = pos.Z;
		}

		m_mapblockages.size = max - min;
		m_mapblockages.offset = min;

		if (m_mapblockages.ages) {
			free(m_mapblockages.ages);
			m_mapblockages.ages = nullptr;
		}

		u32 len = m_mapblockages.size.X * m_mapblockages.size.Y * m_mapblockages.size.Z;
		m_mapblockages.ages = new s32[len];
		// Fill ages (-1 = not loaded)
		for (int i = 0; i < len; i++) {
			m_mapblockages.ages[i] = -1;
		}

		// Create map
		for (const auto &i : m_drawlist) {
			v3s16 pos = i.first - m_mapblockages.offset;
			if (i.second)
 				m_mapblockages.ages[pos.X + pos.Y * m_mapblockages.size.Y + pos.Z * m_mapblockages.size.Z * m_mapblockages.size.Y]
					= now - i.second->getCreationTimestamp();
		}
	}
*/
}

struct MeshBufList
{
	video::SMaterial m;
	std::vector<scene::IMeshBuffer*> bufs;
};

struct MeshBufListList
{
	/*!
	 * Stores the mesh buffers of the world.
	 * The array index is the material's layer.
	 * The vector part groups vertices by material.
	 */
	std::vector<MeshBufList> lists[MAX_TILE_LAYERS];

	void clear()
	{
		for (auto &list : lists)
			list.clear();
	}

	void add(scene::IMeshBuffer *buf, u8 layer)
	{
		// Append to the correct layer
		std::vector<MeshBufList> &list = lists[layer];
		const video::SMaterial &m = buf->getMaterial();
		for (MeshBufList &l : list) {
			// comparing a full material is quite expensive so we don't do it if
			// not even first texture is equal
			if (l.m.TextureLayer[0].Texture != m.TextureLayer[0].Texture)
				continue;

			if (l.m == m) {
				l.bufs.push_back(buf);
				return;
			}
		}
		MeshBufList l;
		l.m = m;
		l.bufs.push_back(buf);
		list.push_back(l);
	}
};

void ClientMap::renderMap(video::IVideoDriver* driver, s32 pass)
{
	bool is_transparent_pass = pass == scene::ESNRP_TRANSPARENT;

	std::string prefix;
	if (pass == scene::ESNRP_SOLID)
		prefix = "renderMap(SOLID): ";
	else
		prefix = "renderMap(TRANSPARENT): ";

	/*
		This is called two times per frame, reset on the non-transparent one
	*/
	if (pass == scene::ESNRP_SOLID)
		m_last_drawn_sectors.clear();

	/*
		Get animation parameters
	*/
	const float animation_time = m_client->getAnimationTime();
	const int crack = m_client->getCrackLevel();
	const u32 daynight_ratio = m_client->getEnv().getDayNightRatio();

	const v3f camera_position = m_camera_position;
	const v3f camera_direction = m_camera_direction;
	const f32 camera_fov = m_camera_fov;

	/*
		Get all blocks and draw all visible ones
	*/

	u32 vertex_count = 0;

	// For limiting number of mesh animations per frame
	u32 mesh_animate_count = 0;
	//u32 mesh_animate_count_far = 0;

	/*
		Draw the selected MapBlocks
	*/

	MeshBufListList drawbufs;

	for (auto &i : m_drawlist) {
		MapBlock *block = i.second;

		// If the mesh of the block happened to get deleted, ignore it
		if (!block->mesh)
			continue;

		float d = 0.0;
		if (!isBlockInSight(block->getPos(), camera_position,
				camera_direction, camera_fov, 100000 * BS, &d))
			continue;

		// Mesh animation
		if (pass == scene::ESNRP_SOLID) {
			//MutexAutoLock lock(block->mesh_mutex);
			MapBlockMesh *mapBlockMesh = block->mesh;
			assert(mapBlockMesh);
			// Pretty random but this should work somewhat nicely
			bool faraway = d >= BS * 50;
			if (mapBlockMesh->isAnimationForced() || !faraway ||
					mesh_animate_count < (m_control.range_all ? 200 : 50)) {

				bool animated = mapBlockMesh->animate(faraway, animation_time,
					crack, daynight_ratio);
				if (animated)
					mesh_animate_count++;
			} else {
				mapBlockMesh->decreaseAnimationForceTimer();
			}
		}

		/*
			Get the meshbuffers of the block
		*/
		{
			//MutexAutoLock lock(block->mesh_mutex);

			MapBlockMesh *mapBlockMesh = block->mesh;
			assert(mapBlockMesh);

			for (int layer = 0; layer < MAX_TILE_LAYERS; layer++) {
				scene::IMesh *mesh = mapBlockMesh->getMesh(layer);
				assert(mesh);

				u32 c = mesh->getMeshBufferCount();
				for (u32 i = 0; i < c; i++) {
					scene::IMeshBuffer *buf = mesh->getMeshBuffer(i);

					video::SMaterial& material = buf->getMaterial();
					video::IMaterialRenderer* rnd =
						driver->getMaterialRenderer(material.MaterialType);
					bool transparent = (rnd && rnd->isTransparent());
					if (transparent == is_transparent_pass) {
						if (buf->getVertexCount() == 0)
							errorstream << "Block [" << analyze_block(block)
								<< "] contains an empty meshbuf" << std::endl;

						material.setFlag(video::EMF_TRILINEAR_FILTER,
							m_cache_trilinear_filter);
						material.setFlag(video::EMF_BILINEAR_FILTER,
							m_cache_bilinear_filter);
						material.setFlag(video::EMF_ANISOTROPIC_FILTER,
							m_cache_anistropic_filter);
						material.setFlag(video::EMF_WIREFRAME,
							m_control.show_wireframe);

						drawbufs.add(buf, layer);
					}
				}
			}
		}
	}

	TimeTaker draw("Drawing mesh buffers");

	// Render all layers in order
	for (auto &lists : drawbufs.lists) {
		for (MeshBufList &list : lists) {
			// Check and abort if the machine is swapping a lot
			if (draw.getTimerTime() > 2000) {
				infostream << "ClientMap::renderMap(): Rendering took >2s, " <<
						"returning." << std::endl;
				return;
			}
			driver->setMaterial(list.m);

			for (scene::IMeshBuffer *buf : list.bufs) {
				driver->drawMeshBuffer(buf);
				vertex_count += buf->getVertexCount();
			}
		}
	}
	g_profiler->avg(prefix + "draw meshes [ms]", draw.stop(true));

	// Log only on solid pass because values are the same
	if (pass == scene::ESNRP_SOLID) {
		g_profiler->avg("renderMap(): animated meshes [#]", mesh_animate_count);
	}

	g_profiler->avg(prefix + "vertices drawn [#]", vertex_count);
}

static bool getVisibleBrightness(Map *map, const v3f &p0, v3f dir, float step,
	float step_multiplier, float start_distance, float end_distance,
	const NodeDefManager *ndef, u32 daylight_factor, float sunlight_min_d,
	int *result, bool *sunlight_seen)
{
	int brightness_sum = 0;
	int brightness_count = 0;
	float distance = start_distance;
	dir.normalize();
	v3f pf = p0;
	pf += dir * distance;
	int noncount = 0;
	bool nonlight_seen = false;
	bool allow_allowing_non_sunlight_propagates = false;
	bool allow_non_sunlight_propagates = false;
	// Check content nearly at camera position
	{
		v3s16 p = floatToInt(p0 /*+ dir * 3*BS*/, BS);
		MapNode n = map->getNode(p);
		if(ndef->get(n).param_type == CPT_LIGHT &&
				!ndef->get(n).sunlight_propagates)
			allow_allowing_non_sunlight_propagates = true;
	}
	// If would start at CONTENT_IGNORE, start closer
	{
		v3s16 p = floatToInt(pf, BS);
		MapNode n = map->getNode(p);
		if(n.getContent() == CONTENT_IGNORE){
			float newd = 2*BS;
			pf = p0 + dir * 2*newd;
			distance = newd;
			sunlight_min_d = 0;
		}
	}
	for (int i=0; distance < end_distance; i++) {
		pf += dir * step;
		distance += step;
		step *= step_multiplier;

		v3s16 p = floatToInt(pf, BS);
		MapNode n = map->getNode(p);
		if (allow_allowing_non_sunlight_propagates && i == 0 &&
				ndef->get(n).param_type == CPT_LIGHT &&
				!ndef->get(n).sunlight_propagates) {
			allow_non_sunlight_propagates = true;
		}

		if (ndef->get(n).param_type != CPT_LIGHT ||
				(!ndef->get(n).sunlight_propagates &&
					!allow_non_sunlight_propagates)){
			nonlight_seen = true;
			noncount++;
			if(noncount >= 4)
				break;
			continue;
		}

		if (distance >= sunlight_min_d && !*sunlight_seen && !nonlight_seen)
			if (n.getLight(LIGHTBANK_DAY, ndef) == LIGHT_SUN)
				*sunlight_seen = true;
		noncount = 0;
		brightness_sum += decode_light(n.getLightBlend(daylight_factor, ndef));
		brightness_count++;
	}
	*result = 0;
	if(brightness_count == 0)
		return false;
	*result = brightness_sum / brightness_count;
	/*std::cerr<<"Sampled "<<brightness_count<<" points; result="
			<<(*result)<<std::endl;*/
	return true;
}

int ClientMap::getBackgroundBrightness(float max_d, u32 daylight_factor,
		int oldvalue, bool *sunlight_seen_result)
{
	ScopeProfiler sp(g_profiler, "CM::getBackgroundBrightness", SPT_AVG);
	static v3f z_directions[50] = {
		v3f(-100, 0, 0)
	};
	static f32 z_offsets[sizeof(z_directions)/sizeof(*z_directions)] = {
		-1000,
	};

	if(z_directions[0].X < -99){
		for(u32 i=0; i<sizeof(z_directions)/sizeof(*z_directions); i++){
			// Assumes FOV of 72 and 16/9 aspect ratio
			z_directions[i] = v3f(
				0.02 * myrand_range(-100, 100),
				1.0,
				0.01 * myrand_range(-100, 100)
			).normalize();
			z_offsets[i] = 0.01 * myrand_range(0,100);
		}
	}

	int sunlight_seen_count = 0;
	float sunlight_min_d = max_d*0.8;
	if(sunlight_min_d > 35*BS)
		sunlight_min_d = 35*BS;
	std::vector<int> values;
	for(u32 i=0; i<sizeof(z_directions)/sizeof(*z_directions); i++){
		v3f z_dir = z_directions[i];
		core::CMatrix4<f32> a;
		a.buildRotateFromTo(v3f(0,1,0), z_dir);
		v3f dir = m_camera_direction;
		a.rotateVect(dir);
		int br = 0;
		float step = BS*1.5;
		if(max_d > 35*BS)
			step = max_d / 35 * 1.5;
		float off = step * z_offsets[i];
		bool sunlight_seen_now = false;
		bool ok = getVisibleBrightness(this, m_camera_position, dir,
				step, 1.0, max_d*0.6+off, max_d, m_nodedef, daylight_factor,
				sunlight_min_d,
				&br, &sunlight_seen_now);
		if(sunlight_seen_now)
			sunlight_seen_count++;
		if(!ok)
			continue;
		values.push_back(br);
		// Don't try too much if being in the sun is clear
		if(sunlight_seen_count >= 20)
			break;
	}
	int brightness_sum = 0;
	int brightness_count = 0;
	std::sort(values.begin(), values.end());
	u32 num_values_to_use = values.size();
	if(num_values_to_use >= 10)
		num_values_to_use -= num_values_to_use/2;
	else if(num_values_to_use >= 7)
		num_values_to_use -= num_values_to_use/3;
	u32 first_value_i = (values.size() - num_values_to_use) / 2;

	for (u32 i=first_value_i; i < first_value_i + num_values_to_use; i++) {
		brightness_sum += values[i];
		brightness_count++;
	}

	int ret = 0;
	if(brightness_count == 0){
		MapNode n = getNode(floatToInt(m_camera_position, BS));
		if(m_nodedef->get(n).param_type == CPT_LIGHT){
			ret = decode_light(n.getLightBlend(daylight_factor, m_nodedef));
		} else {
			ret = oldvalue;
		}
	} else {
		ret = brightness_sum / brightness_count;
	}

	*sunlight_seen_result = (sunlight_seen_count > 0);
	return ret;
}

void ClientMap::renderPostFx(CameraMode cam_mode)
{
	// Sadly ISceneManager has no "post effects" render pass, in that case we
	// could just register for that and handle it in renderMap().

	MapNode n = getNode(floatToInt(m_camera_position, BS));

	// - If the player is in a solid node, make everything black.
	// - If the player is in liquid, draw a semi-transparent overlay.
	// - Do not if player is in third person mode
	const ContentFeatures& features = m_nodedef->get(n);
	video::SColor post_effect_color = features.post_effect_color;
	if(features.solidness == 2 && !(g_settings->getBool("noclip") &&
			m_client->checkLocalPrivilege("noclip")) &&
			cam_mode == CAMERA_MODE_FIRST)
	{
		post_effect_color = video::SColor(255, 0, 0, 0);
	}
	if (post_effect_color.getAlpha() != 0)
	{
		// Draw a full-screen rectangle
		video::IVideoDriver* driver = SceneManager->getVideoDriver();
		v2u32 ss = driver->getScreenSize();
		core::rect<s32> rect(0,0, ss.X, ss.Y);
		driver->draw2DRectangle(post_effect_color, rect);
	}
}

void ClientMap::PrintInfo(std::ostream &out)
{
	out<<"ClientMap: ";
}


