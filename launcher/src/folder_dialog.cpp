#include "folder_dialog.h"

#include <windows.h>
#include <shobjidl.h>

namespace wiwa
{
	std::optional<std::filesystem::path> pick_folder(const std::filesystem::path& start)
	{
		std::optional<std::filesystem::path> result;
		const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		IFileOpenDialog* dialog = nullptr;
		if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
		{
			DWORD options = 0;
			dialog->GetOptions(&options);
			dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

			std::error_code error;
			if (!start.empty() && std::filesystem::is_directory(start, error))
			{
				IShellItem* folder = nullptr;
				if (SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&folder))))
				{
					dialog->SetFolder(folder);
					folder->Release();
				}
			}

			IShellItem* item = nullptr;
			if (SUCCEEDED(dialog->Show(nullptr)) && SUCCEEDED(dialog->GetResult(&item)))
			{
				PWSTR path = nullptr;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
				{
					result = std::filesystem::path(path);
					CoTaskMemFree(path);
				}
				item->Release();
			}
			dialog->Release();
		}
		if (SUCCEEDED(init))
		{
			CoUninitialize();
		}
		return result;
	}
}
