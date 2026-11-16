#include <editor_common.h>
#include "asset_smoke_test.h"

#include <assets/asset_manager.h>
#include <assets/build_info.h>
#include <assets/library_source.h>
#include <assets/pak_source.h>
#include <core/engine.h>

#include <asset_pipeline/asset_templates.h>
#include <asset_pipeline/build_settings.h>
#include <asset_pipeline/file_utils.h>
#include <asset_pipeline/material_file.h>
#include <asset_pipeline/packager.h>
#include <asset_pipeline/physics_material_file.h>
#include <asset_pipeline/physics_settings_file.h>
#include <physics/physics_project_settings.h>
#include <asset_pipeline/prefab_file.h>
#include <asset_pipeline/surface_shader_parser.h>

#include <ui/ui_panel_io.h>

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

#include <test_kit.h>

namespace wiwa
{
	namespace
	{
		struct CheckLog
		{
			static constexpr const char* Area = "Assets";
			static constexpr const char* Prefix = "Smoke test";
		};
		using Check = BasicCheck<CheckLog>;

		struct CookedFile
		{
			AssetGuid Guid{};
			AssetType Type = AssetType::None;
			std::vector<AssetGuid> Dependencies{};
			std::vector<std::byte> Bytes{};
		};

		std::vector<CookedFile> read_library(const std::filesystem::path& library)
		{
			std::vector<CookedFile> files;
			std::error_code error;
			for (auto it = std::filesystem::recursive_directory_iterator(library, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
			{
				if (!it->is_regular_file() || it->path().extension() != ".wasset")
				{
					continue;
				}

				CookedFile file;
				if (std::optional<std::vector<std::byte>> bytes = read_binary_file(it->path()))
				{
					std::span<const std::byte> payload;
					if (std::optional<WAssetHeader> header = parse_wasset_header(*bytes, payload))
					{
						file.Guid = header->Guid;
						file.Type = header->Type;
						file.Dependencies = header->Dependencies;
						file.Bytes = std::move(*bytes);
						files.push_back(std::move(file));
					}
				}
			}
			return files;
		}

		// Starts loading an asset of any type; the returned function tells its state.
		std::function<LoadState()> request(AssetManager& manager, AssetType type, AssetGuid guid)
		{
			const AssetRef ref{ guid, 0 };
			switch (type)
			{
			case AssetType::Texture: { auto h = manager.load<TextureData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Mesh: { auto h = manager.load<MeshData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Material: { auto h = manager.load<MaterialData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Skeleton: { auto h = manager.load<SkeletonData>(ref); return [h]() { return h.state(); }; }
			case AssetType::AnimationClip: { auto h = manager.load<AnimationClipData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Prefab: { auto h = manager.load<PrefabData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Environment: { auto h = manager.load<EnvironmentData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Lut: { auto h = manager.load<LutData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Shader: { auto h = manager.load<ShaderData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Scene: { auto h = manager.load<SceneData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Script: { auto h = manager.load<ScriptData>(ref); return [h]() { return h.state(); }; }
			case AssetType::SurfaceShader: { auto h = manager.load<SurfaceShaderData>(ref); return [h]() { return h.state(); }; }
			case AssetType::UiPanel: { auto h = manager.load<UiPanelData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Font: { auto h = manager.load<FontData>(ref); return [h]() { return h.state(); }; }
			case AssetType::PhysicsMaterial: { auto h = manager.load<PhysicsMaterialData>(ref); return [h]() { return h.state(); }; }
			case AssetType::PhysicsShape: { auto h = manager.load<CookedShapeData>(ref); return [h]() { return h.state(); }; }
			case AssetType::InputMap: { auto h = manager.load<InputMapData>(ref); return [h]() { return h.state(); }; }
			case AssetType::AnimationGraph: { auto h = manager.load<AnimationGraphData>(ref); return [h]() { return h.state(); }; }
			case AssetType::ParticleEffect: { auto h = manager.load<ParticleEffectData>(ref); return [h]() { return h.state(); }; }
			case AssetType::SoundBank: { auto h = manager.load<SoundBankData>(ref); return [h]() { return h.state(); }; }
			case AssetType::AudioClip: { auto h = manager.load<AudioClipData>(ref); return [h]() { return h.state(); }; }
			case AssetType::NavMesh: { auto h = manager.load<NavMeshData>(ref); return [h]() { return h.state(); }; }
			case AssetType::BehaviorTree: { auto h = manager.load<BehaviorTreeData>(ref); return [h]() { return h.state(); }; }
			case AssetType::Video: { auto h = manager.load<VideoData>(ref); return [h]() { return h.state(); }; }
			default: return []() { return LoadState::Failed; };
			}
		}

		struct MetaSnapshot
		{
			AssetGuid Guid{};
			std::map<std::string, u64> SubAssets{};
		};

		std::map<std::filesystem::path, MetaSnapshot> snapshot_metas(const std::filesystem::path& assets)
		{
			std::map<std::filesystem::path, MetaSnapshot> result;
			std::error_code error;
			for (auto it = std::filesystem::recursive_directory_iterator(assets, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
			{
				if (it->is_regular_file() && it->path().extension() == ".meta")
				{
					if (std::optional<MetaFile> meta = MetaFile::load(it->path()))
					{
						result[it->path()] = MetaSnapshot{ meta->Guid, meta->SubAssets };
					}
				}
			}
			return result;
		}
	}

	namespace
	{
		// the .meta files of every source root (Assets/ and engine/)
		std::map<std::filesystem::path, MetaSnapshot> snapshot_all_metas(const AssetDatabase& database)
		{
			std::map<std::filesystem::path, MetaSnapshot> result;
			for (const AssetDatabase::SourceRoot& root : database.get_roots())
			{
				result.merge(snapshot_metas(root.Source));
			}
			return result;
		}
	}

	bool run_asset_smoke_test(AssetPipelineModule& pipeline)
	{
		Check check;
		AssetDatabase& database = pipeline.get_database();
		AssetManager& manager = *Engine::get().get_module<AssetManager>();

		const std::filesystem::path temp = std::filesystem::temp_directory_path() / "wiwa_asset_smoke";
		std::error_code error;
		std::filesystem::remove_all(temp, error);

		// A shader payload survives a round trip, and no truncated copy of it is accepted.
		{
			ShaderData shader;
			shader.Name = "smoke.hlsl";
			shader.Binaries.push_back(ShaderBinary{ ShaderStage::Pixel, ShaderTarget::Spirv, "PSMain", { 1, 2, 3, 4 } });
			shader.Binaries.push_back(ShaderBinary{ ShaderStage::Vertex, ShaderTarget::Dxbc, "VSMain", { 9 } });

			const std::vector<std::byte> bytes = serialize(shader);
			std::optional<ShaderData> back = deserialize<ShaderData>(bytes);
			const ShaderBinary* pixel = back ? back->find(ShaderStage::Pixel, ShaderTarget::Spirv) : nullptr;
			check(pixel != nullptr && pixel->Code == shader.Binaries[0].Code && back->find(ShaderStage::Vertex, ShaderTarget::Dxbc) != nullptr, "a shader payload round-trips");
			check(back && back->find(ShaderStage::Compute, ShaderTarget::Dxbc) == nullptr, "find returns null for a binary that is not there");

			bool truncatedAccepted = false;
			for (size_t length = 0; length < bytes.size(); length++)
			{
				truncatedAccepted = truncatedAccepted || deserialize<ShaderData>(std::span<const std::byte>(bytes.data(), length)).has_value();
			}
			check(!truncatedAccepted, "a truncated shader payload is rejected");
		}

		// The sample environment is cooked into a cube with prefiltered mips and SH.
		if (const std::optional<AssetRef> sample = manager.find_asset("Assets/environments/kloofendal_48d_partly_cloudy_puresky_1k.hdr"))
		{
			Handle<EnvironmentData> handle = manager.load<EnvironmentData>(*sample);
			manager.wait_idle();
			const EnvironmentData* environment = handle.get();
			check(environment != nullptr, "the sample .hdr loads as a cooked environment");
			if (environment != nullptr)
			{
				check(environment->FaceSize == 512 && environment->Mips.size() == 7, "its cube has the face size and the mips of the prefilter chain");
				bool finite = true;
				for (const glm::vec4& coefficient : environment->SH)
				{
					finite = finite && std::isfinite(coefficient.x) && std::isfinite(coefficient.y) && std::isfinite(coefficient.z);
				}
				check(finite && environment->SH[0].x > 0.0f && environment->SH[0].y > 0.0f && environment->SH[0].z > 0.0f, "its SH are finite and the first coefficient is positive");
			}
		}

		// The catalog survives a round trip, and no truncated copy of it is accepted.
		{
			AssetCatalog catalog;
			const AssetGuid guid = AssetGuid::generate();
			catalog.Entries.push_back({ "engine/shaders/x.hlsl", guid });
			catalog.Entries.push_back({ "Assets/models/y.glb", AssetGuid::generate() });

			const std::vector<std::byte> bytes = serialize(catalog);
			std::optional<AssetCatalog> back = deserialize<AssetCatalog>(bytes);
			check(back && back->find("engine\\shaders\\x.hlsl") && back->find("engine/shaders/x.hlsl")->Guid == guid, "the catalog round-trips and finds a path");
			check(back && !back->find("engine/shaders/missing.hlsl"), "the catalog does not find an unknown path");

			// A catalog written before redirects existed ends after the entries and is still valid (see archive()): exactly one cut, the one that
			// drops the empty redirect list, is accepted.
			size_t truncatedAccepted = 0;
			for (size_t length = 0; length < bytes.size(); length++)
			{
				truncatedAccepted += deserialize<AssetCatalog>(std::span<const std::byte>(bytes.data(), length)).has_value() ? 1 : 0;
			}
			check(truncatedAccepted == 1, "a truncated catalog is rejected (but the one that ends after the entries)");
		}

		// The build info (which scene a pack starts with) survives a round trip, and no truncated copy of it is accepted.
		{
			const AssetGuid scene = AssetGuid::generate();
			BuildInfo info;
			info.DefaultScene = scene;

			const std::vector<std::byte> bytes = serialize(info);
			std::optional<BuildInfo> back = deserialize<BuildInfo>(bytes);
			check(back && back->DefaultScene == scene, "build info round-trips");

			bool truncatedAccepted = false;
			for (size_t length = 0; length < bytes.size(); length++)
			{
				truncatedAccepted = truncatedAccepted || deserialize<BuildInfo>(std::span<const std::byte>(bytes.data(), length)).has_value();
			}
			check(!truncatedAccepted, "a truncated build info is rejected");
		}

		// A particle effect (.wfx, particle_effect.h): the cooked payload and the file text round-trip, curves and the gradient are baked, bad files are
		// refused with a reason, and the rules of an effect that can be played hold.
		{
			ParticleEffectData effect;
			effect.Name = "smoke";
			ParticleEmitterData flame;
			flame.Name = "Flame";
			flame.Material = AssetRef{ AssetGuid::generate(), 3 };
			flame.SheetColumns = 4;
			flame.SheetRows = 2;
			flame.Blend = ParticleBlend::Additive;
			flame.Rate = ParticleValue::range(5.0f, 9.0f);
			flame.Bursts = { ParticleBurst{ 0.5f, 20 } };
			flame.SizeOverLife = ParticleValue::curve({ { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.25f } });
			flame.ColorOverLife = { ParticleColorKey{ 0.0f, glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) }, ParticleColorKey{ 1.0f, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f) } };
			flame.bake();
			ParticleEmitterData trail;
			trail.Name = "Trail";
			trail.Renderer = ParticleRenderer::Ribbon;
			trail.RibbonSegments = 24;
			trail.Collision = ParticleCollision::Depth;
			trail.Response = ParticleCollisionResponse::Stick;
			trail.Gravity = glm::vec3(0.0f, -9.8f, 0.0f);
			trail.bake();
			effect.Emitters = { flame, trail };

			// a prefab node that holds an instance of another prefab round-trips; a payload from before instances existed loads with none
			{
				PrefabData outer;
				outer.Name = "outer";
				outer.Nodes.resize(2);
				outer.Nodes[0].Name = "Root";
				outer.Nodes[0].StableId = 11;
				outer.Nodes[1].Name = "Slot";
				outer.Nodes[1].Parent = 0;
				outer.Nodes[1].StableId = 22;
				PrefabNodeInstance instance;
				instance.Prefab = AssetRef{ AssetGuid::generate(), 0 };
				PrefabOverride set;
				set.Kind = OverrideKind::SetField;
				set.NodeId = 7;
				set.Component = "Transform";
				set.Field = "Position";
				set.Value = FieldValue(glm::vec3(1.0f, 2.0f, 3.0f));
				instance.Overrides.push_back(set);
				outer.Nodes[1].Instance = instance;

				const std::vector<std::byte> prefabBytes = serialize(outer);
				std::optional<PrefabData> prefabBack = deserialize<PrefabData>(prefabBytes);
				check(prefabBack && prefabBack->Nodes.size() == 2 && !prefabBack->Nodes[0].Instance && prefabBack->Nodes[1].Instance
					&& prefabBack->Nodes[1].Instance->Prefab == instance.Prefab && prefabBack->Nodes[1].Instance->Overrides.size() == 1
					&& prefabBack->Nodes[1].Instance->Overrides[0].NodeId == 7, "a prefab node's nested instance and its overrides round-trip through the cooked format");

				PrefabData flat = outer;
				flat.Nodes[1].Instance.reset();
				std::vector<std::byte> legacy = serialize(flat);
				legacy.resize(legacy.size() - sizeof(u32)); // the instance count is the last thing written: what is left is the old layout
				std::optional<PrefabData> legacyBack = deserialize<PrefabData>(legacy);
				check(legacyBack && legacyBack->Nodes.size() == 2 && !legacyBack->Nodes[0].Instance && !legacyBack->Nodes[1].Instance, "a prefab payload written before nested instances loads with none");
			}

			const std::vector<std::byte> bytes = serialize(effect);
			std::optional<ParticleEffectData> back = deserialize<ParticleEffectData>(bytes);
			check(back && *back == effect, "a particle effect round-trips through the cooked format");
			check(asset_type_of<ParticleEffectData>() == AssetType::ParticleEffect && AssetType::ParticleEffect < AssetType::Last, "the particle effect type is registered before Last");
			if (back && back->Emitters.size() == 2)
			{
				const ParticleValue& size = back->Emitters[0].SizeOverLife;
				check(std::abs(size.evaluate(0.0f)) < 1e-5f && std::abs(size.evaluate(1.0f) - 0.25f) < 1e-5f && std::abs(size.evaluate(0.5f) - 1.0f) < 0.05f, "a curve is baked into its table and read back at the keys");
				check(back->Emitters[0].ColorSamples.front() == glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) && back->Emitters[0].ColorSamples.back() == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), "the color gradient is baked");
				check(back->Emitters[1].ColorSamples.front() == glm::vec4(1.0f), "an emitter without a gradient bakes white");
				check(std::abs(back->Emitters[0].Rate.evaluate(0.5f) - 7.0f) < 1e-5f && back->Emitters[1].Rate.evaluate(0.0f) == 10.0f, "a range and a constant evaluate as written");
			}

			bool truncatedAccepted = false;
			for (size_t length = 0; length < bytes.size(); length++)
			{
				truncatedAccepted = truncatedAccepted || deserialize<ParticleEffectData>(std::span<const std::byte>(bytes.data(), length)).has_value();
			}
			check(!truncatedAccepted, "a truncated particle effect payload is rejected");

			std::string problem;
			const std::string text = write_particle_file(effect);
			const std::optional<ParticleEffectData> fromText = read_particle_file(text, problem);
			check(fromText && *fromText == effect && write_particle_file(*fromText) == text, "the .wfx text round-trips and is stable");
			check(particle_effect_valid(effect, problem), "the effect is valid");

			check(!read_particle_file("{ not json", problem) && !problem.empty(), "text that is not JSON is refused with a reason");
			check(!read_particle_file("{ \"version\": 99, \"emitters\": [] }", problem) && problem.find("version") != std::string::npos, "an unknown version is refused");
			check(!read_particle_file("{ \"version\": 1, \"emitters\": [ { \"renderer\": \"sideways\" } ] }", problem) && problem.find("renderer") != std::string::npos, "an unknown renderer name is refused and named");
			check(!read_particle_file("{ \"version\": 1, \"emitters\": [ { \"spawn\": { \"rate\": \"fast\" } } ] }", problem) && problem.find("rate") != std::string::npos, "a value that is not a number, range or curve is refused");
			check(!read_particle_file("{ \"version\": 1, \"emitters\": [ { \"maxParticles\": -5 } ] }", problem), "a negative count is refused");
			const std::optional<ParticleEffectData> empty = read_particle_file("{ \"version\": 1, \"emitters\": [] }", problem);
			check(empty && empty->Emitters.empty() && particle_effect_valid(*empty, problem), "an effect with no emitter is valid (it draws nothing)");

			ParticleEffectData many = effect;
			many.Emitters.clear();
			for (u32 i = 0; i <= PARTICLE_MAX_EMITTERS; i++)
			{
				ParticleEmitterData e;
				e.Name = "E" + std::to_string(i);
				many.Emitters.push_back(e);
			}
			check(!particle_effect_valid(many, problem) && problem.find("at most") != std::string::npos, "more than eight emitters are refused");
			many.Emitters.pop_back();
			check(particle_effect_valid(many, problem), "eight emitters are fine");

			const auto refused = [&](const char* why, auto&& change)
			{
				ParticleEffectData broken = effect;
				change(broken.Emitters[0]);
				const bool refusedNow = !particle_effect_valid(broken, problem) && !problem.empty();
				check(refusedNow, why);
			};
			refused("a duplicate emitter name is refused", [](ParticleEmitterData& e) { e.Name = "Trail"; });
			refused("a lifetime of zero is refused", [](ParticleEmitterData& e) { e.Lifetime = ParticleValue::constant(0.0f); });
			refused("a range that runs backwards is refused", [](ParticleEmitterData& e) { e.StartSpeed = ParticleValue::range(3.0f, 1.0f); });
			refused("curve keys that do not rise are refused", [](ParticleEmitterData& e) { e.SizeOverLife = ParticleValue::curve({ { 0.5f, 1.0f }, { 0.2f, 0.0f } }); });
			refused("a curve with no key is refused", [](ParticleEmitterData& e) { e.AlphaOverLife = ParticleValue::curve({}); });
			refused("no particles allowed is refused", [](ParticleEmitterData& e) { e.MaxParticles = 0; });
			refused("too many particles is refused", [](ParticleEmitterData& e) { e.MaxParticles = PARTICLE_LIMIT_MAX_PARTICLES + 1; });
			refused("a sheet without cells is refused", [](ParticleEmitterData& e) { e.SheetColumns = 0; });
			refused("a gradient out of order is refused", [](ParticleEmitterData& e) { e.ColorOverLife = { ParticleColorKey{ 0.8f, glm::vec4(1.0f) }, ParticleColorKey{ 0.2f, glm::vec4(1.0f) } }; });
			refused("a duration of zero is refused", [](ParticleEmitterData& e) { e.Duration = 0.0f; });
			refused("a NaN in a color is refused", [](ParticleEmitterData& e) { e.StartColor.x = std::numeric_limits<float>::quiet_NaN(); });
			ParticleEffectData shortRibbon = effect;
			shortRibbon.Emitters[1].RibbonSegments = 1;
			check(!particle_effect_valid(shortRibbon, problem), "a ribbon of one segment is refused");
		}

		// The build settings survive a round trip; a missing file, a damaged one and a bad guid are told apart.
		{
			std::filesystem::create_directories(temp, error);
			const std::filesystem::path file = temp / "build_settings.json";

			BuildSettings settings;
			settings.Scenes = { AssetGuid::generate(), AssetGuid::generate() };
			settings.DefaultScene = settings.Scenes[1];
			settings.AlwaysInclude = { AssetGuid::generate() };
			check(settings.save(file), "the build settings save");

			std::string loadError;
			std::optional<BuildSettings> back = BuildSettings::load(file, loadError);
			check(back && back->DefaultScene == settings.DefaultScene && back->Scenes == settings.Scenes && back->AlwaysInclude == settings.AlwaysInclude, "the build settings round-trip");

			std::optional<BuildSettings> missing = BuildSettings::load(temp / "no_such_settings.json", loadError);
			check(!missing && loadError.empty(), "a missing settings file is nullopt without an error");

			{
				std::ofstream(file) << "{not json";
			}
			check(!BuildSettings::load(file, loadError) && !loadError.empty(), "a damaged settings file is an error");

			{
				std::ofstream(file) << R"({ "scenes": ["zz"] })";
			}
			loadError.clear();
			check(!BuildSettings::load(file, loadError) && loadError.find("zz") != std::string::npos, "a bad guid is an error that names it");

			BuildSettings empty;
			check(empty.validate(loadError), "empty settings are valid");
			BuildSettings stray;
			stray.DefaultScene = AssetGuid::generate();
			check(!stray.validate(loadError) && !loadError.empty(), "a default scene outside the scene list is invalid");
			stray.Scenes = { stray.DefaultScene };
			check(stray.validate(loadError), "a default scene in the scene list is valid");
		}

		// 0. an import reports its progress and ends with every item done
		{
			Ref<JobProgress> scan = pipeline.scan_async();
			scan->wait();
			check(scan->succeeded(), "the import job succeeds");
			check(scan->done() == scan->total() && scan->fraction() == 1.0f, "the import job reports all its items done");
		}

		// 1. import + pack
		check(Packager::pack(database, temp, nullptr, nullptr, temp / "none.wsettings", temp / "none_input.wsettings"), "packing the Library");
		const std::vector<CookedFile> cooked = read_library(database.get_library_root());
		check(!cooked.empty(), "the Library has cooked assets (put some files in Assets/)");
		WI_ASSET_LOG(Info, "Smoke test: {0} cooked assets", cooked.size());

		// 2. the pack holds exactly the Library's bytes
		PakSource pak(temp / "assets.wpak");
		check(pak.valid(), "the pack opens");
		check(pak.get_index().size() == cooked.size() + 1, "the pack has one entry per cooked asset, and the catalog"); // + the catalog
		check(pak.contains(CATALOG_GUID), "the pack holds the asset catalog");
		for (const CookedFile& file : cooked)
		{
			std::optional<std::vector<std::byte>> fromPak = pak.read(file.Guid);
			check(fromPak && *fromPak == file.Bytes, "pack bytes equal Library bytes for " + file.Guid.to_string());
			for (AssetGuid dependency : file.Dependencies)
			{
				check(pak.contains(dependency), "dependency " + dependency.to_string() + " of " + file.Guid.to_string() + " is in the pack");
			}
		}

		// 3. every asset loads through the AssetManager reading the pack
		manager.set_source(CreateScope<PakSource>(temp / "assets.wpak"));
		{
			// the engine's own assets are found by path through the packed catalog, no guid involved
			std::optional<AssetRef> imgui = manager.find_asset("engine/shaders/imgui.hlsl");
			check(imgui.has_value(), "engine/shaders/imgui.hlsl is found by path in the packed catalog");
			check(!manager.find_asset("engine/shaders/does_not_exist.hlsl").has_value(), "a path that is not in the catalog is not found");
			check(manager.find_asset(".\\engine\\shaders\\imgui.hlsl").has_value(), "paths are accepted with backslashes and a leading ./");

			std::vector<std::function<LoadState()>> handles;
			for (const CookedFile& file : cooked)
			{
				handles.push_back(request(manager, file.Type, file.Guid));
			}
			manager.wait_idle();
			for (size_t i = 0; i < handles.size(); i++)
			{
				check(handles[i]() == LoadState::Ready, "asset " + cooked[i].Guid.to_string() + " loads from the pack");
			}

			// the same assets asked for through preload() report their progress to the end
			std::vector<AssetRef> refs;
			for (const CookedFile& file : cooked)
			{
				refs.push_back(AssetRef{ file.Guid, 0 });
			}
			Ref<JobProgress> preload = manager.preload(refs);
			preload->wait();
			check(preload->succeeded(), "preloading every asset succeeds");
			check(preload->total() == cooked.size() && preload->done() == preload->total(), "preload counts every asset once");

			// the same guid asked for as the wrong type fails, and a missing guid fails
			WI_ASSET_LOG(Info, "Smoke test: the next two load errors are expected");
			const AssetType other = (cooked[0].Type == AssetType::Mesh) ? AssetType::Texture : AssetType::Mesh;
			check(request(manager, other, cooked[0].Guid)() == LoadState::Failed, "a wrong-type load fails");
			auto missing = request(manager, AssetType::Texture, AssetGuid::generate());
			manager.wait_idle();
			check(missing() == LoadState::Failed, "a missing asset fails");
		}
		manager.unload_unused();

		// 3b. which assets a pack holds follows the build settings
		{
			// the selection itself, on made-up assets: A, D and E are scenes; A and D both use C
			const AssetGuid a = AssetGuid::generate(), b = AssetGuid::generate(), c = AssetGuid::generate(), d = AssetGuid::generate();
			const AssetGuid e = AssetGuid::generate(), f = AssetGuid::generate(), g = AssetGuid::generate(), engineAsset = AssetGuid::generate();
			const AssetGuid x = AssetGuid::generate(), y = AssetGuid::generate();
			const std::vector<PackCandidate> candidates = {
				{ a, { b } }, { b, { c } }, { c, {} }, { d, { c } }, { e, { f } }, { f, {} }, { g, {} },
				{ engineAsset, {}, true }, { x, { y } }, { y, { x } } };
			std::vector<AssetGuid> missing;

			BuildSettings settings;
			settings.Scenes = { a };
			std::unordered_set<AssetGuid> set = select_pack_set(candidates, settings, missing);
			check(set.contains(a) && set.contains(b) && set.contains(c), "a chosen scene brings what it uses, transitively");
			check(!set.contains(d) && !set.contains(e) && !set.contains(f), "a scene that is not chosen, and what only it uses, stay out");
			check(set.contains(engineAsset), "engine assets are always packed");
			check(!set.contains(g) && missing.empty(), "an asset nobody asks for stays out");

			settings.Scenes = { d };
			check(select_pack_set(candidates, settings, missing).contains(c), "an asset shared with a chosen scene stays in the pack");

			settings.Scenes.clear();
			settings.AlwaysInclude = { g };
			set = select_pack_set(candidates, settings, missing);
			check(set.contains(g) && !set.contains(a), "an always-included asset is packed without a scene using it");

			settings.AlwaysInclude = { x };
			set = select_pack_set(candidates, settings, missing);
			check(set.contains(x) && set.contains(y), "a dependency cycle ends and holds both assets");

			settings.AlwaysInclude.clear();
			set = select_pack_set(candidates, settings, missing);
			check(set.size() == 1 && set.contains(engineAsset), "empty settings pack the engine assets only");

			const AssetGuid unknown = AssetGuid::generate();
			settings.Scenes = { unknown };
			select_pack_set(candidates, settings, missing);
			check(missing.size() == 1 && missing[0] == unknown, "a guid that is not in the Library is reported as missing");

			// an asset that a packed asset needs but that has no cooked file is reported too, once
			const AssetGuid lost = AssetGuid::generate(), user = AssetGuid::generate(), otherUser = AssetGuid::generate();
			std::vector<PackCandidate> dangling = candidates;
			dangling.push_back({ user, { lost } });
			dangling.push_back({ otherUser, { lost } });
			settings.Scenes = { user, otherUser };
			select_pack_set(dangling, settings, missing);
			check(missing.size() == 1 && missing[0] == lost, "a dependency without a cooked file is reported as missing, once");
			settings.Scenes = { a };
			select_pack_set(dangling, settings, missing);
			check(missing.empty(), "a dangling dependency of an asset that is not packed does not matter");

			// a real pack: the project's scenes, none or all of them
			std::unordered_map<AssetGuid, const CookedFile*> byGuid;
			std::vector<AssetGuid> scenes;
			for (const CookedFile& file : cooked)
			{
				byGuid[file.Guid] = &file;
				if (file.Type == AssetType::Scene)
				{
					scenes.push_back(file.Guid);
				}
			}
			check(!scenes.empty(), "the Library has a scene to test the pack selection with");

			if (!scenes.empty())
			{
				std::unordered_set<AssetGuid> needed; // what the scenes use, transitively
				std::vector<AssetGuid> pending = scenes;
				while (!pending.empty())
				{
					const AssetGuid current = pending.back();
					pending.pop_back();
					if (needed.insert(current).second && byGuid.contains(current))
					{
						pending.insert(pending.end(), byGuid[current]->Dependencies.begin(), byGuid[current]->Dependencies.end());
					}
				}

				BuildSettings none; // no scene: only the engine's assets
				const std::filesystem::path noneDirectory = temp / "none";
				check(Packager::pack(database, noneDirectory, nullptr, &none), "packing with no scene chosen");
				PakSource nonePak(noneDirectory / "assets.wpak");
				check(nonePak.valid() && nonePak.get_index().size() < pak.get_index().size(), "a pack with no scene is smaller than the full pack");
				check(!nonePak.contains(scenes[0]), "an unchosen scene is not in the pack");
				check(!nonePak.contains(BUILD_INFO_GUID), "no default scene, no build info");
				std::optional<std::vector<std::byte>> noneCatalog = nonePak.read_catalog();
				std::optional<AssetCatalog> noneEntries = noneCatalog ? deserialize<AssetCatalog>(*noneCatalog) : std::nullopt;
				check(noneEntries && noneEntries->find("engine/shaders/imgui.hlsl") && !noneEntries->find("Assets/scenes/demo.wscene"), "its catalog lists the engine's assets and not the unchosen scene");

				BuildSettings all;
				all.Scenes = scenes;
				all.DefaultScene = scenes[0];
				const std::filesystem::path allDirectory = temp / "all";
				check(Packager::pack(database, allDirectory, nullptr, &all), "packing with every scene chosen");
				PakSource allPak(allDirectory / "assets.wpak");
				bool everythingNeeded = allPak.valid();
				for (AssetGuid guid : needed)
				{
					everythingNeeded = everythingNeeded && allPak.contains(guid);
				}
				check(everythingNeeded, "a chosen scene and everything it uses are in the pack");
				std::optional<BuildInfo> info = read_build_info(allPak);
				check(info && info->DefaultScene == scenes[0], "the default scene is in the pack's build info");

				// settings that name an asset the Library does not have fail the pack and leave no file
				BuildSettings broken;
				broken.Scenes = { AssetGuid::generate() };
				WI_ASSET_LOG(Info, "Smoke test: the next pack error is expected");
				const std::filesystem::path brokenDirectory = temp / "broken";
				check(!Packager::pack(database, brokenDirectory, nullptr, &broken), "settings that name an unknown asset fail the pack");
				check(!std::filesystem::exists(brokenDirectory / "assets.wpak"), "a failed pack leaves no file");

				// a cooked file that cannot be read fails the pack when the build needs it, and only then
				const AssetGuid damagedGuid = AssetGuid::generate();
				const std::filesystem::path damaged = database.get_library_root() / "zz" / (damagedGuid.to_string() + ".wasset");
				std::filesystem::create_directories(damaged.parent_path(), error);
				{
					std::ofstream(damaged, std::ios::binary) << "not a cooked asset";
				}

				BuildSettings needsDamaged;
				needsDamaged.AlwaysInclude = { damagedGuid };
				WI_ASSET_LOG(Info, "Smoke test: the next pack error is expected");
				check(!Packager::pack(database, temp / "damaged", nullptr, &needsDamaged), "a damaged cooked file the build needs fails the pack");
				check(!std::filesystem::exists(temp / "damaged" / "assets.wpak"), "and leaves no pack");
				check(Packager::pack(database, temp / "ignored", nullptr, &none), "a damaged cooked file the build does not need is left out");

				std::filesystem::remove_all(damaged.parent_path(), error);
			}
		}

		// 4. damaged packs are rejected without a crash
		{
			std::optional<std::vector<std::byte>> bytes = read_binary_file(temp / "assets.wpak");
			check(bytes.has_value(), "read the pack back");
			if (bytes)
			{
				WI_ASSET_LOG(Info, "Smoke test: the next pack errors are expected");

				const std::filesystem::path truncated = temp / "truncated.wpak";
				{
					std::ofstream out(truncated, std::ios::binary);
					out.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size() / 2));
				}
				check(!PakSource(truncated).valid(), "a truncated pack is rejected");

				const PakEntry* first = nullptr;
				for (const auto& [guid, entry] : pak.get_index())
				{
					if (first == nullptr || entry.Offset < first->Offset)
					{
						first = &entry;
					}
				}

				if (first != nullptr)
				{
					std::vector<std::byte> damaged = *bytes;
					damaged[first->Offset + first->CompressedSize / 2] ^= std::byte{ 0x55 };
					const std::filesystem::path corrupt = temp / "corrupt.wpak";
					{
						std::ofstream out(corrupt, std::ios::binary);
						out.write(reinterpret_cast<const char*>(damaged.data()), static_cast<std::streamsize>(damaged.size()));
					}

					PakSource corruptPak(corrupt);
					check(corruptPak.valid() && !corruptPak.read(first->Guid).has_value(), "a flipped byte in a blob is caught");

					manager.set_source(CreateScope<PakSource>(corrupt));
					auto handle = request(manager, static_cast<AssetType>(first->Type), first->Guid);
					manager.wait_idle();
					check(handle() == LoadState::Failed, "loading a damaged asset gives a Failed handle");
				}
			}
		}

		// 5. a forced re-cook keeps every guid and sub-asset id
		manager.set_source(CreateScope<LibrarySource>(database.get_library_root()));
		const auto metasBefore = snapshot_all_metas(database);
		for (const auto& [metaPath, meta] : metasBefore)
		{
			std::filesystem::path source = metaPath;
			source.replace_extension();
			database.reimport(source, true);
		}
		const auto metasAfter = snapshot_all_metas(database);
		check(metasBefore.size() == metasAfter.size(), "no .meta file appeared or disappeared");
		for (const auto& [metaPath, before] : metasBefore)
		{
			auto after = metasAfter.find(metaPath);
			check(after != metasAfter.end() && after->second.Guid == before.Guid && after->second.SubAssets == before.SubAssets,
				"guid and sub-asset ids of " + metaPath.generic_string() + " survive a re-import");
		}

		const std::vector<CookedFile> cookedAfter = read_library(database.get_library_root());
		check(cookedAfter.size() == cooked.size(), "the Library holds the same assets after a re-import");

		// 6. material assets (.wmat)
		{
			const std::filesystem::path dir = database.get_assets_root() / "zz_material_smoke";
			std::filesystem::create_directories(dir, error);

			const unsigned char png[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x64, 0xf8, 0xcf, 0x50, 0x0f, 0x00, 0x03, 0x86, 0x01, 0x80, 0x5a, 0x34, 0x7d, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };
			{
				std::ofstream out(dir / "pixel.png", std::ios::binary);
				out.write(reinterpret_cast<const char*>(png), sizeof(png));
			}

			// the cooked material of a source file, straight from the Library
			const auto cooked_material = [&](const std::filesystem::path& source, std::vector<AssetGuid>* dependencies = nullptr) -> std::optional<MaterialData>
			{
				std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(source));
				if (!meta)
				{
					return std::nullopt;
				}
				for (const CookedFile& file : read_library(database.get_library_root()))
				{
					std::span<const std::byte> payload;
					if (file.Guid == meta->Guid && file.Type == AssetType::Material && parse_wasset_header(file.Bytes, payload))
					{
						if (dependencies != nullptr)
						{
							*dependencies = file.Dependencies;
						}
						return deserialize<MaterialData>(payload);
					}
				}
				return std::nullopt;
			};

			const auto full_material = [](double roughness)
			{
				nlohmann::json json = {
					{ "version", 1 }, { "name", "Smoke" }, { "baseColor", { 0.1, 0.2, 0.3, 0.4 } }, { "metallic", 0.6 }, { "roughness", roughness },
					{ "emissive", { 1.0, 2.0, 3.0 } }, { "alpha", "mask" }, { "alphaCutoff", 0.25 }, { "doubleSided", true },
					{ "clearcoat", 0.7 }, { "clearcoatRoughness", 0.15 }, { "sheen", 0.45 }, { "sheenRoughness", 0.55 }, { "transmission", 0.65 },
					{ "textures", { { "baseColor", "pixel.png" }, { "normal", "pixel.png" }, { "orm", "pixel.png" }, { "emissive", "pixel.png" } } } };
				return json.dump(2);
			};

			const std::filesystem::path wmat = dir / "smoke.wmat";
			write_text(wmat, full_material(0.35));
			database.scan();

			std::vector<AssetGuid> dependencies;
			std::optional<MaterialData> material = cooked_material(wmat, &dependencies);
			check(material.has_value(), "material_import_all_fields: the .wmat is cooked");
			if (material)
			{
				check(material->Name == "Smoke" && material->BaseColorFactor == glm::vec4(0.1f, 0.2f, 0.3f, 0.4f) && material->MetallicFactor == 0.6f &&
					material->RoughnessFactor == 0.35f && material->EmissiveFactor == glm::vec3(1.0f, 2.0f, 3.0f), "material_import_all_fields: name, color, metallic, roughness and emissive");
				check(material->Alpha == AlphaMode::Mask && material->AlphaCutoff == 0.25f && material->DoubleSided, "material_import_all_fields: alpha mode, cutoff and double sided");
				check(material->Clearcoat == 0.7f && material->ClearcoatRoughness == 0.15f && material->SheenIntensity == 0.45f &&
					material->SheenRoughness == 0.55f && material->Transmission == 0.65f, "material_import_all_fields: clearcoat, sheen and transmission");
				check(material->BaseColorTexture.valid() && material->NormalTexture.valid() && material->OrmTexture.valid() && material->EmissiveTexture.valid(), "material_import_all_fields: the four textures resolve");
				check(std::find(dependencies.begin(), dependencies.end(), material->BaseColorTexture.resolved()) != dependencies.end(), "material_import_all_fields: the texture is a dependency");
			}

			write_text(wmat, full_material(0.9));
			database.reimport(wmat);
			material = cooked_material(wmat);
			check(material && material->RoughnessFactor == 0.9f, "material_reimport_on_edit: the cooked roughness follows the edit");

			WI_ASSET_LOG(Info, "Smoke test: the next material import error is expected");
			write_text(wmat, "{");
			database.reimport(wmat);
			material = cooked_material(wmat);
			check(material && material->RoughnessFactor == 0.9f && material->Name == "Smoke", "material_bad_json_keeps_previous: the previous cooked data still loads");

			const std::filesystem::path missing = dir / "missing_texture.wmat";
			write_text(missing, R"({ "version": 1, "roughness": 0.5, "textures": { "baseColor": "no_such_texture.png" } })");
			WI_ASSET_LOG(Info, "Smoke test: the next material warning is expected");
			database.reimport(missing);
			material = cooked_material(missing);
			check(material && !material->BaseColorTexture.valid() && material->RoughnessFactor == 0.5f, "material_missing_texture_warns: imports with an invalid texture ref");

			const std::filesystem::path unknown = dir / "unknown.wmat";
			write_text(unknown, R"({ "version": 1, "roughness": 0.5, "shader": "x.wshader", "params": { "Wobble": 0.5 }, "custom": { "a": [1, 2] }, "zzz": 3 })");
			MaterialFileData first, second;
			std::string fileError;
			check(read_material_file(unknown, first, fileError), "material_roundtrip_keeps_unknown_keys: read");
			check(write_material_file(dir / "copy.wmat", first, fileError), "material_roundtrip_keeps_unknown_keys: write");
			check(read_material_file(dir / "copy.wmat", second, fileError), "material_roundtrip_keeps_unknown_keys: read back");
			check(second.Extra == first.Extra && second.Extra.contains("custom") && second.Extra.contains("zzz") && second.Extra["custom"]["a"].size() == 2,
				"material_roundtrip_keeps_unknown_keys: the unknown keys survive");
			check(second.ShaderPath == "x.wshader" && second.Params.contains("Wobble") && second.Material.RoughnessFactor == 0.5f, "material_roundtrip_keeps_unknown_keys: known keys survive");
			check(!read_material_file(dir / "nope.wmat", first, fileError) && !fileError.empty(), "a missing .wmat is an error");

			// material_create_unique_names: three creations give three distinct files holding the defaults
			{
				std::filesystem::path created[3];
				for (std::filesystem::path& path : created)
				{
					path = create_material_file(dir, "New material");
				}
				std::error_code existsError;
				const bool distinct = created[0] != created[1] && created[1] != created[2] && created[0] != created[2];
				check(distinct && !created[0].empty() && !created[1].empty() && !created[2].empty(), "material_create_unique_names: three distinct paths");
				check(created[0].filename() == "New material.wmat" && created[1].filename() == "New material 2.wmat" && created[2].filename() == "New material 3.wmat",
					"material_create_unique_names: the names count up");
				for (const std::filesystem::path& path : created)
				{
					MaterialFileData read;
					std::string createError;
					const MaterialData defaults{};
					const bool ok = !path.empty() && std::filesystem::exists(path, existsError) && read_material_file(path, read, createError);
					check(ok && read.Material.BaseColorFactor == defaults.BaseColorFactor && read.Material.RoughnessFactor == defaults.RoughnessFactor &&
						read.Material.MetallicFactor == defaults.MetallicFactor && read.Material.AlphaCutoff == defaults.AlphaCutoff &&
						read.Material.Alpha == defaults.Alpha && read.ShaderPath.empty() && read.BaseColorPath.empty(), "material_create_unique_names: the file reads back as the defaults");
				}
			}

			// material_write_clean_floats: 0.35f is written as 0.35, and no temporary file is left behind
			{
				MaterialFileData data;
				data.Material.RoughnessFactor = 0.35f;
				data.Material.BaseColorFactor = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);
				const std::filesystem::path clean = dir / "clean.wmat";
				std::string cleanError;
				check(write_material_file(clean, data, cleanError), "material_write_clean_floats: write");
				std::ifstream in(clean, std::ios::binary);
				const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				check(text.find("0.35,") != std::string::npos && text.find("0.3499") == std::string::npos && text.find("0.2,") != std::string::npos &&
					text.find("0.1,") != std::string::npos, "material_write_clean_floats: floats are written with their shortest form");
				bool leftover = false;
				for (const auto& entry : std::filesystem::directory_iterator(dir, error))
				{
					leftover = leftover || entry.path().extension() == ".tmp";
				}
				check(!leftover, "material_write_atomic: no temporary file is left behind");
			}

			std::filesystem::remove_all(dir, error);
			database.scan();
		}


		// 6b. video assets (.mp4): the bytes are cooked as they are, a file that is not a video is refused, a panel that names a video depends on it
		{
			check(static_cast<u32>(AssetType::Video) == static_cast<u32>(AssetType::BehaviorTree) + 1 && static_cast<u32>(AssetType::Last) == static_cast<u32>(AssetType::Video) + 1,
				"video_asset_type_appended: Video follows the last older type, so no number in an existing file changes");

			VideoData payload;
			payload.Name = "clip";
			payload.Bytes = { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } };
			payload.Info.Width = 64;
			payload.Info.Height = 36;
			payload.Info.Fps = 30.0f;
			payload.Info.DurationSeconds = 2.5;
			payload.Info.HasAudio = true;
			payload.Info.SampleRate = 48000;
			payload.Info.Channels = 2;
			payload.HasInfo = true;
			const std::vector<std::byte> payloadBytes = serialize(payload);
			const std::optional<VideoData> payloadBack = deserialize<VideoData>(payloadBytes);
			check(payloadBack && payloadBack->Name == "clip" && payloadBack->Bytes == payload.Bytes && payloadBack->HasInfo && payloadBack->Info.Width == 64 &&
				payloadBack->Info.Height == 36 && payloadBack->Info.DurationSeconds == 2.5 && payloadBack->Info.SampleRate == 48000, "video_payload_roundtrip: the bytes and the info survive");
			bool truncatedVideoAccepted = false;
			for (size_t length = 0; length < payloadBytes.size(); length++)
			{
				truncatedVideoAccepted = truncatedVideoAccepted || deserialize<VideoData>(std::span<const std::byte>(payloadBytes.data(), length)).has_value();
			}
			check(!truncatedVideoAccepted, "video_payload_roundtrip: a truncated payload is rejected");
			VideoData empty;
			empty.Name = "clip";
			check(!deserialize<VideoData>(serialize(empty)).has_value(), "video_payload_roundtrip: a payload without bytes is rejected");

			const std::filesystem::path dir = database.get_assets_root() / "zz_video_smoke";
			std::filesystem::remove_all(dir, error);
			std::filesystem::create_directories(dir, error);

			// the start of an mp4: an ftyp box
			std::vector<unsigned char> mp4 = { 0, 0, 0, 24, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm', 0, 0, 2, 0, 'i', 's', 'o', 'm', 'm', 'p', '4', '1' };
			for (unsigned char i = 0; i < 100; i++)
			{
				mp4.push_back(i);
			}
			const std::filesystem::path clip = dir / "clip.mp4";
			{
				std::ofstream out(clip, std::ios::binary);
				out.write(reinterpret_cast<const char*>(mp4.data()), static_cast<std::streamsize>(mp4.size()));
			}
			const std::filesystem::path notVideo = dir / "notvideo.mp4";
			{
				std::ofstream out(notVideo, std::ios::binary);
				out << "this is not a video file at all";
			}
			WI_ASSET_LOG(Info, "Smoke test: the next video import error is expected");
			database.scan();

			const auto cooked_video = [&](const std::filesystem::path& source, std::vector<AssetGuid>* dependencies = nullptr) -> std::optional<VideoData>
			{
				std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(source));
				if (!meta)
				{
					return std::nullopt;
				}
				for (const CookedFile& file : read_library(database.get_library_root()))
				{
					std::span<const std::byte> filePayload;
					if (file.Guid == meta->Guid && file.Type == AssetType::Video && parse_wasset_header(file.Bytes, filePayload))
					{
						if (dependencies != nullptr)
						{
							*dependencies = file.Dependencies;
						}
						return deserialize<VideoData>(filePayload);
					}
				}
				return std::nullopt;
			};

			const std::optional<VideoData> cooked = cooked_video(clip);
			check(cooked.has_value(), "video_import_cooks: the .mp4 is cooked as a Video asset");
			check(cooked && cooked->Name == "clip" && cooked->Bytes.size() == mp4.size() && std::memcmp(cooked->Bytes.data(), mp4.data(), mp4.size()) == 0, "video_import_cooks: the bytes are cooked unchanged");
			check(!cooked_video(notVideo).has_value(), "video_bad_header_fails_import: a file that is not an mp4 is refused");

			if (std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(clip)))
			{
				UiPanelData panel;
				UiWidget widget;
				widget.Id = 1;
				widget.Kind = WidgetKind::Video;
				widget.Name = "Intro";
				widget.Video = AssetRef{ meta->Guid, 0 };
				panel.Widgets.push_back(widget);
				panel.NextId = 2;
				{
					std::ofstream out(dir / "intro.wuipanel", std::ios::binary);
					out << ui_panel_to_json(panel).dump(2);
				}
				database.scan();

				std::vector<AssetGuid> dependencies;
				std::optional<MetaFile> panelMeta = MetaFile::load(MetaFile::meta_path_for(dir / "intro.wuipanel"));
				bool dependsOnVideo = false;
				for (const CookedFile& file : read_library(database.get_library_root()))
				{
					if (panelMeta && file.Guid == panelMeta->Guid)
					{
						dependsOnVideo = std::find(file.Dependencies.begin(), file.Dependencies.end(), meta->Guid) != file.Dependencies.end();
					}
				}
				check(dependsOnVideo, "video_followed_by_packer: a panel that names a video lists it as a dependency, so the packer includes it");
			}
			else
			{
				check(false, "video_followed_by_packer: the clip has no .meta file");
			}

			std::filesystem::remove_all(dir, error);
			database.scan();
		}

		// 7. model materials become .wmat files
		{
			const std::filesystem::path dir = database.get_assets_root() / "zz_model_smoke";
			std::filesystem::remove_all(dir, error);
			std::filesystem::create_directories(dir, error);

			const unsigned char png[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x64, 0xf8, 0xcf, 0x50, 0x0f, 0x00, 0x03, 0x86, 0x01, 0x80, 0x5a, 0x34, 0x7d, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };
			{
				std::ofstream out(dir / "pixel.png", std::ios::binary);
				out.write(reinterpret_cast<const char*>(png), sizeof(png));
			}

			const auto read_text = [](const std::filesystem::path& path)
			{
				std::ifstream in(path, std::ios::binary);
				return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			};

			// a glTF with one triangle per material; the first material has a texture
			const auto model_text = [](const std::vector<std::string>& names)
			{
				nlohmann::json primitives = nlohmann::json::array();
				nlohmann::json materials = nlohmann::json::array();
				for (size_t i = 0; i < names.size(); i++)
				{
					primitives.push_back({ { "attributes", { { "POSITION", 0 } } }, { "indices", 1 }, { "material", i } });
					nlohmann::json material = { { "name", names[i] }, { "pbrMetallicRoughness", { { "baseColorFactor", { 0.25, 0.5, 0.75, 1.0 } }, { "roughnessFactor", 0.375 }, { "metallicFactor", 0.125 } } } };
					if (i == 0)
					{
						material["pbrMetallicRoughness"]["baseColorTexture"] = { { "index", 0 } };
					}
					else
					{
						material["doubleSided"] = true;
						material["emissiveFactor"] = { 0.5, 0.25, 0.125 };
					}
					materials.push_back(material);
				}
				nlohmann::json root = {
					{ "asset", { { "version", "2.0" } } }, { "scene", 0 }, { "scenes", { { { "nodes", { 0 } } } } },
					{ "nodes", { { { "name", "Body" }, { "mesh", 0 } } } },
					{ "meshes", { { { "primitives", primitives } } } },
					{ "materials", materials },
					{ "textures", { { { "source", 0 } } } }, { "images", { { { "uri", "pixel.png" } } } },
					{ "accessors", { { { "bufferView", 0 }, { "componentType", 5126 }, { "count", 3 }, { "type", "VEC3" }, { "min", { 0, 0, 0 } }, { "max", { 1, 1, 0 } } },
						{ { "bufferView", 1 }, { "componentType", 5123 }, { "count", 3 }, { "type", "SCALAR" } } } },
					{ "bufferViews", { { { "buffer", 0 }, { "byteOffset", 0 }, { "byteLength", 36 } }, { { "buffer", 0 }, { "byteOffset", 36 }, { "byteLength", 6 } } } },
					{ "buffers", { { { "byteLength", 44 }, { "uri", "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAA=" } } } } };
				return root.dump(2);
			};

			// the cooked asset with this guid
			const auto cooked_payload = [&](AssetGuid guid, AssetType type, std::vector<std::byte>& storage) -> std::span<const std::byte>
			{
				for (CookedFile& file : read_library(database.get_library_root()))
				{
					std::span<const std::byte> payload;
					if (file.Guid == guid && file.Type == type)
					{
						storage = std::move(file.Bytes);
						if (parse_wasset_header(storage, payload))
						{
							return payload;
						}
					}
				}
				return {};
			};

			// the material guid of each primitive of the first mesh of a model
			const auto primitive_materials = [&](const std::filesystem::path& modelFile) -> std::vector<AssetGuid>
			{
				std::vector<AssetGuid> result;
				std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(modelFile));
				if (!meta)
				{
					return result;
				}
				std::vector<std::byte> prefabBytes, meshBytes;
				std::optional<PrefabData> prefab = deserialize<PrefabData>(cooked_payload(meta->Guid, AssetType::Prefab, prefabBytes));
				if (!prefab)
				{
					return result;
				}
				for (const PrefabNode& node : prefab->Nodes)
				{
					if (node.Mesh.valid())
					{
						std::optional<MeshData> mesh = deserialize<MeshData>(cooked_payload(node.Mesh.resolved(), AssetType::Mesh, meshBytes));
						for (const MeshPrimitive& primitive : mesh ? mesh->Primitives : std::vector<MeshPrimitive>{})
						{
							result.push_back(primitive.Material.valid() ? primitive.Material.resolved() : AssetGuid{});
						}
						break;
					}
				}
				return result;
			};

			const auto guid_of = [](const std::filesystem::path& source) -> AssetGuid
			{
				std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(source));
				return meta ? meta->Guid : AssetGuid{};
			};

			// model_extracts_materials
			const std::filesystem::path model = dir / "m.gltf";
			write_text(model, model_text({ "Red", "Blue" }));
			database.scan();
			const std::filesystem::path materialsDir = dir / "m_materials";
			const std::filesystem::path red = materialsDir / "Red.wmat";
			const std::filesystem::path blue = materialsDir / "Blue.wmat";
			{
				std::error_code existsError;
				check(std::filesystem::exists(red, existsError) && std::filesystem::exists(blue, existsError), "model_extracts_materials: one .wmat per material");
				MaterialFileData redData, blueData;
				std::string readError;
				const bool readRed = read_material_file(red, redData, readError);
				const bool readBlue = read_material_file(blue, blueData, readError);
				check(readRed && readBlue, "model_extracts_materials: both files read back");
				if (readRed && readBlue)
				{
					check(redData.Material.Name == "Red" && redData.Material.BaseColorFactor == glm::vec4(0.25f, 0.5f, 0.75f, 1.0f) && redData.Material.RoughnessFactor == 0.375f &&
						redData.Material.MetallicFactor == 0.125f, "model_extracts_materials: the factors of the model");
					check(blueData.Material.DoubleSided && blueData.Material.EmissiveFactor == glm::vec3(0.5f, 0.25f, 0.125f), "model_extracts_materials: double sided and emissive");
					std::error_code pathError;
					check(!redData.BaseColorPath.empty() && std::filesystem::equivalent(materialsDir / redData.BaseColorPath, dir / "pixel.png", pathError) && blueData.BaseColorPath.empty(),
						"model_extracts_materials: the texture is written as a path to the image");
				}

				const std::vector<AssetGuid> used = primitive_materials(model);
				check(used.size() == 2 && used[0].valid() && used[0] == guid_of(red) && used[1].valid() && used[1] == guid_of(blue), "model_extracts_materials: the mesh primitives refer to the files");
				std::vector<std::byte> materialBytes;
				std::optional<MaterialData> cooked = deserialize<MaterialData>(cooked_payload(guid_of(red), AssetType::Material, materialBytes));
				check(cooked && cooked->RoughnessFactor == 0.375f && cooked->BaseColorTexture.valid(), "model_extracts_materials: the file is cooked with its texture");
			}

			// model_reimport_keeps_edits
			{
				MaterialFileData edited;
				std::string editError;
				check(read_material_file(red, edited, editError), "model_reimport_keeps_edits: read");
				edited.Material.RoughnessFactor = 0.9f;
				check(write_material_file(red, edited, editError), "model_reimport_keeps_edits: write");
				const std::string before = read_text(red);

				write_text(model, model_text({ "Red", "Blue" }) + "\n");
				database.reimport(model);

				MaterialFileData after;
				check(read_material_file(red, after, editError) && after.Material.RoughnessFactor == 0.9f && read_text(red) == before, "model_reimport_keeps_edits: the edited file is unchanged");
			}

			// model_material_names_safe
			const std::filesystem::path named = dir / "n.gltf";
			write_text(named, model_text({ "a/b", "", "Mat", "Mat" }));
			database.scan();
			const std::filesystem::path namedDir = dir / "n_materials";
			const std::vector<std::filesystem::path> expectedNames = { namedDir / "a_b.wmat", namedDir / "material.wmat", namedDir / "Mat.wmat", namedDir / "Mat 2.wmat" };
			{
				std::error_code existsError;
				size_t files = 0;
				for (const auto& entry : std::filesystem::directory_iterator(namedDir, existsError))
				{
					files += entry.path().extension() == ".wmat" ? 1 : 0;
				}
				bool all = true;
				for (const std::filesystem::path& path : expectedNames)
				{
					all = all && std::filesystem::exists(path, existsError);
				}
				check(all && files == 4, "model_material_names_safe: four distinct safe files");

				const std::vector<AssetGuid> used = primitive_materials(named);
				bool distinct = used.size() == 4;
				for (size_t i = 0; distinct && i < used.size(); i++)
				{
					distinct = used[i].valid() && used[i] == guid_of(expectedNames[i]);
				}
				check(distinct, "model_material_names_safe: each primitive refers to its own file");
			}

			// model_removed_material_keeps_file: a material the model no longer has leaves its file, and the others keep theirs
			{
				write_text(named, model_text({ "", "Mat", "Mat" }));
				database.reimport(named);
				std::error_code existsError;
				bool all = true;
				for (const std::filesystem::path& path : expectedNames)
				{
					all = all && std::filesystem::exists(path, existsError);
				}
				check(all, "model_removed_material_keeps_file: every file is still there");
				const std::vector<AssetGuid> used = primitive_materials(named);
				check(used.size() == 3 && used[0] == guid_of(expectedNames[1]) && used[1] == guid_of(expectedNames[2]) && used[2] == guid_of(expectedNames[3]),
					"model_removed_material_keeps_file: the remaining materials keep their files");
			}

			// model_embedded_texture_survives_reimport: a material file refers to an embedded image by guid, and the reference
			// is still there when the .wmat itself is cooked again later
			{
				std::vector<unsigned char> buffer;
				const float corners[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
				buffer.insert(buffer.end(), reinterpret_cast<const unsigned char*>(corners), reinterpret_cast<const unsigned char*>(corners) + sizeof(corners));
				buffer.insert(buffer.end(), { 0, 0, 1, 0, 2, 0, 0, 0 });
				buffer.insert(buffer.end(), png, png + sizeof(png));

				const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
				std::string encoded;
				for (size_t i = 0; i < buffer.size(); i += 3)
				{
					const u32 group = (u32(buffer[i]) << 16) | (i + 1 < buffer.size() ? u32(buffer[i + 1]) << 8 : 0u) | (i + 2 < buffer.size() ? u32(buffer[i + 2]) : 0u);
					encoded += alphabet[(group >> 18) & 63];
					encoded += alphabet[(group >> 12) & 63];
					encoded += (i + 1 < buffer.size()) ? alphabet[(group >> 6) & 63] : '=';
					encoded += (i + 2 < buffer.size()) ? alphabet[group & 63] : '=';
				}

				nlohmann::json primitives = nlohmann::json::array();
				primitives.push_back({ { "attributes", { { "POSITION", 0 } } }, { "indices", 1 }, { "material", 0 } });
				nlohmann::json root = {
					{ "asset", { { "version", "2.0" } } }, { "scene", 0 }, { "scenes", { { { "nodes", { 0 } } } } },
					{ "nodes", { { { "name", "Body" }, { "mesh", 0 } } } },
					{ "meshes", { { { "primitives", primitives } } } },
					{ "materials", { { { "name", "Skin" }, { "pbrMetallicRoughness", { { "baseColorTexture", { { "index", 0 } } } } } } } },
					{ "textures", { { { "source", 0 } } } }, { "images", { { { "bufferView", 2 }, { "mimeType", "image/png" } } } },
					{ "accessors", { { { "bufferView", 0 }, { "componentType", 5126 }, { "count", 3 }, { "type", "VEC3" }, { "min", { 0, 0, 0 } }, { "max", { 1, 1, 0 } } },
						{ { "bufferView", 1 }, { "componentType", 5123 }, { "count", 3 }, { "type", "SCALAR" } } } },
					{ "bufferViews", { { { "buffer", 0 }, { "byteOffset", 0 }, { "byteLength", 36 } }, { { "buffer", 0 }, { "byteOffset", 36 }, { "byteLength", 6 } },
						{ { "buffer", 0 }, { "byteOffset", 44 }, { "byteLength", sizeof(png) } } } },
					{ "buffers", { { { "byteLength", buffer.size() }, { "uri", "data:application/octet-stream;base64," + encoded } } } } };

				const std::filesystem::path embedded = dir / "e.gltf";
				write_text(embedded, root.dump(2));
				database.scan();

				const std::filesystem::path skin = dir / "e_materials" / "Skin.wmat";
				MaterialFileData skinData;
				std::string skinError;
				const bool readSkin = read_material_file(skin, skinData, skinError);
				check(readSkin && AssetGuid::from_string(skinData.BaseColorPath).has_value(), "model_embedded_texture_survives_reimport: the file names the embedded image by guid");

				const auto cooked_texture = [&]()
				{
					std::vector<std::byte> materialBytes;
					std::optional<MaterialData> cooked = deserialize<MaterialData>(cooked_payload(guid_of(skin), AssetType::Material, materialBytes));
					return cooked && cooked->BaseColorTexture.valid();
				};
				check(cooked_texture(), "model_embedded_texture_survives_reimport: the first import keeps the texture");

				write_text(skin, read_text(skin) + "\n");
				database.reimport(skin);
				check(cooked_texture(), "model_embedded_texture_survives_reimport: the texture is still there after the file is cooked again");
			}

			// model_material_names_from_meta_are_safe: a hand-edited file name in the .meta cannot leave the material folder
			{
				const std::filesystem::path hand = dir / "h.gltf";
				write_text(hand, model_text({ "Red", "Blue" }));
				database.scan();

				std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(hand));
				check(meta && meta->Settings.contains("materialFiles"), "model_material_names_from_meta_are_safe: the .meta remembers the files");
				if (meta)
				{
					meta->Settings["materialFiles"]["Red"] = "../../escape";
					meta->Settings["materialFiles"]["Blue"] = "sub/dir";
					meta->save(MetaFile::meta_path_for(hand));
					WI_ASSET_LOG(Info, "Smoke test: the next import may warn");
					database.reimport(hand);
				}

				std::error_code existsError;
				const std::filesystem::path root = database.get_assets_root();
				check(!std::filesystem::exists(root / "escape.wmat", existsError) && !std::filesystem::exists(dir / "escape.wmat", existsError) &&
					!std::filesystem::exists(dir / "h_materials" / "sub", existsError), "model_material_names_from_meta_are_safe: nothing is written outside the material folder");
				const std::vector<AssetGuid> used = primitive_materials(hand);
				check(used.size() == 2 && used[0].valid() && used[1].valid() && used[0] != used[1], "model_material_names_from_meta_are_safe: the materials still resolve");
			}

			// material_unknown_guid_texture_rejected: a texture guid that is no asset is a warning and an invalid reference
			{
				const std::filesystem::path bogus = dir / "bogus_guid.wmat";
				nlohmann::json json = { { "version", 1 }, { "roughness", 0.5 }, { "textures", { { "baseColor", AssetGuid::generate().to_string() } } } };
				write_text(bogus, json.dump(2));
				WI_ASSET_LOG(Info, "Smoke test: the next material warning is expected");
				database.reimport(bogus);
				std::vector<std::byte> bogusBytes;
				std::optional<MaterialData> cooked = deserialize<MaterialData>(cooked_payload(guid_of(bogus), AssetType::Material, bogusBytes));
				check(cooked && cooked->RoughnessFactor == 0.5f && !cooked->BaseColorTexture.valid(), "material_unknown_guid_texture_rejected: imports with an invalid texture ref");
			}

			std::filesystem::remove_all(dir, error);
			database.scan();
		}

		// The .wshader parser is pure text processing, no database needed
		{
			const std::string surfaceFn = "void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o) {}\n";
			const auto parse = [](const std::string& source, ParsedSurface& out, std::string& error) { return parse_surface_shader(source, out, error); };
			// True if the source is rejected with an error that starts with "<line>: " and contains text
			const auto rejected_at = [&](const std::string& source, u32 line, const std::string& text)
			{
				ParsedSurface parsed;
				std::string parseError;
				const bool ok = parse(source, parsed, parseError);
				return !ok && parseError.rfind(std::to_string(line) + ": ", 0) == 0 && parseError.find(text) != std::string::npos;
			};

			// surface_parse_all_types
			{
				const std::string source =
					"struct MaterialParams\n"
					"{\n"
					"    float  A;   // range(0,2) default(0.5) label(\"Amount\")\n"
					"    float2 B;   // default(1,2)\n"
					"    float3 C;   // color default(1,0.5,0.25)\n"
					"    float  D;\n"
					"    float4 E;   // color default(1,0.5,0,1)\n"
					"    int    F;   // range(0,3) default(2)\n"
					"    bool   G;   // default(true)\n"
					"    float2 H;\n"
					"    float3 I;\n"
					"};\n"
					"Texture2D Mask; // @param\n"
					"Texture2D Other; // @param\n"
					"Texture2D NotAParam;\n"
					"float3 VertexOffset(VertexInput v, MaterialParams p) { return 0; }\n" + surfaceFn;
				ParsedSurface parsed;
				std::string parseError;
				const bool ok = parse(source, parsed, parseError);
				check(ok && parseError.empty(), "surface_parse_all_types: parses (" + parseError + ")");
				if (ok && parsed.Params.size() == 9)
				{
					const std::vector<SurfaceParam>& p = parsed.Params;
					// HLSL packing: B (float2) fits in 4..12, C (float3) would straddle 16 so it moves to 16, D fills 28..32, H fits 56..64
					const u32 offsets[9] = { 0, 4, 16, 28, 32, 48, 52, 56, 64 };
					bool offsetsMatch = true;
					for (u32 i = 0; i < 9; i++)
					{
						offsetsMatch = offsetsMatch && p[i].Offset == offsets[i];
					}
					check(offsetsMatch, "surface_parse_all_types: offsets are 0,4,16,28,32,48,52,56,64");
					check(parsed.BlockSize == 80, "surface_parse_all_types: the block is 80 bytes");
					check(p[0].Name == "A" && p[0].Type == SurfaceParamType::Float && p[0].Min == 0.0f && p[0].Max == 2.0f && p[0].Default[0] == 0.5f && p[0].Label == "Amount" && !p[0].Color,
						"surface_parse_all_types: float with range, default and label");
					check(p[1].Type == SurfaceParamType::Float2 && p[1].Default[0] == 1.0f && p[1].Default[1] == 2.0f, "surface_parse_all_types: float2 default");
					check(p[2].Type == SurfaceParamType::Float3 && p[2].Color && p[2].Default[2] == 0.25f, "surface_parse_all_types: float3 color");
					check(p[4].Type == SurfaceParamType::Float4 && p[4].Color && p[4].Default[1] == 0.5f && p[4].Default[3] == 1.0f, "surface_parse_all_types: float4 color");
					check(p[5].Type == SurfaceParamType::Int && p[5].Max == 3.0f && p[5].Default[0] == 2.0f, "surface_parse_all_types: int");
					check(p[6].Type == SurfaceParamType::Bool && p[6].Default[0] == 1.0f, "surface_parse_all_types: bool default true");
					check(p[3].Label.empty() && p[3].Min == p[3].Max, "surface_parse_all_types: no annotations, no range");
					check(parsed.Textures.size() == 2 && parsed.Textures[0] == "Mask" && parsed.Textures[1] == "Other", "surface_parse_all_types: only @param textures");
					check(parsed.HasVertexOffset, "surface_parse_all_types: VertexOffset is detected");
					check(parsed.ParamsStructSource.find("struct MaterialParams") != std::string::npos && parsed.ParamsStructSource.find("float3 C;") != std::string::npos,
						"surface_parse_all_types: the struct source is kept");
				}
				else
				{
					check(false, "surface_parse_all_types: nine parameters");
				}
			}

			// surface_parse_block_limit
			{
				const auto with_floats = [&](u32 count)
				{
					std::string source = "struct MaterialParams {\n";
					for (u32 i = 0; i < count; i++)
					{
						source += "float P" + std::to_string(i) + ";\n";
					}
					return source + "};\n" + surfaceFn;
				};
				ParsedSurface parsed;
				std::string parseError;
				check(parse(with_floats(64), parsed, parseError) && parsed.BlockSize == 256, "surface_parse_block_limit: exactly 256 bytes is accepted");
				check(rejected_at(with_floats(65), 66, "256"), "surface_parse_block_limit: 257 bytes is rejected with the limit, at the member that crosses it");
			}

			// surface_parse_errors
			{
				check(rejected_at("struct MaterialParams {\n    half X;\n};\n" + surfaceFn, 2, "half"), "surface_parse_errors: unknown type");
				check(rejected_at("struct MaterialParams {\n    float X;\n    float Y; // default(1) rnge(0,1)\n};\n" + surfaceFn, 3, "rnge"), "surface_parse_errors: unknown annotation");
				check(rejected_at("struct MaterialParams {\n    float X; // range(0)\n};\n" + surfaceFn, 2, "range"), "surface_parse_errors: range with one value");
				check(rejected_at("struct MaterialParams {\n    float2 X; // default(1)\n};\n" + surfaceFn, 2, "default"), "surface_parse_errors: default with the wrong count");
				check(rejected_at("struct MaterialParams {\n    float X; // range(2,1)\n};\n" + surfaceFn, 2, "range"), "surface_parse_errors: range with min above max");
				check(rejected_at("struct MaterialParams {\n    float2 X; // color\n};\n" + surfaceFn, 2, "color"), "surface_parse_errors: color on a float2");
				check(rejected_at("struct MaterialParams {\n    float X;\n    float X;\n};\n" + surfaceFn, 3, "X"), "surface_parse_errors: duplicate name");
				check(rejected_at("struct MaterialParams {\n    float X;\n    float Y; // range(0,1) bogus\n};\n" + surfaceFn, 3, "bogus"), "surface_parse_errors: unknown word after an annotation");
				check(rejected_at("struct MaterialParams {\n    int X; // default(1e30)\n};\n" + surfaceFn, 2, "default"), "surface_parse_errors: int default out of range");
				check(rejected_at("struct MaterialParams {\n    float X; // default(nan)\n};\n" + surfaceFn, 2, "nan"), "surface_parse_errors: nan default");
				check(rejected_at("struct MaterialParams {\n    float X; // default(0x10)\n};\n" + surfaceFn, 2, "0x10"), "surface_parse_errors: hex default");
				{
					ParsedSurface prose;
					std::string proseError;
					check(parse("struct MaterialParams {\n    float Amount; // how strong it is\n    float B; // default(0.5f)\n};\n" + surfaceFn, prose, proseError) && prose.Params.size() == 2 && prose.Params[1].Default[0] == 0.5f,
						"surface_parse_errors: prose comment ignored, f suffix accepted (" + proseError + ")");
				}
				check(rejected_at("Texture2D A; // @param\nTexture2D B; // @param\nTexture2D C; // @param\nTexture2D D; // @param\nTexture2D E; // @param\n" + surfaceFn, 5, "4"),
					"surface_parse_errors: five textures");
				check(rejected_at("struct MaterialParams {\n    float X;\n};\n", 1, "Surface"), "surface_parse_errors: missing Surface");
			}

			// surface_parse_empty_params
			{
				ParsedSurface parsed;
				std::string parseError;
				check(parse(surfaceFn, parsed, parseError) && parsed.Params.empty() && parsed.BlockSize == 0 && parsed.Textures.empty() && !parsed.HasVertexOffset,
					"surface_parse_empty_params: no struct, no parameters");
				check(parse("struct MaterialParams {\n};\n" + surfaceFn, parsed, parseError) && parsed.Params.empty() && parsed.BlockSize == 0, "surface_parse_empty_params: an empty struct");
			}

			// surface_parse_ignores_comments_and_strings
			{
				const std::string source =
					"/* void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o)\n"
					"   float3 VertexOffset(VertexInput v, MaterialParams p) */\n"
					"struct MaterialParams\n"
					"{\n"
					"    float A;\n"
					"    // float Hidden;\n"
					"    /* float AlsoHidden;\n"
					"       float3 Gone; */\n"
					"    float B; // default(2)\n"
					"};\n"
					"// Texture2D Commented; // @param\n"
					"static const string S = \"Surface( VertexOffset( Texture2D T; // @param\";\n"
					+ surfaceFn;
				ParsedSurface parsed;
				std::string parseError;
				const bool ok = parse(source, parsed, parseError);
				check(ok, "surface_parse_ignores_comments_and_strings: parses (" + parseError + ")");
				check(ok && parsed.Params.size() == 2 && parsed.Params[1].Name == "B" && parsed.Params[1].Offset == 4 && parsed.Textures.empty() && !parsed.HasVertexOffset,
					"surface_parse_ignores_comments_and_strings: commented members, textures and hooks are not seen");
				check(rejected_at("/* void Surface(SurfaceInput i) */\n// Surface(\nstatic const string S = \"Surface(\";\n", 1, "Surface"),
					"surface_parse_ignores_comments_and_strings: a Surface only in comments and strings is missing");
			}
		}

		// 7. surface shader assets (.wshader)
		{
			const std::filesystem::path dir = database.get_assets_root() / "zz_surface_smoke";
			std::filesystem::create_directories(dir, error);

			// the cooked surface shader of a source file, straight from the Library
			const auto cooked_surface = [&](const std::filesystem::path& source) -> std::optional<SurfaceShaderData>
			{
				std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(source));
				if (!meta)
				{
					return std::nullopt;
				}
				for (const CookedFile& file : read_library(database.get_library_root()))
				{
					std::span<const std::byte> payload;
					if (file.Guid == meta->Guid && file.Type == AssetType::SurfaceShader && parse_wasset_header(file.Bytes, payload))
					{
						return deserialize<SurfaceShaderData>(payload);
					}
				}
				return std::nullopt;
			};

			// every variant for both stages and both targets, each a binary of its target (DXBC container, SPIR-V magic)
			const auto all_variants = [](const SurfaceShaderData& shader)
			{
				const SurfaceVariantKind kinds[] = { SurfaceVariantKind::Forward, SurfaceVariantKind::Depth, SurfaceVariantKind::DepthMasked,
					SurfaceVariantKind::Velocity, SurfaceVariantKind::Shadow, SurfaceVariantKind::ShadowMasked };
				for (SurfaceVariantKind kind : kinds)
				{
					for (ShaderStage stage : { ShaderStage::Vertex, ShaderStage::Pixel })
					{
						for (ShaderTarget target : { ShaderTarget::Dxbc, ShaderTarget::Spirv })
						{
							const SurfaceVariant* variant = shader.find(kind, stage, target);
							if (variant == nullptr || variant->Code.size() < 4)
							{
								return false;
							}
							const bool dxbc = std::memcmp(variant->Code.data(), "DXBC", 4) == 0;
							u32 magic = 0;
							std::memcpy(&magic, variant->Code.data(), 4);
							if (target == ShaderTarget::Dxbc ? !dxbc : magic != 0x07230203u)
							{
								return false;
							}
						}
					}
				}
				return shader.Variants.size() == 24;
			};

			const auto same_params = [](const std::vector<SurfaceParam>& a, const std::vector<SurfaceParam>& b)
			{
				if (a.size() != b.size())
				{
					return false;
				}
				for (size_t i = 0; i < a.size(); i++)
				{
					if (a[i].Name != b[i].Name || a[i].Type != b[i].Type || a[i].Offset != b[i].Offset || a[i].Min != b[i].Min || a[i].Max != b[i].Max ||
						std::memcmp(a[i].Default, b[i].Default, sizeof(a[i].Default)) != 0 || a[i].Color != b[i].Color || a[i].Label != b[i].Label)
					{
						return false;
					}
				}
				return true;
			};

			// What the importer logs while fn runs (the console panel's callback, if any, still gets every line).
			const auto capture_log = [](const std::function<void()>& fn)
			{
				struct Captured
				{
					std::mutex Mutex{};
					std::string Text{};
				};
				auto captured = std::make_shared<Captured>();
				const LogCallback previous = Log::get_log_callback();
				Log::set_log_callback([captured, previous](LogLevel level, std::string line)
					{
						{
							std::lock_guard lock(captured->Mutex);
							captured->Text += line;
							captured->Text += '\n';
						}
						if (previous)
						{
							previous(level, std::move(line));
						}
					});
				fn();
				Log::set_log_callback(previous);
				std::lock_guard lock(captured->Mutex);
				return captured->Text;
			};

			const auto shader_a = [](const std::string& extraParam, const std::string& amountDefault, const std::string& brokenLine)
			{
				return std::string(
					"struct MaterialParams\n"                                          // 1
					"{\n"                                                              // 2
					"    float4 Tint;   // color default(1,0.5,0,1)\n"                 // 3
					"    float  Amount; // range(0,2) default(") + amountDefault + ")\n" // 4
					"    int    Mode;   // range(0,3) default(1)\n"                    // 5
					"    bool   Flip;   // default(true) label(\"Flip it\")\n"         // 6
					+ extraParam +                                                     // 7
					"};\n"                                                             // 8
					"Texture2D Mask; // @param\n"                                      // 9
					"\n"                                                               // 10
					"float3 VertexOffset(VertexInput v, MaterialParams p) { return v.Normal * (p.Amount * 0.0); }\n" // 11
					"void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o)\n" // 12
					"{\n"                                                              // 13
					"    float mask = Mask.Sample(uSurfaceSampler, i.Uv).r;\n"         // 14
					+ brokenLine +                                                     // 15
					"    o.Albedo *= p.Tint.rgb * mask;\n"                             // 16
					"    o.Emissive += p.Flip ? float3((float)p.Mode, 0.0, 0.0) : float3(0.0, 0.0, 0.0);\n"
					"    o.Alpha *= mask;\n"
					"}\n";
			};
			const std::string validA = shader_a("    float  Unused;\n", "0.5", "\n");

			// surface_import_compiles_all_variants
			const std::filesystem::path a = dir / "a.wshader";
			write_text(a, validA);
			database.reimport(a);
			std::optional<SurfaceShaderData> cooked = cooked_surface(a);
			check(cooked.has_value(), "surface_import_compiles_all_variants: the .wshader is cooked");
			u64 hashA = 0;
			if (cooked)
			{
				hashA = cooked->LayoutHash;
				ParsedSurface parsed;
				std::string parseError;
				const bool parsedOk = parse_surface_shader(validA, parsed, parseError);
				check(all_variants(*cooked), "surface_import_compiles_all_variants: every variant, stage and target is present and valid");
				check(parsedOk && same_params(cooked->Params, parsed.Params) && cooked->BlockSize == parsed.BlockSize && cooked->Textures == parsed.Textures &&
					cooked->HasVertexOffset == parsed.HasVertexOffset, "surface_import_compiles_all_variants: the params table equals the parser's");
				check(cooked->Name == "a.wshader" && cooked->BlockSize == 32 && cooked->Textures.size() == 1 && cooked->HasVertexOffset && cooked->LayoutHash != 0,
					"surface_import_compiles_all_variants: name, block size, texture, vertex offset and hash");
			}

			// surface_import_compile_error_keeps_previous
			{
				write_text(a, shader_a("    float  Unused;\n", "0.5", "    o.Albedo = ;\n"));
				WI_ASSET_LOG(Info, "Smoke test: the next surface shader compile error is expected");
				const std::string log = capture_log([&]() { database.reimport(a); });
				check(log.find("a.wshader(15") != std::string::npos, "surface_import_compile_error_keeps_previous: the log names the line of the error");
				std::optional<SurfaceShaderData> previous = cooked_surface(a);
				check(previous && previous->LayoutHash == hashA && all_variants(*previous), "surface_import_compile_error_keeps_previous: the previous cooked data still loads");
			}

			// surface_hash_changes_with_layout
			{
				write_text(a, shader_a("    float  Unused;\n", "1.5", "\n"));
				database.reimport(a);
				std::optional<SurfaceShaderData> changedDefault = cooked_surface(a);
				check(changedDefault && changedDefault->Params.size() == 5 && changedDefault->Params[1].Default[0] == 1.5f && changedDefault->LayoutHash == hashA,
					"surface_hash_changes_with_layout: a new default keeps the hash");

				write_text(a, shader_a("    float  Unused;\n    float3 Added;\n", "0.5", "\n"));
				database.reimport(a);
				std::optional<SurfaceShaderData> added = cooked_surface(a);
				check(added && added->Params.size() == 6 && added->LayoutHash != hashA, "surface_hash_changes_with_layout: adding a parameter changes the hash");
			}

			// surface_import_no_params_no_textures
			{
				const std::filesystem::path empty = dir / "empty.wshader";
				write_text(empty, "void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o)\n{\n    o.Emissive = float3(1.0, 0.0, 0.0);\n}\n");
				database.reimport(empty);
				std::optional<SurfaceShaderData> shader = cooked_surface(empty);
				check(shader && shader->Params.empty() && shader->BlockSize == 0 && shader->Textures.empty() && !shader->HasVertexOffset && all_variants(*shader),
					"surface_import_no_params_no_textures: imports with every variant");
			}

			// surface_import_256_bytes
			{
				const auto with_floats = [](u32 count)
				{
					std::string source = "struct MaterialParams\n{\n";
					for (u32 i = 0; i < count; i++)
					{
						source += "    float P" + std::to_string(i) + "; // default(" + std::to_string(i) + ")\n";
					}
					return source + "};\nvoid Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o)\n{\n    o.Emissive = float3(p.P0, p.P31, p.P63);\n}\n";
				};
				const std::filesystem::path full = dir / "full.wshader";
				write_text(full, with_floats(64));
				database.reimport(full);
				std::optional<SurfaceShaderData> shader = cooked_surface(full);
				check(shader && shader->BlockSize == 256 && shader->Params.size() == 64 && shader->Params[63].Offset == 252 && all_variants(*shader),
					"surface_import_256_bytes: a 256-byte block imports");

				const std::filesystem::path over = dir / "over.wshader";
				write_text(over, with_floats(65));
				WI_ASSET_LOG(Info, "Smoke test: the next surface shader error is expected");
				const std::string log = capture_log([&]() { database.reimport(over); });
				check(!cooked_surface(over).has_value() && log.find("256") != std::string::npos, "surface_import_256_bytes: a 257-byte block is rejected with the limit");
			}

			std::filesystem::remove_all(dir, error);
			database.scan();
		}

		// 8. materials that reference a surface shader
		{
			const std::filesystem::path dir = database.get_assets_root() / "zz_matshader_smoke";
			std::filesystem::create_directories(dir, error);

			const unsigned char png[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x64, 0xf8, 0xcf, 0x50, 0x0f, 0x00, 0x03, 0x86, 0x01, 0x80, 0x5a, 0x34, 0x7d, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };
			{
				std::ofstream out(dir / "pixel.png", std::ios::binary);
				out.write(reinterpret_cast<const char*>(png), sizeof(png));
			}

			const auto cooked_material = [&](const std::filesystem::path& source, std::vector<AssetGuid>* dependencies = nullptr) -> std::optional<MaterialData>
			{
				std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(source));
				if (!meta)
				{
					return std::nullopt;
				}
				for (const CookedFile& file : read_library(database.get_library_root()))
				{
					std::span<const std::byte> payload;
					if (file.Guid == meta->Guid && file.Type == AssetType::Material && parse_wasset_header(file.Bytes, payload))
					{
						if (dependencies != nullptr)
						{
							*dependencies = file.Dependencies;
						}
						return deserialize<MaterialData>(payload);
					}
				}
				return std::nullopt;
			};

			const auto capture_log = [](const std::function<void()>& fn)
			{
				struct Captured
				{
					std::mutex Mutex{};
					std::string Text{};
				};
				auto captured = std::make_shared<Captured>();
				const LogCallback previous = Log::get_log_callback();
				Log::set_log_callback([captured, previous](LogLevel level, std::string line)
					{
						{
							std::lock_guard lock(captured->Mutex);
							captured->Text += line;
							captured->Text += '\n';
						}
						if (previous)
						{
							previous(level, std::move(line));
						}
					});
				fn();
				Log::set_log_callback(previous);
				std::lock_guard lock(captured->Mutex);
				return captured->Text;
			};

			const auto shader_source = [](const std::string& amountDefault)
			{
				return std::string(
					"struct MaterialParams\n"
					"{\n"
					"    float  Amount; // range(0,2) default(") + amountDefault + ")\n"
					"    float3 Tint;   // color default(1,0.5,0.25)\n"
					"    int    Mode;   // range(0,3) default(1)\n"
					"    bool   Flip;   // default(true)\n"
					"};\n"
					"Texture2D Mask; // @param\n"
					"void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o)\n"
					"{\n"
					"    float mask = Mask.Sample(uSurfaceSampler, i.Uv).r;\n"
					"    o.Albedo *= p.Tint * mask * p.Amount;\n"
					"    o.Emissive += p.Flip ? float3((float)p.Mode, 0.0, 0.0) : float3(0.0, 0.0, 0.0);\n"
					"}\n";
			};

			const std::filesystem::path shaderFile = dir / "s.wshader";
			write_text(shaderFile, shader_source("0.5"));
			database.reimport(shaderFile);

			// the parameter block is Amount@0, Tint@4, Mode@16, Flip@20, padded to 32 bytes
			const auto float_at = [](const MaterialData& m, size_t offset) { float v = -999.0f; if (offset + 4 <= m.ParamBytes.size()) std::memcpy(&v, m.ParamBytes.data() + offset, 4); return v; };
			const auto int_at = [](const MaterialData& m, size_t offset) { i32 v = -999; if (offset + 4 <= m.ParamBytes.size()) std::memcpy(&v, m.ParamBytes.data() + offset, 4); return v; };
			const auto uint_at = [](const MaterialData& m, size_t offset) { u32 v = 999; if (offset + 4 <= m.ParamBytes.size()) std::memcpy(&v, m.ParamBytes.data() + offset, 4); return v; };

			const auto write_wmat = [&](const std::filesystem::path& path, const nlohmann::json& params, const std::string& shader = "s.wshader")
			{
				write_text(path, nlohmann::json{ { "version", 1 }, { "shader", shader }, { "params", params } }.dump(2));
				database.reimport(path);
			};

			// material_old_archive_loads: a material cooked before the shader section ends after the emissive texture
			{
				MaterialData old;
				old.Name = "Old";
				old.RoughnessFactor = 0.3f;
				std::vector<std::byte> bytes = serialize(old);
				const size_t trailer = sizeof(AssetRef) + sizeof(u64) + sizeof(AssetRef) * SURFACE_MAX_TEXTURES; // shader, empty block, textures
				check(bytes.size() > trailer, "material_old_archive_loads: the new archive has the shader section");
				bytes.resize(bytes.size() - trailer);
				std::optional<MaterialData> loaded = deserialize<MaterialData>(bytes);
				check(loaded && loaded->Name == "Old" && loaded->RoughnessFactor == 0.3f && !loaded->Shader.valid() && loaded->ParamBytes.empty() && !loaded->ParamTextures[0].valid(),
					"material_old_archive_loads: an archive without the shader section loads with no shader");
			}

			// material_packs_params
			{
				const std::filesystem::path wmat = dir / "packs.wmat";
				write_wmat(wmat, { { "Amount", 1.5 }, { "Tint", { 0.1, 0.2, 0.3 } }, { "Mode", 2 }, { "Flip", false }, { "Mask", "pixel.png" } });
				std::vector<AssetGuid> dependencies;
				std::optional<MaterialData> material = cooked_material(wmat, &dependencies);
				check(material.has_value(), "material_packs_params: the .wmat is cooked");
				if (material)
				{
					check(material->Shader.valid() && std::find(dependencies.begin(), dependencies.end(), material->Shader.resolved()) != dependencies.end(),
						"material_packs_params: the shader is referenced and a dependency");
					check(material->ParamBytes.size() == 32, "material_packs_params: the block is 32 bytes");
					check(float_at(*material, 0) == 1.5f && float_at(*material, 4) == 0.1f && float_at(*material, 8) == 0.2f && float_at(*material, 12) == 0.3f,
						"material_packs_params: float at 0 and the color at 4");
					check(int_at(*material, 16) == 2 && uint_at(*material, 20) == 0u, "material_packs_params: int at 16 and bool at 20");
					check(uint_at(*material, 24) == 0u && uint_at(*material, 28) == 0u, "material_packs_params: the padding is zero");
					check(material->ParamTextures[0].valid() && !material->ParamTextures[1].valid() && !material->ParamTextures[2].valid() && !material->ParamTextures[3].valid() &&
						std::find(dependencies.begin(), dependencies.end(), material->ParamTextures[0].resolved()) != dependencies.end(),
						"material_packs_params: the texture parameter resolves into slot 0 and is a dependency");
				}
			}

			// material_defaults_for_unset
			const std::filesystem::path defaultsFile = dir / "defaults.wmat";
			{
				write_wmat(defaultsFile, nlohmann::json::object());
				std::optional<MaterialData> material = cooked_material(defaultsFile);
				check(material && material->ParamBytes.size() == 32 && float_at(*material, 0) == 0.5f && float_at(*material, 4) == 1.0f && float_at(*material, 8) == 0.5f &&
					float_at(*material, 12) == 0.25f && int_at(*material, 16) == 1 && uint_at(*material, 20) == 1u, "material_defaults_for_unset: every parameter takes the shader default");
				check(material && !material->ParamTextures[0].valid(), "material_defaults_for_unset: an unset texture stays empty");

				const std::filesystem::path plain = dir / "plain.wmat";
				write_text(plain, R"({ "version": 1, "roughness": 0.5 })");
				database.reimport(plain);
				std::optional<MaterialData> plainMaterial = cooked_material(plain);
				check(plainMaterial && !plainMaterial->Shader.valid() && plainMaterial->ParamBytes.empty() && !plainMaterial->ParamTextures[0].valid(),
					"material_defaults_for_unset: a material without a shader has no block");
			}

			// material_param_clamped_and_wrong_type_ignored
			{
				const std::filesystem::path wmat = dir / "clamped.wmat";
				const std::string log = capture_log([&]()
					{
						write_wmat(wmat, { { "Amount", 9.0 }, { "Tint", "red" }, { "Mode", -5 }, { "Flip", 3 }, { "Bogus", 1 } });
					});
				std::optional<MaterialData> material = cooked_material(wmat);
				check(material && float_at(*material, 0) == 2.0f, "material_param_clamped_and_wrong_type_ignored: a float above its range is clamped");
				check(material && int_at(*material, 16) == 0, "material_param_clamped_and_wrong_type_ignored: an int below its range is clamped");
				check(material && float_at(*material, 4) == 1.0f && float_at(*material, 8) == 0.5f && float_at(*material, 12) == 0.25f,
					"material_param_clamped_and_wrong_type_ignored: a string for a color keeps the default");
				check(material && uint_at(*material, 20) == 1u, "material_param_clamped_and_wrong_type_ignored: a number for a bool keeps the default");
				check(log.find("Tint") != std::string::npos && log.find("Flip") != std::string::npos, "material_param_clamped_and_wrong_type_ignored: wrong-typed values are warned about");
				check(log.find("Bogus") != std::string::npos, "material_param_clamped_and_wrong_type_ignored: an unknown name is warned about");
			}

			// material_wide_types: float2/float4/color lengths, exact and ranged ints, unranged floats
			{
				write_text(dir / "w.wshader",
					"struct MaterialParams\n"
					"{\n"
					"    float2 V2;     // default(1,2)\n"
					"    float4 V4;     // color default(0.1,0.2,0.3,0.4)\n"
					"    float3 C3;     // color default(1,1,1)\n"
					"    int    Big;    // \n"
					"    int    Ranged; // range(10,20) default(15)\n"
					"    float  Free;   // \n"
					"};\n"
					"void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o)\n"
					"{\n"
					"    o.Albedo *= float3(p.V2, p.V4.x) * p.C3 * (float)(p.Big + p.Ranged) * p.Free;\n"
					"}\n");
				database.reimport(dir / "w.wshader");

				// V2@0, V4@16, C3@32, Big@44, Ranged@48, Free@52, 64 bytes
				const auto vec_is = [&](const MaterialData& m, size_t at, std::initializer_list<float> values)
				{
					size_t k = 0;
					for (float value : values)
					{
						if (float_at(m, at + 4 * k++) != value)
						{
							return false;
						}
					}
					return true;
				};

				const std::filesystem::path exact = dir / "wide_exact.wmat";
				write_wmat(exact, { { "Big", 16777217 }, { "Ranged", 100 }, { "Free", 1.0e30 } }, "w.wshader");
				std::optional<MaterialData> material = cooked_material(exact);
				check(material && material->ParamBytes.size() == 64 && int_at(*material, 44) == 16777217, "material_int_exact: 16777217 round-trips exactly");
				check(material && int_at(*material, 48) == 20, "material_int_exact: a ranged int clamps in the integer domain");
				check(material && float_at(*material, 52) == 1.0e30f, "material_unranged_not_clamped: a float without a range is left alone");
				check(material && vec_is(*material, 0, { 1.0f, 2.0f }) && vec_is(*material, 16, { 0.1f, 0.2f, 0.3f, 0.4f }) && vec_is(*material, 32, { 1.0f, 1.0f, 1.0f }),
					"material_wide_types: unset vectors take their defaults");

				const std::filesystem::path maxInt = dir / "wide_max.wmat";
				write_wmat(maxInt, { { "Big", 2147483647 }, { "Ranged", 5 } }, "w.wshader");
				material = cooked_material(maxInt);
				check(material && int_at(*material, 44) == 2147483647 && int_at(*material, 48) == 10, "material_int_exact: int32 max round-trips, a ranged int clamps up to its minimum");

				const std::filesystem::path beyond = dir / "wide_beyond.wmat";
				write_wmat(beyond, { { "Big", 3000000000.0 }, { "Ranged", 12 } }, "w.wshader");
				material = cooked_material(beyond);
				check(material && int_at(*material, 44) == 2147483647 && int_at(*material, 48) == 12, "material_int_exact: a value beyond int32 clamps to int32 max");

				const std::filesystem::path wrong = dir / "wide_wrong.wmat";
				const std::string log = capture_log([&]()
					{
						write_wmat(wrong, { { "V2", { 1, 2, 3 } }, { "V4", { 1, 2 } }, { "C3", { 1, 2 } }, { "Big", 1.5 }, { "Free", 1.0e300 }, { "Ranged", 12 } }, "w.wshader");
					});
				material = cooked_material(wrong);
				check(material && vec_is(*material, 0, { 1.0f, 2.0f }), "material_param_wrong_length: a float2 with three values keeps the default");
				check(material && vec_is(*material, 16, { 0.1f, 0.2f, 0.3f, 0.4f }), "material_param_wrong_length: a float4 with two values keeps the default");
				check(material && vec_is(*material, 32, { 1.0f, 1.0f, 1.0f }), "material_param_wrong_length: a color with two values keeps the default");
				check(material && int_at(*material, 44) == 0, "material_param_wrong_type: a fractional int keeps the default");
				check(material && float_at(*material, 52) == 0.0f && int_at(*material, 48) == 12, "material_param_wrong_type: 1e300 for a float keeps the default");
				check(log.find("parameter V2 ") != std::string::npos && log.find("parameter V4 ") != std::string::npos && log.find("parameter C3 ") != std::string::npos &&
					log.find("parameter Big ") != std::string::npos && log.find("parameter Free ") != std::string::npos && log.find("parameter Ranged ") == std::string::npos,
					"material_param_wrong_type: each rejected value is warned about, accepted ones are not");
			}

			// material_archive_corrupt_trailer: a cut or damaged shader section fails to load instead of over-reading
			{
				MaterialData full;
				full.ParamBytes.assign(16, 7);
				const std::vector<std::byte> bytes = serialize(full);
				const size_t trailer = sizeof(AssetRef) + sizeof(u64) + 16 + sizeof(AssetRef) * SURFACE_MAX_TEXTURES;
				const size_t oldSize = bytes.size() - trailer;
				check(deserialize<MaterialData>(bytes).has_value(), "material_archive_corrupt_trailer: the complete archive loads");

				bool allCutsFail = true;
				for (size_t cut = oldSize + 1; cut < bytes.size(); cut++)
				{
					allCutsFail = allCutsFail && !deserialize<MaterialData>(std::span<const std::byte>(bytes.data(), cut)).has_value();
				}
				check(allCutsFail, "material_archive_corrupt_trailer: every cut inside the shader section fails");

				std::vector<std::byte> huge = bytes;
				const u64 hugeCount = ~0ull;
				std::memcpy(huge.data() + oldSize + sizeof(AssetRef), &hugeCount, sizeof(hugeCount));
				check(!deserialize<MaterialData>(huge).has_value(), "material_archive_corrupt_trailer: a block count beyond the data fails");

				MaterialData oversized;
				oversized.ParamBytes.assign(SURFACE_BLOCK_MAX_SIZE + 1, 1);
				check(!deserialize<MaterialData>(serialize(oversized)).has_value(), "material_archive_corrupt_trailer: a block above 256 bytes fails");
			}

			// material_follows_shader_edit
			{
				std::optional<MaterialData> before = cooked_material(defaultsFile);
				write_text(shaderFile, shader_source("1.25"));
				database.reimport(shaderFile);
				std::optional<MaterialData> after = cooked_material(defaultsFile);
				check(before && after && float_at(*before, 0) == 0.5f && float_at(*after, 0) == 1.25f, "material_follows_shader_edit: a new shader default reaches the material block");
			}

			// material_missing_shader_still_imports
			{
				const std::filesystem::path missing = dir / "missing.wmat";
				std::string log = capture_log([&]() { write_wmat(missing, { { "Amount", 1.0 } }, "no_such_shader.wshader"); });
				std::optional<MaterialData> material = cooked_material(missing);
				check(material && material->ParamBytes.empty() && !material->Shader.valid() && log.find("no_such_shader") != std::string::npos,
					"material_missing_shader_still_imports: a shader that does not exist imports with no block and a warning");

				write_text(dir / "bad.wshader", "void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o)\n{\n    o.Albedo = ;\n}\n");
				WI_ASSET_LOG(Info, "Smoke test: the next surface shader compile error is expected");
				const std::filesystem::path broken = dir / "broken.wmat";
				log = capture_log([&]() { write_wmat(broken, nlohmann::json::object(), "bad.wshader"); });
				std::optional<MaterialData> brokenMaterial = cooked_material(broken);
				check(brokenMaterial && brokenMaterial->Shader.valid() && brokenMaterial->ParamBytes.empty(),
					"material_missing_shader_still_imports: a shader that fails to compile keeps the reference with no block");
			}

			std::filesystem::remove_all(dir, error);
			database.scan();
		}

		// 9. prefab files (.wprefab)
		{
			const std::filesystem::path dir = database.get_assets_root() / "zz_prefab_smoke";
			std::filesystem::create_directories(dir, error);

			const unsigned char png[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x64, 0xf8, 0xcf, 0x50, 0x0f, 0x00, 0x03, 0x86, 0x01, 0x80, 0x5a, 0x34, 0x7d, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };
			{
				std::ofstream out(dir / "pixel.png", std::ios::binary);
				out.write(reinterpret_cast<const char*>(png), sizeof(png));
			}
			const std::optional<AssetRef> pixel = database.ensure_imported(dir / "pixel.png");
			check(pixel.has_value(), "prefab_file: the texture the prefab refers to imports");

			EntityRecord root;
			root.Id = EntityGuid::generate();
			root.Components.push_back(ComponentRecord{ "Name", { FieldRecord{ "Value", FieldValue(std::string("Root")) } } });
			EntityRecord lamp;
			lamp.Id = EntityGuid::generate();
			lamp.Parent = root.Id;
			lamp.Components.push_back(ComponentRecord{ "Name", { FieldRecord{ "Value", FieldValue(std::string("Lamp")) } } });
			lamp.Components.push_back(ComponentRecord{ "Light", { FieldRecord{ "Intensity", FieldValue(5.0f) } } });
			lamp.Components.push_back(ComponentRecord{ "MeshRenderer", { FieldRecord{ "Mesh", FieldValue(pixel.value_or(AssetRef{})) } } });

			const std::filesystem::path file = dir / "lamp.wprefab";
			check(write_prefab_file(file, { root, lamp }, "Lamp"), "prefab_file: the file is written");

			// the cooked prefab of a source file, straight from the Library, with its dependencies
			const auto cooked_prefab = [&](const std::filesystem::path& source, std::vector<AssetGuid>* dependencies = nullptr) -> std::optional<PrefabData>
			{
				std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(source));
				if (!meta)
				{
					return std::nullopt;
				}
				for (const CookedFile& cooked : read_library(database.get_library_root()))
				{
					std::span<const std::byte> payload;
					if (cooked.Guid == meta->Guid && cooked.Type == AssetType::Prefab && parse_wasset_header(cooked.Bytes, payload))
					{
						if (dependencies != nullptr)
						{
							*dependencies = cooked.Dependencies;
						}
						return deserialize<PrefabData>(payload);
					}
				}
				return std::nullopt;
			};

			std::vector<AssetGuid> dependencies;
			const std::optional<AssetRef> ref = database.ensure_imported(file);
			check(ref.has_value(), "prefab_file: the .wprefab imports");
			std::optional<PrefabData> prefab = cooked_prefab(file, &dependencies);
			check(prefab && prefab->Nodes.size() == 2 && prefab->Nodes[0].Name == "Root" && prefab->Nodes[0].Parent == -1 && prefab->Nodes[1].Name == "Lamp" && prefab->Nodes[1].Parent == 0,
				"prefab_file: the cooked prefab has the nodes and the parent of the file");
			check(prefab && prefab->Nodes.size() == 2 && prefab->Nodes[0].StableId != 0 && prefab->Nodes[1].StableId != 0 && prefab->Nodes[0].StableId != prefab->Nodes[1].StableId, "prefab_file: every node has its own id");
			check(prefab && prefab->Nodes.size() == 2 && prefab->Nodes[1].Components.size() == 2 && prefab->Nodes[1].Components[0].Name == "Light" && prefab->Nodes[1].Components[1].Name == "MeshRenderer",
				"prefab_file: the components of a node are kept, and Name and Transform are not among them");
			check(pixel && std::find(dependencies.begin(), dependencies.end(), pixel->Guid) != dependencies.end(), "prefab_file: an asset a component refers to is a dependency");

			// the same file imported again gives the same node ids
			const std::vector<u64> firstIds = prefab ? std::vector<u64>{ prefab->Nodes[0].StableId, prefab->Nodes[1].StableId } : std::vector<u64>{};
			if (ref)
			{
				database.reimport(file, true);
			}
			std::optional<PrefabData> again = cooked_prefab(file);
			check(again && again->Nodes.size() == 2 && firstIds.size() == 2 && again->Nodes[0].StableId == firstIds[0] && again->Nodes[1].StableId == firstIds[1], "prefab_file: a forced re-import gives the same node ids");

			// a damaged file is an import error and leaves no prefab
			const std::filesystem::path broken = dir / "broken.wprefab";
			{
				std::ofstream out(broken, std::ios::binary | std::ios::trunc);
				out << "{";
			}
			WI_ASSET_LOG(Info, "Smoke test: the next import error is expected");
			database.ensure_imported(broken);
			check(!cooked_prefab(broken).has_value(), "prefab_file: a file that is not JSON cooks no prefab");

			// a prefab file can hold an instance of another prefab: the node keeps the prefab and the overrides, and the prefab is a dependency
			if (ref && prefab && prefab->Nodes.size() == 2)
			{
				EntityRecord holderRoot;
				holderRoot.Id = EntityGuid::generate();
				holderRoot.Components.push_back(ComponentRecord{ "Name", { FieldRecord{ "Value", FieldValue(std::string("Holder")) } } });
				EntityRecord slot;
				slot.Id = EntityGuid::generate();
				slot.Parent = holderRoot.Id;
				slot.Components.push_back(ComponentRecord{ "Name", { FieldRecord{ "Value", FieldValue(std::string("Slot")) } } });
				slot.Components.push_back(ComponentRecord{ "PrefabInstance", { FieldRecord{ "Prefab", FieldValue(*ref) } } });
				PrefabOverride brighter;
				brighter.Kind = OverrideKind::SetField;
				brighter.NodeId = prefab->Nodes[1].StableId;
				brighter.Component = "Light";
				brighter.Field = "Intensity";
				brighter.Value = FieldValue(9.0f);
				slot.Overrides.push_back(brighter);

				const std::filesystem::path holderFile = dir / "holder.wprefab";
				check(write_prefab_file(holderFile, { holderRoot, slot }, "Holder"), "prefab_nested: the file with an instance is written");
				std::vector<AssetGuid> holderDependencies;
				check(database.ensure_imported(holderFile).has_value(), "prefab_nested: the .wprefab with an instance imports");
				const std::optional<PrefabData> holder = cooked_prefab(holderFile, &holderDependencies);
				check(holder && holder->Nodes.size() == 2 && holder->Nodes[1].Name == "Slot" && holder->Nodes[1].Parent == 0 && holder->Nodes[1].Instance.has_value()
					&& holder->Nodes[1].Instance->Prefab.Guid == ref->Guid && holder->Nodes[1].Instance->Overrides.size() == 1
					&& holder->Nodes[1].Instance->Overrides[0].NodeId == brighter.NodeId && holder->Nodes[1].Instance->Overrides[0].Field == "Intensity", "prefab_nested: the cooked node keeps the prefab it instantiates and the overrides");
				check(holder && holder->Nodes[1].Components.empty() && !holder->Nodes[0].Instance, "prefab_nested: the PrefabInstance component is not a component of the node");
				check(std::find(holderDependencies.begin(), holderDependencies.end(), ref->Guid) != holderDependencies.end(), "prefab_nested: the prefab it instantiates is a dependency");

				// a prefab that holds an instance of itself fails the import and keeps the previous cooked prefab
				const std::filesystem::path selfFile = dir / "self.wprefab";
				check(write_prefab_file(selfFile, { holderRoot }, "Self"), "prefab_nested: the file that will hold itself is written");
				const std::optional<AssetRef> selfRef = database.ensure_imported(selfFile);
				if (selfRef)
				{
					EntityRecord selfSlot = slot;
					selfSlot.Overrides.clear();
					selfSlot.Components[1] = ComponentRecord{ "PrefabInstance", { FieldRecord{ "Prefab", FieldValue(*selfRef) } } };
					check(write_prefab_file(selfFile, { holderRoot, selfSlot }, "Self"), "prefab_nested: the file holding itself is written");
					WI_ASSET_LOG(Info, "Smoke test: the next import error is expected");
					database.reimport(selfFile, true);
					const std::optional<PrefabData> kept = cooked_prefab(selfFile);
					check(kept && kept->Nodes.size() == 1, "prefab_nested: a prefab that holds an instance of itself is not imported");
				}
			}

			// an instance that names no prefab is refused with a reason; so is no entity at all
			EntityRecord nested;
			nested.Id = EntityGuid::generate();
			nested.Components.push_back(ComponentRecord{ "PrefabInstance", {} });
			std::string conversionError;
			check(!prefab_from_records({ nested }, "Nested", conversionError).has_value() && !conversionError.empty(), "prefab_file: an instance that names no prefab is refused with a reason");
			check(!prefab_from_records({}, "Empty", conversionError).has_value(), "prefab_file: no entities is refused");

			std::filesystem::remove_all(dir, error);
			database.scan();
		}

		// 9b. UI panel files (.wuipanel)
		{
			const std::filesystem::path dir = database.get_assets_root() / "zz_uipanel_smoke";
			std::filesystem::create_directories(dir, error);

			const unsigned char png[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x64, 0xf8, 0xcf, 0x50, 0x0f, 0x00, 0x03, 0x86, 0x01, 0x80, 0x5a, 0x34, 0x7d, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };
			{
				std::ofstream out(dir / "pixel.png", std::ios::binary);
				out.write(reinterpret_cast<const char*>(png), sizeof(png));
			}
			const std::optional<AssetRef> pixel = database.ensure_imported(dir / "pixel.png");
			check(pixel.has_value(), "ui_panel: the texture the panel refers to imports");

			// an Image with a real texture and a Text whose font is an asset that does not exist
			const AssetRef missingFont{ AssetGuid::generate(), 0 };
			UiPanelData panel;
			UiWidget image;
			image.Id = 1;
			image.Kind = WidgetKind::Image;
			image.Name = "Logo";
			image.Texture = pixel.value_or(AssetRef{});
			UiWidget label;
			label.Id = 2;
			label.Kind = WidgetKind::Text;
			label.Name = "Label";
			label.Text = "Hello";
			label.Font = missingFont;
			panel.Widgets = { image, label };
			panel.NextId = 3;

			const std::filesystem::path file = dir / "hud.wuipanel";
			{
				std::ofstream out(file, std::ios::binary | std::ios::trunc);
				out << ui_panel_to_json(panel).dump(2);
			}

			const auto cooked_panel = [&](const std::filesystem::path& source, std::vector<AssetGuid>* dependencies = nullptr, std::vector<std::byte>* bytes = nullptr) -> std::optional<UiPanelData>
			{
				std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(source));
				if (!meta)
				{
					return std::nullopt;
				}
				for (const CookedFile& cooked : read_library(database.get_library_root()))
				{
					std::span<const std::byte> payload;
					if (cooked.Guid == meta->Guid && cooked.Type == AssetType::UiPanel && parse_wasset_header(cooked.Bytes, payload))
					{
						if (dependencies != nullptr)
						{
							*dependencies = cooked.Dependencies;
						}
						if (bytes != nullptr)
						{
							*bytes = cooked.Bytes;
						}
						return deserialize<UiPanelData>(payload);
					}
				}
				return std::nullopt;
			};

			std::vector<AssetGuid> dependencies;
			std::vector<std::byte> libraryBytes;
			const std::optional<AssetRef> ref = database.ensure_imported(file);
			check(ref.has_value(), "ui_panel_imported: the .wuipanel imports");
			const std::optional<UiPanelData> cookedPanel = cooked_panel(file, &dependencies, &libraryBytes);
			check(cookedPanel && cookedPanel->Widgets.size() == 2 && cookedPanel->Widgets[0].Texture == pixel.value_or(AssetRef{}) && cookedPanel->Widgets[1].Text == "Hello",
				"ui_panel_imported: the cooked panel holds the widgets of the file");
			check(pixel && std::find(dependencies.begin(), dependencies.end(), pixel->Guid) != dependencies.end(), "ui_panel_dependency_tracked: a texture a widget uses is a dependency");
			check(std::find(dependencies.begin(), dependencies.end(), missingFont.Guid) == dependencies.end(), "ui_panel_dependency_tracked: a reference to an asset that does not exist is not");

			// it loads through the AssetManager, from the Library and from a pack
			if (ref)
			{
				const std::function<LoadState()> state = request(manager, AssetType::UiPanel, ref->Guid);
				manager.wait_idle();
				check(state() == LoadState::Ready, "ui_panel_loads: the cooked panel loads through the AssetManager");

				const std::filesystem::path packDirectory = dir / "pack";
				check(Packager::pack(database, packDirectory), "ui_panel_packed_loads: packing the Library with a panel in it");
				PakSource panelPak(packDirectory / "assets.wpak");
				const std::optional<std::vector<std::byte>> packed = panelPak.valid() ? panelPak.read(ref->Guid) : std::nullopt;
				check(packed && *packed == libraryBytes, "ui_panel_packed_loads: the pack holds the bytes of the Library");
				std::span<const std::byte> packedPayload;
				check(packed && parse_wasset_header(*packed, packedPayload) && deserialize<UiPanelData>(packedPayload).has_value(), "ui_panel_packed_loads: the packed panel decodes");
			}

			// a damaged file is an import error and leaves no panel
			const std::filesystem::path broken = dir / "broken.wuipanel";
			{
				std::ofstream out(broken, std::ios::binary | std::ios::trunc);
				out << "{ \"widgets\": [ { \"id\": 1 } ] }";
			}
			WI_ASSET_LOG(Info, "Smoke test: the next import error is expected");
			database.ensure_imported(broken);
			check(!cooked_panel(broken).has_value(), "ui_panel_invalid_fails_import: a widget without a kind cooks no panel");

			std::filesystem::remove_all(dir, error);
			database.scan();
		}

		// 9c. physics material files (.wphysmat)
		{
			const std::filesystem::path dir = database.get_assets_root() / "zz_physmat_smoke";
			std::filesystem::remove_all(dir, error);
			std::filesystem::create_directories(dir, error);

			const std::filesystem::path file = dir / "bouncy.wphysmat";
			const auto write_source = [&](const std::filesystem::path& path, const std::string& text)
			{
				std::ofstream out(path, std::ios::binary | std::ios::trunc);
				out << text;
			};
			write_source(file, "{ \"friction\": 0.25, \"restitution\": 0.75, \"density\": 2500, \"frictionCombine\": \"Min\", \"restitutionCombine\": \"Multiply\" }");

			const auto cooked_material = [&](const std::filesystem::path& source, std::vector<std::byte>* bytes = nullptr) -> std::optional<PhysicsMaterialData>
			{
				std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(source));
				if (!meta)
				{
					return std::nullopt;
				}
				for (const CookedFile& cooked : read_library(database.get_library_root()))
				{
					std::span<const std::byte> payload;
					if (cooked.Guid == meta->Guid && cooked.Type == AssetType::PhysicsMaterial && parse_wasset_header(cooked.Bytes, payload))
					{
						if (bytes != nullptr)
						{
							*bytes = cooked.Bytes;
						}
						return deserialize<PhysicsMaterialData>(payload);
					}
				}
				return std::nullopt;
			};

			PhysicsMaterialData expected;
			expected.Friction = 0.25f;
			expected.Restitution = 0.75f;
			expected.Density = 2500.0f;
			expected.FrictionCombine = CombineMode::Min;
			expected.RestitutionCombine = CombineMode::Multiply;

			std::vector<std::byte> libraryBytes;
			const std::optional<AssetRef> ref = database.ensure_imported(file);
			check(ref.has_value(), "physics_material_imported: the .wphysmat imports");
			const std::optional<PhysicsMaterialData> cooked = cooked_material(file, &libraryBytes);
			check(cooked && *cooked == expected, "physics_material_imported: the cooked material holds the values of the file");

			// it loads through the AssetManager, from the Library and from a pack
			if (ref)
			{
				Handle<PhysicsMaterialData> handle = manager.load<PhysicsMaterialData>(*ref);
				manager.wait_idle();
				check(handle.state() == LoadState::Ready && handle.get() != nullptr && *handle.get() == expected, "physics_material_loads: the cooked material loads through the AssetManager with equal values");

				const std::filesystem::path packDirectory = dir / "pack";
				check(Packager::pack(database, packDirectory), "physics_material_packed_loads: packing the Library with a material in it");
				PakSource pak(packDirectory / "assets.wpak");
				const std::optional<std::vector<std::byte>> packed = pak.valid() ? pak.read(ref->Guid) : std::nullopt;
				check(packed && *packed == libraryBytes, "physics_material_packed_loads: the pack holds the bytes of the Library");
				std::span<const std::byte> packedPayload;
				const std::optional<PhysicsMaterialData> fromPack = packed && parse_wasset_header(*packed, packedPayload) ? deserialize<PhysicsMaterialData>(packedPayload) : std::nullopt;
				check(fromPack && *fromPack == expected, "physics_material_packed_loads: the packed material decodes to equal values");
			}

			// the guid survives an edit and a re-import
			write_source(file, "{ \"friction\": 0.5, \"restitution\": 0.1 }");
			database.reimport(file);
			const std::optional<AssetRef> again = database.ensure_imported(file);
			check(ref && again && again->Guid == ref->Guid, "physics_material_guid_stable: a re-import keeps the guid");
			const std::optional<PhysicsMaterialData> edited = cooked_material(file);
			check(edited && edited->Friction == 0.5f && edited->Restitution == 0.1f && edited->Density == 1000.0f && edited->FrictionCombine == CombineMode::Average,
				"physics_material_defaults: keys that are missing take the defaults");

			// the binary payload has a version field and refuses anything else
			{
				std::vector<std::byte> payload = serialize(expected);
				check(payload.size() >= sizeof(u32), "physics_material_version: the payload starts with a version");
				std::vector<std::byte> wrongVersion = payload;
				wrongVersion[0] = std::byte{ 99 };
				check(!deserialize<PhysicsMaterialData>(wrongVersion).has_value(), "physics_material_version: an unknown version is refused");
				check(!deserialize<PhysicsMaterialData>(std::span<const std::byte>(payload.data(), payload.size() - 1)).has_value(), "physics_material_version: a truncated payload is refused");
			}

			// a damaged file is an import error and leaves no .wasset
			WI_ASSET_LOG(Info, "Smoke test: the next import errors are expected");
			const auto count_cooked_materials = [&]()
			{
				size_t count = 0;
				for (const CookedFile& cooked : read_library(database.get_library_root()))
				{
					count += cooked.Type == AssetType::PhysicsMaterial ? 1 : 0;
				}
				return count;
			};
			const size_t cookedBefore = count_cooked_materials();
			for (const auto& [name, text] : std::vector<std::pair<std::string, std::string>>{
				{ "broken_json", "{ \"friction\": " }, { "negative", "{ \"friction\": -1 }" }, { "bad_mode", "{ \"frictionCombine\": \"Sum\" }" },
				{ "wrong_type", "{ \"density\": \"heavy\" }" }, { "too_large", "{ \"density\": 1e39 }" } })
			{
				const std::filesystem::path broken = dir / (name + ".wphysmat");
				write_source(broken, text);
				database.ensure_imported(broken);
				check(!cooked_material(broken).has_value(), "physics_material_invalid_fails_import: " + name + " cooks no material");
			}
			check(count_cooked_materials() == cookedBefore, "physics_material_invalid_fails_import: no .wasset was written for any of them");

			// the Create menu makes a file that imports as the default material
			check(std::string(extension(CreateKind::PhysicsMaterial)) == ".wphysmat", "physics_material_template: the Create menu kind has the extension");
			const std::filesystem::path created = create_asset(CreateKind::PhysicsMaterial, dir, "Made");
			check(!created.empty() && database.ensure_imported(created).has_value(), "physics_material_template: the created file imports");
			const std::optional<PhysicsMaterialData> made = cooked_material(created);
			check(made && *made == default_physics_material(), "physics_material_template: the created file is the default material");

			std::filesystem::remove_all(dir, error);
			database.scan();
		}

		// 9b. the project's physics settings: physics.wsettings round-trips, goes into the pack and comes back from it
		{
			const std::filesystem::path dir = std::filesystem::temp_directory_path() / "wiwa_physics_settings_smoke";
			std::filesystem::remove_all(dir, error);
			std::filesystem::create_directories(dir, error);
			const std::filesystem::path file = dir / "physics.wsettings";

			PhysicsLayers layers;
			layers.Names[1] = "Player";
			layers.Names[2] = "Enemy";
			layers.set_collides(1, 2, false);
			layers.set_collides(0, 2, false);
			std::string saveError;
			check(save_physics_layers(file, layers, saveError), "physics_settings_save: the file is written");

			PhysicsLayers loaded;
			check(load_physics_layers(file, loaded, saveError) && loaded.Names == layers.Names && loaded.Matrix == layers.Matrix, "physics_settings_roundtrip: the names and the matrix come back");
			check(!loaded.collides(1, 2) && !loaded.collides(2, 1) && !loaded.collides(0, 2) && loaded.collides(0, 1) && loaded.collides(1, 1), "physics_settings_cell: a cleared cell stays cleared and the others stay set");

			PhysicsLayers missing;
			check(load_physics_layers(dir / "none.wsettings", missing, saveError) && missing.Names[0] == "Default" && missing.collides(3, 4), "physics_settings_missing: no file gives the default layers");
			PhysicsLayers refused;
			check(!physics_layers_from_text("{ \"matrix\": [1, 2, 3] }", refused, saveError) && !physics_layers_from_text("not json", refused, saveError) &&
				!physics_layers_from_text("{ \"layers\": [ 5 ] }", refused, saveError), "physics_settings_invalid: a matrix of the wrong size, text that is not JSON and a layer without a name are refused");

			// the pack carries them, and a game reads them back with the module's reader
			const std::filesystem::path packDirectory = dir / "pack";
			check(Packager::pack(database, packDirectory, nullptr, nullptr, file), "physics_settings_packed: packing with a settings file");
			PakSource pak(packDirectory / "assets.wpak");
			const std::optional<PhysicsLayers> fromPack = pak.valid() ? read_physics_layers(pak) : std::nullopt;
			check(fromPack && fromPack->Names == layers.Names && fromPack->Matrix == layers.Matrix, "physics_settings_packed: the pack holds the names and the matrix, a cleared cell included");

			const std::filesystem::path noFilePack = dir / "pack_without";
			check(Packager::pack(database, noFilePack, nullptr, nullptr, dir / "none.wsettings"), "physics_settings_packed: packing without a settings file");
			PakSource plain(noFilePack / "assets.wpak");
			check(plain.valid() && !read_physics_layers(plain).has_value(), "physics_settings_packed: a pack made without a file has no settings (the game uses the defaults)");

			std::filesystem::remove_all(dir, error);
		}

		// 10. assets made from templates (the Asset Browser's Create menu)
		{
			const std::filesystem::path dir = database.get_assets_root() / "zz_create_smoke";
			std::filesystem::create_directories(dir, error);

			check(std::string(extension(CreateKind::Script)) == ".lua" && std::string(extension(CreateKind::Folder)).empty() && std::string(extension(CreateKind::Scene)) == ".wscene" &&
				std::string(extension(CreateKind::SurfaceShader)) == ".wshader" && std::string(extension(CreateKind::HlslShader)) == ".hlsl" && std::string(extension(CreateKind::HlslInclude)) == ".hlsli" &&
				std::string(extension(CreateKind::Material)) == ".wmat" && std::string(extension(CreateKind::UiPanel)) == ".wuipanel", "create_asset: every kind has its extension");

			// names that cannot be used say why
			for (const char* bad : { "", "a/b", "a\\b", "a:b", "x?y", "CON", "con", "Com1", "nul", "bad.", "bad ", " ", "tab\tname" })
			{
				check(!validate_name(bad, dir, CreateKind::Script).empty(), std::string("create_asset: the name '") + bad + "' is refused");
			}
			check(validate_name("Good name", dir, CreateKind::Script).empty() && validate_name("Console", dir, CreateKind::Script).empty(), "create_asset: an ordinary name is accepted");
			check(!validate_name(std::string(200, 'a'), dir, CreateKind::Script).empty(), "create_asset: a name that is too long is refused");

			// Windows reserves the part of a name before its first dot
			check(!validate_name("CON.foo", dir, CreateKind::Script).empty() && !validate_name("nul.x", dir, CreateKind::Script).empty() && !validate_name("Com1.a.b", dir, CreateKind::Script).empty() && validate_name("Console.x", dir, CreateKind::Script).empty(),
				"create_asset: a device name followed by a dot is refused");

			// a name taken from an entity is made safe instead of refused
			check(sanitize_file_stem("Lamp") == "Lamp" && sanitize_file_stem("CON") == "_CON" && sanitize_file_stem("com1.x") == "_com1.x" && sanitize_file_stem("a/b:c") == "a_b_c" && sanitize_file_stem("") == "Prefab" &&
				sanitize_file_stem("bad. ") == "bad" && sanitize_file_stem(std::string(300, 'a')).size() == 100, "sanitize_file_stem: unusable characters, device names, trailing dots and length are fixed");

			// every kind is written, imports and loads
			struct Case { CreateKind Kind; const char* Name; AssetType Type; };
			const Case cases[] = {
				{ CreateKind::Script, "Mover", AssetType::Script }, { CreateKind::Scene, "Level", AssetType::Scene }, { CreateKind::SurfaceShader, "Glow", AssetType::SurfaceShader },
				{ CreateKind::HlslShader, "Fullscreen", AssetType::Shader }, { CreateKind::HlslInclude, "Helpers", AssetType::Shader }, { CreateKind::Material, "Metal", AssetType::Material },
				{ CreateKind::UiPanel, "Hud", AssetType::UiPanel } };
			for (const Case& testCase : cases)
			{
				const std::string label = std::string("create_asset: ") + testCase.Name;
				const std::filesystem::path made = create_asset(testCase.Kind, dir, testCase.Name);
				check(made == dir / (std::string(testCase.Name) + extension(testCase.Kind)) && std::filesystem::exists(made, error), label + " is written");
				check(!validate_name(testCase.Name, dir, testCase.Kind).empty(), label + " cannot be made twice");

				const std::optional<AssetRef> ref = database.ensure_imported(made);
				check(ref.has_value(), label + " imports");
				if (ref)
				{
					const std::function<LoadState()> state = request(manager, testCase.Type, ref->Guid);
					manager.wait_idle();
					check(state() == LoadState::Ready, label + " loads as a cooked asset");
				}
			}

			const std::filesystem::path folder = create_asset(CreateKind::Folder, dir, "Sub");
			check(folder == dir / "Sub" && std::filesystem::is_directory(folder, error) && !validate_name("Sub", dir, CreateKind::Folder).empty(), "create_asset: a folder is made, and its name is then taken");
			check(validate_name("Sub", dir, CreateKind::Script).empty(), "create_asset: a script may share the name of a folder");

			// the script template is a class with the three hooks, named after the file
			if (std::ifstream in(dir / "Mover.lua"); in)
			{
				const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				check(text.find("local Mover = Script:extend{") != std::string::npos && text.find("function Mover:on_update(dt)") != std::string::npos && text.find("return Mover") != std::string::npos, "create_asset: the script template is named after the file");
			}
			check(create_asset(CreateKind::Script, dir, "Mover").empty(), "create_asset: an existing file is not overwritten");

			std::filesystem::remove_all(dir, error);
			database.scan();
		}

		std::filesystem::remove_all(temp, error);

		if (check.Failures == 0)
		{
			WI_ASSET_LOG(Info, "Asset smoke test passed");
			return true;
		}

		WI_ASSET_LOG(Error, "Asset smoke test: {0} check(s) failed", check.Failures);
		return false;
	}
}
