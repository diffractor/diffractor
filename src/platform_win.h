// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#define WINVER _WIN32_WINNT_WIN7
#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define _WIN32_WINDOWS _WIN32_WINNT_WIN7
#define _WIN32_IE _WIN32_IE_IE110
#define NTDDI_VERSION   NTDDI_WIN7

#ifndef WIN32_LEAN_AND_MEAN
// WIN32_LEAN_AND_MEAN implies NOCRYPT and NOGDI.
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX 
#endif
#ifndef NOKERNEL
#define NOKERNEL
#endif
//#ifndef NOUSER
//#define NOUSER
//#endif
#ifndef NOSERVICE
#define NOSERVICE
#endif
#ifndef NOSOUND
#define NOSOUND
#endif
#ifndef NOMCX
#define NOMCX
#endif

#define STRICT 
#define _WINSOCKAPI_    // stops windows.h including winsock.h

#include <dxgi.h>
#include <windows.h>
//#include <wrl/client.h>
#include <wrl.h>
using namespace Microsoft::WRL;


struct factories;
struct file_load_result;
using factories_ptr = std::shared_ptr<factories>;

const int max_vert_count = 4 * 512;
const int max_index_count = 6 * 512;
const int max_text_len = 2000;

HWND app_wnd();
extern HINSTANCE get_resource_instance;
FILETIME ts_to_ft(uint64_t ts);
uint64_t ft_to_ts(const FILETIME& ft);

std::u8string win32_to_string(const IID& iid);

struct char_pos_width
{
	float x = 0.0f;
	float y = 0.0f;
	float cx = 0.0f;
};

struct calc_text_extent_result
{
	std::vector<char_pos_width> pos;
	int cy = 0;
	int cx = 0;
};

struct text_line
{
	int begin = 0;
	int end = 0;
	int pixel_width = 0;
};

class draw_context_device : public ui::draw_context
{
public:
	virtual void destroy() = 0;
	virtual void update_font_size(int base_font_size) = 0;
	virtual void begin_draw(sizei client_extent, int base_font_size) = 0;
	virtual void render() = 0;
	virtual void resize(sizei size) = 0;
	virtual bool is_valid() const = 0;
};

using draw_context_device_ptr = std::shared_ptr<draw_context_device>;

class data_object_client : public platform::clipboard_data
{
	ComPtr<IDataObject> _pData;

public:
	data_object_client(IDataObject* pData);

	bool has_data(FORMATETC* pf) const;
	bool has_drop_files() const override;
	bool has_bitmap() const override;
	platform::file_op_result drop_files(df::folder_path target, platform::drop_effect effect) override;
	platform::file_op_result save_bitmap(df::folder_path save_path, std::u8string_view name, bool as_png) override;
	DWORD preferred_drop_effect() const;
	description files_description() const override;
	df::file_path first_path() const override;
};

draw_context_device_ptr d3d11_create_context(const factories_ptr& f, const ComPtr<IDXGISwapChain>& swap_chain,
	int base_font_size);
df::blob load_resource(int id, LPCWSTR lpType);

HGLOBAL image_to_handle(const file_load_result& image);
platform::file_op_result save_bitmap_info(df::folder_path save_path, std::u8string_view name, bool as_png,
	HBITMAP image_buffer_in);
void draw_surface(HDC hdc, sizei dimensions, ui::texture_format format, int stride, const uint8_t* pixels);


struct variant_t
{
	VARIANT v = {};

	variant_t() noexcept
	{
		VariantInit(&v);
	}

	~variant_t() noexcept
	{
		VariantClear(&v);
	}

	variant_t(const variant_t& other) = delete;
	variant_t& operator=(const variant_t& other) = delete;
};

struct prop_variant_t
{
	PROPVARIANT v = {};

	prop_variant_t() noexcept
	{
		PropVariantInit(&v);
	}

	~prop_variant_t() noexcept
	{
		PropVariantClear(&v);
	}

	prop_variant_t(const variant_t& other) = delete;
	prop_variant_t& operator=(const variant_t& other) = delete;
};


struct bstr_t
{
	BSTR m_str = nullptr;

	bstr_t() = default;

	explicit bstr_t(const std::wstring_view sv) : m_str(SysAllocStringLen(sv.data(), static_cast<uint32_t>(sv.size())))
	{
	}

	explicit bstr_t(const std::u8string_view sv) : m_str(SysAllocString(str::utf8_to_utf16(sv).c_str()))
	{
	}

	bstr_t(const bstr_t& other) = delete;
	bstr_t& operator=(const bstr_t& other) = delete;

	operator BSTR() const noexcept
	{
		return m_str;
	}

	~bstr_t() noexcept
	{
		if (m_str)
		{
			SysFreeString(m_str);
		}
	}

	BSTR* operator&() noexcept
	{
		df::assert_true(m_str == nullptr);
		return &m_str;
	}
};
