#include <editor_common.h>
#include "model_import.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>

#include <ufbx.h>

// The FBX reader (Mixamo characters are FBX files). It fills the same ImportedModel as the glTF reader: a node
// hierarchy, skins, meshes with joint weights, and animations baked to keys at 30 Hz.

namespace wiwa
{
	namespace
	{
		constexpr double BAKE_RATE = 30.0; // keys per second of the baked animation

		glm::vec3 to_glm(const ufbx_vec3& v)
		{
			return glm::vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
		}

		glm::mat4 to_glm(const ufbx_matrix& m)
		{
			return glm::mat4(
				glm::vec4(to_glm(m.cols[0]), 0.0f),
				glm::vec4(to_glm(m.cols[1]), 0.0f),
				glm::vec4(to_glm(m.cols[2]), 0.0f),
				glm::vec4(to_glm(m.cols[3]), 1.0f));
		}

		std::string to_string(const ufbx_string& s)
		{
			return std::string(s.data, s.length);
		}

		std::string get_file_name(std::string path)
		{
			std::replace(path.begin(), path.end(), '\\', '/');
			const size_t separator = path.find_last_of('/');
			return (separator == std::string::npos) ? path : path.substr(separator + 1);
		}

		class FbxConverter
		{
		public:
			FbxConverter(const ufbx_scene* scene, ImportedModel& model, const std::filesystem::path& directory)
				: m_Scene(scene), m_Model(model), m_Directory(directory) {}

			void convert()
			{
				convert_nodes();
				convert_animations();

				for (size_t i = 0; i < m_Scene->nodes.count; i++)
				{
					const ufbx_node* node = m_Scene->nodes.data[i];
					if (node->mesh != nullptr)
					{
						convert_mesh_node(node);
					}
				}

				// A material for the parts that name none (they were given a marker until now).
				ImportedMaterial fallback;
				fallback.Data.Name = "(default)";
				fallback.Data.RoughnessFactor = 0.8f;
				m_Model.Materials.push_back(fallback);
				const u32 defaultIndex = static_cast<u32>(m_Model.Materials.size() - 1);
				for (ImportedMesh& mesh : m_Model.Meshes)
				{
					for (u32& material : mesh.PrimitiveMaterials)
					{
						if (material == NO_MATERIAL)
						{
							material = defaultIndex;
						}
					}
				}
			}

		private:
			void convert_nodes()
			{
				const size_t count = m_Scene->nodes.count;
				m_Model.Nodes.resize(count);
				m_Model.NodeMesh.assign(count, -1);
				m_Model.NodeSkin.assign(count, -1);

				for (size_t i = 0; i < count; i++)
				{
					const ufbx_node* node = m_Scene->nodes.data[i];
					PrefabNode& result = m_Model.Nodes[i];
					result.Parent = (node->parent != nullptr) ? static_cast<i32>(node->parent->typed_id) : -1;
					result.Name = to_string(node->name);
					result.Translation = to_glm(node->local_transform.translation);
					result.Rotation = glm::normalize(glm::quat(static_cast<float>(node->local_transform.rotation.w), static_cast<float>(node->local_transform.rotation.x),
						static_cast<float>(node->local_transform.rotation.y), static_cast<float>(node->local_transform.rotation.z)));
					result.Scale = to_glm(node->local_transform.scale);
				}
			}

			// Every animation stack becomes a clip: the animated nodes are sampled at BAKE_RATE and stored as linear keys.
			void convert_animations()
			{
				for (size_t s = 0; s < m_Scene->anim_stacks.count; s++)
				{
					const ufbx_anim_stack* stack = m_Scene->anim_stacks.data[s];

					std::set<u32> animatedNodes;
					for (size_t l = 0; l < stack->layers.count; l++)
					{
						const ufbx_anim_layer* layer = stack->layers.data[l];
						for (size_t p = 0; p < layer->anim_props.count; p++)
						{
							const ufbx_element* element = layer->anim_props.data[p].element;
							if (element != nullptr && element->type == UFBX_ELEMENT_NODE)
							{
								animatedNodes.insert(element->typed_id);
							}
						}
					}

					const double duration = std::max(stack->time_end - stack->time_begin, 1.0 / BAKE_RATE);
					const size_t keyCount = static_cast<size_t>(std::ceil(duration * BAKE_RATE)) + 1;

					ImportedClip animation;
					animation.Name = to_string(stack->name);
					if (animation.Name.empty())
					{
						animation.Name = "Animation " + std::to_string(s + 1);
					}
					animation.Duration = static_cast<float>(duration);

					for (u32 nodeIndex : animatedNodes)
					{
						const ufbx_node* node = m_Scene->nodes.data[nodeIndex];

						AnimationChannel translation;
						translation.Path = AnimationPath::Translation;
						translation.Components = 3;
						AnimationChannel rotation = translation;
						rotation.Path = AnimationPath::Rotation;
						rotation.Components = 4;
						AnimationChannel scale = translation;
						scale.Path = AnimationPath::Scale;

						glm::vec4 previous(0.0f);
						for (size_t k = 0; k < keyCount; k++)
						{
							const double relative = std::min(static_cast<double>(k) / BAKE_RATE, duration);
							const ufbx_transform transform = ufbx_evaluate_transform(stack->anim, node, stack->time_begin + relative);

							translation.Times.push_back(static_cast<float>(relative));
							rotation.Times.push_back(static_cast<float>(relative));
							scale.Times.push_back(static_cast<float>(relative));

							const glm::vec3 t = to_glm(transform.translation);
							translation.Values.insert(translation.Values.end(), { t.x, t.y, t.z });
							const glm::vec3 sc = to_glm(transform.scale);
							scale.Values.insert(scale.Values.end(), { sc.x, sc.y, sc.z });

							// q and -q are the same rotation, but interpolating between them goes the long way round.
							glm::vec4 q(static_cast<float>(transform.rotation.x), static_cast<float>(transform.rotation.y),
								static_cast<float>(transform.rotation.z), static_cast<float>(transform.rotation.w));
							q /= std::max(glm::length(q), 1e-8f);
							if (k > 0 && glm::dot(q, previous) < 0.0f)
							{
								q = -q;
							}
							previous = q;
							rotation.Values.insert(rotation.Values.end(), { q.x, q.y, q.z, q.w });
						}

						animation.Channels.push_back(std::move(translation));
						animation.Channels.push_back(std::move(rotation));
						animation.Channels.push_back(std::move(scale));
						animation.ChannelNodes.insert(animation.ChannelNodes.end(), 3, nodeIndex);
					}

					if (!animation.Channels.empty())
					{
						m_Model.Animations.push_back(std::move(animation));
					}
				}
			}

			// ---- Materials and textures ----

			int get_image(const ufbx_texture* texture, TextureKind kind)
			{
				if (texture == nullptr)
				{
					return -1;
				}

				const ufbx_texture* file = texture;
				if (file->type != UFBX_TEXTURE_FILE)
				{
					if (texture->file_textures.count == 0)
					{
						return -1; // a procedural texture: nothing to load
					}
					file = texture->file_textures.data[0];
				}

				const std::string key = std::to_string(file->element_id) + ":" + std::to_string(static_cast<int>(kind));
				const auto found = m_ImageLookup.find(key);
				if (found != m_ImageLookup.end())
				{
					return found->second;
				}

				ImportedImage image;
				image.Name = get_file_name(to_string(file->filename));
				image.Kind = kind;

				bool found_data = false;
				if (file->content.size > 0)
				{
					const std::byte* bytes = static_cast<const std::byte*>(file->content.data);
					image.Embedded.assign(bytes, bytes + file->content.size); // embedded
					found_data = true;
				}
				else
				{
					// The file name in an FBX is the one on the artist's machine: try it as given (relative), then by its name here.
					std::string relative = to_string(file->relative_filename);
					std::replace(relative.begin(), relative.end(), '\\', '/');

					std::vector<std::filesystem::path> candidates;
					if (!relative.empty())
					{
						candidates.push_back(m_Directory / relative);
					}
					candidates.push_back(m_Directory / image.Name);
					candidates.push_back(m_Directory / "textures" / image.Name);

					for (const std::filesystem::path& candidate : candidates)
					{
						std::error_code error;
						if (std::filesystem::exists(candidate, error))
						{
							image.File = candidate;
							found_data = true;
							break;
						}
					}
				}

				int index = -1;
				if (found_data)
				{
					image.Used = true;
					m_Model.Images.push_back(std::move(image));
					index = static_cast<int>(m_Model.Images.size() - 1);
				}
				else
				{
					m_Model.Warnings.push_back("Texture " + image.Name + " could not be found");
				}
				m_ImageLookup[key] = index;
				return index;
			}

			u32 get_material(const ufbx_material* material)
			{
				if (material == nullptr)
				{
					return NO_MATERIAL;
				}
				const auto found = m_MaterialLookup.find(material);
				if (found != m_MaterialLookup.end())
				{
					return found->second;
				}

				ImportedMaterial imported;
				MaterialData& result = imported.Data;
				result.Name = (material->name.length > 0) ? std::string(material->name.data, material->name.length) : std::string("(unnamed)");
				const ufbx_material_pbr_maps& pbr = material->pbr;

				if (pbr.base_color.has_value)
				{
					result.BaseColorFactor = glm::vec4(static_cast<float>(pbr.base_color.value_vec4.x), static_cast<float>(pbr.base_color.value_vec4.y),
						static_cast<float>(pbr.base_color.value_vec4.z), 1.0f);
				}
				if (pbr.base_factor.has_value)
				{
					result.BaseColorFactor *= glm::vec4(glm::vec3(static_cast<float>(pbr.base_factor.value_real)), 1.0f);
				}
				result.RoughnessFactor = pbr.roughness.has_value ? std::clamp(static_cast<float>(pbr.roughness.value_real), 0.25f, 1.0f) : 0.7f;
				result.MetallicFactor = pbr.metalness.has_value ? std::clamp(static_cast<float>(pbr.metalness.value_real), 0.0f, 1.0f) : 0.0f;

				imported.BaseColorImage = pbr.base_color.texture_enabled ? get_image(pbr.base_color.texture, TextureKind::Color) : -1;
				imported.NormalImage = pbr.normal_map.texture_enabled ? get_image(pbr.normal_map.texture, TextureKind::Normal) : -1;
				if (imported.BaseColorImage >= 0)
				{
					result.BaseColorFactor = glm::vec4(1.0f); // the texture carries the color
				}

				// Hair and lashes come with an opacity map: cut them out.
				if (pbr.opacity.texture != nullptr && pbr.opacity.texture_enabled)
				{
					result.Alpha = AlphaMode::Mask;
					result.AlphaCutoff = 0.5f;
					const int opacityImage = get_image(pbr.opacity.texture, TextureKind::Color);
					if (opacityImage >= 0 && imported.BaseColorImage < 0)
					{
						imported.BaseColorImage = opacityImage;
					}
				}

				m_Model.Materials.push_back(std::move(imported));
				const u32 index = static_cast<u32>(m_Model.Materials.size() - 1);
				m_MaterialLookup[material] = index;
				return index;
			}

			// ---- Meshes ----

			int get_skin(const ufbx_skin_deformer* deformer)
			{
				const auto found = m_SkinLookup.find(deformer);
				if (found != m_SkinLookup.end())
				{
					return found->second;
				}

				ImportedSkeleton skin;
				skin.Name = "Skin " + std::to_string(m_Model.Skeletons.size());
				for (size_t i = 0; i < deformer->clusters.count; i++)
				{
					const ufbx_skin_cluster* cluster = deformer->clusters.data[i];
					skin.Joints.push_back(cluster->bone_node != nullptr ? cluster->bone_node->typed_id : 0u);
					skin.InverseBind.push_back(to_glm(cluster->geometry_to_bone));
				}
				m_Model.Skeletons.push_back(std::move(skin));
				const int index = static_cast<int>(m_Model.Skeletons.size() - 1);
				m_SkinLookup[deformer] = index;
				return index;
			}

			void convert_mesh_node(const ufbx_node* node)
			{
				const ufbx_mesh* mesh = node->mesh;
				const ufbx_skin_deformer* deformer = mesh->skin_deformers.count > 0 ? mesh->skin_deformers.data[0] : nullptr;
				const int skinIndex = (deformer != nullptr) ? get_skin(deformer) : -1;
				if (mesh->blend_deformers.count > 0)
				{
					m_Model.Warnings.push_back("Blend shapes of " + to_string(node->name) + " are not supported (skipped)");
				}

				// A mesh that is not skinned moves with its node: its geometry offset goes into the vertices, so that the node's
				// own transform is all that is left to animate. A skinned mesh stays in its own space (the joints carry it).
				const glm::mat4 geometryToNode = to_glm(node->geometry_to_node);
				const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(geometryToNode)));

				std::vector<u32> triangle(std::max<size_t>(mesh->max_face_triangles, 1) * 3);

				ImportedMesh imported;
				MeshData& data = imported.Data;
				data.Name = to_string(node->name);
				if (data.Name.empty())
				{
					data.Name = "Mesh " + std::to_string(node->typed_id);
				}

				for (size_t partIndex = 0; partIndex < mesh->material_parts.count; partIndex++)
				{
					const ufbx_mesh_part& part = mesh->material_parts.data[partIndex];
					if (part.num_triangles == 0)
					{
						continue;
					}

					const ufbx_material* material = (part.index < node->materials.count) ? node->materials.data[part.index]
						: (part.index < mesh->materials.count ? mesh->materials.data[part.index] : nullptr);

					const size_t firstVertex = data.Vertices.size();
					glm::vec3 boundsMin(1e30f), boundsMax(-1e30f);

					for (size_t f = 0; f < part.face_indices.count; f++)
					{
						const ufbx_face face = mesh->faces.data[part.face_indices.data[f]];
						const u32 triangleCount = ufbx_triangulate_face(triangle.data(), triangle.size(), mesh, face);

						for (u32 t = 0; t < triangleCount; t++)
						{
							for (int corner = 0; corner < 3; corner++)
							{
								// A mirrored mesh has its winding reversed by the importer: restore it.
								const int ordered = mesh->reversed_winding ? (corner == 0 ? 0 : 3 - corner) : corner;
								const u32 index = triangle[t * 3 + ordered];

								glm::vec3 position = to_glm(ufbx_get_vertex_vec3(&mesh->vertex_position, index));
								glm::vec3 normal = mesh->vertex_normal.exists ? to_glm(ufbx_get_vertex_vec3(&mesh->vertex_normal, index)) : glm::vec3(0.0f, 1.0f, 0.0f);
								glm::vec2 uv(0.0f);
								if (mesh->vertex_uv.exists)
								{
									const ufbx_vec2 value = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
									uv = glm::vec2(static_cast<float>(value.x), 1.0f - static_cast<float>(value.y)); // FBX's V goes up, ours down
								}

								if (skinIndex < 0)
								{
									position = glm::vec3(geometryToNode * glm::vec4(position, 1.0f));
									normal = glm::normalize(normalMatrix * normal);
								}

								MeshVertex vertex;
								vertex.Position[0] = position.x;
								vertex.Position[1] = position.y;
								vertex.Position[2] = position.z;
								vertex.Normal[0] = normal.x;
								vertex.Normal[1] = normal.y;
								vertex.Normal[2] = normal.z;
								vertex.Uv[0] = uv.x;
								vertex.Uv[1] = uv.y;
								data.Vertices.push_back(vertex);
								boundsMin = glm::min(boundsMin, position);
								boundsMax = glm::max(boundsMax, position);

								if (deformer != nullptr)
								{
									data.SkinVertices.resize(data.Vertices.size() - 1);
									data.SkinVertices.push_back(make_skin_vertex(deformer, mesh->vertex_indices.data[index]));
								}
							}
						}
					}

					MeshPrimitive primitive;
					primitive.BaseVertex = static_cast<i32>(firstVertex);
					primitive.VertexCount = static_cast<u32>(data.Vertices.size() - firstVertex);
					primitive.FirstIndex = static_cast<u32>(data.Indices.size());
					primitive.IndexCount = primitive.VertexCount;
					primitive.BoundsMin = boundsMin;
					primitive.BoundsMax = boundsMax;
					primitive.Skinned = (deformer != nullptr);
					primitive.LodCount = 1;
					primitive.Lods[0] = MeshLod{ primitive.FirstIndex, primitive.IndexCount, 0.0f };
					for (u32 i = 0; i < primitive.VertexCount; i++)
					{
						data.Indices.push_back(i); // relative to BaseVertex: the corners are not shared
					}
					data.Primitives.push_back(primitive);
					imported.PrimitiveMaterials.push_back(get_material(material));
				}

				if (data.Primitives.empty())
				{
					return;
				}

				if (!data.SkinVertices.empty())
				{
					data.SkinVertices.resize(data.Vertices.size());
				}

				glm::vec3 minimum(1e30f), maximum(-1e30f);
				for (const MeshPrimitive& primitive : data.Primitives)
				{
					minimum = glm::min(minimum, primitive.BoundsMin);
					maximum = glm::max(maximum, primitive.BoundsMax);
				}
				data.BoundsMin = minimum;
				data.BoundsMax = maximum;

				m_Model.Meshes.push_back(std::move(imported));
				m_Model.NodeMesh[node->typed_id] = static_cast<int>(m_Model.Meshes.size() - 1);
				m_Model.NodeSkin[node->typed_id] = skinIndex;
			}

			// The four strongest joints of a vertex (ufbx sorts the weights, strongest first), normalized.
			MeshSkinVertex make_skin_vertex(const ufbx_skin_deformer* deformer, u32 vertexIndex)
			{
				MeshSkinVertex result;
				const ufbx_skin_vertex& skinVertex = deformer->vertices.data[vertexIndex];
				const u32 count = std::min<u32>(skinVertex.num_weights, 4);
				float total = 0.0f;
				for (u32 i = 0; i < count; i++)
				{
					const ufbx_skin_weight& weight = deformer->weights.data[skinVertex.weight_begin + i];
					result.Joints[i] = weight.cluster_index;
					result.Weights[i] = static_cast<float>(weight.weight);
					total += result.Weights[i];
				}
				for (int i = 0; i < 4; i++)
				{
					result.Weights[i] = (total > 1e-6f) ? result.Weights[i] / total : (i == 0 ? 1.0f : 0.0f);
				}
				return result;
			}

			const ufbx_scene* m_Scene{};
			ImportedModel& m_Model;
			std::filesystem::path m_Directory{};
			static constexpr u32 NO_MATERIAL = 0xFFFFFFFFu;
			std::unordered_map<const ufbx_material*, u32> m_MaterialLookup{};
			std::unordered_map<const ufbx_skin_deformer*, int> m_SkinLookup{};
			std::unordered_map<std::string, int> m_ImageLookup{};
		};
	}

	bool read_fbx_model(const std::filesystem::path& path, ImportedModel& model, std::string& error)
	{
		ufbx_load_opts options = {};
		options.target_axes = ufbx_axes_right_handed_y_up; // right-handed, Y up
		options.target_unit_meters = 1.0f;                 // centimeters in a Mixamo file become meters
		options.generate_missing_normals = true;

		const std::string pathText = path.string();
		ufbx_error fbxError;
		ufbx_scene* scene = ufbx_load_file(pathText.c_str(), &options, &fbxError);
		if (scene == nullptr)
		{
			char text[512];
			ufbx_format_error(text, sizeof(text), &fbxError);
			error = "Failed to read " + pathText + " as FBX: " + text;
			return false;
		}

		model.Name = path.stem().string();

		FbxConverter converter(scene, model, path.parent_path());
		converter.convert();
		ufbx_free_scene(scene);

		if (model.Meshes.empty())
		{
			error = pathText + " has no triangle meshes to draw";
			return false;
		}

		return true;
	}
}
