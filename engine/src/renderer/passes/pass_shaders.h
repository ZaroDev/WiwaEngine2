#pragma once

#include <initializer_list>
#include <string>
#include <vector>

#include "../render_pass.h"
#include "../shader_library.h"

namespace wiwa
{
	// The shader bookkeeping every pass repeats: the stages it asked the ShaderLibrary for, whether they are all there, whether one failed,
	// and the generation its pipelines were last built for. A pass holds one as m_Shaders and still reads shaders through it
	// (m_Shaders->get(path, stage)).
	class PassShaders
	{
	public:
		struct Stage
		{
			const char* Path;
			ShaderStage Stage;
		};

		// Takes the library from the description and requests every stage. False (logged) when there is no library.
		bool initialize(const PassInitializationDescription& description, const char* passName, std::initializer_list<Stage> stages)
		{
			m_Library = description.Shaders;
			if (m_Library == nullptr)
			{
				WI_LOG(Error, "Renderer", "The {0} pass needs the shader library", passName);
				return false;
			}
			m_Stages.assign(stages.begin(), stages.end());
			for (const Stage& stage : m_Stages)
			{
				m_Library->request(stage.Path, stage.Stage);
			}
			return true;
		}

		// Every requested stage has a shader object.
		bool ready() const
		{
			for (const Stage& stage : m_Stages)
			{
				if (m_Library->get(stage.Path, stage.Stage) == nullptr)
				{
					return false;
				}
			}
			return true;
		}

		// At least one requested stage has no shader and none is coming.
		bool failed() const
		{
			for (const Stage& stage : m_Stages)
			{
				if (m_Library->has_failed(stage.Path, stage.Stage))
				{
					return true;
				}
			}
			return false;
		}

		// True when a shader appeared or was replaced since the last call: the pipelines have to be built again.
		bool take_generation_change()
		{
			const u32 generation = m_Library->get_generation();
			const bool changed = generation != m_BuiltGeneration;
			m_BuiltGeneration = generation;
			return changed;
		}

		ShaderLibrary* operator->() const { return m_Library; }

	private:
		ShaderLibrary* m_Library = nullptr;
		std::vector<Stage> m_Stages{};
		u32 m_BuiltGeneration = ~0u;
	};
}
