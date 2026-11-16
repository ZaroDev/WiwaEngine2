#include <editor_common.h>
#include "importers.h"

#include <ai/bt_file.h>

#include "../file_utils.h"

#include <assets/asset_log.h>

namespace wiwa
{
	namespace
	{
		// .wbt: a behaviour tree as JSON. A file that cannot be read, or has a problem validate_bt calls an error, fails the import (the cooked tree from before
		// stays) with the reasons; warnings are logged. The Lua leaf scripts it names are dependencies.
		class BehaviorTreeImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "BehaviorTreeImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".wbt" }; }
			virtual bool imports_dependencies() const override { return true; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();
				const std::optional<std::vector<std::byte>> bytes = ctx.read_source();
				if (!bytes)
				{
					return false;
				}

				const std::string text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
				std::string error;
				const std::optional<BehaviorTreeDoc> doc = read_bt_file(text, error);
				if (!doc)
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}

				bool failed = false;
				for (const BtProblem& problem : validate_bt(*doc))
				{
					const std::string where = problem.NodeId.empty() ? std::string() : " (node " + problem.NodeId + ")";
					if (problem.Error)
					{
						WI_ASSET_LOG(Error, "{0}: {1}{2}", file, problem.Message, where);
						failed = true;
					}
					else
					{
						ctx.warn(file + ": " + problem.Message + where);
					}
				}
				if (failed)
				{
					return false;
				}

				std::vector<AssetRef> dependencies;
				for (const BtNodeDesc& node : doc->Nodes)
				{
					if ((node.Type != "LuaAction" && node.Type != "LuaCondition") || !node.Params.contains("script"))
					{
						continue;
					}
					const std::optional<AssetGuid> guid = AssetGuid::from_string(node.Params["script"].get<std::string>());
					if (!guid)
					{
						continue;
					}
					ctx.depend_on(*guid, "a script");
					dependencies.push_back(AssetRef{ *guid, 0 });
				}

				BehaviorTreeData data;
				data.Text = text;
				ctx.emit<BehaviorTreeData>(ctx.main_asset(), data, dependencies);
				return true;
			}
		};
	}

	Scope<IImporter> create_behavior_tree_importer() { return CreateScope<BehaviorTreeImporter>(); }
}
