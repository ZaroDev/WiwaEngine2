// wiwa_launcher: the project list (launcher.md). A GLFW window with ImGui on its stock D3D11 backend; nothing of the engine's renderer.
#include "launcher_ui.h"

#include <core/log.h>
#include <core/project_dir.h>
#include <core/project_registry.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_glfw.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <d3d11.h>
#include <windows.h>

namespace
{
	struct Gpu
	{
		ID3D11Device* Device = nullptr;
		ID3D11DeviceContext* Context = nullptr;
		IDXGISwapChain* SwapChain = nullptr;
		ID3D11RenderTargetView* Target = nullptr;

		bool create(HWND window)
		{
			DXGI_SWAP_CHAIN_DESC desc{};
			desc.BufferCount = 2;
			desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc.OutputWindow = window;
			desc.SampleDesc.Count = 1;
			desc.Windowed = TRUE;
			desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
			HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &desc, &SwapChain, &Device, nullptr, &Context);
			if (FAILED(result)) // no usable GPU (a remote session, say): the software rasterizer draws a list just as well
			{
				result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &desc, &SwapChain, &Device, nullptr, &Context);
			}
			if (FAILED(result))
			{
				return false;
			}
			create_target();
			return true;
		}

		void create_target()
		{
			ID3D11Texture2D* back = nullptr;
			SwapChain->GetBuffer(0, IID_PPV_ARGS(&back));
			if (back)
			{
				Device->CreateRenderTargetView(back, nullptr, &Target);
				back->Release();
			}
		}

		void resize(int width, int height)
		{
			if (Target)
			{
				Target->Release();
				Target = nullptr;
			}
			SwapChain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
			create_target();
		}

		void destroy()
		{
			if (Target) Target->Release();
			if (SwapChain) SwapChain->Release();
			if (Context) Context->Release();
			if (Device) Device->Release();
		}
	};

	std::filesystem::path executable_folder()
	{
		wchar_t buffer[4096];
		const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
		return length > 0 && length < std::size(buffer) ? std::filesystem::path(buffer).parent_path() : std::filesystem::current_path();
	}

	void style()
	{
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowPadding = ImVec2(16.0f, 14.0f);
		style.FramePadding = ImVec2(10.0f, 6.0f);
		style.ItemSpacing = ImVec2(10.0f, 8.0f);
		style.FrameRounding = 4.0f;
		style.PopupRounding = 4.0f;
		style.WindowRounding = 0.0f;
	}

	void font(const std::filesystem::path& executableFolder)
	{
		const std::filesystem::path engine = wiwa::find_engine_assets(executableFolder);
		const std::filesystem::path file = engine / "fonts" / "CascadiaCode.ttf";
		std::error_code error;
		if (!engine.empty() && std::filesystem::is_regular_file(file, error))
		{
			const std::u8string name = file.u8string();
			ImGui::GetIO().Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(name.c_str()), 17.0f);
		}
	}

	int run()
	{
		using namespace wiwa; // WI_LOG names wiwa::LogLevel unqualified
		const std::filesystem::path exeFolder = executable_folder();

		// the log (wiwa.log) goes next to the project list, not wherever the launcher was started from
		std::filesystem::path list = wiwa::ProjectRegistry::default_file();
		if (list.empty())
		{
			list = exeFolder / "projects.json";
		}
		std::error_code error;
		std::filesystem::create_directories(list.parent_path(), error);
		std::filesystem::current_path(list.parent_path(), error);
		wiwa::Log::init();

		wiwa::ProjectRegistry registry(list);
		std::string warning;
		if (!registry.load(warning))
		{
			WI_LOG(Warning, "Launcher", "{0}", warning);
		}

		if (!glfwInit())
		{
			WI_LOG(Error, "Launcher", "GLFW could not start");
			return 1;
		}
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		GLFWwindow* window = glfwCreateWindow(900, 560, "Wiwa Projects", nullptr, nullptr);
		if (!window)
		{
			glfwTerminate();
			return 1;
		}

		Gpu gpu;
		if (!gpu.create(glfwGetWin32Window(window)))
		{
			WI_LOG(Error, "Launcher", "Direct3D 11 could not start");
			glfwDestroyWindow(window);
			glfwTerminate();
			return 1;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::GetIO().IniFilename = nullptr;
		style();
		font(exeFolder);
		ImGui_ImplGlfw_InitForOther(window, true);
		ImGui_ImplDX11_Init(gpu.Device, gpu.Context);

		wiwa::LauncherUi ui(registry, exeFolder);
		glfwSetWindowUserPointer(window, &ui);
		glfwSetDropCallback(window, [](GLFWwindow* w, int count, const char** paths)
		{
			std::vector<std::filesystem::path> dropped;
			for (int i = 0; i < count; i++)
			{
				const std::string text(paths[i]); // GLFW gives UTF-8
				dropped.emplace_back(std::u8string(text.begin(), text.end()));
			}
			static_cast<wiwa::LauncherUi*>(glfwGetWindowUserPointer(w))->on_drop(dropped);
		});

		int width = 0, height = 0;
		glfwGetFramebufferSize(window, &width, &height);
		while (!glfwWindowShouldClose(window) && !ui.should_close())
		{
			glfwPollEvents();
			int w = 0, h = 0;
			glfwGetFramebufferSize(window, &w, &h);
			if (w == 0 || h == 0) // minimized
			{
				glfwWaitEvents();
				continue;
			}
			if (w != width || h != height)
			{
				width = w;
				height = h;
				gpu.resize(width, height);
			}

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
			ui.draw();
			ImGui::Render();

			const float clear[4] = { 0.09f, 0.09f, 0.1f, 1.0f };
			gpu.Context->OMSetRenderTargets(1, &gpu.Target, nullptr);
			gpu.Context->ClearRenderTargetView(gpu.Target, clear);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			gpu.SwapChain->Present(1, 0);
		}

		ImGui_ImplDX11_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		gpu.destroy();
		glfwDestroyWindow(window);
		glfwTerminate();
		return 0;
	}
}

int main()
{
	return run();
}

#ifdef WI_SHIPPING
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	return run();
}
#endif
