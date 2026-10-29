#include <editor_common.h>
#include "model_import.h"

#include <algorithm>
#include <memory>
#include <unordered_map>

#include <glm/gtc/type_ptr.hpp>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

namespace wiwa
{
	namespace
	{
		struct CgltfDeleter
		{
			void operator()(cgltf_data* data) const { cgltf_free(data); }
		};
		using CgltfPointer = std::unique_ptr<cgltf_data, CgltfDeleter>;

		std::string get_material_name(const cgltf_material& material)
		{
			return material.name != nullptr ? material.name : "(unnamed)";
		}

		// Smooth normals for a primitive that has none: each triangle adds its (area-weighted) normal to its corners.
		void compute_normals(std::vector<MeshVertex>& vertices, size_t firstVertex, const std::vector<u32>& localIndices)
		{
			std::vector<glm::vec3> accumulated(vertices.size() - firstVertex, glm::vec3(0.0f));

			for (size_t i = 0; i + 2 < localIndices.size(); i += 3)
			{
				const u32 a = localIndices[i];
				const u32 b = localIndices[i + 1];
				const u32 c = localIndices[i + 2];

				const glm::vec3 pa = glm::make_vec3(vertices[firstVertex + a].Position);
				const glm::vec3 pb = glm::make_vec3(vertices[firstVertex + b].Position);
				const glm::vec3 pc = glm::make_vec3(vertices[firstVertex + c].Position);

				const glm::vec3 faceNormal = glm::cross(pb - pa, pc - pa); // length = twice the area
				accumulated[a] += faceNormal;
				accumulated[b] += faceNormal;
				accumulated[c] += faceNormal;
			}

			for (size_t i = 0; i < accumulated.size(); i++)
			{
				const float length = glm::length(accumulated[i]);
				const glm::vec3 normal = (length > 1e-12f) ? accumulated[i] / length : glm::vec3(0.0f, 1.0f, 0.0f);
				float* destination = vertices[firstVertex + i].Normal;
				destination[0] = normal.x;
				destination[1] = normal.y;
				destination[2] = normal.z;
			}
		}

		int get_image_index(const cgltf_data* data, const cgltf_texture_view& view)
		{
			if (view.texture == nullptr || view.texture->image == nullptr)
			{
				return -1;
			}
			return static_cast<int>(cgltf_image_index(data, view.texture->image));
		}

		class Converter
		{
		public:
			Converter(const cgltf_data* data, ImportedModel& model) : m_Data(data), m_Model(model) {}

			void convert_materials()
			{
				m_Model.Materials.reserve(m_Data->materials_count + 1);
				for (cgltf_size i = 0; i < m_Data->materials_count; i++)
				{
					m_Model.Materials.push_back(convert_material(m_Data->materials[i]));
				}

				// The material of primitives that name none: a plain dielectric (the glTF default is a rough metal, which looks wrong here).
				ImportedMaterial defaultMaterial;
				defaultMaterial.Data.Name = "(default)";
				defaultMaterial.Data.MetallicFactor = 0.0f;
				defaultMaterial.Data.RoughnessFactor = 0.8f;
				m_Model.Materials.push_back(defaultMaterial);
				m_DefaultMaterial = static_cast<u32>(m_Model.Materials.size() - 1);
			}

			// The node hierarchy at rest, the skins and the animations.
			void convert_nodes_and_animation()
			{
				const size_t nodeCount = m_Data->nodes_count;
				m_Model.Nodes.resize(nodeCount);
				m_Model.NodeMesh.assign(nodeCount, -1);
				m_Model.NodeSkin.assign(nodeCount, -1);

				for (cgltf_size i = 0; i < nodeCount; i++)
				{
					const cgltf_node& node = m_Data->nodes[i];
					PrefabNode& result = m_Model.Nodes[i];
					result.Parent = (node.parent != nullptr) ? static_cast<i32>(cgltf_node_index(m_Data, node.parent)) : -1;
					result.Name = (node.name != nullptr) ? node.name : "";

					if (node.has_matrix)
					{
						const glm::mat4 matrix = glm::make_mat4(node.matrix);
						result.Translation = glm::vec3(matrix[3]);
						glm::vec3 axes[3] = { glm::vec3(matrix[0]), glm::vec3(matrix[1]), glm::vec3(matrix[2]) };
						result.Scale = glm::vec3(glm::length(axes[0]), glm::length(axes[1]), glm::length(axes[2]));
						for (int axis = 0; axis < 3; axis++)
						{
							axes[axis] = (result.Scale[axis] > 1e-8f) ? axes[axis] / result.Scale[axis] : axes[axis];
						}
						result.Rotation = glm::normalize(glm::quat_cast(glm::mat3(axes[0], axes[1], axes[2])));
					}
					else
					{
						if (node.has_translation)
						{
							result.Translation = glm::make_vec3(node.translation);
						}
						if (node.has_rotation)
						{
							result.Rotation = glm::normalize(glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]));
						}
						if (node.has_scale)
						{
							result.Scale = glm::make_vec3(node.scale);
						}
					}

					if (node.weights != nullptr && node.weights_count > 0)
					{
						result.MorphWeights.assign(node.weights, node.weights + node.weights_count);
					}
					else if (node.mesh != nullptr && node.mesh->weights != nullptr && node.mesh->weights_count > 0)
					{
						result.MorphWeights.assign(node.mesh->weights, node.mesh->weights + node.mesh->weights_count);
					}

					if (node.skin != nullptr)
					{
						m_Model.NodeSkin[i] = static_cast<int>(cgltf_skin_index(m_Data, node.skin));
					}
				}

				for (cgltf_size i = 0; i < m_Data->skins_count; i++)
				{
					const cgltf_skin& skin = m_Data->skins[i];
					SkeletonData result;
					result.Name = (skin.name != nullptr) ? skin.name : ("Skin " + std::to_string(i));
					for (cgltf_size j = 0; j < skin.joints_count; j++)
					{
						result.Joints.push_back(static_cast<u32>(cgltf_node_index(m_Data, skin.joints[j])));
					}
					result.InverseBind.assign(skin.joints_count, glm::mat4(1.0f));
					if (skin.inverse_bind_matrices != nullptr)
					{
						for (cgltf_size j = 0; j < skin.joints_count && j < skin.inverse_bind_matrices->count; j++)
						{
							float matrix[16];
							cgltf_accessor_read_float(skin.inverse_bind_matrices, j, matrix, 16);
							result.InverseBind[j] = glm::make_mat4(matrix);
						}
					}
					m_Model.Skeletons.push_back(std::move(result));
				}

				for (cgltf_size i = 0; i < m_Data->animations_count; i++)
				{
					const cgltf_animation& animation = m_Data->animations[i];
					AnimationClipData result;
					result.Name = (animation.name != nullptr) ? animation.name : ("Animation " + std::to_string(i + 1));

					for (cgltf_size c = 0; c < animation.channels_count; c++)
					{
						const cgltf_animation_channel& source = animation.channels[c];
						if (source.target_node == nullptr || source.sampler == nullptr || source.sampler->input == nullptr || source.sampler->output == nullptr)
						{
							continue;
						}

						AnimationChannel channel;
						channel.Node = static_cast<u32>(cgltf_node_index(m_Data, source.target_node));
						switch (source.target_path)
						{
						case cgltf_animation_path_type_translation: channel.Path = AnimationPath::Translation; channel.Components = 3; break;
						case cgltf_animation_path_type_rotation: channel.Path = AnimationPath::Rotation; channel.Components = 4; break;
						case cgltf_animation_path_type_scale: channel.Path = AnimationPath::Scale; channel.Components = 3; break;
						case cgltf_animation_path_type_weights: channel.Path = AnimationPath::Weights; break;
						default:
							continue; // a path that moves nothing we draw
						}

						switch (source.sampler->interpolation)
						{
						case cgltf_interpolation_type_step: channel.Interpolation = AnimationInterpolation::Step; break;
						case cgltf_interpolation_type_cubic_spline: channel.Interpolation = AnimationInterpolation::CubicSpline; break;
						default: channel.Interpolation = AnimationInterpolation::Linear; break;
						}

						const cgltf_size timeCount = cgltf_accessor_unpack_floats(source.sampler->input, nullptr, 0);
						channel.Times.resize(timeCount);
						cgltf_accessor_unpack_floats(source.sampler->input, channel.Times.data(), timeCount);

						const cgltf_size valueCount = cgltf_accessor_unpack_floats(source.sampler->output, nullptr, 0);
						channel.Values.resize(valueCount);
						cgltf_accessor_unpack_floats(source.sampler->output, channel.Values.data(), valueCount);

						const cgltf_size perKey = (channel.Interpolation == AnimationInterpolation::CubicSpline) ? 3 : 1;
						if (channel.Path == AnimationPath::Weights)
						{
							channel.Components = (timeCount > 0) ? static_cast<u32>(valueCount / (timeCount * perKey)) : 0;
						}
						if (timeCount == 0 || channel.Components == 0 || valueCount < timeCount * channel.Components * perKey)
						{
							m_Model.Warnings.push_back("An animation channel with missing keys was skipped");
							continue;
						}

						result.Duration = std::max(result.Duration, channel.Times.back());
						result.Channels.push_back(std::move(channel));
					}

					if (!result.Channels.empty())
					{
						m_Model.Animations.push_back(std::move(result));
					}
				}
			}

			// One ImportedMesh per glTF mesh, attached to the nodes that use it.
			void convert_meshes()
			{
				m_Model.Meshes.resize(m_Data->meshes_count);
				std::vector<bool> usable(m_Data->meshes_count, false);

				for (cgltf_size m = 0; m < m_Data->meshes_count; m++)
				{
					const cgltf_mesh& source = m_Data->meshes[m];
					ImportedMesh& mesh = m_Model.Meshes[m];
					mesh.Data.Name = (source.name != nullptr) ? source.name : ("Mesh " + std::to_string(m));

					for (cgltf_size p = 0; p < source.primitives_count; p++)
					{
						convert_primitive(source.primitives[p], mesh);
					}

					if (mesh.Data.Primitives.empty())
					{
						continue;
					}

					if (!mesh.Data.SkinVertices.empty())
					{
						mesh.Data.SkinVertices.resize(mesh.Data.Vertices.size()); // vertices after the last skinned primitive have no skin
					}

					glm::vec3 minimum(1e30f), maximum(-1e30f);
					for (const MeshPrimitive& primitive : mesh.Data.Primitives)
					{
						minimum = glm::min(minimum, primitive.BoundsMin);
						maximum = glm::max(maximum, primitive.BoundsMax);
					}
					mesh.Data.BoundsMin = minimum;
					mesh.Data.BoundsMax = maximum;
					usable[m] = true;
				}

				for (cgltf_size i = 0; i < m_Data->nodes_count; i++)
				{
					const cgltf_node& node = m_Data->nodes[i];
					if (node.mesh != nullptr)
					{
						const size_t meshIndex = cgltf_mesh_index(m_Data, node.mesh);
						m_Model.NodeMesh[i] = usable[meshIndex] ? static_cast<int>(meshIndex) : -1;
					}
				}
			}

		private:
			ImportedMaterial convert_material(const cgltf_material& material)
			{
				ImportedMaterial imported;
				MaterialData& result = imported.Data;
				result.Name = get_material_name(material);

				if (material.has_pbr_metallic_roughness)
				{
					const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
					result.BaseColorFactor = glm::make_vec4(pbr.base_color_factor);
					result.MetallicFactor = pbr.metallic_factor;
					result.RoughnessFactor = pbr.roughness_factor;
					imported.BaseColorImage = get_image_index(m_Data, pbr.base_color_texture);
					imported.OrmImage = get_image_index(m_Data, pbr.metallic_roughness_texture);

					// A packed ORM image (the same image for occlusion and metallic-roughness) keeps its red channel
					// as ambient occlusion. Otherwise red is garbage as far as the spec goes, and is ignored.
					const int occlusionImage = get_image_index(m_Data, material.occlusion_texture);
					imported.PackedOrm = imported.OrmImage >= 0 && imported.OrmImage == occlusionImage;
				}
				else if (material.has_pbr_specular_glossiness)
				{
					// The old extension: only roughly mapped. Diffuse becomes the base color, glossiness the roughness.
					const cgltf_pbr_specular_glossiness& pbr = material.pbr_specular_glossiness;
					result.BaseColorFactor = glm::make_vec4(pbr.diffuse_factor);
					result.MetallicFactor = 0.0f;
					result.RoughnessFactor = 1.0f - pbr.glossiness_factor;
					imported.BaseColorImage = get_image_index(m_Data, pbr.diffuse_texture);
					m_Model.Warnings.push_back("Material " + get_material_name(material) + " uses specular-glossiness: approximated");
				}

				imported.NormalImage = get_image_index(m_Data, material.normal_texture);
				imported.EmissiveImage = get_image_index(m_Data, material.emissive_texture);
				result.EmissiveFactor = glm::make_vec3(material.emissive_factor);
				if (material.has_emissive_strength)
				{
					result.EmissiveFactor *= material.emissive_strength.emissive_strength;
				}
				if (material.has_clearcoat)
				{
					result.Clearcoat = std::clamp(material.clearcoat.clearcoat_factor, 0.0f, 1.0f);
					result.ClearcoatRoughness = std::clamp(material.clearcoat.clearcoat_roughness_factor, 0.0f, 1.0f);
				}
				if (material.has_sheen)
				{
					const float* color = material.sheen.sheen_color_factor;
					result.SheenIntensity = std::clamp(std::max(color[0], std::max(color[1], color[2])), 0.0f, 1.0f);
					result.SheenRoughness = std::clamp(material.sheen.sheen_roughness_factor, 0.0f, 1.0f);
				}
				if (material.has_transmission)
				{
					result.Transmission = std::clamp(material.transmission.transmission_factor, 0.0f, 1.0f);
				}

				result.DoubleSided = material.double_sided != 0;

				switch (material.alpha_mode)
				{
				case cgltf_alpha_mode_mask:
					result.Alpha = AlphaMode::Mask;
					result.AlphaCutoff = std::max(material.alpha_cutoff, 1e-3f); // 0 would mean "no test" to the shaders
					break;
				case cgltf_alpha_mode_blend:
					result.Alpha = AlphaMode::Blend;
					break;
				default:
					break;
				}

				// A material that lets the scene behind it through is drawn after the lighting, like a blended one.
				if (result.Transmission > 0.0f && result.Alpha == AlphaMode::Opaque)
				{
					result.Alpha = AlphaMode::Blend;
				}

				return imported;
			}

			void convert_primitive(const cgltf_primitive& primitive, ImportedMesh& mesh)
			{
				if (primitive.type != cgltf_primitive_type_triangles)
				{
					m_Model.Warnings.push_back("A primitive that is not a plain triangle list was skipped");
					return;
				}

				const cgltf_accessor* positions = nullptr;
				const cgltf_accessor* normals = nullptr;
				const cgltf_accessor* uvs = nullptr;
				const cgltf_accessor* joints = nullptr;
				const cgltf_accessor* weights = nullptr;
				for (cgltf_size i = 0; i < primitive.attributes_count; i++)
				{
					const cgltf_attribute& attribute = primitive.attributes[i];
					if (attribute.index != 0)
					{
						continue; // only the first set of texture coordinates is used
					}
					switch (attribute.type)
					{
					case cgltf_attribute_type_position: positions = attribute.data; break;
					case cgltf_attribute_type_normal: normals = attribute.data; break;
					case cgltf_attribute_type_texcoord: uvs = attribute.data; break;
					case cgltf_attribute_type_joints: joints = attribute.data; break;
					case cgltf_attribute_type_weights: weights = attribute.data; break;
					default: break;
					}
				}

				if (positions == nullptr || positions->count == 0)
				{
					m_Model.Warnings.push_back("A primitive without positions was skipped");
					return;
				}

				MeshData& data = mesh.Data;
				const size_t firstVertex = data.Vertices.size();
				const size_t vertexCount = positions->count;
				const size_t normalCount = (normals != nullptr) ? normals->count : 0;
				const size_t uvCount = (uvs != nullptr) ? uvs->count : 0;

				glm::vec3 boundsMin(1e30f), boundsMax(-1e30f);
				data.Vertices.resize(firstVertex + vertexCount);

				for (size_t i = 0; i < vertexCount; i++)
				{
					MeshVertex& vertex = data.Vertices[firstVertex + i];
					cgltf_accessor_read_float(positions, i, vertex.Position, 3);

					if (i < normalCount)
					{
						cgltf_accessor_read_float(normals, i, vertex.Normal, 3);
					}
					if (i < uvCount)
					{
						cgltf_accessor_read_float(uvs, i, vertex.Uv, 2);
					}

					boundsMin = glm::min(boundsMin, glm::make_vec3(vertex.Position));
					boundsMax = glm::max(boundsMax, glm::make_vec3(vertex.Position));
				}

				// Indices, relative to the first vertex of the primitive. A primitive without indices is drawn in order.
				// A broken file can point past its own vertices: those triangles are dropped instead of reading garbage.
				const size_t indexCount = (primitive.indices != nullptr) ? primitive.indices->count : vertexCount;
				const size_t triangleIndexCount = indexCount - indexCount % 3; // whole triangles only

				std::vector<u32> validIndices;
				validIndices.reserve(triangleIndexCount);
				for (size_t i = 0; i < triangleIndexCount; i += 3)
				{
					u32 triangle[3];
					for (size_t corner = 0; corner < 3; corner++)
					{
						triangle[corner] = (primitive.indices != nullptr)
							? static_cast<u32>(cgltf_accessor_read_index(primitive.indices, i + corner))
							: static_cast<u32>(i + corner);
					}

					if (triangle[0] < vertexCount && triangle[1] < vertexCount && triangle[2] < vertexCount)
					{
						validIndices.insert(validIndices.end(), triangle, triangle + 3);
					}
				}

				if (validIndices.empty())
				{
					data.Vertices.resize(firstVertex);
					m_Model.Warnings.push_back("A primitive without valid triangles was skipped");
					return;
				}

				if (normals == nullptr)
				{
					compute_normals(data.Vertices, firstVertex, validIndices);
				}

				MeshPrimitive result;
				result.VertexCount = static_cast<u32>(vertexCount);

				// Skinning: four joints and weights per vertex. SkinVertices stays parallel to Vertices.
				if (joints != nullptr && weights != nullptr && joints->count >= vertexCount && weights->count >= vertexCount)
				{
					data.SkinVertices.resize(firstVertex + vertexCount);
					for (size_t i = 0; i < vertexCount; i++)
					{
						MeshSkinVertex& skinVertex = data.SkinVertices[firstVertex + i];
						cgltf_uint jointIndices[4] = {};
						cgltf_accessor_read_uint(joints, i, jointIndices, 4);
						cgltf_accessor_read_float(weights, i, skinVertex.Weights, 4);
						const float total = skinVertex.Weights[0] + skinVertex.Weights[1] + skinVertex.Weights[2] + skinVertex.Weights[3];
						for (int k = 0; k < 4; k++)
						{
							skinVertex.Joints[k] = jointIndices[k];
							skinVertex.Weights[k] = (total > 1e-6f) ? skinVertex.Weights[k] / total : (k == 0 ? 1.0f : 0.0f);
						}
					}
					result.Skinned = true;
				}

				// Morph targets: the offsets of every vertex for every target.
				if (primitive.targets_count > 0)
				{
					result.MorphTargetCount = static_cast<u32>(primitive.targets_count);
					result.MorphOffset = static_cast<u32>(data.MorphPositions.size());
					data.MorphPositions.resize(result.MorphOffset + primitive.targets_count * vertexCount, glm::vec4(0.0f));
					data.MorphNormals.resize(result.MorphOffset + primitive.targets_count * vertexCount, glm::vec4(0.0f));

					for (cgltf_size t = 0; t < primitive.targets_count; t++)
					{
						const cgltf_morph_target& target = primitive.targets[t];
						for (cgltf_size a = 0; a < target.attributes_count; a++)
						{
							const cgltf_attribute& attribute = target.attributes[a];
							const bool isPosition = attribute.type == cgltf_attribute_type_position;
							const bool isNormal = attribute.type == cgltf_attribute_type_normal;
							if ((!isPosition && !isNormal) || attribute.data == nullptr || attribute.data->count < vertexCount)
							{
								continue;
							}

							std::vector<glm::vec4>& destination = isPosition ? data.MorphPositions : data.MorphNormals;
							for (size_t i = 0; i < vertexCount; i++)
							{
								float delta[3] = {};
								cgltf_accessor_read_float(attribute.data, i, delta, 3);
								destination[result.MorphOffset + t * vertexCount + i] = glm::vec4(delta[0], delta[1], delta[2], 0.0f);
								if (isPosition)
								{
									result.MaxMorphDelta = std::max(result.MaxMorphDelta, glm::length(glm::vec3(delta[0], delta[1], delta[2])));
								}
							}
						}
					}
				}

				result.BaseVertex = static_cast<i32>(firstVertex);
				result.FirstIndex = static_cast<u32>(data.Indices.size());
				result.IndexCount = static_cast<u32>(validIndices.size());
				result.BoundsMin = boundsMin;
				result.BoundsMax = boundsMax;
				result.LodCount = 1; // no simplified levels yet (meshoptimizer is not part of the asset pipeline)
				result.Lods[0] = MeshLod{ result.FirstIndex, result.IndexCount, 0.0f };

				data.Indices.insert(data.Indices.end(), validIndices.begin(), validIndices.end());
				data.Primitives.push_back(result);
				mesh.PrimitiveMaterials.push_back((primitive.material != nullptr)
					? static_cast<u32>(cgltf_material_index(m_Data, primitive.material))
					: m_DefaultMaterial);
			}

			const cgltf_data* m_Data{};
			ImportedModel& m_Model;
			u32 m_DefaultMaterial = 0;
		};

		std::string decode_uri(std::string uri)
		{
			uri.resize(cgltf_decode_uri(uri.data())); // %20 and friends
			return uri;
		}
	}

	bool read_gltf_model(const std::filesystem::path& path, ImportedModel& model, std::string& error)
	{
		const std::string pathText = path.string();

		cgltf_options options{};
		cgltf_data* rawData = nullptr;
		cgltf_result result = cgltf_parse_file(&options, pathText.c_str(), &rawData);
		if (result != cgltf_result_success)
		{
			error = "Failed to read " + pathText + " as glTF (cgltf error " + std::to_string(static_cast<int>(result)) + ")";
			return false;
		}
		CgltfPointer data(rawData);

		// The .bin files next to a .gltf; for a .glb the data is already in memory.
		result = cgltf_load_buffers(&options, data.get(), pathText.c_str());
		if (result != cgltf_result_success)
		{
			error = "Failed to load the buffers of " + pathText + " (cgltf error " + std::to_string(static_cast<int>(result)) + ")";
			return false;
		}

		model.Name = path.stem().string();

		Converter converter(data.get(), model);
		converter.convert_materials();
		converter.convert_nodes_and_animation();
		converter.convert_meshes();

		if (std::none_of(model.NodeMesh.begin(), model.NodeMesh.end(), [](int mesh) { return mesh >= 0; }))
		{
			error = pathText + " has no triangle meshes to draw";
			return false;
		}

		// Images: only the ones some material uses. How each is used decides how it is decoded (sRGB color, normals, data).
		model.Images.resize(data->images_count);
		for (size_t i = 0; i < data->images_count; i++)
		{
			const cgltf_image& image = data->images[i];
			ImportedImage& imported = model.Images[i];
			imported.Name = image.name != nullptr ? image.name : (image.uri != nullptr ? image.uri : "image" + std::to_string(i));
		}

		auto assign = [&](int image, TextureKind kind)
		{
			if (image < 0 || image >= static_cast<int>(model.Images.size()))
			{
				return;
			}

			ImportedImage& imported = model.Images[image];
			if (!imported.Used)
			{
				imported.Used = true;
				imported.Kind = kind;
			}
			else if (imported.Kind != kind)
			{
				model.Warnings.push_back("Image " + imported.Name + " is used in two different ways: it is only decoded for the first");
			}
		};

		for (size_t i = 0; i + 1 < model.Materials.size(); i++) // the last material is the default
		{
			const ImportedMaterial& material = model.Materials[i];
			assign(material.BaseColorImage, TextureKind::Color);
			assign(material.NormalImage, TextureKind::Normal);
			assign(material.EmissiveImage, TextureKind::Color);
			assign(material.OrmImage, material.PackedOrm ? TextureKind::Data : TextureKind::MetallicRoughness);
		}

		for (size_t i = 0; i < data->images_count; i++)
		{
			const cgltf_image& image = data->images[i];
			ImportedImage& imported = model.Images[i];
			if (!imported.Used)
			{
				continue;
			}

			if (image.buffer_view != nullptr && image.buffer_view->buffer->data != nullptr)
			{
				// Embedded in the .glb (or in a buffer): the bytes of a PNG or JPEG file.
				const std::byte* bytes = static_cast<const std::byte*>(image.buffer_view->buffer->data) + image.buffer_view->offset;
				imported.Embedded.assign(bytes, bytes + image.buffer_view->size);
			}
			else if (image.uri != nullptr)
			{
				const std::string uri = image.uri;
				if (uri.rfind("data:", 0) == 0)
				{
					model.Warnings.push_back("Image " + imported.Name + " is a data: URI, which is not supported");
					imported.Used = false;
				}
				else
				{
					imported.File = path.parent_path() / decode_uri(uri);
				}
			}
			else
			{
				model.Warnings.push_back("Image " + imported.Name + " has no data");
				imported.Used = false;
			}
		}

		return true;
	}
}
