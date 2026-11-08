#include <editor_common.h>
#include "thumbnail_cache.h"

#include <algorithm>
#include <cstring>

#include <imgui.h>
#include <stb_image.h>

#include <assets/native_anim_files.h>
#include <assets/wasset_io.h>
#include <core/engine.h>
#include <renderer/render_module.h>
#include <renderer/view/view_camera.h>
#include <scene/components.h>
#include <scene/preview_scene.h>

#include <asset_pipeline/asset_database.h>
#include <asset_pipeline/file_utils.h>
#include <panels/png_writer.h>

#include "asset_preview.h"
#include "preview_mesh_resolver.h"

using wiwa::LogLevel; // for WI_LOG
#define WI_THUMB_LOG(level, ...) WI_LOG(level, "Thumbnails", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr float FOV = 35.0f;
		constexpr int MAX_JOB_FRAMES = 900;
		constexpr int KEYS_PER_FRAME = 6;

		u64 fnv(u64 hash, const void* data, size_t size)
		{
			const unsigned char* bytes = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < size; i++)
			{
				hash = (hash ^ bytes[i]) * 1099511628211ull;
			}
			return hash;
		}

		u64 fnv_file(u64 hash, const std::filesystem::path& file)
		{
			if (const std::optional<std::vector<std::byte>> bytes = read_binary_file(file))
			{
				return fnv(hash, bytes->data(), bytes->size());
			}
			return fnv(hash, "missing", 7);
		}

		std::string hex16(u64 value)
		{
			char text[17];
			std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
			return text;
		}

		AssetManager* assets() { return Engine::get().get_module<AssetManager>(); }
		AnimationModule* animation() { return Engine::get().get_module<AnimationModule>(); }
		RenderModule* renderer() { return Engine::get().get_module<RenderModule>(); }
	}

	ThumbnailCache::ThumbnailCache(AssetDatabase& database, SkeletonIndex& index, EditorState& state)
		: m_Database(database), m_Index(index), m_State(state), m_Folder(database.get_library_root() / "thumbnails")
	{
		trim_disk();
	}

	ThumbnailCache::~ThumbnailCache()
	{
		m_Job.reset();
		m_View = RenderView{}; // the handle goes before the scene it shows
		if (AnimationModule* module = animation())
		{
			module->unregister_preview_scene(m_Scene);
		}
		for (auto& [key, entry] : m_Entries)
		{
			release_texture(entry);
		}
	}

	bool ThumbnailCache::handles(const std::filesystem::path& source)
	{
		const std::filesystem::path extension = source.extension();
		return extension == ".wskel" || extension == ".wanim" || extension == ".wmesh";
	}

	// The oldest PNGs go when the folder is over its limit.
	void ThumbnailCache::trim_disk() const
	{
		struct File
		{
			std::filesystem::path Path;
			std::filesystem::file_time_type Time;
			u64 Size;
		};
		std::vector<File> files;
		u64 total = 0;
		std::error_code error;
		for (std::filesystem::directory_iterator it(m_Folder, error), end; !error && it != end; it.increment(error))
		{
			std::error_code fileError;
			if (it->is_regular_file(fileError))
			{
				const u64 size = it->file_size(fileError);
				files.push_back(File{ it->path(), it->last_write_time(fileError), size });
				total += size;
			}
		}
		if (total <= MAX_DISK_BYTES)
		{
			return;
		}
		std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.Time < b.Time; });
		for (const File& file : files)
		{
			if (total <= MAX_DISK_BYTES)
			{
				break;
			}
			std::error_code removed;
			std::filesystem::remove(file.Path, removed);
			total -= std::min(total, file.Size);
		}
	}

	ThumbnailCache::Entry& ThumbnailCache::entry_for(const std::filesystem::path& source)
	{
		const std::string key = source.string();
		const auto found = m_Entries.find(key);
		if (found != m_Entries.end())
		{
			return found->second;
		}
		Entry entry;
		entry.Source = source;
		const std::filesystem::path extension = source.extension();
		entry.Type = extension == ".wskel" ? Kind::Skeleton : (extension == ".wanim" ? Kind::Clip : Kind::Mesh);
		return m_Entries.emplace(key, std::move(entry)).first->second;
	}

	std::filesystem::path ThumbnailCache::file_for(const Entry& entry) const
	{
		return m_Folder / (entry.Ref.Guid.to_string() + "-" + hex16(entry.Key) + ".png");
	}

	// What the thumbnail is made from, and the hash of those files. False while that cannot be known yet (the catalog has not got the file, the
	// index has not scanned).
	bool ThumbnailCache::make_key(Entry& entry)
	{
		AssetManager* manager = assets();
		if (manager == nullptr || !m_Index.ready())
		{
			return false;
		}
		const std::optional<AssetRef> self = manager->find_asset(entry.Source.string());
		if (!self)
		{
			return false;
		}
		entry.Ref = *self;
		std::error_code time;
		entry.FileTime = std::filesystem::last_write_time(entry.Source, time);

		const std::optional<std::vector<std::byte>> bytes = read_binary_file(entry.Source);
		if (!bytes)
		{
			return false;
		}
		std::string reason;
		AssetRef skeleton{};
		entry.Mesh = {};
		entry.Clip = {};
		switch (entry.Type)
		{
		case Kind::Skeleton:
			skeleton = entry.Ref;
			break;
		case Kind::Clip:
			if (const std::optional<AnimationClipData> clip = read_clip_file(*bytes, reason))
			{
				skeleton = clip->Skeleton.valid() ? m_Database.redirected(clip->Skeleton) : AssetRef{};
			}
			entry.Clip = entry.Ref;
			break;
		case Kind::Mesh:
			if (const std::optional<MeshData> mesh = read_mesh_file(*bytes, reason))
			{
				skeleton = mesh->Skeleton.valid() ? m_Database.redirected(mesh->Skeleton) : AssetRef{};
			}
			entry.Mesh = entry.Ref;
			break;
		}
		entry.Skeleton = skeleton;
		if (entry.Type != Kind::Mesh && skeleton.valid())
		{
			entry.Mesh = resolve_preview_mesh(*manager, m_Index, m_State, skeleton.Guid).value_or(AssetRef{});
		}

		u64 hash = 14695981039346656037ull;
		const u32 versions[2] = { VERSION, WASSET_VERSION };
		hash = fnv(hash, versions, sizeof(versions));
		hash = fnv(hash, bytes->data(), bytes->size());
		for (const AssetRef& dependency : { entry.Skeleton, entry.Type == Kind::Mesh ? AssetRef{} : entry.Mesh })
		{
			if (dependency.valid())
			{
				if (const std::optional<std::filesystem::path> path = m_Database.path_of(dependency.Guid))
				{
					hash = fnv_file(hash, *path);
				}
			}
		}
		entry.Key = hash;
		return true;
	}

	bool ThumbnailCache::load_from_disk(Entry& entry)
	{
		const std::filesystem::path file = file_for(entry);
		std::error_code exists;
		if (!std::filesystem::exists(file, exists))
		{
			return false;
		}
		int width = 0;
		int height = 0;
		int channels = 0;
		unsigned char* pixels = stbi_load(file.string().c_str(), &width, &height, &channels, 4);
		RenderModule* render = renderer();
		if (pixels == nullptr || width <= 0 || height <= 0 || render == nullptr)
		{
			if (pixels != nullptr)
			{
				stbi_image_free(pixels);
			}
			// a damaged file: it is made again
			std::error_code removed;
			std::filesystem::remove(file, removed);
			return false;
		}
		ImageData image;
		image.Width = static_cast<u32>(width);
		image.Height = static_cast<u32>(height);
		image.Rgba.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
		stbi_image_free(pixels);
		entry.Texture = render->create_ui_image(image);
		if (entry.Texture == 0)
		{
			return false;
		}
		// the PNG gets a fresh time: the oldest are the ones that go when the folder is full
		std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now(), exists);
		return true;
	}

	u32 ThumbnailCache::request(const std::filesystem::path& source)
	{
		if (!handles(source))
		{
			return 0;
		}
		Entry& entry = entry_for(source);
		entry.LastRequested = m_Frame;

		// the file may have changed since: looked at now and then, not every frame of every cell
		if (m_Frame >= entry.NextCheck)
		{
			entry.NextCheck = m_Frame + 90;
			std::error_code time;
			const std::filesystem::file_time_type now = std::filesystem::last_write_time(source, time);
			if (!time && entry.Status != State::New && now != entry.FileTime)
			{
				release_texture(entry);
				entry.Status = State::New;
			}
		}

		if (entry.Status == State::New && m_KeysThisFrame < KEYS_PER_FRAME)
		{
			m_KeysThisFrame++;
			if (make_key(entry))
			{
				if (load_from_disk(entry))
				{
					entry.Status = State::Ready;
				}
				else
				{
					entry.Status = State::Queued;
					m_Queue.push_back(source.string());
				}
			}
		}
		return entry.Status == State::Ready ? entry.Texture : 0;
	}

	void ThumbnailCache::release_texture(Entry& entry)
	{
		if (entry.Texture != 0)
		{
			if (RenderModule* render = renderer())
			{
				render->release_ui_image(entry.Texture);
			}
			entry.Texture = 0;
		}
	}

	void ThumbnailCache::fail(const std::filesystem::path& source, const char* why)
	{
		WI_THUMB_LOG(Warning, "No thumbnail for {0}: {1}", source.filename().string(), why);
		const auto found = m_Entries.find(source.string());
		if (found != m_Entries.end())
		{
			found->second.Status = State::Failed; // until the file changes
		}
	}

	void ThumbnailCache::begin(Entry& entry)
	{
		AssetManager* manager = assets();
		AnimationModule* module = animation();
		RenderModule* render = renderer();
		if (manager == nullptr || module == nullptr || render == nullptr || !render->is_ready())
		{
			entry.Status = State::Queued;
			m_Queue.push_back(entry.Source.string());
			return;
		}
		if (!entry.Skeleton.valid())
		{
			fail(entry.Source, "the file names no skeleton");
			return;
		}
		if (m_Scene == nullptr)
		{
			m_Scene = make_preview_scene();
			module->register_preview_scene(m_Scene);
		}
		if (!m_View.valid())
		{
			ViewDesc description;
			description.Width = SIZE;
			description.Height = SIZE;
			description.Profile = ViewProfile::Thumbnail;
			description.TransparentBackground = true;
			description.Mode = ViewMode::OneShot;
			m_View = render->create_view(description);
			if (!m_View.valid())
			{
				fail(entry.Source, "no view could be made");
				return;
			}
			m_View.set_scene(m_Scene);
		}

		Job job;
		job.Source = entry.Source;
		job.Key = entry.Key;
		job.Skeleton = entry.Skeleton;
		job.Mesh = entry.Mesh;
		job.Clip = entry.Clip;
		job.SkeletonHandle = manager->load<SkeletonData>(job.Skeleton);
		job.MeshHandle = job.Mesh.valid() ? manager->load<MeshData>(job.Mesh) : Handle<MeshData>{};
		job.ClipHandle = job.Clip.valid() ? manager->load<AnimationClipData>(job.Clip) : Handle<AnimationClipData>{};
		job.Entity = make_preview_entity(*m_Scene, job.Skeleton, job.Mesh, job.Clip);
		entry.Status = State::Working;
		m_Job = std::move(job);
	}

	// Lines and dots of the pose on a transparent image, for a skeleton no mesh uses.
	ImageData ThumbnailCache::draw_bones(const AnimatorInstance& instance, const RenderCamera& camera) const
	{
		ImageData image;
		image.Width = SIZE;
		image.Height = SIZE;
		image.Rgba.assign(static_cast<size_t>(SIZE) * SIZE * 4, 0);

		const std::vector<glm::mat4>& model = instance.model();
		const SkeletonData& skeleton = *instance.skeleton();
		std::vector<glm::vec2> points(model.size());
		std::vector<bool> visible(model.size(), false);
		const glm::mat4 viewProjection = camera.Projection * camera.View;
		for (size_t i = 0; i < model.size(); i++)
		{
			const glm::vec4 clip = viewProjection * glm::vec4(glm::vec3(model[i][3]), 1.0f);
			if (clip.w > 1e-4f)
			{
				points[i] = glm::vec2((clip.x / clip.w * 0.5f + 0.5f) * SIZE, (0.5f - clip.y / clip.w * 0.5f) * SIZE);
				visible[i] = true;
			}
		}
		const auto plot = [&](int x, int y, const unsigned char color[4])
		{
			if (x >= 0 && y >= 0 && x < static_cast<int>(SIZE) && y < static_cast<int>(SIZE))
			{
				std::memcpy(&image.Rgba[(static_cast<size_t>(y) * SIZE + static_cast<size_t>(x)) * 4], color, 4);
			}
		};
		static const unsigned char line[4] = { 255, 210, 90, 255 };
		static const unsigned char dot[4] = { 255, 245, 190, 255 };
		for (size_t i = 0; i < model.size() && i < skeleton.Parents.size(); i++)
		{
			const i32 parent = skeleton.Parents[i];
			if (parent < 0 || !visible[i] || !visible[static_cast<size_t>(parent)])
			{
				continue;
			}
			const glm::vec2 from = points[static_cast<size_t>(parent)];
			const glm::vec2 to = points[i];
			const int steps = std::max(1, static_cast<int>(std::max(std::abs(to.x - from.x), std::abs(to.y - from.y))));
			for (int s = 0; s <= steps; s++)
			{
				const glm::vec2 at = from + (to - from) * (static_cast<float>(s) / static_cast<float>(steps));
				plot(static_cast<int>(at.x), static_cast<int>(at.y), line);
				plot(static_cast<int>(at.x) + 1, static_cast<int>(at.y), line);
			}
		}
		for (size_t i = 0; i < model.size(); i++)
		{
			if (visible[i])
			{
				for (int dy = -1; dy <= 1; dy++)
				{
					for (int dx = -1; dx <= 1; dx++)
					{
						plot(static_cast<int>(points[i].x) + dx, static_cast<int>(points[i].y) + dy, dot);
					}
				}
			}
		}
		return image;
	}

	void ThumbnailCache::finish(const std::filesystem::path& source, u64 key, const ImageData& image)
	{
		const auto found = m_Entries.find(source.string());
		if (found == m_Entries.end())
		{
			return;
		}
		Entry& entry = found->second;
		if (entry.Key != key)
		{
			entry.Status = State::New; // the files changed while it was made: it starts again
			return;
		}

		// the PNG, and no older ones of this asset
		std::error_code error;
		std::filesystem::create_directories(m_Folder, error);
		const std::string prefix = entry.Ref.Guid.to_string() + "-";
		for (std::filesystem::directory_iterator it(m_Folder, error), end; !error && it != end; it.increment(error))
		{
			const std::string name = it->path().filename().string();
			if (name.rfind(prefix, 0) == 0)
			{
				std::error_code removed;
				std::filesystem::remove(it->path(), removed);
			}
		}
		if (!png_writer::write_rgba(file_for(entry), image.Width, image.Height, image.Rgba))
		{
			WI_THUMB_LOG(Warning, "Could not write the thumbnail of {0}", source.filename().string());
		}

		RenderModule* render = renderer();
		release_texture(entry);
		entry.Texture = render != nullptr ? render->create_ui_image(image) : 0;
		entry.Status = entry.Texture != 0 ? State::Ready : State::Failed;
	}

	void ThumbnailCache::end_job()
	{
		if (m_Job && m_Scene != nullptr && m_Scene->registry().valid(m_Job->Entity))
		{
			m_Scene->destroy(m_Job->Entity);
		}
		m_Job.reset();
	}

	void ThumbnailCache::step_job()
	{
		Job& job = *m_Job;
		const auto found = m_Entries.find(job.Source.string());
		if (found == m_Entries.end() || found->second.Key != job.Key)
		{
			if (found != m_Entries.end())
			{
				found->second.Status = State::New;
			}
			end_job();
			return;
		}
		if (++job.Frames > MAX_JOB_FRAMES)
		{
			fail(job.Source, "its assets did not load in time");
			end_job();
			return;
		}
		if (job.ReadBack)
		{
			return; // waiting for the pixels
		}

		AnimationModule* module = animation();
		if (!job.Posed)
		{
			if (job.SkeletonHandle.state() == LoadState::Failed || (job.Mesh.valid() && job.MeshHandle.state() == LoadState::Failed)
				|| (job.Clip.valid() && job.ClipHandle.state() == LoadState::Failed))
			{
				fail(job.Source, "an asset it needs failed to load");
				end_job();
				return;
			}
			AnimatorInstance* instance = module != nullptr ? module->instance_in(m_Scene.get(), job.Entity) : nullptr;
			const bool loaded = job.SkeletonHandle.ready() && (!job.Mesh.valid() || job.MeshHandle.ready()) && (!job.Clip.valid() || job.ClipHandle.ready());
			if (!loaded || instance == nullptr || !instance->ready() || instance->model().empty())
			{
				return;
			}
			// the first frame of the clip (or the bind pose): held still
			module->set_preview_paused_in(m_Scene.get(), job.Entity, true);
			instance->seek(0.0f);
			job.Posed = true;
			job.Settled = 0;
			return;
		}

		AnimatorInstance* instance = module != nullptr ? module->instance_in(m_Scene.get(), job.Entity) : nullptr;
		if (instance == nullptr || !instance->ready())
		{
			return;
		}

		// the camera looks at the joints (they are in the space the mesh is drawn in), from far enough to see them all
		glm::vec3 low(1e9f);
		glm::vec3 high(-1e9f);
		for (const glm::mat4& model : instance->model())
		{
			low = glm::min(low, glm::vec3(model[3]));
			high = glm::max(high, glm::vec3(model[3]));
		}
		const float radius = std::max(glm::length(high - low) * 0.5f * 1.35f, 0.1f);
		const glm::vec3 target = (low + high) * 0.5f;
		const float distance = radius / std::sin(glm::radians(FOV * 0.5f)) * 1.1f;
		const RenderCamera camera = make_orbit_camera(target, distance, 25.0f, 10.0f, FOV, 1.0f);

		if (!job.Mesh.valid())
		{
			if (++job.Settled < 3)
			{
				return; // the pose follows the seek with the next update
			}
			const ImageData image = draw_bones(*instance, camera);
			const std::filesystem::path source = job.Source;
			const u64 key = job.Key;
			end_job();
			finish(source, key, image);
			return;
		}

		m_View.set_camera(camera);
		if (++job.Settled < 8)
		{
			return; // the render thread gets the pose, the camera and the mesh
		}
		job.ReadBack = true;
		m_View.request_render();
		m_View.read_back([this, source = job.Source, key = job.Key](ImageData image)
			{
				if (!m_Job || m_Job->Source != source || m_Job->Key != key)
				{
					return;
				}
				bool drawn = false;
				for (size_t i = 3; i < image.Rgba.size(); i += 4)
				{
					drawn = drawn || image.Rgba[i] > 8;
				}
				if (!drawn && m_Job->Attempts < 4)
				{
					// nothing yet (the mesh was still going to the GPU): once more a little later
					m_Job->Attempts++;
					m_Job->ReadBack = false;
					m_Job->Settled = 0;
					return;
				}
				end_job();
				if (drawn)
				{
					finish(source, key, image);
				}
				else
				{
					fail(source, "the view drew nothing");
				}
			});
	}

	void ThumbnailCache::update()
	{
		m_Frame++;
		m_KeysThisFrame = 0;

		if (m_Job)
		{
			step_job();
		}
		// the next render: the oldest of the cells that are still on screen
		while (!m_Job && !m_Queue.empty())
		{
			const std::string key = m_Queue.front();
			m_Queue.erase(m_Queue.begin());
			const auto found = m_Entries.find(key);
			if (found == m_Entries.end() || found->second.Status != State::Queued)
			{
				continue;
			}
			if (found->second.LastRequested < m_Frame - 2)
			{
				found->second.Status = State::New; // it scrolled out of view: asked for again when it is back
				continue;
			}
			begin(found->second);
		}

		// too many on the GPU: the ones not shown for the longest give theirs back (their PNG is still there)
		size_t live = 0;
		for (const auto& [key, entry] : m_Entries)
		{
			live += entry.Texture != 0 ? 1 : 0;
		}
		if (live > MAX_TEXTURES)
		{
			std::vector<Entry*> idle;
			for (auto& [key, entry] : m_Entries)
			{
				if (entry.Texture != 0 && entry.LastRequested < m_Frame - 120)
				{
					idle.push_back(&entry);
				}
			}
			std::sort(idle.begin(), idle.end(), [](const Entry* a, const Entry* b) { return a->LastRequested < b->LastRequested; });
			for (Entry* entry : idle)
			{
				if (live <= MAX_TEXTURES)
				{
					break;
				}
				release_texture(*entry);
				entry->Status = State::New;
				live--;
			}
		}
	}
}
