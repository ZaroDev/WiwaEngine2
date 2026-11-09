#include <editor_common.h>
#include "reference_finder.h"

#include <sstream>

namespace wiwa
{
	namespace
	{
		bool holds_references(const std::filesystem::path& file)
		{
			const std::string extension = file.extension().string();
			return extension == ".wscene" || extension == ".wprefab" || extension == ".wmat" || extension == ".wuipanel" || extension == ".winput" || extension == ".wphysmat";
		}

		int count_of(const std::string& text, const std::string& needle)
		{
			int count = 0;
			for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size()))
			{
				count++;
			}
			return count;
		}
	}

	ReferenceFinder::~ReferenceFinder()
	{
		stop();
	}

	void ReferenceFinder::stop()
	{
		if (m_State != nullptr)
		{
			m_State->Cancel = true;
		}
		if (m_Task.valid())
		{
			m_Task.wait();
		}
	}

	float ReferenceFinder::progress() const
	{
		if (m_State == nullptr)
		{
			return 0.0f;
		}
		const size_t total = m_State->Total.load();
		return total == 0 ? (m_State->Finished.load() ? 1.0f : 0.0f) : static_cast<float>(m_State->Done.load()) / static_cast<float>(total);
	}

	std::vector<ReferenceHit> ReferenceFinder::hits() const
	{
		if (m_State == nullptr)
		{
			return {};
		}
		std::vector<ReferenceHit> copy;
		{
			std::lock_guard<std::mutex> lock(m_State->Mutex);
			copy = m_State->Hits;
		}
		std::sort(copy.begin(), copy.end(), [](const ReferenceHit& a, const ReferenceHit& b) { return a.Count != b.Count ? a.Count > b.Count : a.File < b.File; });
		return copy;
	}

	void ReferenceFinder::start(AssetGuid guid, std::string fileName, std::vector<std::filesystem::path> roots, std::vector<AssetGuid> redirectedFrom)
	{
		stop();
		m_Target = guid;
		m_State = std::make_shared<State>();
		const std::shared_ptr<State> state = m_State;
		const std::string needle = guid.to_string();
		std::vector<std::string> legacyNeedles;
		for (AssetGuid source : redirectedFrom)
		{
			legacyNeedles.push_back(source.to_string());
		}

		m_Task = std::async(std::launch::async, [state, needle, legacyNeedles = std::move(legacyNeedles), fileName = std::move(fileName), roots = std::move(roots)]()
		{
			std::vector<std::filesystem::path> files;
			for (const std::filesystem::path& root : roots)
			{
				std::error_code error;
				for (std::filesystem::recursive_directory_iterator it(root, error), end; !error && it != end && !state->Cancel; it.increment(error))
				{
					if (it->is_regular_file(error) && holds_references(it->path()))
					{
						files.push_back(it->path());
					}
				}
			}
			state->Total = files.size();

			for (const std::filesystem::path& file : files)
			{
				if (state->Cancel)
				{
					break;
				}
				std::ifstream in(file, std::ios::binary);
				std::stringstream text;
				text << in.rdbuf();
				int count = count_of(text.str(), needle);
				// a reference that still names the model (and the local id the asset had as a sub-asset) reaches the asset through a redirect
				for (const std::string& legacy : legacyNeedles)
				{
					count += count_of(text.str(), legacy);
				}
				if (!fileName.empty() && file.extension() == ".wmat")
				{
					count += count_of(text.str(), fileName);
				}
				if (count > 0)
				{
					std::lock_guard<std::mutex> lock(state->Mutex);
					state->Hits.push_back(ReferenceHit{ file, count });
				}
				state->Done++;
			}
			state->Finished = true;
		});
	}
}
