#include <editor_common.h>
#include "asset_templates.h"

#include <fstream>

#include <scene/scene_data.h>
#include <ui/ui_panel_io.h>

#include "../scene/scene_json.h"
#include "material_file.h"
#include "physics_material_file.h"
#include <input/input_map_io.h>

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// The map a new .winput starts with: Jump on Space and the pad's A, Move on WASD and the left stick.
		InputMapData default_input_map()
		{
			Binding space;
			space.Parts[0] = control(Key::Space);
			Binding padA;
			padA.Parts[0] = control(PadButton::A);

			Binding wasd;
			wasd.BindingKind = Binding::Kind::Composite2D;
			wasd.Parts[0] = control(Key::W);
			wasd.Parts[1] = control(Key::S);
			wasd.Parts[2] = control(Key::A);
			wasd.Parts[3] = control(Key::D);
			Binding stick;
			stick.BindingKind = Binding::Kind::Pair2D;
			stick.Parts[0] = control(PadAxis::LeftX);
			stick.Parts[1] = control(PadAxis::LeftY);
			stick.Deadzone = 0.2f;

			ActionMap gameplay;
			gameplay.Name = "Gameplay";
			gameplay.Actions.push_back({ "Jump", ActionType::Button, { space, padA } });
			gameplay.Actions.push_back({ "Move", ActionType::Axis2D, { wasd, stick } });

			InputMapData data;
			data.Maps.push_back(std::move(gameplay));
			return data;
		}

		constexpr size_t MAX_NAME_LENGTH = 100;

		// Windows treats everything before the first dot as the device name: "CON.txt" is the console too.
		bool is_device_name(std::string name)
		{
			name = name.substr(0, name.find('.'));
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			if (name == "CON" || name == "PRN" || name == "AUX" || name == "NUL")
			{
				return true;
			}
			return name.size() == 4 && (name.rfind("COM", 0) == 0 || name.rfind("LPT", 0) == 0) && name[3] >= '1' && name[3] <= '9';
		}

		// A Lua identifier or an include guard from a file name: letters, digits and underscores, not starting with a digit.
		std::string identifier(const std::string& name, bool upper)
		{
			std::string result;
			for (const char character : name)
			{
				const unsigned char c = static_cast<unsigned char>(character);
				result += std::isalnum(c) ? static_cast<char>(upper ? std::toupper(c) : c) : '_';
			}
			if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front())))
			{
				result.insert(result.begin(), '_');
			}
			return result;
		}

		std::string script_template(const std::string& name)
		{
			const std::string cls = identifier(name, false);
			return "local " + cls + " = Script:extend{\n"
				"  properties = {},\n"
				"}\n"
				"\n"
				"function " + cls + ":on_init() end\n"
				"\n"
				"function " + cls + ":on_update(dt) end\n"
				"\n"
				"function " + cls + ":on_destroy() end\n"
				"\n"
				"return " + cls + "\n";
		}

		std::string surface_shader_template()
		{
			return "struct MaterialParams\n"
				"{\n"
				"    float4 Tint; // color default(1,1,1,1)\n"
				"};\n"
				"\n"
				"void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o)\n"
				"{\n"
				"    o.Albedo *= p.Tint.rgb;\n"
				"}\n";
		}

		std::string hlsl_shader_template()
		{
			return "struct VertexOutput\n"
				"{\n"
				"    float4 Position : SV_Position;\n"
				"    float2 Uv : TEXCOORD0;\n"
				"};\n"
				"\n"
				"VertexOutput VSMain(uint id : SV_VertexID)\n"
				"{\n"
				"    float2 uv = float2((id << 1) & 2, id & 2);\n"
				"    VertexOutput output;\n"
				"    output.Position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);\n"
				"    output.Uv = uv;\n"
				"    return output;\n"
				"}\n"
				"\n"
				"float4 PSMain(VertexOutput input) : SV_Target0\n"
				"{\n"
				"    return float4(input.Uv, 0.0, 1.0);\n"
				"}\n";
		}

		std::string hlsl_include_template(const std::string& name)
		{
			const std::string guard = identifier(name, true) + "_HLSLI";
			return "#ifndef " + guard + "\n#define " + guard + "\n\n#endif\n";
		}

		// A panel with one background widget that fills the screen, to put the rest in.
		std::string ui_panel_template()
		{
			UiPanelData panel;
			UiWidget root;
			root.Id = 1;
			root.Kind = WidgetKind::Panel;
			root.Name = "Root";
			root.AnchorMin = { 0.0f, 0.0f };
			root.AnchorMax = { 1.0f, 1.0f };
			root.OffsetMin = { 0.0f, 0.0f };
			root.OffsetMax = { 0.0f, 0.0f };
			root.Color = { 0.0f, 0.0f, 0.0f, 0.0f };
			panel.Widgets.push_back(root);
			panel.NextId = 2;
			return ui_panel_to_json(panel).dump(2);
		}

		bool write_text(const std::filesystem::path& file, const std::string& text)
		{
			std::ofstream out(file, std::ios::binary | std::ios::trunc);
			out << text;
			return static_cast<bool>(out);
		}
	}

	const char* display_name(CreateKind kind)
	{
		switch (kind)
		{
		case CreateKind::Folder: return "Folder";
		case CreateKind::Material: return "Material";
		case CreateKind::Script: return "Lua script";
		case CreateKind::Scene: return "Scene";
		case CreateKind::SurfaceShader: return "Surface shader";
		case CreateKind::HlslShader: return "HLSL shader";
		case CreateKind::HlslInclude: return "HLSL include";
		case CreateKind::UiPanel: return "UI panel";
		case CreateKind::PhysicsMaterial: return "Physics Material";
		case CreateKind::InputMap: return "Input Map";
		}
		return "";
	}

	const char* default_name(CreateKind kind)
	{
		switch (kind)
		{
		case CreateKind::Folder: return "New folder";
		case CreateKind::Material: return "New material";
		case CreateKind::Script: return "NewScript";
		case CreateKind::Scene: return "New scene";
		case CreateKind::SurfaceShader: return "NewSurface";
		case CreateKind::HlslShader: return "NewShader";
		case CreateKind::HlslInclude: return "NewInclude";
		case CreateKind::UiPanel: return "NewPanel";
		case CreateKind::PhysicsMaterial: return "New physics material";
		case CreateKind::InputMap: return "New input map";
		}
		return "";
	}

	const char* extension(CreateKind kind)
	{
		switch (kind)
		{
		case CreateKind::Folder: return "";
		case CreateKind::Material: return ".wmat";
		case CreateKind::Script: return ".lua";
		case CreateKind::Scene: return ".wscene";
		case CreateKind::SurfaceShader: return ".wshader";
		case CreateKind::HlslShader: return ".hlsl";
		case CreateKind::HlslInclude: return ".hlsli";
		case CreateKind::UiPanel: return ".wuipanel";
		case CreateKind::PhysicsMaterial: return ".wphysmat";
		case CreateKind::InputMap: return ".winput";
		}
		return "";
	}

	std::string sanitize_file_stem(const std::string& name)
	{
		std::string stem;
		for (const char character : name)
		{
			stem += (std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos || static_cast<unsigned char>(character) < 32) ? '_' : character;
		}
		stem = stem.substr(0, MAX_NAME_LENGTH);
		while (!stem.empty() && (stem.back() == '.' || stem.back() == ' '))
		{
			stem.pop_back();
		}
		while (!stem.empty() && stem.front() == ' ')
		{
			stem.erase(stem.begin());
		}
		if (stem.empty())
		{
			return "Prefab";
		}
		return is_device_name(stem) ? "_" + stem : stem;
	}

	std::string validate_name(const std::string& name, const std::filesystem::path& directory, CreateKind kind)
	{
		if (name.empty())
		{
			return "Type a name";
		}
		if (name.size() > MAX_NAME_LENGTH)
		{
			return "The name is too long";
		}
		for (const char character : name)
		{
			if (std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos || static_cast<unsigned char>(character) < 32)
			{
				return "A name cannot contain < > : \" / \\ | ? * or control characters";
			}
		}
		if (name.back() == '.' || name.back() == ' ' || name.front() == ' ')
		{
			return "A name cannot start with a space or end with a space or a dot";
		}
		if (is_device_name(name))
		{
			return "That name is reserved by Windows";
		}

		std::error_code error;
		if (std::filesystem::exists(directory / (name + extension(kind)), error))
		{
			return std::string("There is already a ") + (kind == CreateKind::Folder ? "folder" : "file") + " called " + name + extension(kind);
		}
		return {};
	}

	std::filesystem::path create_asset(CreateKind kind, const std::filesystem::path& directory, const std::string& name)
	{
		if (!validate_name(name, directory, kind).empty())
		{
			return {};
		}

		std::error_code error;
		const std::filesystem::path path = directory / (name + extension(kind));
		bool written = false;
		switch (kind)
		{
		case CreateKind::Folder:
			written = std::filesystem::create_directory(path, error);
			break;
		case CreateKind::Material:
			return create_material_file(directory, name);
		case CreateKind::Script:
			written = write_text(path, script_template(name));
			break;
		case CreateKind::Scene:
		{
			SceneData scene;
			scene.Name = name;
			written = write_text(path, scene_to_json(scene).dump(2));
			break;
		}
		case CreateKind::SurfaceShader:
			written = write_text(path, surface_shader_template());
			break;
		case CreateKind::HlslShader:
			written = write_text(path, hlsl_shader_template());
			break;
		case CreateKind::HlslInclude:
			written = write_text(path, hlsl_include_template(name));
			break;
		case CreateKind::UiPanel:
			written = write_text(path, ui_panel_template());
			break;
		case CreateKind::PhysicsMaterial:
			written = write_text(path, physics_material_to_json(default_physics_material()).dump(2));
			break;
		case CreateKind::InputMap:
			written = write_text(path, input_map_to_text(default_input_map()));
			break;
		}

		if (!written)
		{
			WI_ASSET_LOG(Error, "Could not create {0}", path.string());
			return {};
		}
		return path;
	}
}
