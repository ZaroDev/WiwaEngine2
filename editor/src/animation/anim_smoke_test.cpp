#include <editor_common.h>
#include "anim_smoke_test.h"

#include <animation/animation_log.h>
#include <assets/wasset_io.h>

#include "../asset_pipeline/importers/model_import.h"
#include "../asset_pipeline/importers/model_skeleton.h"

namespace wiwa
{
	namespace
	{
		struct Check
		{
			u32 Failures = 0;

			void operator()(bool condition, const std::string& what)
			{
				if (!condition)
				{
					Failures++;
					WI_ANIM_LOG(Error, "Animation smoke test FAILED: {0}", what);
				}
			}
		};

		bool near_equal(float a, float b)
		{
			return std::abs(a - b) < 1e-4f;
		}

		PrefabNode make_node(const char* name, i32 parent, glm::vec3 translation, glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
		{
			PrefabNode node;
			node.Name = name;
			node.Parent = parent;
			node.Translation = translation;
			node.Rotation = rotation;
			return node;
		}

		// A scene root (not a joint), then hip, mid and tip, with the joints listed child first: tip, mid, hip.
		ImportedModel make_model()
		{
			ImportedModel model;
			model.Name = "synthetic";
			model.Nodes = {
				make_node("root", -1, glm::vec3(0.0f, 1.0f, 0.0f)),
				make_node("hip", 0, glm::vec3(0.0f, 2.0f, 0.0f)),
				make_node("mid", 1, glm::vec3(1.0f, 0.0f, 0.0f), glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f))),
				make_node("tip", 2, glm::vec3(0.0f, 0.0f, 3.0f)),
				make_node("body", 0, glm::vec3(0.0f)),
			};
			model.NodeMesh = { -1, -1, -1, -1, 0 };
			model.NodeSkin = { -1, -1, -1, -1, 0 };

			SkeletonData skeleton;
			skeleton.Name = "skin";
			skeleton.Joints = { 3, 2, 1 };
			skeleton.InverseBind = { glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f) };
			model.Skeletons.push_back(skeleton);

			ImportedMesh mesh;
			mesh.Data.Vertices.resize(2);
			mesh.Data.SkinVertices.resize(2);
			mesh.Data.SkinVertices[0].Joints[0] = 0; // the tip
			mesh.Data.SkinVertices[0].Weights[0] = 1.0f;
			mesh.Data.SkinVertices[1].Joints[0] = 2; // the hip
			mesh.Data.SkinVertices[1].Weights[0] = 1.0f;
			model.Meshes.push_back(mesh);

			AnimationClipData clip;
			clip.Name = "Walk";
			clip.Duration = 1.0f;
			model.Animations.push_back(clip);
			return model;
		}

		void import_round_trip(Check& check)
		{
			ImportedModel model = make_model();
			finalize_model_skeletons(model);
			merge_animation_events(model, nlohmann::json::parse(R"({"animationEvents":[
				{"clip":"Walk","time":0.75,"name":"late"},
				{"clip":"Walk","time":0.25,"name":"early"},
				{"clip":"Nope","time":0.5,"name":"lost"}]})"));

			const SkeletonData& skeleton = model.Skeletons[0];
			check(skeleton.Joints == std::vector<u32>({ 1, 2, 3 }), "joints are ordered parent first");
			check(skeleton.Parents == std::vector<i32>({ -1, 0, 1 }), "parent indices");
			check(skeleton.JointNames.size() == 3 && skeleton.JointNames[0] == "hip" && skeleton.JointNames[2] == "tip", "joint names");
			check(skeleton.RestTranslation.size() == 3 && near_equal(skeleton.RestTranslation[0].y, 3.0f), "a root joint's rest pose includes the nodes above it");
			check(skeleton.RestTranslation[2].z == 3.0f, "a child joint's rest pose is local to its parent");
			check(near_equal(skeleton.RestRotation[1].z, glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)).z), "rest rotation");
			check(model.Meshes[0].Data.SkinVertices[0].Joints[0] == 2, "a vertex weighted to the tip still names the tip");
			check(model.Meshes[0].Data.SkinVertices[1].Joints[0] == 0, "a vertex weighted to the hip still names the hip");

			const std::vector<AnimationEvent>& events = model.Animations[0].Events;
			check(events.size() == 2 && events[0].Name == "early" && events[1].Name == "late", "events are merged and sorted by time");
			check(!model.Warnings.empty(), "an event for an unknown clip is warned about");

			const std::optional<SkeletonData> loadedSkeleton = deserialize<SkeletonData>(serialize(skeleton));
			check(loadedSkeleton && loadedSkeleton->Parents == skeleton.Parents && loadedSkeleton->JointNames == skeleton.JointNames
				&& loadedSkeleton->RestTranslation.size() == 3, "skeleton round trip");
			const std::optional<AnimationClipData> loadedClip = deserialize<AnimationClipData>(serialize(model.Animations[0]));
			check(loadedClip && loadedClip->Events.size() == 2 && loadedClip->Events[1].Time == 0.75f, "clip events round trip");

			std::vector<std::byte> truncated = serialize(skeleton);
			truncated.resize(truncated.size() / 2);
			check(!deserialize<SkeletonData>(truncated).has_value(), "a truncated skeleton is refused");

			ImportedModel big;
			big.Name = "big";
			big.Nodes.push_back(make_node("n0", -1, glm::vec3(0.0f)));
			SkeletonData bigSkeleton;
			for (u32 i = 0; i < 300; i++)
			{
				if (i > 0)
				{
					big.Nodes.push_back(make_node("n", static_cast<i32>(i) - 1, glm::vec3(0.0f)));
				}
				bigSkeleton.Joints.push_back(i);
				bigSkeleton.InverseBind.push_back(glm::mat4(1.0f));
			}
			big.NodeMesh.assign(big.Nodes.size(), -1);
			big.NodeSkin.assign(big.Nodes.size(), -1);
			big.Skeletons.push_back(bigSkeleton);
			finalize_model_skeletons(big);
			check(big.Warnings.size() == 1 && big.Warnings[0].find("256") != std::string::npos, "above 256 joints gives one warning");
		}
	}

	bool run_anim_smoke_test(AssetPipelineModule& pipeline)
	{
		Check check;
		import_round_trip(check);

		if (check.Failures == 0)
		{
			WI_ANIM_LOG(Info, "Animation smoke test passed");
		}
		else
		{
			WI_ANIM_LOG(Error, "Animation smoke test FAILED: {0} check(s)", check.Failures);
		}
		return check.Failures == 0;
	}
}
