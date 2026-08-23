// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: HTTP client using WinInet. Handles web requests, downloads,
// form uploads, and network connectivity checks.

#include "pch.h"
#include "platform_win.h"

#include <WinInet.h>
#include <ws2tcpip.h>

#include <utility>

df_assert_movable(platform::web_request);
df_assert_movable(platform::web_response);

bool platform::is_online()
{
	DWORD flags;
	return 0 != InternetGetConnectedState(&flags, 0);
}

constexpr auto REQ_STATE_SEND_REQ = 0;
constexpr auto REQ_STATE_POST_GET_DATA = 2;
constexpr auto REQ_STATE_POST_SEND_DATA = 3;
constexpr auto REQ_STATE_POST_COMPLETE = 4;
constexpr auto REQ_STATE_RESPONSE_RECV_DATA = 5;
constexpr auto REQ_STATE_RESPONSE_READY = 6;
constexpr auto REQ_STATE_RESPONSE_WRITE_DATA = 7;
constexpr auto REQ_STATE_COMPLETE = 8;

static int get_status_code(const HINTERNET h)
{
	DWORD result = 0;
	DWORD result_size = sizeof(result);
	if (!HttpQueryInfo(h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &result, &result_size, nullptr))
	{
		return 0; // Return 0 if query fails
	}
	return static_cast<int>(result);
}

static std::string get_content_type(const HINTERNET request_handle)
{
	std::string result;
	DWORD result_size = 0;
	DWORD header_index = 0;

	// First call to get the required buffer size
	HttpQueryInfoA(request_handle, HTTP_QUERY_CONTENT_TYPE, nullptr, &result_size, &header_index);

	if (result_size > 0)
	{
		result.resize(result_size);
		header_index = 0; // Reset header index

		if (HttpQueryInfoA(request_handle, HTTP_QUERY_CONTENT_TYPE, result.data(), &result_size, &header_index))
		{
			// result_size now contains the actual string length (excluding null terminator)
			if (result_size > 0 && result_size <= result.size())
			{
				result.resize(result_size);
			}
			else
			{
				result.clear();
			}
		}
		else
		{
			result.clear();
		}
	}

	return result;
}

static std::string format_path(const platform::web_request& req)
{
	auto result = req.path;

	if (!req.query.empty())
	{
		bool is_first = true;
		result += "?";

		for (const auto& qp : req.query)
		{
			if (!is_first)
			{
				result += "&";
			}

			result += df::url_encode(qp.first);
			result += "=";
			result += df::url_encode(qp.second);
			is_first = false;
		}
	}

	return result;
}

// RAII wrapper for WinInet handles
class inet_handle
{
	HINTERNET _h;

public:
	explicit inet_handle(const HINTERNET handle = nullptr) : _h(handle)
	{
	}

	~inet_handle()
	{
		if (_h)
		{
			InternetCloseHandle(_h);
		}
	}

	HINTERNET detach()
	{
		const auto handle = _h;
		_h = nullptr;
		return handle;
	}

	// No copy constructor/assignment
	inet_handle(const inet_handle&) = delete;
	inet_handle& operator=(const inet_handle&) = delete;

	// Move constructor/assignment
	inet_handle(inet_handle&& other) noexcept : _h(other._h)
	{
		other._h = nullptr;
	}

	inet_handle& operator=(inet_handle&& other) noexcept
	{
		if (this != &other)
		{
			if (_h)
			{
				InternetCloseHandle(_h);
			}
			_h = other._h;
			other._h = nullptr;
		}
		return *this;
	}

	operator HINTERNET() const { return _h; }
	HINTERNET get() const { return _h; }
	bool is_valid() const { return _h != nullptr; }

	void reset(const HINTERNET handle = nullptr)
	{
		if (_h)
		{
			InternetCloseHandle(_h);
		}
		_h = handle;
	}
};

struct platform::web_host
{
	HINTERNET session_handle = nullptr;
	HINTERNET connection_handle = nullptr;
	bool secure = true;

	web_host() = default;
	web_host(const web_host&) = delete;
	web_host& operator=(const web_host&) = delete;

	~web_host()
	{
		if (connection_handle) InternetCloseHandle(connection_handle);
		if (session_handle) InternetCloseHandle(session_handle);
	}
};

platform::web_host_ptr platform::connect_to_host(const std::string_view host, const bool secure_in, const int port_in)
{
	// InternetOpen and InternetConnect
	const auto agent = str::utf8_to_utf16(s_app_name);
	inet_handle session_handle(::InternetOpen(agent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0));

	if (!session_handle.is_valid())
	{
		return nullptr; // Return empty response on failure
	}

	const auto hostW = str::utf8_to_utf16(host);
	const auto port = port_in == 0
		                  ? (secure_in ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT)
		                  : port_in;
	inet_handle conn(::InternetConnect(session_handle, hostW.c_str(), port, nullptr, nullptr,
	                                   INTERNET_SERVICE_HTTP, 0, 0));

	if (!conn.is_valid())
	{
		return nullptr; // Return empty response on failure
	}

	auto result_host = std::make_shared<web_host>();
	result_host->session_handle = session_handle.detach();
	result_host->connection_handle = conn.detach();
	result_host->secure = secure_in;
	return result_host;
}

platform::web_response platform::send_request(const web_host_ptr& host, const web_request& req)
{
	web_response result;

	if (!host)
		return result;

	std::ostringstream content;
	std::ostringstream header;

	for (const auto& h : req.headers)
	{
		header << h.first << ": " << h.second << "\r\n";
	}

	if (!req.form_data.empty())
	{
		constexpr auto boundary = "54B8723DE6044695A68C838E8BF0CB00";

		for (const auto& f : req.form_data)
		{
			content << "--" << boundary << "\r\n";
			content << "Content-Disposition: form-data; name=\"" << f.first << "\"\r\n";
			content << "Content-Type: text/plain; charset=\"utf-8\"\r\n";
			content << "\r\n";
			content << f.second << "\r\n";
		}

		if (!req.file_path.is_empty() && !req.file_form_data_name.empty())
		{
			auto content_type = "application/octet-stream";
			if (str::ends(req.file_path.extension(), "zip")) content_type = "application/x-zip-compressed";

			content << "--" << boundary << "\r\n";
			content << "Content-Disposition: form-data; name=\"" << req.file_form_data_name << "\"; filename=\""
				<<
				req.file_name << "\"\r\n";
			content << "Content-Type: " << content_type << "\r\n";
			content << "\r\n";

			df::file f;

			if (f.open_read(req.file_path, true))
			{
				while (f.read64k())
				{
					content << std::string_view(reinterpret_cast<const char*>(f.buffer()), f.buffer_data_size());
				}
			}

			content << "\r\n";
		}

		content << "--" << boundary << "--";
		header << "Content-Type: multipart/form-data; boundary=" << boundary << "\r\n";
	}

	const auto wverb = req.verb == web_request_verb::GET ? L"GET" : L"POST";
	const auto wpath = str::utf8_to_utf16(format_path(req));
	auto flags = INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_AUTH |
		INTERNET_FLAG_RELOAD;
	if (host->secure) flags |= INTERNET_FLAG_SECURE;

	inet_handle request_handle(HttpOpenRequest(host->connection_handle, wverb, wpath.c_str(), nullptr, nullptr, nullptr,
	                                           flags, 0));

	if (!request_handle.is_valid())
	{
		return result; // Return empty response on failure
	}

	const auto headerW = str::utf8_to_utf16(header.str());
	const auto content_data = content.str();
	if (headerW.size() > MAXDWORD || content_data.size() > MAXDWORD) return result;

	INTERNET_BUFFERS buffers = {};
	buffers.dwStructSize = sizeof(INTERNET_BUFFERS);
	buffers.Next = nullptr;
	buffers.lpcszHeader = headerW.c_str();
	buffers.dwHeadersTotal = buffers.dwHeadersLength = static_cast<DWORD>(headerW.size());
	buffers.lpvBuffer = nullptr;
	buffers.dwBufferLength = 0;
	buffers.dwBufferTotal = static_cast<DWORD>(content_data.size());
	buffers.dwOffsetLow = 0;
	buffers.dwOffsetHigh = 0;

	if (!HttpSendRequestEx(request_handle, &buffers, nullptr, 0, 0))
	{
		return result; // Return empty response on failure
	}

	// Send content data in chunks
	if (!content_data.empty())
	{
		constexpr size_t chunk_size = 8192; // Increased chunk size for better performance
		size_t total_written = 0;

		while (total_written < content_data.size())
		{
			const auto remaining = content_data.size() - total_written;
			const auto to_write = std::min(chunk_size, remaining);
			DWORD written = 0;

			if (!InternetWriteFile(request_handle, content_data.data() + total_written, static_cast<DWORD>(to_write),
			                       &written))
			{
				return result; // Return empty response on write failure
			}

			if (written == 0)
			{
				return result; // No progress made, abort
			}

			total_written += written;
		}
	}

	if (!::HttpEndRequest(request_handle, nullptr, 0, 0))
	{
		return result; // Return empty response on failure
	}

	result.status_code = get_status_code(request_handle);
	result.content_type = get_content_type(request_handle);

	if (!req.download_file_path.is_empty())
	{
		auto download_file = open_file(req.download_file_path, file_open_mode::create);

		if (download_file)
		{
			uint8_t buffer[8192]; // Increased buffer size
			bool download_complete = false;
			for (;;)
			{
				DWORD read = 0;
				if (!InternetReadFile(request_handle, buffer, sizeof(buffer), &read)) break;
				if (read == 0)
				{
					download_complete = true;
					break;
				}
				if (download_file->write(buffer, read) != read)
				{
					break;
				}
			}

			if (!download_complete)
			{
				result.status_code = 0;
				download_file.reset();
				delete_file(req.download_file_path);
			}
		}
	}
	else
	{
		uint8_t buffer[8192]; // Increased buffer size
		for (;;)
		{
			DWORD read = 0;
			if (!InternetReadFile(request_handle, buffer, sizeof(buffer), &read))
			{
				result.status_code = 0;
				result.body.clear();
				break;
			}
			if (read == 0) break;
			result.body.append(buffer, buffer + read);
		}
	}

	return result;
}
