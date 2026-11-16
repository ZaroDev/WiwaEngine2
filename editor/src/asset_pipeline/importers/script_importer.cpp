#include <editor_common.h>
#include "importers.h"

#include <algorithm>

#include <scripting/script_component.h>
#include <scripting/script_requires.h>
#include <scripting/script_runtime.h>

#include "../file_utils.h"

#include <assets/asset_log.h>

namespace wiwa
{
	namespace
	{
		std::optional<std::string> read_text(const std::filesystem::path& path)
		{
			std::optional<std::vector<std::byte>> bytes = read_binary_file(path);
			if (!bytes)
			{
				return std::nullopt;
			}
			return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
		}

		// Reads scripts from the source folders while the importer evaluates one. The script being imported keeps its real asset;
		// the ones it requires get a throw-away asset, because only their code matters here.
		class DiskScriptSource final : public IScriptSource
		{
		public:
			DiskScriptSource(std::string mainPath, std::string mainSource, AssetRef mainAsset)
			{
				m_Scripts.push_back(LoadedScript{ mainAsset, std::move(mainPath), std::move(mainSource) });
			}

			virtual std::optional<LoadedScript> find_by_path(std::string_view path) override
			{
				for (const LoadedScript& script : m_Scripts)
				{
					if (script.Path == path)
					{
						return script;
					}
				}

				std::optional<std::string> text = read_text(std::filesystem::path(std::string(path)));
				if (!text)
				{
					return std::nullopt;
				}
				m_Scripts.push_back(LoadedScript{ AssetRef{ AssetGuid::generate(), 0 }, std::string(path), std::move(*text) });
				return m_Scripts.back();
			}

			virtual std::optional<LoadedScript> find_by_asset(AssetRef ref) override
			{
				for (const LoadedScript& script : m_Scripts)
				{
					if (script.Asset == ref)
					{
						return script;
					}
				}
				return std::nullopt;
			}

		private:
			std::vector<LoadedScript> m_Scripts{};
		};

		// The name the catalog gives the source: the root as it was given, then the path below it.
		std::string catalog_path_of(const ImportContext& ctx)
		{
			for (const std::filesystem::path& root : ctx.get_source_roots())
			{
				const std::filesystem::path relative = ctx.source().lexically_normal().lexically_relative(root.lexically_normal());
				if (!relative.empty() && *relative.begin() != "..")
				{
					std::string name = root.generic_string();
					while (name.ends_with("/"))
					{
						name.pop_back();
					}
					return name + "/" + relative.generic_string();
				}
			}
			return ctx.source().generic_string();
		}

		// The chain of requires that leads back to a script on `stack`, empty if none does. Scripts that cannot be read end the walk.
		std::vector<std::string> find_cycle(const std::string& path, const std::string& source, std::vector<std::string>& stack)
		{
			stack.push_back(path);
			for (const std::string& required : scan_requires(source).Paths)
			{
				if (std::find(stack.begin(), stack.end(), required) != stack.end())
				{
					std::vector<std::string> cycle(std::find(stack.begin(), stack.end(), required), stack.end());
					cycle.push_back(required);
					return cycle;
				}

				if (std::optional<std::string> text = read_text(std::filesystem::path(required)))
				{
					std::vector<std::string> cycle = find_cycle(required, *text, stack);
					if (!cycle.empty())
					{
						return cycle;
					}
				}
			}
			stack.pop_back();
			return {};
		}

		// .lua: a script, cooked into a ScriptData that keeps the source. Importing evaluates the script once in a throw-away
		// runtime, which checks that it runs and returns a class, and registers its properties as a component for the scene
		// importer (scenes are imported after scripts). Top-level code must therefore only define things: it runs here without
		// the engine API. The scripts it requires with a literal path are its dependencies; a require cycle fails the import,
		// because importing the cycle's members as dependencies of each other would wait forever.
		class ScriptImporter final : public IImporter
		{
		public:
			virtual const char* name() const override { return "ScriptImporter"; }
			virtual u32 version() const override { return 1; }
			virtual std::vector<std::string> extensions() const override { return { ".lua" }; }
			virtual bool imports_dependencies() const override { return true; }

			virtual bool import(ImportContext& ctx) override
			{
				const std::string file = ctx.source().filename().string();

				std::optional<std::string> text = read_text(ctx.source());
				if (!text)
				{
					WI_ASSET_LOG(Error, "Cannot read {0}", ctx.source().string());
					return false;
				}

				const std::string path = catalog_path_of(ctx);

				std::vector<std::string> stack;
				const std::vector<std::string> cycle = find_cycle(path, *text, stack);
				if (!cycle.empty())
				{
					std::string chain;
					for (const std::string& step : cycle)
					{
						chain += (chain.empty() ? "" : " -> ") + step;
					}
					WI_ASSET_LOG(Error, "{0}: require cycle: {1}", file, chain);
					return false;
				}

				DiskScriptSource source(path, *text, ctx.main_asset());
				ScriptRuntime runtime(source);
				std::string error;
				const ScriptClass* scriptClass = runtime.load(ctx.main_asset(), error);
				if (scriptClass == nullptr)
				{
					WI_ASSET_LOG(Error, "{0}: {1}", file, error);
					return false;
				}
				register_script_component(*scriptClass);

				const RequireScan scan = scan_requires(*text);
				std::vector<AssetRef> dependencies;
				for (const std::string& required : scan.Paths)
				{
					if (std::optional<AssetRef> asset = ctx.import_dependency(std::filesystem::absolute(required)))
					{
						dependencies.push_back(*asset);
					}
					else
					{
						ctx.warn("require \"" + required + "\" is not a script in the project");
					}
				}
				if (scan.DynamicCalls > 0)
				{
					ctx.warn("require with a computed path cannot be followed: the script it loads may be missing from a build");
				}

				ScriptData data;
				data.Source = std::move(*text);
				data.SourcePath = path;
				ctx.emit(ctx.main_asset(), data, dependencies);
				return true;
			}
		};
	}

	Scope<IImporter> create_script_importer() { return CreateScope<ScriptImporter>(); }
}
