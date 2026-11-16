#include <editor_common.h>
#include "importers.h"

#include <assets/native_anim_files.h>

#include "../file_utils.h"

#include <assets/asset_log.h>

namespace wiwa
{
	namespace
	{
		// The skeleton a clip or a mesh names, cooked first (this import may run before the skeleton's own), or nullopt when there is none (no
		// reference, a skeleton that was deleted, or one that failed to import).
		std::optional<SkeletonData> cooked_skeleton(ImportContext& ctx, const AssetRef& ref)
		{
			if (!ref.valid())
			{
				return std::nullopt;
			}
			if (const std::optional<std::filesystem::path> path = ctx.path_of(ref.Guid))
			{
				ctx.import_dependency(*path);
			}
			return ctx.load_cooked<SkeletonData>(ref);
		}

		// .wskel: a skeleton as JSON (the joints by name, their parents, the rest pose and the inverse bind matrices).
		class SkeletonFileImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "SkeletonFileImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".wskel" }; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();
				const std::optional<std::vector<std::byte>> bytes = ctx.read_source();
				if (!bytes)
				{
					return false;
				}

				std::string error;
				std::optional<SkeletonData> skeleton = read_skeleton_file(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), error);
				if (!skeleton)
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}
				if (skeleton->Name.empty())
				{
					skeleton->Name = ctx.source().stem().string();
				}
				ctx.emit<SkeletonData>(ctx.main_asset(), *skeleton);
				return true;
			}
		};

		// .wanim: a clip, channels by joint name. A clip that cannot be read fails the import (the cooked clip from before stays).
		class ClipFileImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "ClipFileImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".wanim" }; }
			virtual bool imports_dependencies() const override { return true; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();
				const std::optional<std::vector<std::byte>> bytes = ctx.read_source();
				if (!bytes)
				{
					return false;
				}

				std::string error;
				std::optional<AnimationClipData> clip = read_clip_file(*bytes, error);
				if (!clip)
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}
				if (clip->Name.empty())
				{
					clip->Name = ctx.source().stem().string();
				}

				// The skeleton the clip was made for is a dependency; a clip works without it (it binds to whatever skeleton plays it).
				if (clip->Skeleton.valid() && !cooked_skeleton(ctx, clip->Skeleton))
				{
					ctx.warn(file + ": the skeleton it was made for is missing; the clip is kept without it");
					clip->Skeleton = AssetRef{};
				}
				ctx.emit<AnimationClipData>(ctx.main_asset(), *clip, { clip->Skeleton });
				return true;
			}
		};

		// .wmesh: a skinned mesh. Its materials and its skeleton are dependencies; when the skeleton's joints are not the ones the mesh was made
		// for, the mesh is cooked without its skin (it draws rigid) instead of deforming wrongly.
		class MeshFileImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "MeshFileImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".wmesh" }; }
			virtual bool imports_dependencies() const override { return true; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();
				const std::optional<std::vector<std::byte>> bytes = ctx.read_source();
				if (!bytes)
				{
					return false;
				}

				std::string error;
				std::optional<MeshData> mesh = read_mesh_file(*bytes, error);
				if (!mesh)
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}
				if (mesh->Name.empty())
				{
					mesh->Name = ctx.source().stem().string();
				}

				if (mesh->Skeleton.valid())
				{
					const std::optional<SkeletonData> skeleton = cooked_skeleton(ctx, mesh->Skeleton);
					if (!skeleton)
					{
						ctx.warn(file + ": its skeleton is missing; the mesh is cooked without its skin");
						unskin(*mesh);
					}
					else if (skeleton->JointNames != mesh->SkinJointNames)
					{
						ctx.warn(file + ": the joints of its skeleton are not the ones the mesh was made for; the mesh is cooked without its skin");
						unskin(*mesh);
					}
				}

				std::vector<AssetRef> dependencies = { mesh->Skeleton };
				for (MeshPrimitive& primitive : mesh->Primitives)
				{
					if (primitive.Material.valid() && !ctx.asset_exists(primitive.Material))
					{
						ctx.warn(file + ": a material it names does not exist; the primitive has none");
						primitive.Material = AssetRef{};
					}
					dependencies.push_back(primitive.Material);
				}
				ctx.emit<MeshData>(ctx.main_asset(), *mesh, dependencies);
				return true;
			}

		private:
			static void unskin(MeshData& mesh)
			{
				mesh.SkinVertices.clear();
				mesh.SkinJointNames.clear();
				mesh.Skeleton = AssetRef{};
				for (MeshPrimitive& primitive : mesh.Primitives)
				{
					primitive.Skinned = false;
				}
			}
		};
	}

	Scope<IImporter> create_skeleton_file_importer() { return CreateScope<SkeletonFileImporter>(); }
	Scope<IImporter> create_clip_file_importer() { return CreateScope<ClipFileImporter>(); }
	Scope<IImporter> create_mesh_file_importer() { return CreateScope<MeshFileImporter>(); }
}
