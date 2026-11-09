#include <editor_common.h>
#include "importers.h"

#include <assets/particle_effect.h>

#include "../file_utils.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// .wfx: a particle effect as JSON (particle_effect.h). A file that cannot be read or breaks a rule of particle_effect_valid fails the import (the cooked
		// effect from before stays) with the reason. The materials and meshes it names are dependencies.
		class ParticleFileImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "ParticleFileImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".wfx" }; }
			virtual bool imports_dependencies() const override { return true; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();
				const std::optional<std::vector<std::byte>> bytes = read_binary_file(ctx.source());
				if (!bytes)
				{
					WI_ASSET_LOG(Error, "Cannot read {0}", ctx.source().string());
					return false;
				}

				std::string error;
				std::optional<ParticleEffectData> effect = read_particle_file(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), error);
				if (!effect || !particle_effect_valid(*effect, error))
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}
				if (effect->Name.empty())
				{
					effect->Name = ctx.source().stem().string();
				}

				std::vector<AssetRef> dependencies;
				const auto depend = [&](const AssetRef& ref)
				{
					if (!ref.valid())
					{
						return;
					}
					if (const std::optional<std::filesystem::path> path = ctx.path_of(ref.Guid))
					{
						ctx.import_dependency(*path);
					}
					else
					{
						ctx.warn(file + ": an asset it names (" + ref.Guid.to_string() + ") was not found");
					}
					dependencies.push_back(ref);
				};
				for (const ParticleEmitterData& emitter : effect->Emitters)
				{
					depend(emitter.Material);
					depend(emitter.Mesh);
				}
				ctx.emit<ParticleEffectData>(ctx.main_asset(), *effect, dependencies);
				return true;
			}
		};
	}

	Scope<IImporter> create_particle_file_importer() { return CreateScope<ParticleFileImporter>(); }
}
