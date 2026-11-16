#include <editor_common.h>
#include "importers.h"

#include <assets/native_anim_files.h>

#include "../file_utils.h"

#include <assets/asset_log.h>

namespace wiwa
{
	namespace
	{
		// .wgraph: an animation graph as JSON. A file that cannot be read or breaks a rule of graph_valid fails the import (the cooked graph from
		// before stays) with the reason. The clips and the skeleton it names are dependencies.
		class GraphFileImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "GraphFileImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".wgraph" }; }
			virtual bool imports_dependencies() const override { return true; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();
				const std::optional<std::vector<std::byte>> bytes = ctx.read_source();
				if (!bytes)
				{
					return false;
				}

				std::string error;
				std::optional<AnimationGraphData> graph = read_graph_file(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), error);
				if (!graph)
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}
				if (graph->Name.empty())
				{
					graph->Name = ctx.source().stem().string();
				}

				std::vector<AssetRef> dependencies;
				const auto depend = [&](const AssetRef& ref)
				{
					if (!ref.valid())
					{
						return;
					}
					ctx.depend_on(ref.Guid);
					dependencies.push_back(ref);
				};
				depend(graph->Skeleton);
				for (const GraphLayer& layer : graph->Layers)
				{
					for (const GraphState& state : layer.States)
					{
						depend(state.Clip);
						for (const GraphBlendSample& sample : state.Samples)
						{
							depend(sample.Clip);
						}
					}
				}
				ctx.emit<AnimationGraphData>(ctx.main_asset(), *graph, dependencies);
				return true;
			}
		};
	}

	Scope<IImporter> create_graph_file_importer() { return CreateScope<GraphFileImporter>(); }
}
