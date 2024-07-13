// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "platform_win.h"

#include <WinInet.h>
#include <ws2tcpip.h>

#include <utility>

static_assert(std::is_move_constructible_v<platform::web_request>);
static_assert(std::is_move_constructible_v<platform::web_response>);

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

static int get_status_code(HINTERNET h)
{
	DWORD result = 0;
	DWORD result_size = sizeof(result);
	HttpQueryInfo(h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &result, &result_size, nullptr);
	return result;
}

static std::u8string get_content_type(HINTERNET request_handle)
{
	std::u8string result;
	const DWORD buffer_length = 1024;
	result.resize(buffer_length);
	DWORD result_size = buffer_length;
	DWORD header_index = 0;

	if (HttpQueryInfoA(request_handle, HTTP_QUERY_CONTENT_TYPE, result.data(), &result_size, &header_index))
	{
		result.resize(result_size);
	}

	return result;
}

static std::u8string format_path(const platform::web_request& req)
{
	auto result = req.path;

	if (!req.query.empty())
	{
		bool is_first = true;
		result += u8"?"sv;

		for (const auto& qp : req.query)
		{
			if (!is_first)
			{
				result += u8"&"sv;
			}

			result += df::url_encode(qp.first);
			result += u8"="sv;
			result += df::url_encode(qp.second);
			is_first = false;
		}
	}

	return result;
}


platform::web_response platform::send_request(const web_request& req)
{
	u8ostringstream content;
	u8ostringstream header;

	for (const auto& h : req.headers)
	{
		header << h.first << u8": "sv << h.second << u8"\r\n"sv;
	}

	if (!req.form_data.empty())
	{
		constexpr auto boundary = u8"54B8723DE6044695A68C838E8BF0CB00"sv;

		for (const auto& f : req.form_data)
		{
			content << u8"--"sv << boundary << u8"\r\n"sv;
			content << u8"Content-Disposition: form-data; name=\""sv << f.first << u8"\"\r\n"sv;
			content << u8"Content-Type: text/plain; charset=\"utf-8\"\r\n"sv;
			content << u8"\r\n"sv;
			content << f.second << u8"\r\n"sv;
		}

		if (!req.file_path.is_empty() && !req.file_form_data_name.empty())
		{
			auto content_type = u8"application/octet-stream"sv;
			if (str::ends(req.file_path.extension(), u8"zip"sv)) content_type = u8"application/x-zip-compressed"sv;

			content << u8"--"sv << boundary << u8"\r\n"sv;
			content << u8"Content-Disposition: form-data; name=\""sv << req.file_form_data_name << u8"\"; filename=\""sv <<
				req.file_name << u8"\"\r\n"sv;
			content << u8"Content-Type: "sv << content_type << u8"\r\n"sv;
			content << u8"\r\n"sv;

			df::file f;

			if (f.open_read(req.file_path, true))
			{
				while (f.read64k())
				{
					content << std::u8string_view(reinterpret_cast<const char8_t*>(f.buffer()), f.buffer_data_size());
				}
			}

			content << u8"\r\n"sv;
		}

		content << u8"--"sv << boundary << u8"--"sv;
		header << u8"Content-Type: multipart/form-data; boundary="sv << boundary << u8"\r\n"sv;
	}


	web_response result;
	const auto session_handle = ::InternetOpen(s_app_name_l, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);

	if (session_handle)
	{
		const auto hostW = str::utf8_to_utf16(req.host);
		const auto port = req.port == 0 ? (req.secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT) : req.port;
		const auto conn = ::InternetConnect(session_handle, hostW.c_str(), port, nullptr, nullptr,
		                                    INTERNET_SERVICE_HTTP, 0, 0);

		if (conn)
		{
			const auto wverb = req.verb == web_request_verb::GET ? L"GET" : L"POST";
			const auto wpath = str::utf8_to_utf16(format_path(req));
			auto flags = INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_AUTH |
				INTERNET_FLAG_RELOAD;
			if (req.secure) flags |= INTERNET_FLAG_SECURE;
			const auto request_handle =
				HttpOpenRequest(conn, wverb, wpath.c_str(), nullptr, nullptr, nullptr, flags, 0);

			if (request_handle)
			{
				const auto headerW = str::utf8_to_utf16(header.str());
				const auto content_data = content.str();

				INTERNET_BUFFERS buffers;
				buffers.dwStructSize = sizeof(INTERNET_BUFFERS);
				buffers.Next = nullptr;
				buffers.lpcszHeader = headerW.c_str();
				buffers.dwHeadersTotal = buffers.dwHeadersLength = static_cast<DWORD>(headerW.size());
				buffers.lpvBuffer = nullptr;
				buffers.dwBufferLength = 0;
				buffers.dwBufferTotal = static_cast<DWORD>(content_data.size());
				buffers.dwOffsetLow = 0;
				buffers.dwOffsetHigh = 0;

				if (HttpSendRequestEx(request_handle, &buffers, nullptr, 0, 0))
				{
					uint8_t buffer[1024 + 1];
					DWORD read = 0;
					DWORD written = 0;
					uint32_t n = 0;

					while (n < content_data.size())
					{
						written = 0;
						const auto w = std::min(256_z, content_data.size() - n);
						InternetWriteFile(request_handle, content_data.data() + n, static_cast<DWORD>(w), &written);
						n += written;

						if (written == 0)
							break;
					}

					::HttpEndRequest(request_handle, nullptr, 0, 0);

					result.status_code = get_status_code(request_handle);
					result.content_type = get_content_type(request_handle);

					if (!req.download_file_path.is_empty())
					{
						const auto download_file = open_file(req.download_file_path, file_open_mode::create);

						if (download_file)
						{
							while (InternetReadFile(request_handle, buffer, 1024, &read) != 0 && read > 0)
							{
								download_file->write(buffer, read);
							}
						}
					}
					else
					{
						while (InternetReadFile(request_handle, buffer, 1024, &read) != 0 && read > 0)
						{
							result.body.append(buffer, buffer + read);
						}
					}
				}

				InternetCloseHandle(request_handle);
			}

			InternetCloseHandle(conn);
		}

		InternetCloseHandle(session_handle);
	}

	return result;
}
