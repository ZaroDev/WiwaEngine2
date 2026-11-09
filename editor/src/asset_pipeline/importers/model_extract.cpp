#include <editor_common.h>
#include "model_extract.h"

#include <fstream>
#include <functional>
#include <set>

#include <assets/native_anim_files.h>

#include "../asset_database.h"
#include "../atomic_write.h"
#include "../meta_file.h"
#include "file_names.h"
#include "model_skeleton.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		using json = nlohmann::json;

		bool write_file(const std::filesystem::path& path, const void* data, size_t size, std::string& error)
		{
			return write_file_atomic(path, data, size, error);
		}

		// What one kind of file needs: its extension, and how to write the content.
		struct Kind
		{
			const char* Key;       // "skeleton", "clip", "mesh": the first part of the key in the .meta
			const char* Legacy;    // the first part of the name of the sub-asset it used to be
			const char* Extension;
			const char* Fallback;  // the file name of something that has no name
		};

		class Extractor
		{
		public:
			Extractor(ImportContext& ctx, json& table, std::string& error)
				: m_Ctx(ctx), m_Table(table), m_Error(error), m_ModelFolder(ctx.source().parent_path()),
				m_Root(ctx.source().parent_path() / (ctx.source().stem().string() + "_animation"))
			{
				// the files the .meta already names are taken, whatever happens to be on disk
				for (auto it = m_Table.begin(); it != m_Table.end(); ++it)
				{
					if (it.value().is_object() && it.value().contains("file") && it.value()["file"].is_string())
					{
						m_Taken.insert(lower_text(it.value()["file"].get<std::string>()));
					}
				}
			}

			// subfolder: "" or "clips" / "meshes" below the model's animation folder.
			std::optional<AssetRef> place(const Kind& kind, const std::string& baseName, size_t index, const std::string& subfolder, const std::function<bool(const std::filesystem::path&, std::string&)>& write)
			{
				// the key of this item in the .meta: its name, with " 2", " 3" for a name that came before
				const std::string name = baseName.empty() ? std::string(kind.Fallback) + " " + std::to_string(index) : baseName;
				std::string key = std::string(kind.Key) + ":" + name;
				for (int number = 2; m_UsedKeys.contains(key); number++)
				{
					key = std::string(kind.Key) + ":" + name + " " + std::to_string(number);
				}
				m_UsedKeys.insert(key);

				const u64 legacy = m_Ctx.sub_asset(std::string(kind.Legacy) + "/" + std::to_string(index)).LocalId;
				const json entry = (m_Table.contains(key) && m_Table[key].is_object()) ? m_Table[key] : json::object();

				// a file the .meta knows by guid: found again wherever the user put it
				if (entry.contains("guid") && entry["guid"].is_string())
				{
					if (const std::optional<AssetGuid> guid = AssetGuid::from_string(entry["guid"].get<std::string>()))
					{
						std::error_code fileError;
						const std::optional<std::filesystem::path> path = m_Ctx.path_of(*guid);
						if (path && std::filesystem::exists(*path, fileError))
						{
							return record(key, legacy, AssetRef{ *guid, 0 }, *path);
						}
					}
				}

				// where it goes: where it was, or a new name that no file has
				std::filesystem::path path;
				if (entry.contains("file") && entry["file"].is_string() && is_plain_relative(entry["file"].get<std::string>()))
				{
					path = m_ModelFolder / entry["file"].get<std::string>();
				}
				else
				{
					const std::filesystem::path folder = subfolder.empty() ? m_Root : m_Root / subfolder;
					const std::string stem = safe_file_stem(name, kind.Fallback);
					std::string candidate = stem;
					for (int number = 2;; number++)
					{
						path = folder / (candidate + kind.Extension);
						std::error_code fileError;
						const std::string relative = lower_text(path.lexically_relative(m_ModelFolder).generic_string());
						if (!m_Taken.contains(relative) && !std::filesystem::exists(path, fileError))
						{
							break; // a file that exists and is not ours is somebody's: never written over
						}
						candidate = stem + " " + std::to_string(number);
					}
					m_Taken.insert(lower_text(path.lexically_relative(m_ModelFolder).generic_string()));
				}

				std::error_code fileError;
				if (!std::filesystem::exists(path, fileError))
				{
					std::string writeError;
					if (!write(path, writeError))
					{
						m_Error = writeError;
						return std::nullopt;
					}
					m_Written.push_back(path);
				}

				const std::optional<AssetRef> ref = m_Ctx.import_dependency(path);
				if (!ref)
				{
					m_Error = path.filename().string() + " has no importer";
					return std::nullopt;
				}
				return record(key, legacy, *ref, path);
			}

			bool failed() const { return !m_Error.empty(); }

			// The import failed after some files were written: they are not in the model's .meta (it is only saved when the import succeeds), so a
			// retry would not know them and would write copies next to them. They go again, with the .meta files made for them.
			void rollback()
			{
				for (const std::filesystem::path& path : m_Written)
				{
					std::error_code ignored;
					std::filesystem::remove(path, ignored);
					std::filesystem::remove(MetaFile::meta_path_for(path), ignored);
				}
				m_Written.clear();
			}

		private:
			static bool is_plain_relative(const std::string& file)
			{
				const std::filesystem::path path(file);
				if (file.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
				{
					return false;
				}
				for (const std::filesystem::path& part : path)
				{
					if (part == "..")
					{
						return false;
					}
				}
				return true;
			}

			AssetRef record(const std::string& key, u64 legacy, AssetRef ref, const std::filesystem::path& path)
			{
				json entry = json::object();
				entry["legacy"] = legacy;
				entry["guid"] = ref.Guid.to_string();
				entry["file"] = path.lexically_normal().lexically_relative(m_ModelFolder.lexically_normal()).generic_string();
				m_Table[key] = std::move(entry);
				m_Ctx.add_redirect(AssetRef{ m_Ctx.main_asset().Guid, legacy }, ref);
				return ref;
			}

			ImportContext& m_Ctx;
			json& m_Table;
			std::string& m_Error;
			std::filesystem::path m_ModelFolder;
			std::filesystem::path m_Root;
			std::set<std::string> m_Taken{};
			std::set<std::string> m_UsedKeys{};
			std::vector<std::filesystem::path> m_Written{};
		};
	}

	bool extract_animation_assets(ImportContext& ctx, ImportedModel& model, ExtractedAssets& out, std::string& error)
	{
		error.clear();
		out = ExtractedAssets{};
		out.Skeletons.assign(model.Skeletons.size(), AssetRef{});
		out.Clips.assign(model.Animations.size(), AssetRef{});
		out.Meshes.assign(model.Meshes.size(), AssetRef{});

		json& table = ctx.mutable_settings()["extracted"];
		if (!table.is_object())
		{
			table = json::object();
		}
		Extractor extractor(ctx, table, error);

		static const Kind skeletonKind{ "skeleton", "skeleton", ".wskel", "Skeleton" };
		static const Kind clipKind{ "clip", "animation", ".wanim", "Animation" };
		static const Kind meshKind{ "mesh", "mesh", ".wmesh", "Mesh" };

		// Skeletons first: clips and meshes name them
		for (size_t i = 0; i < model.Skeletons.size(); i++)
		{
			const SkeletonData& skeleton = model.Skeletons[i];
			const std::optional<AssetRef> ref = extractor.place(skeletonKind, skeleton.Name, i, "", [&](const std::filesystem::path& path, std::string& writeError)
				{
					const std::string text = write_skeleton_file(skeleton);
					return write_file(path, text.data(), text.size(), writeError);
				});
			if (!ref)
			{
				extractor.rollback();
				return false;
			}
			out.Skeletons[i] = *ref;
		}

		for (size_t i = 0; i < model.Animations.size(); i++)
		{
			int skeletonIndex = -1;
			std::vector<std::string> warnings;
			AnimationClipData clip = make_named_clip(model.Animations[i], model.Skeletons, skeletonIndex, warnings);
			for (const std::string& warning : warnings)
			{
				ctx.warn(warning);
			}
			if (clip.Channels.empty())
			{
				ctx.warn("The clip " + clip.Name + " animates no joint of any skeleton and was not extracted");
				continue;
			}
			if (skeletonIndex >= 0)
			{
				clip.Skeleton = out.Skeletons[static_cast<size_t>(skeletonIndex)];
			}

			const std::optional<AssetRef> ref = extractor.place(clipKind, clip.Name, i, "clips", [&](const std::filesystem::path& path, std::string& writeError)
				{
					const std::vector<std::byte> bytes = write_clip_file(clip);
					return write_file(path, bytes.data(), bytes.size(), writeError);
				});
			if (!ref)
			{
				extractor.rollback();
				return false;
			}
			out.Clips[i] = *ref;
		}

		// Skinned meshes only: a static mesh stays a sub-asset of the model
		for (size_t i = 0; i < model.Meshes.size(); i++)
		{
			MeshData mesh = model.Meshes[i].Data;
			if (mesh.SkinVertices.empty())
			{
				continue;
			}

			int skin = -1;
			for (size_t node = 0; node < model.NodeMesh.size() && node < model.NodeSkin.size(); node++)
			{
				if (model.NodeMesh[node] == static_cast<int>(i) && model.NodeSkin[node] >= 0 && model.NodeSkin[node] < static_cast<int>(model.Skeletons.size()))
				{
					if (skin < 0)
					{
						skin = model.NodeSkin[node];
					}
					else if (skin != model.NodeSkin[node])
					{
						ctx.warn("The mesh " + mesh.Name + " is used under different skeletons; the first one is used");
					}
				}
			}
			if (skin < 0)
			{
				continue; // skin weights but no skeleton to name: it stays an ordinary mesh
			}
			mesh.Skeleton = out.Skeletons[static_cast<size_t>(skin)];
			mesh.SkinJointNames = model.Skeletons[static_cast<size_t>(skin)].JointNames;

			const std::optional<AssetRef> ref = extractor.place(meshKind, mesh.Name, i, "meshes", [&](const std::filesystem::path& path, std::string& writeError)
				{
					const std::vector<std::byte> bytes = write_mesh_file(mesh);
					return write_file(path, bytes.data(), bytes.size(), writeError);
				});
			if (!ref)
			{
				extractor.rollback();
				return false;
			}
			out.Meshes[i] = *ref;
		}
		if (extractor.failed())
		{
			extractor.rollback();
		}
		return !extractor.failed();
	}
}
