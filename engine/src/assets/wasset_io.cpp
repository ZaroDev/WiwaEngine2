#include <pch.h>
#include "wasset_io.h"

#include <fstream>

namespace wiwa
{
	BinaryArchive::BinaryArchive(bool writing, std::span<const std::byte> bytes) : m_Writing(writing)
	{
		if (!writing)
		{
			m_Bytes.assign(bytes.begin(), bytes.end());
		}
	}

	void BinaryArchive::raw(void* data, size_t size)
	{
		if (!m_Ok || size == 0)
		{
			return;
		}

		if (m_Writing)
		{
			const std::byte* begin = static_cast<const std::byte*>(data);
			m_Bytes.insert(m_Bytes.end(), begin, begin + size);
			return;
		}

		if (size > remaining())
		{
			m_Ok = false;
			return;
		}

		std::memcpy(data, m_Bytes.data() + m_Cursor, size);
		m_Cursor += size;
	}

	void BinaryArchive::string(std::string& s)
	{
		u64 length = s.size();
		value(length);
		if (reading())
		{
			if (!m_Ok || length > remaining())
			{
				fail();
				return;
			}
			s.resize(static_cast<size_t>(length));
		}
		raw(s.data(), static_cast<size_t>(length));
	}

	// ---- header ----

	std::vector<std::byte> make_wasset_blob(const WAssetHeader& header, std::span<const std::byte> payload)
	{
		BinaryArchive ar = BinaryArchive::writer();

		u32 magic = WASSET_MAGIC;
		u32 version = WASSET_VERSION;
		u32 type = static_cast<u32>(header.Type);
		u32 dependencyCount = static_cast<u32>(header.Dependencies.size());
		u64 payloadSize = payload.size();

		ar.value(magic);
		ar.value(version);
		ar.value(type);
		AssetGuid guid = header.Guid;
		ar.value(guid);
		ar.value(dependencyCount);
		for (AssetGuid dependency : header.Dependencies)
		{
			ar.value(dependency);
		}
		ar.value(payloadSize);

		std::vector<std::byte> blob = ar.take();
		blob.insert(blob.end(), payload.begin(), payload.end());
		return blob;
	}

	bool write_wasset(const std::filesystem::path& path, const WAssetHeader& header, std::span<const std::byte> payload)
	{
		const std::vector<std::byte> blob = make_wasset_blob(header, payload);

		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);

		std::filesystem::path temp = path;
		temp += ".tmp";

		{
			std::ofstream file(temp, std::ios::binary | std::ios::trunc);
			if (!file)
			{
				return false;
			}
			file.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
			if (!file)
			{
				return false;
			}
		}

		std::filesystem::rename(temp, path, error);
		if (error)
		{
			std::filesystem::remove(temp, error);
			return false;
		}

		return true;
	}

	std::optional<WAssetHeader> parse_wasset_header(std::span<const std::byte> blob, std::span<const std::byte>& payload)
	{
		BinaryArchive ar = BinaryArchive::reader(blob);

		u32 magic = 0;
		u32 version = 0;
		u32 type = 0;
		u32 dependencyCount = 0;
		WAssetHeader header;

		ar.value(magic);
		ar.value(version);
		ar.value(type);
		ar.value(header.Guid);
		ar.value(dependencyCount);

		if (!ar.ok() || magic != WASSET_MAGIC || version != WASSET_VERSION || type == 0 || type >= static_cast<u32>(AssetType::Last))
		{
			return std::nullopt;
		}

		header.Type = static_cast<AssetType>(type);

		// 8 bytes of payload size follow the dependencies
		constexpr size_t headFixed = 4 * 3 + sizeof(AssetGuid) + 4;
		if (dependencyCount > (blob.size() - headFixed) / sizeof(AssetGuid))
		{
			return std::nullopt;
		}

		header.Dependencies.resize(dependencyCount);
		for (AssetGuid& dependency : header.Dependencies)
		{
			ar.value(dependency);
		}
		ar.value(header.PayloadSize);

		if (!ar.ok())
		{
			return std::nullopt;
		}

		const size_t headerSize = headFixed + static_cast<size_t>(dependencyCount) * sizeof(AssetGuid) + sizeof(u64);
		if (header.PayloadSize != blob.size() - headerSize)
		{
			return std::nullopt;
		}

		payload = blob.subspan(headerSize);
		return header;
	}

	// ---- payloads ----

	namespace
	{
		template<typename E>
		void archive_enum(BinaryArchive& ar, E& e, u32 count)
		{
			u32 raw = static_cast<u32>(e);
			ar.value(raw);
			if (ar.reading())
			{
				if (raw >= count)
				{
					ar.fail();
					return;
				}
				e = static_cast<E>(raw);
			}
		}

		void archive_flag(BinaryArchive& ar, bool& flag)
		{
			u8 raw = flag ? 1 : 0;
			ar.value(raw);
			if (ar.reading())
			{
				flag = raw != 0;
			}
		}
	}

	void archive(BinaryArchive& ar, TextureData& v)
	{
		ar.value(v.Width);
		ar.value(v.Height);
		archive_enum(ar, v.Kind, 4);
		ar.list(v.Mips, [&](std::vector<u8>& mip) { ar.pod_vector(mip); });

		if (ar.reading() && ar.ok())
		{
			const u64 expected = static_cast<u64>(v.Width) * v.Height * 4;
			if (v.Width == 0 || v.Height == 0 || v.Mips.empty() || v.Mips[0].size() != expected)
			{
				ar.fail();
			}
		}
	}

	void archive(BinaryArchive& ar, EnvironmentData& v)
	{
		ar.value(v.FaceSize);
		ar.list(v.Mips, [&](std::vector<u16>& mip) { ar.pod_vector(mip); });
		for (glm::vec4& coefficient : v.SH)
		{
			ar.value(coefficient);
		}

		if (ar.reading() && ar.ok())
		{
			bool valid = v.FaceSize >= 8 && v.FaceSize <= 2048 && (v.FaceSize & (v.FaceSize - 1)) == 0 && !v.Mips.empty() && v.Mips.size() <= 12;
			for (size_t mip = 0; valid && mip < v.Mips.size(); mip++)
			{
				const u64 size = std::max<u32>(1, v.FaceSize >> mip);
				valid = v.Mips[mip].size() == 6 * size * size * 4;
			}
			for (const glm::vec4& coefficient : v.SH)
			{
				valid = valid && std::isfinite(coefficient.x) && std::isfinite(coefficient.y) && std::isfinite(coefficient.z);
			}
			if (!valid)
			{
				ar.fail();
			}
		}
	}

	void archive(BinaryArchive& ar, LutData& v)
	{
		ar.string(v.Title);
		ar.value(v.Size);
		ar.pod_vector(v.Table);

		if (ar.reading() && ar.ok())
		{
			const u64 size = v.Size;
			if (v.Size > 256 || v.Table.size() != size * size * size * 3)
			{
				ar.fail();
			}
		}
	}

	void archive(BinaryArchive& ar, ShaderData& v)
	{
		ar.string(v.Name);
		ar.list(v.Binaries, [&](ShaderBinary& b)
			{
				archive_enum(ar, b.Stage, 3);
				archive_enum(ar, b.Target, 2);
				ar.string(b.Entry);
				ar.pod_vector(b.Code);
				if (ar.reading() && ar.ok() && b.Code.empty())
				{
					ar.fail();
				}
			});
	}

	void archive(BinaryArchive& ar, MeshData& v)
	{
		ar.string(v.Name);
		ar.pod_vector(v.Vertices);
		ar.pod_vector(v.Indices);
		ar.list(v.Primitives, [&](MeshPrimitive& p)
			{
				ar.value(p.IndexCount);
				ar.value(p.FirstIndex);
				ar.value(p.BaseVertex);
				ar.value(p.Material);
				ar.value(p.BoundsMin);
				ar.value(p.BoundsMax);
				ar.value(p.LodCount);
				ar.value(p.Lods);
				ar.value(p.VertexCount);
				archive_flag(ar, p.Skinned);
				ar.value(p.MorphTargetCount);
				ar.value(p.MorphOffset);
				ar.value(p.MaxMorphDelta);
				if (ar.reading() && (p.LodCount == 0 || p.LodCount > MAX_MESH_LODS))
				{
					ar.fail();
				}
			});
		ar.pod_vector(v.SkinVertices);
		ar.pod_vector(v.MorphPositions);
		ar.pod_vector(v.MorphNormals);
		ar.value(v.BoundsMin);
		ar.value(v.BoundsMax);

		if (ar.reading() && ar.ok())
		{
			// primitives must stay inside the buffers they index
			for (const MeshPrimitive& p : v.Primitives)
			{
				for (u32 lod = 0; lod < p.LodCount; lod++)
				{
					const u64 end = static_cast<u64>(p.Lods[lod].FirstIndex) + p.Lods[lod].IndexCount;
					if (end > v.Indices.size())
					{
						ar.fail();
						return;
					}
				}
				const u64 firstIndexEnd = static_cast<u64>(p.FirstIndex) + p.IndexCount;
				if (firstIndexEnd > v.Indices.size())
				{
					ar.fail();
					return;
				}
			}
		}
	}

	void archive(BinaryArchive& ar, MaterialData& v)
	{
		ar.string(v.Name);
		ar.value(v.BaseColorFactor);
		archive_enum(ar, v.Alpha, 3);
		ar.value(v.AlphaCutoff);
		archive_flag(ar, v.DoubleSided);
		ar.value(v.MetallicFactor);
		ar.value(v.RoughnessFactor);
		ar.value(v.EmissiveFactor);
		ar.value(v.Clearcoat);
		ar.value(v.ClearcoatRoughness);
		ar.value(v.SheenIntensity);
		ar.value(v.SheenRoughness);
		ar.value(v.Transmission);
		ar.value(v.BaseColorTexture);
		ar.value(v.NormalTexture);
		ar.value(v.OrmTexture);
		ar.value(v.EmissiveTexture);
	}

	void archive(BinaryArchive& ar, SkeletonData& v)
	{
		ar.string(v.Name);
		ar.pod_vector(v.Joints);
		ar.pod_vector(v.InverseBind);

		if (ar.reading() && ar.ok() && v.Joints.size() != v.InverseBind.size())
		{
			ar.fail();
		}
	}

	void archive(BinaryArchive& ar, AnimationClipData& v)
	{
		ar.string(v.Name);
		ar.value(v.Duration);
		ar.list(v.Channels, [&](AnimationChannel& c)
			{
				ar.value(c.Node);
				archive_enum(ar, c.Path, 4);
				archive_enum(ar, c.Interpolation, 3);
				ar.value(c.Components);
				ar.pod_vector(c.Times);
				ar.pod_vector(c.Values);
			});
	}

	void archive(BinaryArchive& ar, PrefabData& v)
	{
		ar.string(v.Name);
		ar.list(v.Nodes, [&](PrefabNode& n)
			{
				ar.string(n.Name);
				ar.value(n.Parent);
				ar.value(n.StableId);
				ar.value(n.Translation);
				ar.value(n.Rotation);
				ar.value(n.Scale);
				ar.pod_vector(n.MorphWeights);
				ar.value(n.Mesh);
				ar.value(n.Skeleton);
			});
		ar.pod_vector(v.Animations);
		ar.value(v.BoundsMin);
		ar.value(v.BoundsMax);

		if (ar.reading() && ar.ok())
		{
			for (const PrefabNode& n : v.Nodes)
			{
				if (n.StableId == 0 || n.Parent < -1 || n.Parent >= static_cast<i32>(v.Nodes.size()))
				{
					ar.fail();
					return;
				}
			}
		}
	}
}
