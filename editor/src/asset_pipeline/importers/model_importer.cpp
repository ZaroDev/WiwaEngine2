#include <editor_common.h>
#include "importers.h"

#include <algorithm>
#include <set>

#include <glm/gtc/matrix_transform.hpp>

#include <physics/cooked_shape.h>
#include <scene/prefab_node_id.h>

#include "../image_utils.h"
#include "../material_file.h"
#include "file_names.h"
#include "model_extract.h"
#include "model_import.h"
#include "model_skeleton.h"

#include <assets/asset_log.h>

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

		// The collision geometry of a mesh: every vertex for the hull, the triangles of every primitive for the mesh shape.
		void cook_physics_shapes(ImportContext& ctx, const MeshData& mesh, AssetRef meshRef, size_t index)
		{
			std::vector<glm::vec3> positions;
			positions.reserve(mesh.Vertices.size());
			for (const MeshVertex& vertex : mesh.Vertices)
			{
				positions.emplace_back(vertex.Position[0], vertex.Position[1], vertex.Position[2]);
			}

			std::vector<u32> indices;
			for (const MeshPrimitive& primitive : mesh.Primitives)
			{
				for (u32 i = 0; i < primitive.IndexCount && primitive.FirstIndex + i < mesh.Indices.size(); i++)
				{
					indices.push_back(static_cast<u32>(static_cast<i64>(mesh.Indices[primitive.FirstIndex + i]) + primitive.BaseVertex));
				}
			}

			const std::string label = "Mesh " + std::to_string(index) + " (" + mesh.Name + ")";
			const struct { CookedShapeKind Kind; const char* Name; } kinds[] = { { CookedShapeKind::ConvexHull, "hull" }, { CookedShapeKind::Mesh, "mesh" } };
			for (const auto& kind : kinds)
			{
				CookedShapeData shape;
				std::string error;
				const bool cooked = kind.Kind == CookedShapeKind::ConvexHull ? cook_convex_hull(positions, shape, error) : cook_triangle_mesh(positions, indices, shape, error);
				if (!cooked)
				{
					ctx.warn(label + ": no physics " + kind.Name + ": " + error);
					continue;
				}
				const AssetRef ref = cooked_shape_ref(meshRef, kind.Kind);
				ctx.sub_asset_with_id(std::string("physics/") + kind.Name + "/" + std::to_string(index), ref.LocalId);
				ctx.emit(ref, shape);
			}
		}

		// glTF (.gltf/.glb) and FBX models become a Prefab (the node hierarchy) that refers to Mesh, Material, Skeleton and
		// AnimationClip sub-assets, plus Texture assets: separate files are imported as their own assets, embedded ones
		// become sub-assets of the model.
		class ModelImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "ModelImporter"; }
			virtual u32 version() const override { return 9; } // 9: FBX skinned vertices are in model space (bind pose) and rigid meshes below a joint get an AnimationSocket; 8: skeletons, clips and skinned meshes are extracted to .wskel/.wanim/.wmesh files next to the model, once; 7: skeleton joints keep the nodes folded above them in Pre; 6: skeletons have names, parents and a rest pose, clips have events; 5: optional cooked physics shapes; 4: materials are .wmat files next to the model; 3: triangles that run against their normals are turned round; 2: prefab nodes have stable ids
			virtual bool imports_dependencies() const override { return true; }
			virtual bool editable() const override { return true; }
			virtual std::vector<ImportOption> option_schema() const override
			{
				return {
					ImportOption::make_float("scale", "Scale", 1.0, 0.001, 1000.0, "Scales the whole prefab (applied to its root nodes and its bounds). Use 0.01 for a model made in centimeters."),
					ImportOption::make_bool("importAnimations", "Import animations", true, "Off ignores the animation clips of the file (none are extracted)."),
					ImportOption::make_bool("generatePhysicsShapes", "Generate physics shapes", false, "Also cook a convex hull and a triangle mesh shape of every mesh, for ConvexHull and Mesh colliders."),
				};
			}
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

				if (!ctx.settings().value("importAnimations", true))
				{
					model.Animations.clear();
				}
				finalize_model_skeletons(model);
				merge_animation_events(model, ctx.settings());

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

				// Materials become .wmat files next to the model, once: a file that exists is the user's and stays as it is.
				// Which file belongs to which material name is kept in the .meta, so that the files keep their names when the
				// model changes. Models imported before this had the materials as sub-assets of the model.
				if (ctx.drop_sub_assets("material/"))
				{
					WI_ASSET_LOG(Info, "{0}: the materials of this model are now .wmat files in {1}_materials", ctx.source().filename().string(), ctx.source().stem().string());
				}

				for (const AssetRef& embedded : imageRefs)
				{
					// the material files refer to the embedded images by guid, before the images are written
					if (embedded.valid() && embedded.LocalId != 0)
					{
						ctx.declare_pending_asset(embedded.resolved());
					}
				}

				const std::filesystem::path materialsDirectory = ctx.source().parent_path() / (ctx.source().stem().string() + "_materials");
				nlohmann::json& materialFiles = ctx.mutable_settings()["materialFiles"];
				if (!materialFiles.is_object())
				{
					materialFiles = nlohmann::json::object();
				}

				std::set<std::string> takenFiles;
				for (auto it = materialFiles.begin(); it != materialFiles.end(); ++it)
				{
					if (it.value().is_string())
					{
						takenFiles.insert(lower_text(it.value().get<std::string>()));
					}
				}

				// the path of an image as a material file writes it: relative to the material folder, or the guid of an embedded image
				auto imagePath = [&](int image) -> std::string
				{
					if (image < 0 || image >= static_cast<int>(imageRefs.size()))
					{
						return {};
					}
					const ImportedImage& source = model.Images[image];
					if (!source.File.empty())
					{
						const std::filesystem::path absolute = source.File.is_absolute() ? source.File : ctx.source().parent_path() / source.File;
						const std::filesystem::path relative = absolute.lexically_normal().lexically_relative(materialsDirectory.lexically_normal());
						return (relative.empty() ? absolute : relative).generic_string();
					}
					return imageRefs[image].valid() ? imageRefs[image].resolved().to_string() : std::string{};
				};

				std::vector<AssetRef> materialRefs(model.Materials.size());
				std::set<std::string> usedKeys;
				for (size_t i = 0; i < model.Materials.size(); i++)
				{
					if (!materialUsed[i])
					{
						continue;
					}

					// the key of this material in the .meta: its name, with " 2", " 3" for a name that came before
					const std::string baseKey = model.Materials[i].Data.Name.empty() ? "material" : model.Materials[i].Data.Name;
					std::string key = baseKey;
					for (int number = 2; usedKeys.contains(key); number++)
					{
						key = baseKey + " " + std::to_string(number);
					}
					usedKeys.insert(key);

					// a name read from the .meta may have been edited by hand: only a plain file stem is used
					if (!materialFiles.contains(key) || !materialFiles[key].is_string() || safe_file_stem(materialFiles[key].get<std::string>()) != materialFiles[key].get<std::string>())
					{
						const std::string stem = safe_file_stem(key);
						std::string candidate = stem;
						for (int number = 2; takenFiles.contains(lower_text(candidate)); number++)
						{
							candidate = stem + " " + std::to_string(number);
						}
						takenFiles.insert(lower_text(candidate));
						materialFiles[key] = candidate;
					}

					const std::filesystem::path file = materialsDirectory / (materialFiles[key].get<std::string>() + ".wmat");
					std::error_code fileError;
					if (!std::filesystem::exists(file, fileError))
					{
						std::filesystem::create_directories(materialsDirectory, fileError);

						MaterialFileData data;
						data.Material = model.Materials[i].Data;
						data.BaseColorPath = imagePath(model.Materials[i].BaseColorImage);
						data.NormalPath = imagePath(model.Materials[i].NormalImage);
						data.OrmPath = imagePath(model.Materials[i].OrmImage);
						data.EmissivePath = imagePath(model.Materials[i].EmissiveImage);

						std::string writeError;
						if (!write_material_file(file, data, writeError))
						{
							ctx.warn("Cannot write the material " + file.filename().string() + ": " + writeError);
							continue;
						}
					}

					if (std::optional<AssetRef> ref = ctx.import_dependency(file))
					{
						materialRefs[i] = *ref;
					}
					else
					{
						ctx.warn("The material " + file.filename().string() + " has no importer");
					}
				}

				// Every primitive gets the material it uses (the files of skinned meshes carry them as their defaults)
				for (ImportedMesh& mesh : model.Meshes)
				{
					for (size_t p = 0; p < mesh.Data.Primitives.size(); p++)
					{
						mesh.Data.Primitives[p].Material = materialRefs[mesh.PrimitiveMaterials[p]];
					}
				}

				// Skeletons, clips and skinned meshes become files of their own next to the model, once (model_extract.h); the sub-assets they
				// used to be are redirected to them. Static meshes stay sub-assets of the model.
				ExtractedAssets extracted;
				std::string extractError;
				if (!extract_animation_assets(ctx, model, extracted, extractError))
				{
					WI_ASSET_LOG(Error, "{0}: the animation assets could not be extracted: {1}", ctx.source().filename().string(), extractError);
					return false;
				}

				// Meshes
				std::vector<AssetRef> meshRefs(model.Meshes.size());
				for (size_t i = 0; i < model.Meshes.size(); i++)
				{
					if (extracted.Meshes[i].valid())
					{
						meshRefs[i] = extracted.Meshes[i];
						continue;
					}

					ImportedMesh& mesh = model.Meshes[i];
					std::vector<AssetRef> dependencies;
					for (const MeshPrimitive& primitive : mesh.Data.Primitives)
					{
						dependencies.push_back(primitive.Material);
					}
					meshRefs[i] = ctx.sub_asset("mesh/" + std::to_string(i));
					ctx.emit(meshRefs[i], mesh.Data, dependencies);
				}

				// Physics shapes: a convex hull and a triangle mesh of every mesh, at the mesh's local id plus a fixed offset
				// (cooked_shape.h), so that a Collider that names the mesh names its shapes too. A skinned mesh is a file of its own and has none.
				if (ctx.settings().value("generatePhysicsShapes", false))
				{
					bool skippedSkinned = false;
					for (size_t i = 0; i < model.Meshes.size(); i++)
					{
						if (extracted.Meshes[i].valid())
						{
							skippedSkinned = true;
							continue;
						}
						cook_physics_shapes(ctx, model.Meshes[i].Data, meshRefs[i], i);
					}
					if (skippedSkinned)
					{
						ctx.warn("Skinned meshes get no physics shapes");
					}
				}
				else
				{
					ctx.drop_sub_assets("physics/");
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
					if (model.NodeSkin[i] >= 0 && model.NodeSkin[i] < static_cast<int>(extracted.Skeletons.size()))
					{
						prefab.Nodes[i].Skeleton = extracted.Skeletons[model.NodeSkin[i]];
					}
				}
				add_attachment_sockets(model, prefab.Nodes);
				assign_stable_ids(prefab.Nodes);
				for (const AssetRef& clip : extracted.Clips)
				{
					if (clip.valid())
					{
						prefab.Animations.push_back(clip);
					}
				}
				compute_bounds(model, prefab.BoundsMin, prefab.BoundsMax);
				if (const float modelScale = ctx.settings().value("scale", 1.0f); modelScale != 1.0f && modelScale > 0.0f)
				{
					for (PrefabNode& node : prefab.Nodes)
					{
						if (node.Parent < 0)
						{
							node.Scale *= modelScale;
							node.Translation *= modelScale;
						}
					}
					prefab.BoundsMin *= modelScale;
					prefab.BoundsMax *= modelScale;
				}

				std::vector<AssetRef> prefabDependencies;
				prefabDependencies.insert(prefabDependencies.end(), meshRefs.begin(), meshRefs.end());
				prefabDependencies.insert(prefabDependencies.end(), extracted.Skeletons.begin(), extracted.Skeletons.end());
				prefabDependencies.insert(prefabDependencies.end(), prefab.Animations.begin(), prefab.Animations.end());

				ctx.emit(ctx.main_asset(), prefab, prefabDependencies);
				return true;
			}
		};
	}

	Scope<IImporter> create_model_importer() { return CreateScope<ModelImporter>(); }
}
