// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: The process lifecycle wWinMain runs around the window: the console the command-line tools
// write to, the fatal-error message, the crash dump and the unhandled-exception filter that writes it,
// restart and recovery registration with Windows, and the graphics crash guard read at startup.

#include "pch.h"
#include "platform_win.h"
#include "platform_win_process.h"
#include "app_text.h"

#include <io.h>
#include <fcntl.h>

#ifndef WINSTORE
#include <DbgHelp.h>
#endif

std::weak_ptr<ui::app> g_app;

// Wire up stdout/stderr for the headless console commands (/test, /gen-docs,
// /validate-po). This app is built as a GUI subsystem executable, so it has no
// console by default. When its output is redirected to a pipe or file (e.g.
// `diffractor64.exe /test | Out-Host` or `> log.txt`, as CI does), we must bind
// the CRT stdout/stderr to the INHERITED handle so the parent process captures
// it. Reopening "CONOUT$" instead (the old behaviour) sends the output to a
// console screen buffer that a piped/redirected/headless caller never sees --
// which made CI show an empty log. Only attach/allocate a real console when the
// output is NOT redirected (i.e. a genuine interactive run).
void setup_headless_console()
{
	const auto bind_std_stream = [](const DWORD std_handle, FILE* const stream)
	{
		const HANDLE h = GetStdHandle(std_handle);
		const DWORD type = GetFileType(h);

		if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK)
		{
			// Redirected: point the CRT stream at the inherited pipe/file.
			const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(h), _O_TEXT);

			// Either failure leaves the stream on its original handle, so the caller still
			// needs the console fallback.
			if (fd == -1)
			{
				return false;
			}

			fflush(stream);
			const int duped = _dup2(fd, _fileno(stream));
			_close(fd);

			if (duped != 0)
			{
				return false;
			}

			setvbuf(stream, nullptr, _IONBF, 0);
			return true;
		}

		return false;
	};

	const bool out_redirected = bind_std_stream(STD_OUTPUT_HANDLE, stdout);
	const bool err_redirected = bind_std_stream(STD_ERROR_HANDLE, stderr);

	if (!out_redirected || !err_redirected)
	{
		// At least one stream is not redirected, so we need a console for it.
		if (!AttachConsole(ATTACH_PARENT_PROCESS))
		{
			AllocConsole();
		}

		FILE* fp = nullptr;
		if (!out_redirected) freopen_s(&fp, "CONOUT$", "w", stdout);
		if (!err_redirected) freopen_s(&fp, "CONOUT$", "w", stderr);
	}

	std::setlocale(LC_ALL, "en_US.UTF-8");
}

void log_open_files_to_crash_files_list();
void flush_open_files_to_crash_files_list();

#ifndef WINSTORE

static bool create_dump(EXCEPTION_POINTERS* exception_pointers, const df::file_path dump_file_path)
{
	using MINIDUMP_WRITE_DUMP = BOOL(WINAPI*)(
		IN HANDLE hProcess,
		IN uint32_t ProcessId,
		IN HANDLE hFile,
		IN MINIDUMP_TYPE DumpType,
		IN PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam, OPTIONAL
		IN PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam, OPTIONAL
		IN PMINIDUMP_CALLBACK_INFORMATION CallbackParam OPTIONAL
	);

	auto* const dbg_help = LoadLibrary(L"DBGHELP.DLL");
	auto dump_successful = false;

	if (dbg_help)
	{
		const auto write_dump = (MINIDUMP_WRITE_DUMP)GetProcAddress(dbg_help, "MiniDumpWriteDump");

		if (write_dump)
		{
			const auto w = platform::to_file_system_path(dump_file_path);
			auto* const dump_file = CreateFile(w.c_str(), GENERIC_READ | GENERIC_WRITE,
			                                   FILE_SHARE_WRITE | FILE_SHARE_READ,
			                                   nullptr, CREATE_ALWAYS, 0, nullptr);

			if (dump_file != INVALID_HANDLE_VALUE)
			{
				MINIDUMP_EXCEPTION_INFORMATION exception_information;
				exception_information.ThreadId = GetCurrentThreadId();
				exception_information.ExceptionPointers = exception_pointers;
				exception_information.ClientPointers = TRUE;

				auto dump_flags = MiniDumpWithDataSegs | // Include DS from all loaded modules
					MiniDumpWithHandleData | // Include high level OS handle info
					MiniDumpScanMemory | // Scan for pointer references in module list
					MiniDumpWithUnloadedModules | // Recently unloaded modules
					MiniDumpWithThreadInfo | // Include thread state information
					MiniDumpIgnoreInaccessibleMemory |
					// Ignore memory read failures when attempting to read inaccessible regions
					MiniDumpNormal; // Normal stack trace info

				dump_successful = write_dump(GetCurrentProcess(), GetCurrentProcessId(), dump_file,
				                             static_cast<MINIDUMP_TYPE>(dump_flags), &exception_information, nullptr,
				                             nullptr);

				CloseHandle(dump_file);
			}
		}
	}

	if (dbg_help)
	{
		FreeLibrary(dbg_help);
	}

	return dump_successful;
}


static LONG WINAPI exception_callback(EXCEPTION_POINTERS* pExceptionPointers)
{
	const auto app = g_app.lock();

	if (app)
	{
		const auto dump_file_path = platform::temp_file();
		const auto dump_created = create_dump(pExceptionPointers, dump_file_path);

		// A failed write still leaves the file create_dump opened.
		if (!dump_created) platform::delete_file(dump_file_path);

		// Reported even when no dump could be written: dbghelp or the temp file can fail, and that
		// is exactly the machine whose next launch would crash again. The handler also records the
		// crashed file in the skip list, which must not depend on the minidump.
		app->crash(dump_created ? dump_file_path : df::file_path{});
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

unhandled_exception_filter::unhandled_exception_filter() : _original(SetUnhandledExceptionFilter(exception_callback))
{
}

unhandled_exception_filter::~unhandled_exception_filter()
{
	SetUnhandledExceptionFilter(_original);
}

#endif // !WINSTORE

// The application recovery/restart APIs live in kernel32 but are not implemented on every
// host. Wine, for example, terminates the process when its unimplemented
// UnregisterApplicationRecoveryCallback stub is called. Resolve them dynamically so a missing
// export degrades to a harmless no-op instead of aborting the app.
namespace
{
	template <typename T>
	T resolve_kernel32(const char* name)
	{
		const auto h = GetModuleHandleW(L"kernel32.dll");
		return h ? reinterpret_cast<T>(GetProcAddress(h, name)) : nullptr;
	}

	using register_application_restart_t = HRESULT(WINAPI*)(PCWSTR, DWORD);
	using unregister_application_restart_t = HRESULT(WINAPI*)();
	using register_application_recovery_callback_t = HRESULT(WINAPI*)(APPLICATION_RECOVERY_CALLBACK, PVOID, DWORD,
	                                                                  DWORD);
	using unregister_application_recovery_callback_t = HRESULT(WINAPI*)();
	using application_recovery_in_progress_t = HRESULT(WINAPI*)(PBOOL);
	using application_recovery_finished_t = void(WINAPI*)(BOOL);
}

#ifndef WINSTORE

// Restart is desktop-only: RegisterApplicationRestart re-launches the executable directly,
// which for a packaged build would start the process without its package identity.
static void register_restart(const std::string_view restart_cmd_line)
{
	const auto pRegisterApplicationRestart = resolve_kernel32<register_application_restart_t>(
		"RegisterApplicationRestart");
	if (!pRegisterApplicationRestart) return;

	const auto restart_cmd_line_w = str::utf8_to_utf16(restart_cmd_line);

	// RegisterApplicationRestart rejects anything longer, and wcscpy_s would terminate the process
	// rather than truncate. Losing restart registration is preferable to either.
	if (restart_cmd_line_w.size() >= RESTART_MAX_CMD_LINE)
	{
		df::log(__FUNCTION__, "restart command line too long to register");
		return;
	}

	static WCHAR wsCommandLine[RESTART_MAX_CMD_LINE];
	wcscpy_s(wsCommandLine, restart_cmd_line_w.c_str());
	const auto hr = pRegisterApplicationRestart(wsCommandLine, RESTART_NO_PATCH | RESTART_NO_REBOOT);
	df::assert_true(SUCCEEDED(hr));
}

#endif // !WINSTORE

void unregister_restart()
{
	const auto pUnregisterApplicationRecoveryCallback = resolve_kernel32<unregister_application_recovery_callback_t>(
		"UnregisterApplicationRecoveryCallback");
	const auto pUnregisterApplicationRestart = resolve_kernel32<unregister_application_restart_t>(
		"UnregisterApplicationRestart");
	if (pUnregisterApplicationRecoveryCallback) pUnregisterApplicationRecoveryCallback();
	if (pUnregisterApplicationRestart) pUnregisterApplicationRestart();
}

static DWORD WINAPI recover_callback(PVOID pContext)
{
	df::log(__FUNCTION__, "*** recover callback ***");
	log_open_files_to_crash_files_list();
	flush_open_files_to_crash_files_list();

	BOOL bCanceled = FALSE;
	const auto pApplicationRecoveryInProgress = resolve_kernel32<application_recovery_in_progress_t>(
		"ApplicationRecoveryInProgress");
	if (pApplicationRecoveryInProgress) pApplicationRecoveryInProgress(&bCanceled);

	if (bCanceled)
	{
		df::log(__FUNCTION__, "Recovery was canceled by the user.");
	}

	const auto app = g_app.lock();

	if (app)
	{
		app->save_recovery_state();
	}

	df::close_log();

	const auto pApplicationRecoveryFinished = resolve_kernel32<application_recovery_finished_t>(
		"ApplicationRecoveryFinished");
	if (pApplicationRecoveryFinished) pApplicationRecoveryFinished(bCanceled ? FALSE : TRUE);
	return 0;
}


void setup_restart(const std::string_view restart_cmd_line)
{
#ifndef WINSTORE
	register_restart(restart_cmd_line);
#endif
	// The recovery callback runs in both builds: it is what persists the crashed-file skip list
	// and the session recovery state when Windows terminates a hung or crashing process.
	const auto pRegisterApplicationRecoveryCallback = resolve_kernel32<register_application_recovery_callback_t>(
		"RegisterApplicationRecoveryCallback");
	if (!pRegisterApplicationRecoveryCallback) return;
	const auto hr = pRegisterApplicationRecoveryCallback(recover_callback, nullptr, RECOVERY_DEFAULT_PING_INTERVAL, 0);
	df::assert_true(SUCCEEDED(hr));
}

// Inspect the graphics crash-guard flags left by the previous run and fall back if it
// crashed while a graphics subsystem was active. A HW-decode crash disables only hardware
// video decoding (narrowest attribution); a crash with the GPU active but no decode in
// progress disables GPU rendering. GPU device loss is handled as a one-session software
// recovery: the user's preference is preserved and retried after that session exits cleanly.
// Escalation is graceful: a decode crash drops decode first; GPU is dropped only if a later
// run still crashes with decode already off (so only gpu_render remains set).
void apply_gpu_crash_guard()
{
	const auto decode_crashed = platform::read_crash_guard(platform::crash_guard::hw_video_decode);
	const auto gpu_crashed = platform::read_crash_guard(platform::crash_guard::gpu_render);

	if (!decode_crashed && !gpu_crashed)
	{
		return;
	}

	// Attribute to the narrowest subsystem: a hardware-decode crash disables only HW video
	// decode (never the GPU), even if HW decode happens to already be off; only a crash with
	// no decode in progress disables GPU rendering.
	if (decode_crashed)
	{
		if (setting.use_d3d11va)
		{
			setting.use_d3d11va = false;
			df::log(__FUNCTION__,
			        "Previous run crashed during hardware video decode - disabling HW video decode (GPU rendering kept)");
		}
	}
	else if (gpu_crashed)
	{
		if (setting.use_gpu)
		{
			platform::suppress_crash_guard(platform::crash_guard::gpu_render, true);
			df::log(__FUNCTION__,
			        "Previous run lost the GPU device - using software rendering for this recovery session");
		}
		return;
	}

	// Hardware-decode failures retain the existing persistent fallback policy.
	setting.write();
	platform::set_crash_guard(platform::crash_guard::hw_video_decode, false);
	platform::set_crash_guard(platform::crash_guard::gpu_render, false);
}

void show_fatal_error(const std::string_view message)
{
	df::log(__FUNCTION__, message);

	std::wstring s;
	s += str::utf8_to_utf16(tt.title_error);
	s += L"\n\n";
	s += str::utf8_to_utf16(tt.error_cannot_continue);
	s += L"\n\n";
	s += str::utf8_to_utf16(message);

	::MessageBox(nullptr, s.c_str(), str::utf8_to_utf16(s_app_name).c_str(), MB_OK | MB_ICONHAND);
}

// The app's own failure dialog needs a window and a running message loop. Startup failures happen
// before both exist, so they come back here for a bare message box instead of being queued to a
// loop that is never entered - a silent exit is the one outcome worth ruling out.
void platform::show_startup_failure(const std::string_view message)
{
	show_fatal_error(message);
}
