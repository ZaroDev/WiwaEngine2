#include <editor_common.h>
#include "importers.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

#include <scene/prefab_node_id.h>

#include "../image_utils.h"
#include "model_import.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		std::string lower_extension(const std::filesystem::path& path)
		{
			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return extension;
		}

		// Box around the whole model: each mesh's box through the world transform of its node.
		void compute_bounds(const ImportedModel& model, glm::vec3& minimum, glm::vec3& maximum)
		{
			const size_t count = model.Nodes.size();
			std::vector<glm::mat4> world(count, glm::mat4(1.0f));
			std::vector<bool> done(count, false);

			auto local = [&](size_t i)
			{
				const PrefabNode& node = model.Nodes[i];
				return glm::translate(glm::mat4(1.0f), node.Translation) * glm::mat4_cast(node.Rotation) * glm::scale(glm::mat4(1.0f), node.Scale);
			};

			// walk up to the first node whose world matrix is known, then come back down; the depth limit stops a parent loop
			for (size_t i = 0; i < count; i++)
			{
				std::vector<size_t> chain;
				i32 current = static_cast<i32>(i);
				while (current >= 0 && !done[current] && chain.size() < 1024)
				{
					chain.push_back(static_cast<size_t>(current));
					current = model.Nodes[current].Parent;
				}

				glm::mat4 parent = (current >= 0 && done[current]) ? world[current] : glm::mat4(1.0f);
				for (auto it = chain.rbegin(); it != chain.rend(); ++it)
				{
					parent = parent * local(*it);
					world[*it] = parent;
					done[*it] = true;
				}
			}

			minimum = glm::vec3(1e30f);
			maximum = glm::vec3(-1e30f);
			for (size_t i = 0; i < count; i++)
			{
				if (model.NodeMesh[i] < 0)
				{
					continue;
				}

				const MeshData& mesh = model.Meshes[model.NodeMesh[i]].Data;
				for (int corner = 0; corner < 8; corner++)
				{
					const glm::vec3 point((corner & 1) ? mesh.BoundsMax.x : mesh.BoundsMin.x,
						(corner & 2) ? mesh.BoundsMax.y : mesh.BoundsMin.y,
						(corner & 4) ? mesh.BoundsMax.z : mesh.BoundsMin.z);
					const glm::vec3 transformed = glm::vec3(world[i] * glm::vec4(point, 1.0f));
					minimum = glm::min(minimum, transformed);
					maximum = glm::max(maximum, transformed);
				}
			}

			if (minimum.x > maximum.x)
			{
				minimum = maximum = glm::vec3(0.0f);
			}
		}

		// glTF (.gltf/.glb) and FBX models become a Prefab (the node hierarchy) that refers to Mesh, Material, Skeleton and
		// AnimationClip sub-assets, plus Texture assets: separate files are imported as their own assets, embedded ones
		// become sub-assets of the model.
		class ModelImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "ModelImporter"; }
			virtual u32 version() const override { return 3; } // 3: triangles that run against their normals are turned round; 2: prefab nodes have stable ids
			virtual bool imports_dependencies() const override { return true; }
			virtual std::vector<std::string> extensions() const override { return { ".gltf", ".glb", ".fbx" }; }

			virtual bool import(ImportContext& ctx) override
			{
				ImportedModel model;
				std::string error;
				const bool read = (lower_extension(ctx.source()) == ".fbx") ? read_fbx_model(ctx.source(), model, error) : read_gltf_model(ctx.source(), model, error);
				if (!read)
				{
					WI_ASSET_LOG(Error, "{0}", error);
					return false;
				}

				for (const std::string& warning : model.Warnings)
				{
					ctx.warn(warning);
				}

				// Textures
				std::vector<AssetRef> imageRefs(model.Images.size());
				for (size_t i = 0; i < model.Images.size(); i++)
				{
					const ImportedImage& image = model.Images[i];
					if (!image.Used)
					{
						continue;
					}

					if (!image.File.empty())
					{
						const nlohmann::json settings = { { "kind", texture_kind_to_string(image.Kind) } };
						std::optional<AssetRef> ref = ctx.import_dependency(image.File, &settings);
						if (ref)
						{
							imageRefs[i] = *ref;
						}
						else
						{
							ctx.warn("Texture " + image.File.string() + " has no importer");
						}
					}
					else if (!image.Embedded.empty())
					{
						std::string decodeError;
						std::optional<TextureData> texture = decode_texture(image.Embedded, image.Kind, image.Name, decodeError);
						if (!texture)
						{
							ctx.warn(decodeError);
							continue;
						}

						imageRefs[i] = ctx.sub_asset("image/" + std::to_string(i));
						ctx.emit(imageRefs[i], *texture);
					}
				}

				// Materials: the default one only if some primitive uses it
				std::vector<bool> materialUsed(model.Materials.size(), false);
				for (const ImportedMesh& mesh : model.Meshes)
				{
					for (u32 material : mesh.PrimitiveMaterials)
					{
						if (material < materialUsed.size())
						{
							materialUsed[material] = true;
						}
					}
				}

				auto imageRef = [&](int image) { return (image >= 0 && image < static_cast<int>(imageRefs.size())) ? imageRefs[image] : AssetRef{}; };

				std::vector<AssetRef> materialRefs(model.Materials.size());
				for (size_t i = 0; i < model.Materials.size(); i++)
				{
					if (!materialUsed[i])
					{
						continue;
					}

					MaterialData material = model.Materials[i].Data;
					material.BaseColorTexture = imageRef(model.Materials[i].BaseColorImage);
					material.NormalTexture = imageRef(model.Materials[i].NormalImage);
					material.OrmTexture = imageRef(model.Materials[i].OrmImage);
					material.EmissiveTexture = imageRef(model.Materials[i].EmissiveImage);

					materialRefs[i] = ctx.sub_asset("material/" + std::to_string(i));
					ctx.emit(materialRefs[i], material,
						{ material.BaseColorTexture, material.NormalTexture, material.OrmTexture, material.EmissiveTexture });
				}

				// Skeletons and clips
				std::vector<AssetRef> skeletonRefs(model.Skeletons.size());
				for (size_t i = 0; i < model.Skeletons.size(); i++)
				{
					skeletonRefs[i] = ctx.sub_asset("skeleton/" + std::to_string(i));
					ctx.emit(skeletonRefs[i], model.Skeletons[i]);
				}

				std::vector<AssetRef> clipRefs(model.Animations.size());
				for (size_t i = 0; i < model.Animations.size(); i++)
				{
					clipRefs[i] = ctx.sub_asset("animation/" + std::to_string(i));
					ctx.emit(clipRefs[i], model.Animations[i]);
				}

				// Meshes
				std::vector<AssetRef> meshRefs(model.Meshes.size());
				for (size_t i = 0; i < model.Meshes.size(); i++)
				{
					ImportedMesh& mesh = model.Meshes[i];
					std::vector<AssetRef> dependencies;
					for (size_t p = 0; p < mesh.Data.Primitives.size(); p++)
					{
						const AssetRef material = materialRefs[mesh.PrimitiveMaterials[p]];
						mesh.Data.Primitives[p].Material = material;
						dependencies.push_back(material);
					}

					meshRefs[i] = ctx.sub_asset("mesh/" + std::to_string(i));
					ctx.emit(meshRefs[i], mesh.Data, dependencies);
				}

				// The prefab ties it together. A skinned mesh node ignores its own transform (the joints place it), as in glTF.
				PrefabData prefab;
				prefab.Name = model.Name;
				prefab.Nodes = model.Nodes;
				for (size_t i = 0; i < prefab.Nodes.size(); i++)
				{
					if (model.NodeMesh[i] >= 0)
					{
						prefab.Nodes[i].Mesh = meshRefs[model.NodeMesh[i]];
					}
					if (model.NodeSkin[i] >= 0 && model.NodeSkin[i] < static_cast<int>(skeletonRefs.size()))
					{
						prefab.Nodes[i].Skeleton = skeletonRefs[model.NodeSkin[i]];
					}
				}
				assign_stable_ids(prefab.Nodes);
				prefab.Animations = clipRefs;
				compute_bounds(model, prefab.BoundsMin, prefab.BoundsMax);

				std::vector<AssetRef> prefabDependencies;
				prefabDependencies.insert(prefabDependencies.end(), meshRefs.begin(), meshRefs.end());
				prefabDependencies.insert(prefabDependencies.end(), skeletonRefs.begin(), skeletonRefs.end());
				prefabDependencies.insert(prefabDependencies.end(), clipRefs.begin(), clipRefs.end());

				ctx.emit(ctx.main_asset(), prefab, prefabDependencies);
				return true;
			}
		};
	}

	Scope<IImporter> create_model_importer() { return CreateScope<ModelImporter>(); }
}
