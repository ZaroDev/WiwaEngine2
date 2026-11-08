#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <assets/asset_data.h>

#include "edit_history.h"

namespace wiwa
{
	// A key of a channel of the clip: the channel's index in AnimationClipData::Channels and the key's index in time order.
	struct KeyRef
	{
		size_t Channel = 0;
		size_t Key = 0;

		bool operator==(const KeyRef& other) const { return Channel == other.Channel && Key == other.Key; }
		bool operator<(const KeyRef& other) const { return Channel != other.Channel ? Channel < other.Channel : Key < other.Key; }
	};

	// Sorted by (channel, key), no duplicates.
	using KeySelection = std::vector<KeyRef>;
	void normalize_selection(KeySelection& selection);

	// Keys taken off a clip to put somewhere else: where they came from (joint and path) and when, relative to the first of them.
	struct KeyClipboard
	{
		struct Item
		{
			std::string Joint{};
			AnimationPath Path = AnimationPath::Translation;
			AnimationInterpolation Interpolation = AnimationInterpolation::Linear;
			float Time = 0.0f;
			glm::vec4 Value{ 0.0f };
		};
		std::vector<Item> Items{};
		bool empty() const { return Items.empty(); }
	};

	// The document the Clip editor edits: an AnimationClipData and its history. No ImGui. Every call that changes the clip is one undo step (or part of
	// the gesture going on); a call that is refused (it returns false / nullopt, and error() says why) changes nothing and makes no step, and neither does
	// a call that would change nothing. After every call: times are finite and sorted per channel and inside [0, Duration], rotations are unit
	// quaternions, no channel is empty, event names are valid, events are sorted. Rotations are edited as Euler angles (degrees) and stored as
	// quaternions that take the short way from their neighbour. Weights channels are never changed by a key operation (they have no keys to show).
	class ClipEditModel
	{
	public:
		static constexpr size_t MAX_NAME_LENGTH = 64;

		explicit ClipEditModel(AnimationClipData clip);

		const AnimationClipData& data() const { return m_Clip; }
		const std::string& error() const { return m_Error; }
		void clear_error() { m_Error.clear(); }

		std::optional<size_t> find_channel(const std::string& joint, AnimationPath path) const;

		// Keys. A move or scale returns the new indices of the same keys in `moved` (keys that land on an unselected key of their channel replace it).
		bool move_keys(const KeySelection& selection, float dt, KeySelection* moved = nullptr);
		bool scale_keys(const KeySelection& selection, float about, float factor, KeySelection* moved = nullptr);
		// value: Components numbers; for a rotation channel three Euler angles in degrees.
		bool set_key_value(KeyRef key, std::span<const float> value);
		// Interpolation is a property of a channel: every channel that holds a selected key changes.
		bool set_interpolation(const KeySelection& selection, AnimationInterpolation interpolation);
		std::optional<KeyRef> insert_key(size_t channel, float time);
		// Refused when a channel would be left without keys.
		bool delete_keys(const KeySelection& selection);

		KeyClipboard copy_keys(const KeySelection& selection) const;
		// Puts the keys at the playhead. When they all came from one joint and targetJoint is not empty they go to targetJoint's channels. A joint the
		// skeleton (when given) lacks is refused; a channel the clip lacks is made. Returns the pasted keys.
		KeySelection paste_keys(const KeyClipboard& clipboard, float playhead, const std::string& targetJoint, const SkeletonData* skeleton);

		// Writes a key at `time` on the joint's translation, rotation and scale channels (made when missing, with a key at 0 holding the rest pose).
		bool key_pose(const std::string& joint, float time, const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale, const SkeletonData* skeleton);

		// The Euler angles (degrees) of the keys of a rotation channel, each the closest equivalent to the one before.
		std::vector<glm::vec3> euler_values(size_t channel) const;

		// Events.
		std::optional<size_t> add_event(float time, const std::string& name);
		std::optional<size_t> move_event(size_t event, float time);
		bool rename_event(size_t event, const std::string& name);
		bool remove_event(size_t event);

		// Whole-clip operations (see the spec). Each is one undo step and refused, with error(), when its parameters do not fit.
		bool trim(float from, float to);
		bool scale_time(float factor);
		bool set_duration(float seconds);
		bool reverse();
		bool resample(float fps);
		// Returns how many keys were dropped (0: nothing changed).
		size_t reduce_keys(float translationTolerance, float rotationToleranceDegrees, float scaleTolerance);
		bool make_additive(float referenceTime);
		bool rename(const std::string& name);

		// A drag changes the clip many times: between begin_gesture and end_gesture the changes make one undo step.
		void begin_gesture();
		void end_gesture();

		void undo();
		void redo();
		bool can_undo() const { return m_History.can_undo(); }
		bool can_redo() const { return m_History.can_redo(); }
		bool dirty() const { return m_History.dirty(); }
		void mark_saved() { m_History.mark_saved(); }

		static bool valid_name(const std::string& name);

	private:
		bool refuse(const std::string& why);
		// The edited copy replaces the clip as one step (or part of the gesture).
		void commit(AnimationClipData edited);
		// A copy of the clip with the keys of the selection moved in time by map; the new selection.
		bool remap_keys(const KeySelection& selection, const std::function<float(float)>& map, KeySelection* moved);
		size_t make_channel(AnimationClipData& clip, const std::string& joint, AnimationPath path) const;
		bool valid_selection(const KeySelection& selection) const;

		AnimationClipData m_Clip;
		std::string m_Error{};
		EditHistory<AnimationClipData> m_History;
		bool m_InGesture = false;
		bool m_GestureChanged = false;
		AnimationClipData m_GestureBefore{};
	};
}
