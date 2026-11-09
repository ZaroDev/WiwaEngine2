#include <editor_common.h>
#include "skeleton_dependents.h"

#include <unordered_map>

#include <assets/native_anim_files.h>

#include <asset_pipeline/asset_database.h>
#include <asset_pipeline/atomic_write.h>
#include <asset_pipeline/file_utils.h>

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		using RenameMap = std::unordered_map<std::string, std::string>;

		RenameMap to_map(const std::vector<std::pair<std::string, std::string>>& renames)
		{
			RenameMap map;
			for (const auto& [from, to] : renames)
			{
				map[from] = to;
			}
			return map;
		}

		u32 count_renamed(const std::string& name, const RenameMap& map) { return map.contains(name) ? 1u : 0u; }

		// The joint names of the file that a rename changes; the file's skeleton, or an empty reference when it cannot be read.
		struct Reading
		{
			bool Ok = false;
			AssetRef Skeleton{};
			u32 Joints = 0;
		};

		Reading read_mesh(const std::filesystem::path& file, const RenameMap& map)
		{
			Reading out;
			const std::optional<std::vector<std::byte>> bytes = read_binary_file(file);
			std::string reason;
			const std::optional<MeshData> mesh = bytes ? read_mesh_file(*bytes, reason) : std::nullopt;
			if (mesh)
			{
				out.Ok = true;
				out.Skeleton = mesh->Skeleton;
				for (const std::string& name : mesh->SkinJointNames)
				{
					out.Joints += count_renamed(name, map);
				}
			}
			return out;
		}

		Reading read_clip(const std::filesystem::path& file, const RenameMap& map)
		{
			Reading out;
			const std::optional<std::vector<std::byte>> bytes = read_binary_file(file);
			std::string reason;
			const std::optional<AnimationClipData> clip = bytes ? read_clip_file(*bytes, reason) : std::nullopt;
			if (clip)
			{
				out.Ok = true;
				out.Skeleton = clip->Skeleton;
				std::unordered_map<std::string, bool> seen;
				for (const AnimationChannel& channel : clip->Channels)
				{
					if (map.contains(channel.Joint) && seen.emplace(channel.Joint, true).second)
					{
						out.Joints++;
					}
				}
			}
			return out;
		}

		bool is_writable(const std::filesystem::path& file)
		{
			std::error_code error;
			const std::filesystem::perms permissions = std::filesystem::status(file, error).permissions();
			return !error && (permissions & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
		}

		// The new content of a dependent, or nullopt (and why) when it cannot be read again.
		std::optional<std::vector<std::byte>> renamed_content(const SkeletonDependent& item, const RenameMap& map, std::string& error)
		{
			const std::optional<std::vector<std::byte>> bytes = read_binary_file(item.File);
			std::string reason;
			if (item.IsMesh)
			{
				std::optional<MeshData> mesh = bytes ? read_mesh_file(*bytes, reason) : std::nullopt;
				if (!mesh)
				{
					error = item.File.filename().string() + ": " + (bytes ? reason : std::string("cannot read the file"));
					return std::nullopt;
				}
				for (std::string& name : mesh->SkinJointNames)
				{
					if (const auto found = map.find(name); found != map.end())
					{
						name = found->second;
					}
				}
				return write_mesh_file(*mesh);
			}
			std::optional<AnimationClipData> clip = bytes ? read_clip_file(*bytes, reason) : std::nullopt;
			if (!clip)
			{
				error = item.File.filename().string() + ": " + (bytes ? reason : std::string("cannot read the file"));
				return std::nullopt;
			}
			for (AnimationChannel& channel : clip->Channels)
			{
				if (const auto found = map.find(channel.Joint); found != map.end())
				{
					channel.Joint = found->second;
				}
			}
			return write_clip_file(*clip);
		}
	}

	std::unordered_map<AssetGuid, SkeletonUsers> index_skeleton_users(const AssetDatabase& database, const std::atomic<bool>* cancel)
	{
		const RenameMap none;
		std::unordered_map<AssetGuid, SkeletonUsers> index;
		for (const AssetDatabase::SourceRoot& root : database.get_roots())
		{
			std::error_code error;
			for (std::filesystem::recursive_directory_iterator it(root.Source, std::filesystem::directory_options::skip_permission_denied, error), end;
				!error && it != end; it.increment(error))
			{
				if (cancel != nullptr && cancel->load())
				{
					return index;
				}
				if (!it->is_regular_file(error))
				{
					continue;
				}
				const std::filesystem::path& file = it->path();
				const bool isMesh = file.extension() == ".wmesh";
				if (!isMesh && file.extension() != ".wanim")
				{
					continue;
				}
				const Reading reading = isMesh ? read_mesh(file, none) : read_clip(file, none);
				if (reading.Ok && reading.Skeleton.valid())
				{
					SkeletonUsers& users = index[database.redirected(reading.Skeleton).Guid];
					(isMesh ? users.Meshes : users.Clips).push_back(file);
				}
			}
		}
		for (auto& [guid, users] : index)
		{
			std::sort(users.Meshes.begin(), users.Meshes.end());
			std::sort(users.Clips.begin(), users.Clips.end());
		}
		return index;
	}

	SkeletonDependents find_skeleton_dependents(const AssetDatabase& database, AssetGuid skeleton,
		const std::vector<std::pair<std::string, std::string>>& renames, const std::atomic<bool>* cancel)
	{
		const RenameMap map = to_map(renames);
		SkeletonDependents out;
		for (const AssetDatabase::SourceRoot& root : database.get_roots())
		{
			std::error_code error;
			for (std::filesystem::recursive_directory_iterator it(root.Source, std::filesystem::directory_options::skip_permission_denied, error), end;
				!error && it != end; it.increment(error))
			{
				if (cancel != nullptr && cancel->load())
				{
					return out;
				}
				if (!it->is_regular_file(error))
				{
					continue;
				}
				const std::filesystem::path& file = it->path();
				const bool isMesh = file.extension() == ".wmesh";
				if (!isMesh && file.extension() != ".wanim")
				{
					continue;
				}
				const Reading reading = isMesh ? read_mesh(file, map) : read_clip(file, map);
				if (!reading.Ok)
				{
					out.Unreadable.push_back(file);
					continue;
				}
				if (reading.Skeleton.valid() && database.redirected(reading.Skeleton).Guid == skeleton)
				{
					out.Files.push_back(SkeletonDependent{ file, isMesh, reading.Joints, is_writable(file) });
				}
			}
		}
		std::sort(out.Files.begin(), out.Files.end(), [](const SkeletonDependent& a, const SkeletonDependent& b) { return a.File < b.File; });
		return out;
	}

	bool apply_joint_renames(const SkeletonDependents& dependents, const std::vector<std::pair<std::string, std::string>>& renames,
		const std::filesystem::path& skeletonFile, const std::string& skeletonText, std::string& error)
	{
		error.clear();
		const RenameMap map = to_map(renames);

		// everything is made before anything is written
		struct Pending
		{
			std::filesystem::path File{};
			std::vector<std::byte> NewBytes{};
			std::vector<std::byte> OldBytes{};
		};
		std::vector<Pending> pending;
		if (!renames.empty())
		{
			for (const SkeletonDependent& item : dependents.Files)
			{
				if (item.Joints == 0)
				{
					continue;
				}
				if (!is_writable(item.File))
				{
					error = item.File.filename().string() + " is read-only";
					return false;
				}
				std::optional<std::vector<std::byte>> content = renamed_content(item, map, error);
				std::optional<std::vector<std::byte>> old = read_binary_file(item.File);
				if (!content || !old)
				{
					if (error.empty())
					{
						error = "cannot read " + item.File.string();
					}
					return false;
				}
				pending.push_back(Pending{ item.File, std::move(*content), std::move(*old) });
			}
		}

		for (size_t i = 0; i < pending.size(); i++)
		{
			if (!write_file_atomic(pending[i].File, pending[i].NewBytes.data(), pending[i].NewBytes.size(), error))
			{
				std::string ignored;
				for (size_t done = 0; done < i; done++)
				{
					write_file_atomic(pending[done].File, pending[done].OldBytes.data(), pending[done].OldBytes.size(), ignored);
				}
				return false;
			}
		}

		// the skeleton last: a failure before this leaves it as it was
		if (!write_file_atomic(skeletonFile, skeletonText.data(), skeletonText.size(), error))
		{
			std::string ignored;
			for (const Pending& item : pending)
			{
				write_file_atomic(item.File, item.OldBytes.data(), item.OldBytes.size(), ignored);
			}
			return false;
		}
		return true;
	}
}
