#include <pch.h>
#include "graph_params.h"

#include <cmath>

namespace wiwa
{
	GraphParams::GraphParams(const std::vector<GraphParameter>& parameters)
	{
		for (const GraphParameter& parameter : parameters)
		{
			m_Names.push_back(parameter.Name);
			m_Types.push_back(parameter.Type);
			float value = parameter.Default;
			if (parameter.Type == GraphParamType::Bool)
			{
				value = value != 0.0f ? 1.0f : 0.0f;
			}
			else if (parameter.Type == GraphParamType::Int)
			{
				value = std::trunc(value);
			}
			else if (parameter.Type == GraphParamType::Trigger)
			{
				value = 0.0f;
			}
			m_Defaults.push_back(value);
		}
		m_Values = m_Defaults;
		m_Raised.assign(m_Names.size(), 0);
	}

	int GraphParams::index_of(const std::string& name) const
	{
		for (size_t i = 0; i < m_Names.size(); i++)
		{
			if (m_Names[i] == name)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void GraphParams::set(int index, float value)
	{
		if (!valid(index) || !std::isfinite(value))
		{
			return;
		}
		const size_t i = static_cast<size_t>(index);
		switch (m_Types[i])
		{
		case GraphParamType::Float: m_Values[i] = value; break;
		case GraphParamType::Int: m_Values[i] = std::trunc(value); break;
		case GraphParamType::Bool: m_Values[i] = value != 0.0f ? 1.0f : 0.0f; break;
		case GraphParamType::Trigger: break;
		}
	}

	float GraphParams::get(int index) const
	{
		if (!valid(index))
		{
			return 0.0f;
		}
		const size_t i = static_cast<size_t>(index);
		return m_Types[i] == GraphParamType::Trigger ? (m_Raised[i] != 0 ? 1.0f : 0.0f) : m_Values[i];
	}

	void GraphParams::fire(int index)
	{
		if (valid(index) && m_Types[static_cast<size_t>(index)] == GraphParamType::Trigger)
		{
			m_Raised[static_cast<size_t>(index)] = 1;
		}
	}

	bool GraphParams::fired(int index) const
	{
		return valid(index) && m_Raised[static_cast<size_t>(index)] != 0;
	}

	void GraphParams::consume(int index)
	{
		if (valid(index))
		{
			m_Raised[static_cast<size_t>(index)] = 0;
		}
	}

	void GraphParams::end_frame()
	{
		std::fill(m_Raised.begin(), m_Raised.end(), static_cast<char>(0));
	}

	void GraphParams::reset()
	{
		m_Values = m_Defaults;
		end_frame();
	}
}
