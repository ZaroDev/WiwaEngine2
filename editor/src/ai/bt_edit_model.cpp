#include <editor_common.h>
#include "bt_edit_model.h"

#include <algorithm>

#include "../asset_pipeline/atomic_write.h"

namespace wiwa
{
	namespace
	{
		using Json = nlohmann::json;

		bool is_key_param(const BtNodeDesc& node, const std::string& name)
		{
			const BtNodeSpec* spec = bt_find_node_spec(node.Type);
			if (spec == nullptr)
			{
				return false;
			}
			return std::any_of(spec->Params.begin(), spec->Params.end(), [&](const BtParamSpec& p) { return p.Name == name && p.Kind == BtParamKind::Key; });
		}

		bool names_key(const BtNodeDesc& node, const std::string& key)
		{
			for (const auto& [name, value] : node.Params.items())
			{
				if (value.is_string() && is_key_param(node, name) && value.get<std::string>() == key)
				{
					return true;
				}
			}
			const auto target = node.Params.find("target");
			return node.Type == "MoveTo" && target != node.Params.end() && target->is_string() && target->get<std::string>() == "key:" + key;
		}

		Json default_params(const std::string& type)
		{
			if (type == "Wait") return { { "seconds", 1.0 } };
			if (type == "Cooldown") return { { "seconds", 1.0 } };
			if (type == "MoveTo") return { { "target", Json::array({ 0, 0, 0 }) } };
			if (type == "Log") return { { "message", "" } };
			if (type == "PlayAnimation") return { { "clip", "" } };
			return Json::object();
		}
	}

	BtEditModel::BtEditModel(BehaviorTreeDoc doc) : m_Doc(std::move(doc))
	{
		// ids that look like n<number> are counted so that new ones do not collide with them
		for (const BtNodeDesc& node : m_Doc.Nodes)
		{
			if (node.Id.size() > 1 && node.Id[0] == 'n' && std::all_of(node.Id.begin() + 1, node.Id.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
			{
				m_NextId = std::max(m_NextId, static_cast<u32>(std::stoul(node.Id.substr(1))) + 1);
			}
		}
	}

	bool BtEditModel::refuse(const std::string& why)
	{
		m_Error = why;
		return false;
	}

	void BtEditModel::commit(BehaviorTreeDoc before)
	{
		if (m_Gesture > 0)
		{
			m_GestureChanged = true;
			return;
		}
		m_History.push(std::move(before));
	}

	void BtEditModel::begin_gesture()
	{
		if (m_Gesture++ == 0)
		{
			m_GestureBefore = m_Doc;
			m_GestureChanged = false;
		}
	}

	void BtEditModel::end_gesture()
	{
		if (m_Gesture > 0 && --m_Gesture == 0 && m_GestureChanged)
		{
			m_History.push(std::move(m_GestureBefore));
			m_GestureChanged = false;
		}
	}

	bool BtEditModel::undo()
	{
		if (!m_History.can_undo() || m_Gesture > 0)
		{
			return false;
		}
		m_Doc = m_History.undo(m_Doc);
		return true;
	}

	bool BtEditModel::redo()
	{
		if (!m_History.can_redo() || m_Gesture > 0)
		{
			return false;
		}
		m_Doc = m_History.redo(m_Doc);
		return true;
	}

	BtNodeDesc* BtEditModel::find(const std::string& id)
	{
		for (BtNodeDesc& node : m_Doc.Nodes)
		{
			if (node.Id == id)
			{
				return &node;
			}
		}
		return nullptr;
	}

	BtNodeDesc* BtEditModel::parent_of(const std::string& id)
	{
		for (BtNodeDesc& node : m_Doc.Nodes)
		{
			if (std::find(node.Children.begin(), node.Children.end(), id) != node.Children.end())
			{
				return &node;
			}
		}
		return nullptr;
	}

	bool BtEditModel::in_subtree(const std::string& root, const std::string& id) const
	{
		if (root == id)
		{
			return true;
		}
		const BtNodeDesc* node = m_Doc.find(root);
		if (node == nullptr)
		{
			return false;
		}
		return std::any_of(node->Children.begin(), node->Children.end(), [&](const std::string& child) { return in_subtree(child, id); });
	}

	std::string BtEditModel::new_id()
	{
		std::string id;
		do
		{
			id = "n" + std::to_string(m_NextId++);
		} while (m_Doc.find(id) != nullptr);
		return id;
	}

	std::string BtEditModel::add_node(const std::string& type, const std::string& parent, size_t index)
	{
		const BtNodeSpec* spec = bt_find_node_spec(type);
		if (spec == nullptr)
		{
			refuse("unknown node type \"" + type + "\"");
			return {};
		}

		const BehaviorTreeDoc before = m_Doc;
		BtNodeDesc node;
		node.Type = type;
		node.Params = default_params(type);

		if (parent.empty())
		{
			if (!m_Doc.Nodes.empty())
			{
				refuse("the tree has a root already: give the node a parent");
				return {};
			}
			node.Id = new_id();
			m_Doc.Root = node.Id;
			m_Doc.Nodes.push_back(node);
			commit(before);
			m_Error.clear();
			return node.Id;
		}

		BtNodeDesc* owner = find(parent);
		if (owner == nullptr)
		{
			refuse("there is no node \"" + parent + "\"");
			return {};
		}
		const BtNodeSpec* ownerSpec = bt_find_node_spec(owner->Type);
		if (ownerSpec == nullptr || ownerSpec->Leaf())
		{
			refuse(owner->Type + " takes no children");
			return {};
		}
		if (ownerSpec->MaxChildren >= 0 && static_cast<int>(owner->Children.size()) >= ownerSpec->MaxChildren)
		{
			refuse(owner->Type + " takes " + std::to_string(ownerSpec->MaxChildren) + " child at most");
			return {};
		}

		node.Id = new_id();
		owner = find(parent); // new_id does not move nodes, but be safe about the pointer
		owner->Children.insert(owner->Children.begin() + static_cast<std::ptrdiff_t>(std::min(index, owner->Children.size())), node.Id);
		m_Doc.Nodes.push_back(node);
		commit(before);
		m_Error.clear();
		return node.Id;
	}

	bool BtEditModel::remove_node(const std::string& id)
	{
		if (find(id) == nullptr)
		{
			return refuse("there is no node \"" + id + "\"");
		}
		if (id == m_Doc.Root)
		{
			return refuse("the root cannot be removed");
		}

		const BehaviorTreeDoc before = m_Doc;
		std::vector<std::string> doomed;
		std::vector<std::string> stack{ id };
		while (!stack.empty())
		{
			const std::string current = stack.back();
			stack.pop_back();
			doomed.push_back(current);
			if (const BtNodeDesc* node = m_Doc.find(current))
			{
				stack.insert(stack.end(), node->Children.begin(), node->Children.end());
			}
		}
		if (BtNodeDesc* owner = parent_of(id))
		{
			owner->Children.erase(std::find(owner->Children.begin(), owner->Children.end(), id));
		}
		m_Doc.Nodes.erase(std::remove_if(m_Doc.Nodes.begin(), m_Doc.Nodes.end(), [&](const BtNodeDesc& n) { return std::find(doomed.begin(), doomed.end(), n.Id) != doomed.end(); }), m_Doc.Nodes.end());
		commit(before);
		m_Error.clear();
		return true;
	}

	bool BtEditModel::move_node(const std::string& id, const std::string& newParent, size_t index)
	{
		BtNodeDesc* node = find(id);
		BtNodeDesc* target = find(newParent);
		if (node == nullptr || target == nullptr)
		{
			return refuse("there is no such node");
		}
		if (id == m_Doc.Root)
		{
			return refuse("the root cannot be moved");
		}
		if (in_subtree(id, newParent))
		{
			return refuse("a node cannot be moved into itself");
		}
		const BtNodeSpec* spec = bt_find_node_spec(target->Type);
		if (spec == nullptr || spec->Leaf())
		{
			return refuse(target->Type + " takes no children");
		}

		BtNodeDesc* oldParent = parent_of(id);
		const bool same = oldParent == target;
		const size_t oldIndex = oldParent != nullptr ? static_cast<size_t>(std::find(oldParent->Children.begin(), oldParent->Children.end(), id) - oldParent->Children.begin()) : 0;
		const size_t used = target->Children.size() - (same ? 1 : 0);
		if (spec->MaxChildren >= 0 && static_cast<int>(used) >= spec->MaxChildren)
		{
			return refuse(target->Type + " takes " + std::to_string(spec->MaxChildren) + " child at most");
		}

		const BehaviorTreeDoc before = m_Doc;
		if (oldParent != nullptr)
		{
			oldParent->Children.erase(oldParent->Children.begin() + static_cast<std::ptrdiff_t>(oldIndex));
		}
		target = find(newParent);
		const size_t at = std::min(index, target->Children.size());
		if (same && at == oldIndex)
		{
			m_Doc = before;
			m_Error.clear();
			return true; // nothing changes
		}
		target->Children.insert(target->Children.begin() + static_cast<std::ptrdiff_t>(at), id);
		commit(before);
		m_Error.clear();
		return true;
	}

	bool BtEditModel::rename_node(const std::string& id, const std::string& name)
	{
		BtNodeDesc* node = find(id);
		if (node == nullptr)
		{
			return refuse("there is no node \"" + id + "\"");
		}
		if (node->Name == name)
		{
			return true;
		}
		const BehaviorTreeDoc before = m_Doc;
		node->Name = name;
		commit(before);
		m_Error.clear();
		return true;
	}

	bool BtEditModel::set_param(const std::string& id, const std::string& key, const Json& value)
	{
		BtNodeDesc* node = find(id);
		if (node == nullptr)
		{
			return refuse("there is no node \"" + id + "\"");
		}
		if (key.empty())
		{
			return refuse("a parameter needs a name");
		}
		const auto existing = node->Params.find(key);
		if (value.is_null() ? existing == node->Params.end() : (existing != node->Params.end() && *existing == value))
		{
			return true; // nothing changes
		}
		const BehaviorTreeDoc before = m_Doc;
		if (value.is_null())
		{
			node->Params.erase(key);
		}
		else
		{
			node->Params[key] = value;
		}
		commit(before);
		m_Error.clear();
		return true;
	}

	std::vector<std::string> BtEditModel::users_of_key(const std::string& key) const
	{
		std::vector<std::string> users;
		for (const BtNodeDesc& node : m_Doc.Nodes)
		{
			if (names_key(node, key))
			{
				users.push_back(node.Id);
			}
		}
		return users;
	}

	bool BtEditModel::add_key(const std::string& key, BtValueType type, const Json& fallback)
	{
		if (key.empty())
		{
			return refuse("a blackboard key needs a name");
		}
		if (m_Doc.find_key(key) != nullptr)
		{
			return refuse("the key \"" + key + "\" exists already");
		}
		if (!fallback.is_null() && !bt_json_fits(type, fallback))
		{
			return refuse("the default does not fit " + std::string(bt_value_type_name(type)));
		}
		const BehaviorTreeDoc before = m_Doc;
		BtKeyDesc desc;
		desc.Key = key;
		desc.Type = type;
		desc.Default = fallback;
		m_Doc.Blackboard.push_back(std::move(desc));
		commit(before);
		m_Error.clear();
		return true;
	}

	bool BtEditModel::rename_key(const std::string& key, const std::string& name)
	{
		const auto it = std::find_if(m_Doc.Blackboard.begin(), m_Doc.Blackboard.end(), [&](const BtKeyDesc& k) { return k.Key == key; });
		if (it == m_Doc.Blackboard.end())
		{
			return refuse("there is no key \"" + key + "\"");
		}
		if (name.empty() || (name != key && m_Doc.find_key(name) != nullptr))
		{
			return refuse(name.empty() ? "a blackboard key needs a name" : "the key \"" + name + "\" exists already");
		}
		if (name == key)
		{
			return true;
		}
		const BehaviorTreeDoc before = m_Doc;
		it->Key = name;
		for (BtNodeDesc& node : m_Doc.Nodes)
		{
			for (auto& [param, value] : node.Params.items())
			{
				if (value.is_string() && is_key_param(node, param) && value.get<std::string>() == key)
				{
					value = name;
				}
			}
			const auto target = node.Params.find("target");
			if (node.Type == "MoveTo" && target != node.Params.end() && target->is_string() && target->get<std::string>() == "key:" + key)
			{
				*target = "key:" + name;
			}
		}
		commit(before);
		m_Error.clear();
		return true;
	}

	bool BtEditModel::retype_key(const std::string& key, BtValueType type, bool force)
	{
		const auto it = std::find_if(m_Doc.Blackboard.begin(), m_Doc.Blackboard.end(), [&](const BtKeyDesc& k) { return k.Key == key; });
		if (it == m_Doc.Blackboard.end())
		{
			return refuse("there is no key \"" + key + "\"");
		}
		if (it->Type == type)
		{
			return true;
		}
		const std::vector<std::string> users = users_of_key(key);
		if (!users.empty() && !force)
		{
			std::string list;
			for (const std::string& user : users) { list += (list.empty() ? "" : ", ") + user; }
			return refuse("the key \"" + key + "\" is used by " + list);
		}
		const BehaviorTreeDoc before = m_Doc;
		it->Type = type;
		it->Default = Json();
		for (BtNodeDesc& node : m_Doc.Nodes)
		{
			if (std::find(users.begin(), users.end(), node.Id) != users.end())
			{
				for (auto param = node.Params.begin(); param != node.Params.end();)
				{
					const bool names = param.value().is_string() && ((is_key_param(node, param.key()) && param.value().get<std::string>() == key) || (node.Type == "MoveTo" && param.key() == "target" && param.value().get<std::string>() == "key:" + key));
					param = names ? node.Params.erase(param) : std::next(param);
				}
				node.Params.erase("value");
			}
		}
		commit(before);
		m_Error.clear();
		return true;
	}

	bool BtEditModel::set_key_default(const std::string& key, const Json& fallback)
	{
		const auto it = std::find_if(m_Doc.Blackboard.begin(), m_Doc.Blackboard.end(), [&](const BtKeyDesc& k) { return k.Key == key; });
		if (it == m_Doc.Blackboard.end())
		{
			return refuse("there is no key \"" + key + "\"");
		}
		if (!fallback.is_null() && !bt_json_fits(it->Type, fallback))
		{
			return refuse("the default does not fit " + std::string(bt_value_type_name(it->Type)));
		}
		if (it->Default == fallback)
		{
			return true;
		}
		const BehaviorTreeDoc before = m_Doc;
		it->Default = fallback;
		commit(before);
		m_Error.clear();
		return true;
	}

	bool BtEditModel::remove_key(const std::string& key, bool force)
	{
		const auto it = std::find_if(m_Doc.Blackboard.begin(), m_Doc.Blackboard.end(), [&](const BtKeyDesc& k) { return k.Key == key; });
		if (it == m_Doc.Blackboard.end())
		{
			return refuse("there is no key \"" + key + "\"");
		}
		const std::vector<std::string> users = users_of_key(key);
		if (!users.empty() && !force)
		{
			std::string list;
			for (const std::string& user : users) { list += (list.empty() ? "" : ", ") + user; }
			return refuse("the key \"" + key + "\" is used by " + list);
		}
		const BehaviorTreeDoc before = m_Doc;
		m_Doc.Blackboard.erase(it);
		for (BtNodeDesc& node : m_Doc.Nodes)
		{
			if (std::find(users.begin(), users.end(), node.Id) != users.end())
			{
				for (auto param = node.Params.begin(); param != node.Params.end();)
				{
					const bool names = param.value().is_string() && ((is_key_param(node, param.key()) && param.value().get<std::string>() == key) || (node.Type == "MoveTo" && param.key() == "target" && param.value().get<std::string>() == "key:" + key));
					param = names ? node.Params.erase(param) : std::next(param);
				}
				node.Params.erase("value");
			}
		}
		commit(before);
		m_Error.clear();
		return true;
	}

	bool BtEditModel::save(const std::filesystem::path& path)
	{
		for (const BtProblem& problem : validate_bt(m_Doc))
		{
			if (problem.Error)
			{
				return refuse((problem.NodeId.empty() ? std::string() : "node " + problem.NodeId + ": ") + problem.Message);
			}
		}
		const std::string text = write_bt_file(m_Doc);
		std::string error;
		if (!write_file_atomic(path, text.data(), text.size(), error))
		{
			return refuse(error);
		}
		m_History.mark_saved();
		m_Error.clear();
		return true;
	}
}
