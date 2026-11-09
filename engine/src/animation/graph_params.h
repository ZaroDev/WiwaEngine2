#pragma once

#include <string>
#include <vector>

#include <assets/asset_data.h>

namespace wiwa
{
	// The parameter values of one graph instance, by the order of AnimationGraphData::Parameters. A bool is 0 or 1, an int is whole, a trigger is
	// raised with fire() and stays up until a transition consumes it or the frame ends (end_frame). Main thread only.
	class GraphParams
	{
	public:
		explicit GraphParams(const std::vector<GraphParameter>& parameters);

		size_t count() const { return m_Names.size(); }
		// -1 when there is no parameter with this name.
		int index_of(const std::string& name) const;
		const std::string& name(int index) const { return m_Names[static_cast<size_t>(index)]; }
		GraphParamType type_of(int index) const { return m_Types[static_cast<size_t>(index)]; }

		// A number that is not finite, an index out of range and a trigger are ignored (a trigger is raised with fire). Bools become 0/1, ints are truncated.
		void set(int index, float value);
		// The value; for a trigger 1 while it is raised. 0 for an index out of range.
		float get(int index) const;
		void fire(int index);
		bool fired(int index) const;
		void consume(int index);
		// Drops every raised trigger (after the whole tree advanced).
		void end_frame();
		// Back to the defaults, no trigger raised.
		void reset();

	private:
		bool valid(int index) const { return index >= 0 && static_cast<size_t>(index) < m_Names.size(); }

		std::vector<std::string> m_Names{};
		std::vector<GraphParamType> m_Types{};
		std::vector<float> m_Defaults{};
		std::vector<float> m_Values{};
		std::vector<char> m_Raised{};
	};
}
