#include <editor_common.h>
#include "thumbnail_cache.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <imgui.h>
#include <stb_image.h>

#include <assets/builtin_assets.h>
#include <assets/native_anim_files.h>
#include <assets/wasset_io.h>
#include <core/engine.h>
#include <renderer/render_module.h>
#include <renderer/view/view_camera.h>
#include <scene/components.h>
#include <scene/preview_scene.h>

#include <asset_pipeline/asset_database.h>
#include <asset_pipeline/file_utils.h>
#include <asset_pipeline/material_file.h>
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

		constexpr int MAX_ZOOMS = 5;        // times the camera backs off when the picture touches its border
		constexpr float ZOOM_STEP = 1.3f;
		constexpr u32 BORDER = 3;           // pixels at the border that must stay empty
		constexpr int STATIC_SETTLE_FRAMES = 30; // a material's textures and a prefab's meshes are still going to the GPU for a while

		// Float helpers of the CPU thumbnails.
		float half_to_float(u16 half)
		{
			const u32 sign = (half >> 15) & 1u;
			const u32 exponent = (half >> 10) & 31u;
			const u32 mantissa = half & 1023u;
			float value;
			if (exponent == 0)
			{
				value = std::ldexp(static_cast<float>(mantissa), -24);
			}
			else if (exponent == 31)
			{
				value = 65504.0f;
			}
			else
			{
				value = std::ldexp(static_cast<float>(mantissa | 1024u), static_cast<int>(exponent) - 25);
			}
			return sign != 0 ? -value : value;
		}

		unsigned char to_byte(float linear)
		{
			const float clamped = std::clamp(linear, 0.0f, 1.0f);
			return static_cast<unsigned char>(std::pow(clamped, 1.0f / 2.2f) * 255.0f + 0.5f);
		}

		void put_pixel(ImageData& image, u32 x, u32 y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
		{
			unsigned char* at = &image.Rgba[(static_cast<size_t>(y) * image.Width + x) * 4];
			at[0] = r;
			at[1] = g;
			at[2] = b;
			at[3] = a;
		}

		ImageData blank_image(u32 size)
		{
			ImageData image;
			image.Width = size;
			image.Height = size;
			image.Rgba.assign(static_cast<size_t>(size) * size * 4, 0);
			return image;
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

	// A model file imports as a prefab (its meshes, materials and skeleton under one root).
	bool ThumbnailCache::is_model(const std::filesystem::path& source)
	{
		const std::filesystem::path extension = source.extension();
		return extension == ".glb" || extension == ".gltf" || extension == ".fbx";
	}

	bool ThumbnailCache::is_animation_asset(const std::filesystem::path& source)
	{
		const std::filesystem::path extension = source.extension();
		return extension == ".wskel" || extension == ".wanim" || extension == ".wmesh";
	}

	bool ThumbnailCache::handles(const std::filesystem::path& source)
	{
		const std::filesystem::path extension = source.extension();
		return is_animation_asset(source) || is_model(source) || extension == ".wmat" || extension == ".wprefab" || extension == ".hdr"
			|| extension == ".cube" || extension == ".ttf" || extension == ".otf";
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
		if (extension == ".wskel") entry.Type = Kind::Skeleton;
		else if (extension == ".wanim") entry.Type = Kind::Clip;
		else if (extension == ".wmesh") entry.Type = Kind::Mesh;
		else if (extension == ".wmat") entry.Type = Kind::Material;
		else if (extension == ".wprefab" || is_model(source)) entry.Type = Kind::Prefab;
		else if (extension == ".hdr") entry.Type = Kind::Environment;
		else if (extension == ".cube") entry.Type = Kind::Lut;
		else entry.Type = Kind::Font;
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
		if (manager == nullptr)
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

		if (entry.Type == Kind::Material || entry.Type == Kind::Prefab || entry.Type == Kind::Environment || entry.Type == Kind::Lut || entry.Type == Kind::Font)
		{
			entry.Skeleton = {};
			entry.Mesh = {};
			entry.Clip = {};
			u64 hash = 14695981039346656037ull;
			const u32 versions[2] = { VERSION, WASSET_VERSION };
			hash = fnv(hash, versions, sizeof(versions));
			const u32 kind = static_cast<u32>(entry.Type);
			hash = fnv(hash, &kind, sizeof(kind));
			if (entry.Type == Kind::Material || (entry.Type == Kind::Prefab && !is_model(entry.Source)))
			{
				hash = fnv_file(hash, entry.Source);
			}
			else
			{
				// big files (an HDR or a model is megabytes): the size and the time stand in for the content
				std::error_code sizeError;
				const u64 size = std::filesystem::file_size(entry.Source, sizeError);
				const long long stamp = static_cast<long long>(entry.FileTime.time_since_epoch().count());
				hash = fnv(hash, &size, sizeof(size));
				hash = fnv(hash, &stamp, sizeof(stamp));
			}
			if (entry.Type == Kind::Material)
			{
				// the textures and the shader it names change what it looks like
				MaterialFileData material;
				std::string reason;
				if (read_material_file(entry.Source, material, reason))
				{
					const std::filesystem::path roots[2] = { entry.Source.parent_path(), m_Database.get_assets_root().parent_path() };
					for (const std::string& name : { material.BaseColorPath, material.NormalPath, material.OrmPath, material.EmissivePath, material.ShaderPath })
					{
						if (name.empty())
						{
							continue;
						}
						for (const std::filesystem::path& root : roots)
						{
							std::error_code exists;
							const std::filesystem::path candidate = root / name;
							if (std::filesystem::exists(candidate, exists))
							{
								hash = fnv_file(hash, candidate);
								break;
							}
						}
					}
				}
			}
			entry.Key = hash;
			return true;
		}

		if (!m_Index.ready())
		{
			return false;
		}

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
		default:
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
		const bool animated = entry.Type == Kind::Skeleton || entry.Type == Kind::Clip || entry.Type == Kind::Mesh;
		const bool cpu = entry.Type == Kind::Environment || entry.Type == Kind::Lut || entry.Type == Kind::Font;
		if (animated && !entry.Skeleton.valid())
		{
			fail(entry.Source, "the file names no skeleton");
			return;
		}
		if (cpu)
		{
			Job job;
			job.Source = entry.Source;
			job.Key = entry.Key;
			job.Type = entry.Type;
			job.Asset = entry.Ref;
			if (entry.Type == Kind::Environment) job.EnvironmentHandle = manager->load<EnvironmentData>(entry.Ref);
			else if (entry.Type == Kind::Lut) job.LutHandle = manager->load<LutData>(entry.Ref);
			else job.FontHandle = manager->load<FontData>(entry.Ref);
			entry.Status = State::Working;
			m_Job = std::move(job);
			return;
		}
		if (m_Scene == nullptr)
		{
			// no shadows in a thumbnail and no environment: a strong ambient keeps the faces away from the lights (and dark materials) readable
			PreviewSceneOptions lighting;
			lighting.AmbientIntensity = 1.0f;
			m_Scene = make_preview_scene(lighting);
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

		if (!animated)
		{
			Job job;
			job.Source = entry.Source;
			job.Key = entry.Key;
			job.Type = entry.Type;
			job.Asset = entry.Ref;
			if (entry.Type == Kind::Material)
			{
				// the engine's own sphere (builtin_assets.h), held by the render thread: no asset of the project is needed
				job.Entity = m_Scene->create("Preview");
				m_Scene->registry().emplace<MeshRenderer>(job.Entity, MeshRenderer{ builtin_assets::PREVIEW_SPHERE, { entry.Ref } });
				m_Scene->update_world_transforms();
			}
			else
			{
				job.PrefabHandle = manager->load<PrefabData>(entry.Ref);
				job.Model = std::make_unique<PreviewModel>(*manager, m_Scene);
				job.Model->set_prefab(entry.Ref);
			}
			entry.Status = State::Working;
			m_Job = std::move(job);
			return;
		}

		Job job;
		job.Source = entry.Source;
		job.Key = entry.Key;
		job.Type = entry.Type;
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
		if (job.Type == Kind::Material || job.Type == Kind::Prefab)
		{
			step_static_job();
			return;
		}
		if (job.Type == Kind::Environment || job.Type == Kind::Lut || job.Type == Kind::Font)
		{
			step_cpu_job();
			return;
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
		const float distance = radius / std::sin(glm::radians(FOV * 0.5f)) * 1.1f * job.Zoom;
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

		render_job(camera, 8); // the render thread gets the pose, the camera and the mesh
	}

	// Points the view with the camera, and once the render thread has had settleFrames updates to get everything, takes the picture.
	void ThumbnailCache::render_job(const RenderCamera& camera, int settleFrames)
	{
		Job& job = *m_Job;
		m_View.set_camera(camera);
		if (++job.Settled < settleFrames)
		{
			return;
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
				bool clipped = false; // something is drawn at the border: part of the asset may be out of view
				for (size_t i = 3; i < image.Rgba.size(); i += 4)
				{
					if (image.Rgba[i] > 8)
					{
						drawn = true;
						const u32 pixel = static_cast<u32>(i / 4);
						const u32 x = pixel % image.Width;
						const u32 y = pixel / image.Width;
						clipped = clipped || x < BORDER || y < BORDER || x >= image.Width - BORDER || y >= image.Height - BORDER;
					}
				}
				if (drawn && clipped && m_Job->Zooms < MAX_ZOOMS)
				{
					// the camera backs off and the picture is taken again
					m_Job->Zooms++;
					m_Job->Zoom *= ZOOM_STEP;
					m_Job->ReadBack = false;
					m_Job->Settled = 0;
					return;
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

	// A material on the sphere of the shapes model, a prefab as it stands: framed by the bounds the asset knows.
	void ThumbnailCache::step_static_job()
	{
		Job& job = *m_Job;
		glm::vec3 low(0.0f);
		glm::vec3 high(0.0f);
		if (job.Type == Kind::Material)
		{
			low = glm::vec3(-builtin_assets::PREVIEW_SPHERE_RADIUS);
			high = glm::vec3(builtin_assets::PREVIEW_SPHERE_RADIUS);
		}
		else
		{
			job.Model->update();
			if (job.Model->failed() || job.PrefabHandle.state() == LoadState::Failed)
			{
				fail(job.Source, "the prefab failed to load");
				end_job();
				return;
			}
			const PrefabData* prefab = job.PrefabHandle.get();
			if (prefab == nullptr || !job.Model->ready())
			{
				return;
			}
			low = prefab->BoundsMin;
			high = prefab->BoundsMax;
		}
		const float radius = std::max(glm::length(high - low) * 0.5f * 1.1f, 0.1f);
		const glm::vec3 target = (low + high) * 0.5f;
		const float distance = radius / std::sin(glm::radians(FOV * 0.5f)) * 1.1f * job.Zoom;
		render_job(make_orbit_camera(target, distance, 20.0f, 15.0f, FOV, 1.0f), STATIC_SETTLE_FRAMES);
	}

	namespace
	{
		// The sky as a lat-long strip, tonemapped (mip 0 of the cube).
		bool draw_environment(const EnvironmentData& data, ImageData& image)
		{
			const size_t size = data.FaceSize;
			if (size == 0 || data.Mips.empty() || data.Mips[0].size() != 6 * size * size * 4)
			{
				return false;
			}
			const std::vector<u16>& texels = data.Mips[0];
			const u32 width = image.Width;
			const u32 height = image.Width / 2;
			const u32 top = (image.Height - height) / 2;
			constexpr float PI = 3.14159265f;
			for (u32 y = 0; y < height; y++)
			{
				for (u32 x = 0; x < width; x++)
				{
					const float lon = (static_cast<float>(x) + 0.5f) / static_cast<float>(width) * 2.0f * PI - PI;
					const float lat = PI * 0.5f - (static_cast<float>(y) + 0.5f) / static_cast<float>(height) * PI;
					const float dx = std::cos(lat) * std::sin(lon);
					const float dy = std::sin(lat);
					const float dz = -std::cos(lat) * std::cos(lon);
					const float ax = std::abs(dx);
					const float ay = std::abs(dy);
					const float az = std::abs(dz);
					size_t face;
					float sc, tc, ma;
					if (ax >= ay && ax >= az)
					{
						face = dx > 0.0f ? 0 : 1;
						sc = dx > 0.0f ? -dz : dz;
						tc = -dy;
						ma = ax;
					}
					else if (ay >= az)
					{
						face = dy > 0.0f ? 2 : 3;
						sc = dx;
						tc = dy > 0.0f ? dz : -dz;
						ma = ay;
					}
					else
					{
						face = dz > 0.0f ? 4 : 5;
						sc = dz > 0.0f ? dx : -dx;
						tc = -dy;
						ma = az;
					}
					const size_t px = std::min(static_cast<size_t>((sc / ma * 0.5f + 0.5f) * static_cast<float>(size)), size - 1);
					const size_t py = std::min(static_cast<size_t>((tc / ma * 0.5f + 0.5f) * static_cast<float>(size)), size - 1);
					const u16* at = &texels[((face * size + py) * size + px) * 4];
					float rgb[3];
					for (int c = 0; c < 3; c++)
					{
						const float linear = std::max(half_to_float(at[c]), 0.0f);
						rgb[c] = linear / (1.0f + linear); // Reinhard
					}
					put_pixel(image, x, top + y, to_byte(rgb[0]), to_byte(rgb[1]), to_byte(rgb[2]), 255);
				}
			}
			return true;
		}

		glm::vec3 hsv_to_rgb(float hue, float saturation, float value)
		{
			const float h = hue * 6.0f;
			const glm::vec3 rgb(std::clamp(std::abs(h - 3.0f) - 1.0f, 0.0f, 1.0f), std::clamp(2.0f - std::abs(h - 2.0f), 0.0f, 1.0f),
				std::clamp(2.0f - std::abs(h - 4.0f), 0.0f, 1.0f));
			return glm::mix(glm::vec3(1.0f), rgb, saturation) * value;
		}

		// A hue sweep that fades to dark, and a gray ramp below it, both put through the table: what the grade does to colors.
		bool draw_lut(const LutData& data, ImageData& image)
		{
			const size_t n = data.Size;
			if (n < 2 || data.Table.size() != n * n * n * 3)
			{
				return false;
			}
			const auto fetch = [&](size_t r, size_t g, size_t b)
			{
				const float* at = &data.Table[(r + g * n + b * n * n) * 3];
				return glm::vec3(at[0], at[1], at[2]);
			};
			const auto grade = [&](glm::vec3 color)
			{
				const glm::vec3 p = glm::clamp(color, 0.0f, 1.0f) * static_cast<float>(n - 1);
				const glm::vec3 base = glm::floor(p);
				const glm::vec3 f = p - base;
				const size_t r0 = static_cast<size_t>(base.x);
				const size_t g0 = static_cast<size_t>(base.y);
				const size_t b0 = static_cast<size_t>(base.z);
				const size_t r1 = std::min(r0 + 1, n - 1);
				const size_t g1 = std::min(g0 + 1, n - 1);
				const size_t b1 = std::min(b0 + 1, n - 1);
				const glm::vec3 c00 = glm::mix(fetch(r0, g0, b0), fetch(r1, g0, b0), f.x);
				const glm::vec3 c10 = glm::mix(fetch(r0, g1, b0), fetch(r1, g1, b0), f.x);
				const glm::vec3 c01 = glm::mix(fetch(r0, g0, b1), fetch(r1, g0, b1), f.x);
				const glm::vec3 c11 = glm::mix(fetch(r0, g1, b1), fetch(r1, g1, b1), f.x);
				return glm::mix(glm::mix(c00, c10, f.y), glm::mix(c01, c11, f.y), f.z);
			};
			const u32 ramp = 20;
			for (u32 y = 0; y < image.Height; y++)
			{
				for (u32 x = 0; x < image.Width; x++)
				{
					const float u = static_cast<float>(x) / static_cast<float>(image.Width - 1);
					glm::vec3 color;
					if (y >= image.Height - ramp)
					{
						color = glm::vec3(u);
					}
					else
					{
						const float v = static_cast<float>(y) / static_cast<float>(image.Height - ramp);
						color = hsv_to_rgb(u * 0.98f, 0.85f, 1.0f - 0.75f * v);
					}
					const glm::vec3 out = glm::clamp(grade(color), 0.0f, 1.0f) * 255.0f + 0.5f;
					put_pixel(image, x, y, static_cast<unsigned char>(out.x), static_cast<unsigned char>(out.y), static_cast<unsigned char>(out.z), 255);
				}
			}
			return true;
		}

		float atlas_at(const FontData& font, float x, float y)
		{
			const int width = static_cast<int>(font.AtlasWidth);
			const int height = static_cast<int>(font.AtlasHeight);
			const int x0 = static_cast<int>(std::floor(x));
			const int y0 = static_cast<int>(std::floor(y));
			const float fx = x - static_cast<float>(x0);
			const float fy = y - static_cast<float>(y0);
			const auto texel = [&](int tx, int ty)
			{
				tx = std::clamp(tx, 0, width - 1);
				ty = std::clamp(ty, 0, height - 1);
				return static_cast<float>(font.Atlas[static_cast<size_t>(ty) * static_cast<size_t>(width) + static_cast<size_t>(tx)]) / 255.0f;
			};
			return glm::mix(glm::mix(texel(x0, y0), texel(x0 + 1, y0), fx), glm::mix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), fx), fy);
		}

		// "Aa" from the signed distance field, centered.
		bool draw_font(const FontData& font, ImageData& image)
		{
			if (font.AtlasWidth == 0 || font.AtlasHeight == 0 || font.Atlas.size() < static_cast<size_t>(font.AtlasWidth) * font.AtlasHeight || font.SdfRange <= 0.0f)
			{
				return false;
			}
			std::vector<const FontGlyph*> glyphs;
			for (const u32 codepoint : { u32('A'), u32('a') })
			{
				if (const FontGlyph* glyph = font.find(codepoint); glyph != nullptr && glyph->Size.x > 0.0f)
				{
					glyphs.push_back(glyph);
				}
			}
			for (size_t i = 0; glyphs.empty() && i < font.Glyphs.size() && glyphs.size() < 2; i++)
			{
				if (font.Glyphs[i].Size.x > 0.0f)
				{
					glyphs.push_back(&font.Glyphs[i]);
				}
			}
			if (glyphs.empty())
			{
				return false;
			}

			float total = 0.0f;
			for (size_t i = 0; i < glyphs.size(); i++)
			{
				total += glyphs[i]->Advance + (i > 0 ? font.kerning(glyphs[i - 1]->Codepoint, glyphs[i]->Codepoint) : 0.0f);
			}
			const float lineHeight = std::max(font.Ascent - font.Descent, 1.0f);
			const float size = static_cast<float>(image.Width);
			const float scale = std::min(size * 0.8f / std::max(total, 1.0f), size * 0.7f / lineHeight);
			float pen = (size - total * scale) * 0.5f;
			const float baseline = (size - lineHeight * scale) * 0.5f + font.Ascent * scale;
			const float edge = 0.75f * 0.5f / (font.SdfRange * scale);

			for (size_t i = 0; i < glyphs.size(); i++)
			{
				const FontGlyph& glyph = *glyphs[i];
				if (i > 0)
				{
					pen += font.kerning(glyphs[i - 1]->Codepoint, glyph.Codepoint) * scale;
				}
				const float left = pen + glyph.Bearing.x * scale;
				const float topEdge = baseline + glyph.Bearing.y * scale;
				const float width = glyph.Size.x * scale;
				const float height = glyph.Size.y * scale;
				const int x0 = std::max(static_cast<int>(std::floor(left)), 0);
				const int y0 = std::max(static_cast<int>(std::floor(topEdge)), 0);
				const int x1 = std::min(static_cast<int>(std::ceil(left + width)), static_cast<int>(image.Width));
				const int y1 = std::min(static_cast<int>(std::ceil(topEdge + height)), static_cast<int>(image.Height));
				for (int y = y0; y < y1; y++)
				{
					for (int x = x0; x < x1; x++)
					{
						const float u = std::clamp((static_cast<float>(x) + 0.5f - left) / width, 0.0f, 1.0f);
						const float v = std::clamp((static_cast<float>(y) + 0.5f - topEdge) / height, 0.0f, 1.0f);
						const float ax = glm::mix(glyph.UvMin.x, glyph.UvMax.x, u) * static_cast<float>(font.AtlasWidth) - 0.5f;
						const float ay = glm::mix(glyph.UvMin.y, glyph.UvMax.y, v) * static_cast<float>(font.AtlasHeight) - 0.5f;
						const float distance = atlas_at(font, ax, ay);
						const float t = std::clamp((distance - (0.5f - edge)) / (2.0f * edge), 0.0f, 1.0f);
						unsigned char* at = &image.Rgba[(static_cast<size_t>(y) * image.Width + static_cast<size_t>(x)) * 4];
						const unsigned char alpha = static_cast<unsigned char>(t * 255.0f + 0.5f);
						if (alpha > at[3])
						{
							at[0] = at[1] = at[2] = 235;
							at[3] = alpha;
						}
					}
				}
				pen += glyph.Advance * scale;
			}
			return true;
		}
	}

	// The thumbnails that need no view: made from the cooked data as soon as it is loaded.
	void ThumbnailCache::step_cpu_job()
	{
		Job& job = *m_Job;
		ImageData image = blank_image(SIZE);
		bool ok = false;
		switch (job.Type)
		{
		case Kind::Environment:
		{
			if (job.EnvironmentHandle.state() != LoadState::Failed && job.EnvironmentHandle.get() == nullptr)
			{
				return;
			}
			const EnvironmentData* data = job.EnvironmentHandle.get();
			ok = data != nullptr && draw_environment(*data, image);
			break;
		}
		case Kind::Lut:
		{
			if (job.LutHandle.state() != LoadState::Failed && job.LutHandle.get() == nullptr)
			{
				return;
			}
			const LutData* data = job.LutHandle.get();
			ok = data != nullptr && draw_lut(*data, image);
			break;
		}
		default:
		{
			if (job.FontHandle.state() != LoadState::Failed && job.FontHandle.get() == nullptr)
			{
				return;
			}
			const FontData* data = job.FontHandle.get();
			ok = data != nullptr && draw_font(*data, image);
			break;
		}
		}
		const std::filesystem::path source = job.Source;
		const u64 key = job.Key;
		end_job();
		if (ok)
		{
			finish(source, key, image);
		}
		else
		{
			fail(source, "its data could not be drawn");
		}
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
