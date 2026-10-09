// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Offers files to other apps through the Windows share sheet. The sheet is a Windows
// Runtime API, reached here through C++/WinRT so that no other file carries its headers.

#include "pch.h"

#include "platform_win.h"

#include <Unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>

#pragma comment(lib, "runtimeobject")

namespace
{
	namespace data_transfer = winrt::Windows::ApplicationModel::DataTransfer;
	namespace storage = winrt::Windows::Storage;

	// ShObjIdl_core.h declares IDataTransferManagerInterop only when targeting Windows 8 or later,
	// and the platform headers target Windows 7. The interface is fixed by its IID, so it is
	// declared here under a name of its own.
	struct __declspec(uuid("3A3DCD6C-3EAB-43DC-BCDE-45671CE800C8")) __declspec(novtable)
		data_transfer_manager_interop : ::IUnknown
	{
		virtual HRESULT __stdcall GetForWindow(HWND app_window, REFIID riid, void** manager) = 0;
		virtual HRESULT __stdcall ShowShareUIForWindow(HWND app_window) = 0;
	};

	// The handler registered for the sheet showing now, if it has not yet run. UI thread only.
	winrt::event_token requested_token{};

	std::string describe(const winrt::hresult_error& e)
	{
		return std::format("{:#010x} {}", static_cast<uint32_t>(e.code().value), str::utf16_to_utf8(e.message()));
	}

	// Resolving a path to a StorageFile is asynchronous, so the sheet is asked to wait for it. A
	// fire_and_forget coroutine terminates the process on an escaping exception, so none escapes.
	winrt::fire_and_forget fill_request(const data_transfer::DataRequest request, const std::vector<std::wstring> paths,
	                                    const winrt::hstring title)
	{
		data_transfer::DataRequestDeferral deferral{nullptr};
		winrt::hstring failure;

		try
		{
			deferral = request.GetDeferral();

			const auto data = request.Data();
			data.Properties().Title(title);

			const auto items = winrt::single_threaded_vector<storage::IStorageItem>();

			for (const auto& path : paths)
			{
				items.Append(co_await storage::StorageFile::GetFileFromPathAsync(path));
			}

			// Read-only: the receiving app gets the files to read, not to change.
			data.SetStorageItems(items);
		}
		catch (const winrt::hresult_error& e)
		{
			df::log(__FUNCTION__, describe(e));
			failure = e.message();
		}
		catch (const std::exception& e)
		{
			df::log(__FUNCTION__, e.what());
			failure = winrt::to_hstring(e.what());
		}

		try
		{
			if (!failure.empty()) request.FailWithDisplayText(failure);
			if (deferral) deferral.Complete();
		}
		catch (const winrt::hresult_error& e)
		{
			df::log(__FUNCTION__, describe(e));
		}
	}
}

bool platform::can_share_files()
{
	// Desktop apps reach the share sheet through IDataTransferManagerInterop, from Windows 10 on.
	static const auto release = windows_release();
	return release == df::os_release::windows_10 || release == df::os_release::windows_11 ||
		release == df::os_release::windows_later;
}

bool platform::share_files(const std::vector<df::file_path>& files, const std::string_view title)
{
	df::assert_true(ui::is_ui_thread());

	if (files.empty() || !can_share_files()) return false;

	std::vector<std::wstring> paths;
	paths.reserve(files.size());

	for (const auto& f : files)
	{
		paths.emplace_back(to_shell_path(f));
	}

	try
	{
		const auto window = app_wnd();
		const auto interop = winrt::get_activation_factory<data_transfer::DataTransferManager,
		                                                   data_transfer_manager_interop>();

		data_transfer::DataTransferManager manager{nullptr};
		winrt::check_hresult(interop->GetForWindow(window, winrt::guid_of<data_transfer::DataTransferManager>(),
		                                           winrt::put_abi(manager)));

		// A showing that never asked for its data must not answer for this one.
		if (requested_token) manager.DataRequested(std::exchange(requested_token, {}));

		requested_token = manager.DataRequested(
			[paths = std::move(paths), title = winrt::to_hstring(title)](
			const data_transfer::DataTransferManager& sender, const data_transfer::DataRequestedEventArgs& args)
			{
				fill_request(args.Request(), paths, title);

				// One showing, one request. Revoked once answered, so a later showing - including one the
				// system raises on its own - never offers these files again.
				sender.DataRequested(std::exchange(requested_token, {}));
			});

		winrt::check_hresult(interop->ShowShareUIForWindow(window));
		return true;
	}
	catch (const winrt::hresult_error& e)
	{
		df::log(__FUNCTION__, describe(e));
		return false;
	}
}
