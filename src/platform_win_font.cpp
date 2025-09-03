// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"

#include "platform_win.h"
#include "platform_win_res.h"
#include "platform_win_visual.h"

template <typename InterfaceType>
InterfaceType* SafeAcquire(InterfaceType* newObject)
{
	if (newObject != nullptr)
		newObject->AddRef();

	return newObject;
}

static constexpr uint32_t icon_font_collection_id = 19;
static constexpr uint32_t icon_font_collection_id2 = 33;
static const auto icon_font_name = L"Segoe MDL2 Assets";
//static const auto petscii_font_name = L"Basic Engine ASCII 8x8";
static const auto petscii_font_name = L"C64 Pro Mono";

class resource_font_file_stream : public IDWriteFontFileStream
{
public:
	explicit resource_font_file_stream(uint32_t resourceID);
	virtual ~resource_font_file_stream() = default;

	// IUnknown methods

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
	{
		if (riid == IID_IUnknown ||
			riid == __uuidof(IDWriteFontFileStream))
		{
			*ppvObject = this;
			AddRef();
			return S_OK;
		}
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return InterlockedIncrement(&refCount_);
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG newCount = InterlockedDecrement(&refCount_);
		if (newCount == 0)
			delete this;

		return newCount;
	}

	// IDWriteFontFileStream methods
	HRESULT STDMETHODCALLTYPE ReadFileFragment(
		const void** fragmentStart, // [fragmentSize] in bytes
		UINT64 fileOffset,
		UINT64 fragmentSize,
		OUT void** fragmentContext
	) override;

	void STDMETHODCALLTYPE ReleaseFileFragment(void* fragmentContext) override;

	HRESULT STDMETHODCALLTYPE GetFileSize(OUT UINT64* fileSize) override;

	HRESULT STDMETHODCALLTYPE GetLastWriteTime(OUT UINT64* lastWriteTime) override;

	bool is_initialized() const
	{
		return _resource_ptr != nullptr;
	}

private:
	ULONG refCount_;
	const void* _resource_ptr; // [resourceSize_] in bytes
	DWORD _resource_size;
};

resource_font_file_stream::resource_font_file_stream(uint32_t resourceID) :
	refCount_(0),
	_resource_ptr(nullptr),
	_resource_size(0)
{
	auto* const resource = FindResource(get_resource_instance, MAKEINTRESOURCE(resourceID), L"BINARY");
	if (resource != nullptr)
	{
		auto* const memHandle = LoadResource(get_resource_instance, resource);
		if (memHandle != nullptr)
		{
			_resource_ptr = LockResource(memHandle);

			if (_resource_ptr != nullptr)
			{
				_resource_size = SizeofResource(get_resource_instance, resource);
			}
		}
	}
}


// IDWriteFontFileStream methods
HRESULT STDMETHODCALLTYPE resource_font_file_stream::ReadFileFragment(
	const void** fragmentStart, // [fragmentSize] in bytes
	UINT64 fileOffset,
	UINT64 fragmentSize,
	OUT void** fragmentContext
)
{
	// The loader is responsible for doing a bounds check.
	if (fileOffset <= _resource_size &&
		fragmentSize <= _resource_size &&
		fileOffset <= _resource_size - fragmentSize) // Prevent overflow
	{
		*fragmentStart = static_cast<const BYTE*>(_resource_ptr) + fileOffset;
		*fragmentContext = nullptr;
		return S_OK;
	}
	*fragmentStart = nullptr;
	*fragmentContext = nullptr;
	return E_FAIL;
}

void STDMETHODCALLTYPE resource_font_file_stream::ReleaseFileFragment(void* fragmentContext)
{
}

HRESULT STDMETHODCALLTYPE resource_font_file_stream::GetFileSize(OUT UINT64* fileSize)
{
	*fileSize = _resource_size;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE resource_font_file_stream::GetLastWriteTime(OUT UINT64* lastWriteTime)
{
	// The concept of last write time does not apply to this loader.
	*lastWriteTime = 0;
	return E_NOTIMPL;
}

std::u8string format_guid(REFGUID id)
{
	wchar_t sz[50]; // GUID string is typically 38 characters + null terminator
	const int result = StringFromGUID2(id, sz, _countof(sz));
	if (result == 0)
	{
		return u8"<invalid-guid>";
	}
	return str::utf16_to_utf8(sz);
}

class resource_font_file_loader : public IDWriteFontFileLoader
{
public:
	resource_font_file_loader() : refCount_(0) {}
	virtual ~resource_font_file_loader() = default;

	ULONG refCount_;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
	{
		if (riid == __uuidof(IUnknown) ||
			riid == __uuidof(IDWriteFontFileLoader))
		{
			AddRef();
			(*ppvObject) = static_cast<IDWriteFontFileLoader*>(this);
			return S_OK;
		}

		df::log(__FUNCTION__, str::format(u8"E_NOINTERFACE {}"sv, format_guid(riid)));
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return InterlockedIncrement(&refCount_);
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG newCount = InterlockedDecrement(&refCount_);
		if (newCount == 0)
			delete this;
		return newCount;
	}

	HRESULT STDMETHODCALLTYPE CreateStreamFromKey(
		const void* fontFileReferenceKey, // [fontFileReferenceKeySize] in bytes
		UINT32 fontFileReferenceKeySize,
		OUT IDWriteFontFileStream** fontFileStream
	) override
	{
		*fontFileStream = nullptr;

		if (fontFileReferenceKeySize != sizeof(uint32_t))
			return E_INVALIDARG;

		const auto resource_id = *static_cast<const uint32_t*>(fontFileReferenceKey);
		auto* const stream = new(std::nothrow) resource_font_file_stream(resource_id);

		if (stream == nullptr)
			return E_OUTOFMEMORY;

		if (!stream->is_initialized())
		{
			// Log which resource failed to load for debugging
			df::log(__FUNCTION__, str::format(u8"Failed to load font resource ID {}"sv, resource_id));
			delete stream;
			return E_FAIL;
		}

		*fontFileStream = SafeAcquire(stream);

		return S_OK;
	}
};

static resource_font_file_loader font_loader;

class resource_font_file_enumerator : public IDWriteFontFileEnumerator
{
public:
	resource_font_file_enumerator() : refCount_(0), _index(0), _factory(nullptr), _collection_key(nullptr), _collection_key_size(0) {}

	explicit resource_font_file_enumerator(IDWriteFactory* factory,
		const void* collectionKey,
		UINT32 collectionKeySize) : refCount_(0), _factory(factory),
		_collection_key(collectionKey),
		_collection_key_size(collectionKeySize),
		_index(0)
	{
	}

	virtual ~resource_font_file_enumerator() = default;

	ULONG refCount_;
	int _index = 0;
	IDWriteFactory* _factory = nullptr;
	const void* _collection_key = nullptr;
	UINT32 _collection_key_size = 0;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
	{
		if (riid == __uuidof(IUnknown) ||
			riid == __uuidof(IDWriteFontFileEnumerator))
		{
			AddRef();
			(*ppvObject) = static_cast<IDWriteFontFileEnumerator*>(this);
			return S_OK;
		}

		df::log(__FUNCTION__, str::format(u8"E_NOINTERFACE {}"sv, format_guid(riid)));

		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return InterlockedIncrement(&refCount_);
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG newCount = InterlockedDecrement(&refCount_);
		if (newCount == 0)
			delete this;
		return newCount;
	}

	ComPtr<IDWriteFontFile> _current;

	// IDWriteFontFileEnumerator methods
	HRESULT STDMETHODCALLTYPE MoveNext(OUT BOOL* hasCurrentFile) override
	{
		if (hasCurrentFile) *hasCurrentFile = false;
		_current.Reset();

		auto result = S_FALSE;

		uint32_t font_id = 0;
		if (_index == 1) font_id = IDF_ICONS;
		if (_index == 0) font_id = IDF_PETSCII;

		if (font_id != 0)
		{
			ComPtr<IDWriteFontFile> current;

			const auto hr = _factory->CreateCustomFontFileReference(
				&font_id,
				static_cast<uint32_t>(sizeof(font_id)),
				&font_loader,
				&current);

			df::log(__FUNCTION__, str::format(u8"CreateCustomFontFileReference {}"sv, hr));

			if (SUCCEEDED(hr))
			{
				_current = current;
				result = S_OK;
			}
		}

		if (hasCurrentFile) *hasCurrentFile = result == S_OK;
		_index += 1;
		return result;
	}

	HRESULT STDMETHODCALLTYPE GetCurrentFontFile(OUT IDWriteFontFile** fontFile) override
	{
		_current.CopyTo(fontFile);
		return S_OK;
	}
};

class resource_font_collection_loader : public IDWriteFontCollectionLoader
{
public:
	resource_font_collection_loader() : refCount_(0) {}
	virtual ~resource_font_collection_loader() = default;

	ULONG refCount_;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
	{
		if (riid == __uuidof(IUnknown) ||
			riid == __uuidof(IDWriteFontCollectionLoader))
		{
			AddRef();
			(*ppvObject) = static_cast<IDWriteFontCollectionLoader*>(this);
			return S_OK;
		}

		df::log(__FUNCTION__, str::format(u8"E_NOINTERFACE {}"sv, format_guid(riid)));
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return InterlockedIncrement(&refCount_);
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG newCount = InterlockedDecrement(&refCount_);
		// Do not delete this
		return newCount;
	}

	// IDWriteFontCollectionLoader methods
	HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(
		IDWriteFactory* factory,
		const void* collectionKey, // [collectionKeySize] in bytes
		UINT32 collectionKeySize,
		OUT IDWriteFontFileEnumerator** fontFileEnumerator
	) override
	{
		auto* const enumerator = new(std::nothrow) resource_font_file_enumerator(factory, collectionKey, collectionKeySize);
		if (enumerator == nullptr)
		{
			*fontFileEnumerator = nullptr;
			return E_OUTOFMEMORY;
		}
		*fontFileEnumerator = SafeAcquire(enumerator);
		return S_OK;
	}
};

static resource_font_collection_loader font_collection_loader;


static font_renderer_ptr create_font_renderer(IDWriteFactory* dwrite, IDWriteFontCollection* font_collection,
	const wchar_t* font_name, int font_height)
{
	uint32_t index = {};
	BOOL exists = {};
	ComPtr<IDWriteFontFamily> family;
	ComPtr<IDWriteFont> font;
	ComPtr<IDWriteFontFace> font_face;
	ComPtr<IDWriteTextFormat> text_format;

	auto hr = font_collection->FindFamilyName(font_name, &index, &exists);

	if (SUCCEEDED(hr))
	{
		hr = font_collection->GetFontFamily(index, &family);
	}

	if (SUCCEEDED(hr))
	{
		hr = family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			DWRITE_FONT_STYLE_NORMAL, &font);
	}
	if (SUCCEEDED(hr))
	{
		hr = font->CreateFontFace(&font_face);
	}

	if (SUCCEEDED(hr))
	{
		hr = dwrite->CreateTextFormat(
			font_name,
			font_collection,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			static_cast<float>(font_height),
			L"", //locale
			&text_format);
	}

	if (SUCCEEDED(hr))
	{
		df::log(__FUNCTION__, str::format(u8"Created font {}"sv, str::utf16_to_utf8(font_name)));
		return std::make_shared<font_renderer>(dwrite, font_face, text_format, font_height);
	}

	df::log(__FUNCTION__, str::format(u8"Failed to create font {}"sv, str::utf16_to_utf8(font_name)));

	return nullptr;
}


font_renderer_ptr factories::create_icon_font_face(const int font_height)
{
	font_renderer_ptr result;
	ComPtr<IDWriteFontCollection> custom_collection;

	const auto hr = dwrite->CreateCustomFontCollection(
		&font_collection_loader,
		&icon_font_collection_id,
		static_cast<uint32_t>(sizeof(icon_font_collection_id)),
		&custom_collection);

	if (SUCCEEDED(hr))
	{
		result = create_font_renderer(dwrite.Get(), custom_collection.Get(), icon_font_name, font_height);
	}
	else
	{
		df::log(__FUNCTION__, str::format(u8"CreateCustomFontCollection failed {:x}"sv, hr));
	}

	if (!result)
	{
		result = create_font_face(icon_font_name, font_height);
	}

	if (!result)
	{
		result = create_font_face(L"Consolas", font_height);
	}

	return result;
}

font_renderer_ptr factories::create_petscii_font_face(const int font_height)
{
	font_renderer_ptr result;
	ComPtr<IDWriteFontCollection> custom_collection;

	const auto hr = dwrite->CreateCustomFontCollection(
		&font_collection_loader,
		&icon_font_collection_id2,
		static_cast<uint32_t>(sizeof(icon_font_collection_id2)),
		&custom_collection);

	if (SUCCEEDED(hr))
	{
		result = create_font_renderer(dwrite.Get(), custom_collection.Get(), petscii_font_name, font_height);
	}
	else
	{
		df::log(__FUNCTION__, str::format(u8"CreateCustomFontCollection failed {:x}"sv, hr));
	}

	if (!result)
	{
		result = create_font_face(petscii_font_name, font_height);
	}

	if (!result)
	{
		result = create_font_face(L"Consolas", font_height);
	}

	return result;
}

font_renderer_ptr factories::create_font_face(const wchar_t* font_name, const int font_height)
{
	return create_font_renderer(dwrite.Get(), font_collection.Get(), font_name, font_height);
}

void factories::register_fonts()
{
	if (dwrite)
	{
		auto hr = dwrite->RegisterFontFileLoader(&font_loader);
		df::assert_true(SUCCEEDED(hr));
		df::log(__FUNCTION__, str::format(u8"RegisterFontFileLoader {:x}"sv, hr));
		hr = dwrite->RegisterFontCollectionLoader(&font_collection_loader);
		df::log(__FUNCTION__, str::format(u8"RegisterFontCollectionLoader {:x}"sv, hr));
		df::assert_true(SUCCEEDED(hr));
	}
}

void factories::unregister_fonts()
{
	if (dwrite)
	{
		dwrite->UnregisterFontFileLoader(&font_loader);
		dwrite->UnregisterFontCollectionLoader(&font_collection_loader);
	}
}

font_renderer_ptr factories::font_face(const ui::style::font_face type, const int base_font_size)
{
	const auto key = static_cast<uint32_t>(type) | static_cast<uint32_t>(base_font_size << 16);
	const auto found = font_renderers.find(key);

	if (found != font_renderers.end())
	{
		return found->second;
	}

	if (!font_collection)
	{
		dwrite->GetSystemFontCollection(font_collection.GetAddressOf());
	}

	font_renderer_ptr result;

	if (font_collection)
	{
		switch (type)
		{
		case ui::style::font_face::dialog:
			result = create_font_face(L"Calibri", base_font_size);
			break;
		case ui::style::font_face::code:
			result = create_font_face(L"Consolas", base_font_size * 4 / 5);
			break;
		case ui::style::font_face::icons:
			result = create_icon_font_face(base_font_size);
			break;
		case ui::style::font_face::small_icons:
			result = create_icon_font_face(base_font_size * 10 / 16);
			break;
		case ui::style::font_face::title:
			result = create_font_face(L"Calibri", base_font_size * 3 / 2);
			break;
		case ui::style::font_face::mega:
			result = create_font_face(L"Calibri", base_font_size * 9 / 4);
			break;
		case ui::style::font_face::petscii:
			result = create_petscii_font_face(base_font_size);
			break;
		default:
			break;
		}

		if (result)
		{
			font_renderers[key] = result;
		}
	}

	if (!result)
	{
		// backup font
		result = create_font_face(L"Arial", base_font_size);
	}

	if (!result)
	{
		// backup font 2
		result = create_font_face(L"Tahoma", base_font_size);
	}

	return result;
}

font_renderer::font_renderer(const ComPtr<IDWriteFactory>& factory, const ComPtr<IDWriteFontFace>& face,
	ComPtr<IDWriteTextFormat> text_format, const int font_size) : _factory(factory),
	_face(face), _text_format(std::move(text_format)), _font_size(font_size)
{
	_face->GetMetrics(&_metrics);
}

uint32_t font_renderer::calc_line_height() const
{
	if (_metrics.designUnitsPerEm == 0)
	{
		return _font_size; // Fallback to font size if metrics are invalid
	}
	return df::mul_div(_metrics.ascent + _metrics.descent + _metrics.lineGap, _font_size, _metrics.designUnitsPerEm);
}

uint32_t font_renderer::calc_base_line_height() const
{
	if (_metrics.designUnitsPerEm == 0)
	{
		return _font_size; // Fallback to font size if metrics are invalid
	}
	return df::mul_div(_metrics.ascent + _metrics.lineGap, _font_size, _metrics.designUnitsPerEm);
}

calc_text_extent_result font_renderer::calc_glyph_extent(const std::u32string_view code_points)
{
	calc_text_extent_result result;
	
	if (code_points.empty() || code_points.size() > INT_MAX)
	{
		return result; // Return empty result for invalid input
	}
	
	std::vector<uint16_t> glyph_indices(code_points.size());

	if (SUCCEEDED(
		_face->GetGlyphIndices(reinterpret_cast<const uint32_t*>(code_points.data()), static_cast<int>(code_points.size()),
			glyph_indices.data())))
	{
		std::vector<DWRITE_GLYPH_METRICS> glyph_metrics(glyph_indices.size());

		if (SUCCEEDED(
			_face->GetDesignGlyphMetrics(glyph_indices.data(), static_cast<int>(glyph_indices.size()), glyph_metrics.
				data())))
		{
			result.pos.reserve(glyph_metrics.size());

			auto xx = 0;
			auto xmax = 0.0f;

			for (const auto& gm : glyph_metrics)
			{
				const auto x = static_cast<float>(xx /*+ gm.leftSideBearing*/) * _font_size / _metrics.designUnitsPerEm;
				xmax = static_cast<float>(xx + gm.advanceWidth) * _font_size / _metrics.designUnitsPerEm;

				result.pos.emplace_back(
					x,
					0.0f, //static_cast<float>(gm.topSideBearing) * _font_size /  _metrics.designUnitsPerEm,
					static_cast<float>(gm.advanceWidth) * _font_size / _metrics.designUnitsPerEm);

				xx += gm.advanceWidth;
			}

			result.cx = df::round_up(xmax);
			result.cy = df::round_up(
				static_cast<float>(_metrics.ascent + _metrics.descent + _metrics.lineGap) * _font_size / _metrics.
				designUnitsPerEm);
		}
	}

	return result;
}

// https://stackoverflow.com/questions/5995293/get-single-glyph-metrics-net

render_char_result font_renderer::render_glyph(const uint16_t glyph_index, const int spacing, const DWRITE_GLYPH_RUN* glyph_run)
{
	render_char_result result{};

	const auto line_height = df::mul_div(_metrics.ascent + _metrics.descent + _metrics.lineGap, _font_size,
		_metrics.designUnitsPerEm);
	const auto base_line_height = df::mul_div(_metrics.ascent, _font_size, _metrics.designUnitsPerEm);

	DWRITE_GLYPH_METRICS glyph_metrics{};

	if (SUCCEEDED(_face->GetDesignGlyphMetrics(&glyph_index, 1, &glyph_metrics)))
	{
		float glyph_advance = 0;

		DWRITE_GLYPH_OFFSET glyphOffset{};
		glyphOffset.advanceOffset = 0.0f;
		glyphOffset.ascenderOffset = 0.0f;

		DWRITE_GLYPH_RUN run{};
		run.fontFace = glyph_run->fontFace;
		run.fontEmSize = glyph_run->fontEmSize;
		run.glyphCount = 1;
		run.glyphIndices = &glyph_index;
		run.glyphAdvances = &glyph_advance;
		run.glyphOffsets = &glyphOffset;
		run.isSideways = FALSE;
		run.bidiLevel = 0;

		ComPtr<IDWriteGlyphRunAnalysis> analysis;
		if (SUCCEEDED(_factory->CreateGlyphRunAnalysis(
			&run,
			1.0f,
			nullptr,
			DWRITE_RENDERING_MODE_NATURAL,
			DWRITE_MEASURING_MODE_NATURAL,
			0,
			0,
			&analysis)))
		{
			RECT bbox{};

			if (SUCCEEDED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bbox)))
			{
				//const auto char_width = bbox.right - bbox.left;

				RECT bbox2; // { 0, -char_height, char_width, 0 };
				bbox2.left = -spacing;
				bbox2.right = bbox.right + spacing; // -bbox.left) + spacing;
				bbox2.top = -base_line_height;
				bbox2.bottom = bbox2.top + line_height;

				const auto char_width = bbox2.right - bbox2.left;
				const auto char_top = df::mul_div(glyph_metrics.verticalOriginY, _font_size, _metrics.designUnitsPerEm);
				const auto buffer_len = char_width * line_height * 3;

				//df::assert_true(char_top >= 0 && (char_top + char_height) <= line_height);

				if (buffer_len > 0)
				{
					const auto buffer = df::unique_alloc<uint8_t>(buffer_len);

					if (buffer)
					{
						memset(buffer.get(), 0, buffer_len);

						if (SUCCEEDED(
							analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bbox2, buffer.get(), buffer_len
							)))
						{
							result.pixels.resize(char_width * line_height);
							result.cx = char_width;
							result.cy = line_height;
							result.x = char_top;
							result.glyph = glyph_index;

							memset(result.pixels.data(), 0, char_width * line_height);

							for (auto y = 0; y < line_height; ++y)
							{
								const auto dest = result.pixels.data() + (y * char_width);
								auto src = buffer.get() + char_width * 3 * y;

								for (auto x = 0; x < char_width; ++x)
								{
									// Bounds check to prevent buffer overrun
									if ((dest + x) < (result.pixels.data() + result.pixels.size()) &&
										(src + 2) < (buffer.get() + buffer_len))
									{
										dest[x] = (*(src) + *(src + 1) + *(src + 2)) / 3;
									}
									src += 3;
								}
							}
						}
					}
				}
			}
		}
	}

	return result;
}

sizei font_renderer::measure(const std::wstring_view text, ui::style::text_style style, int width, int height)
{
	if (text.empty() || text.size() > INT_MAX)
	{
		return {}; // Return empty result for invalid input
	}
	
	if (height == 0) height = 1000;

	ComPtr<IDWriteTextLayout> layout;
	auto hr = _factory->CreateTextLayout(text.data(), static_cast<int>(text.size()), _text_format.Get(), 0, 0, &layout);
	//auto hr = _factory->CreateGdiCompatibleTextLayout(text.data(), static_cast<int>(text.size()), _text_format.Get(), 0.0f, 0.0f, 1.0f, nullptr, TRUE, &layout);

	if (SUCCEEDED(hr))
	{
		const auto no_wrap = style != ui::style::text_style::multiline && style !=
			ui::style::text_style::multiline_center;
		layout->SetWordWrapping(no_wrap ? DWRITE_WORD_WRAPPING_NO_WRAP : DWRITE_WORD_WRAPPING_WRAP);

		layout->SetMaxWidth(static_cast<float>(width));
		layout->SetMaxHeight(static_cast<float>(height));

		DWRITE_TEXT_METRICS metrics{};
		hr = layout->GetMetrics(&metrics);

		if (SUCCEEDED(hr))
		{
			return { df::round_up(metrics.width), df::round_up(metrics.height) };
		}
	}

	return {};
}


static void configure_layout(const ComPtr<IDWriteTextLayout>& layout, const ui::style::text_style& style)
{
	auto word_wrapping = DWRITE_WORD_WRAPPING_NO_WRAP;
	auto text_alignment = DWRITE_TEXT_ALIGNMENT_LEADING;

	switch (style)
	{
	case ui::style::text_style::none: break;
	case ui::style::text_style::single_line: break;
	case ui::style::text_style::single_line_center:
		text_alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
		break;
	case ui::style::text_style::single_line_far:
		text_alignment = DWRITE_TEXT_ALIGNMENT_TRAILING;
		break;
	case ui::style::text_style::multiline:
		word_wrapping = DWRITE_WORD_WRAPPING_WRAP;
		break;
	case ui::style::text_style::multiline_center:
		text_alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
		word_wrapping = DWRITE_WORD_WRAPPING_WRAP;
		break;
	default:;
	}

	layout->SetWordWrapping(word_wrapping);
	layout->SetTextAlignment(text_alignment);
	layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	const DWRITE_TRIMMING trimmingOpt = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
	layout->SetTrimming(&trimmingOpt, nullptr);
}

void text_layout_impl::update(const std::u8string_view text, ui::style::text_style text_style)
{
	const auto textw = str::utf8_to_utf16(text);

	ComPtr<IDWriteTextLayout> layout;
	const auto hr = _renderer->_factory->CreateTextLayout(textw.data(), static_cast<int>(textw.size()),
		_renderer->_text_format.Get(), 0, 0, &layout);

	if (SUCCEEDED(hr))
	{
		configure_layout(layout, text_style);
		_layout = layout;
	}
	else
	{
		_layout.Reset();
	}
}

sizei text_layout_impl::measure_text(const int cx, const int cy)
{
	df::assert_true(_layout);

	if (_layout)
	{
		_layout->SetMaxWidth(static_cast<float>(cx));
		_layout->SetMaxHeight(static_cast<float>(cy));

		DWRITE_TEXT_METRICS metrics{};
		const auto hr = _layout->GetMetrics(&metrics);

		if (SUCCEEDED(hr))
		{
			return { df::round_up(metrics.width), df::round_up(metrics.height) };
		}
	}

	return {};
}

void font_renderer::draw(ui::draw_context* dc, ID2D1RenderTarget* rt, const std::wstring_view text, const recti bounds,
	ui::style::text_style style, const ui::color color, const ui::color bg,
	const std::vector<ui::text_highlight_t>& highlights)
{
	ComPtr<IDWriteTextLayout> layout;
	auto hr = _factory->CreateTextLayout(text.data(), static_cast<int>(text.size()), _text_format.Get(), 0.0f, 0.0f,
		&layout);
	//auto hr = _factory->CreateGdiCompatibleTextLayout(text.data(), static_cast<int>(text.size()), _text_format.Get(), 0.0f, 0.0f, 1.0f, nullptr, TRUE, &layout);

	if (SUCCEEDED(hr))
	{
		configure_layout(layout, style);
		layout->SetMaxWidth(static_cast<float>(bounds.width()));
		layout->SetMaxHeight(static_cast<float>(bounds.height()));

		for (const auto& h : highlights)
		{
			ComPtr<ID2D1SolidColorBrush> brush;

			hr = rt->CreateSolidColorBrush(
				D2D1::ColorF(h.clr.r, h.clr.g, h.clr.b, h.clr.a),
				&brush);

			if (SUCCEEDED(hr))
			{
				layout->SetDrawingEffect(brush.Get(), { h.offset, h.length });
			}
		}

		if (bg.a > 0.0f)
		{
			DWRITE_TEXT_METRICS m{};
			hr = layout->GetMetrics(&m);

			if (SUCCEEDED(hr))
			{
				const rectd bb{ bounds.left + m.left, bounds.top + m.top, m.width, m.height };
				dc->draw_rounded_rect(bb.round().inflate(2), bg, dc->padding1);
			}
		}

		ComPtr<ID2D1SolidColorBrush> brush;

		hr = rt->CreateSolidColorBrush(
			D2D1::ColorF(color.r, color.g, color.b, color.a),
			&brush
		);

		if (SUCCEEDED(hr))
		{
			rt->DrawTextLayout(
				{ static_cast<float>(bounds.left), static_cast<float>(bounds.top) },
				layout.Get(),
				brush.Get()
			);
		}
	}
}

void font_renderer::draw(ui::draw_context* dc, ID2D1RenderTarget* rt, IDWriteTextLayout* layout, const recti bounds,
	const ui::color color, const ui::color bg)
{
	if (rt && layout)
	{
		layout->SetMaxWidth(static_cast<float>(bounds.width()));
		layout->SetMaxHeight(static_cast<float>(bounds.height()));

		if (bg.a > 0.0f)
		{
			DWRITE_TEXT_METRICS m{};
			const auto hr = layout->GetMetrics(&m);

			if (SUCCEEDED(hr))
			{
				const rectd bb{
					static_cast<double>(bounds.left) + m.left, static_cast<double>(bounds.top) + m.top, m.width,
					m.height
				};
				dc->draw_rounded_rect(bb.round().inflate(2), bg, dc->padding1);
			}
		}

		ComPtr<ID2D1SolidColorBrush> brush;

		const auto hr = rt->CreateSolidColorBrush(
			D2D1::ColorF(color.r, color.g, color.b, color.a),
			&brush
		);

		if (SUCCEEDED(hr))
		{
			rt->DrawTextLayout(
				{ static_cast<float>(bounds.left), static_cast<float>(bounds.top) },
				layout,
				brush.Get()
			);
		}
	}
}


void font_renderer::draw(ui::draw_context* dc, IDWriteTextRenderer* tr, const std::wstring_view text,
	const recti bounds, ui::style::text_style style, const ui::color color, const ui::color bg,
	const std::vector<ui::text_highlight_t>& highlights)
{
	if (text.empty() || text.size() > INT_MAX)
	{
		return; // Nothing to draw for invalid input
	}
	
	ComPtr<IDWriteTextLayout> layout;
	const auto hr = _factory->CreateTextLayout(text.data(), static_cast<int>(text.size()), _text_format.Get(), 0.0f,
		0.0f,
		&layout);
	//auto hr = _factory->CreateGdiCompatibleTextLayout(text.data(), static_cast<int>(text.size()), _text_format.Get(), 0.0f, 0.0f, 1.0f, nullptr, TRUE,  &layout);

	if (SUCCEEDED(hr))
	{
		configure_layout(layout, style);
		draw(dc, tr, layout.Get(), bounds, color, bg);
	}
}

void font_renderer::draw(ui::draw_context* dc, IDWriteTextRenderer* tr, IDWriteTextLayout* layout, const recti bounds,
	const ui::color color, const ui::color bg)
{
	layout->SetMaxWidth(static_cast<float>(bounds.width()));
	layout->SetMaxHeight(static_cast<float>(bounds.height()));

	if (bg.a > 0.0f)
	{
		DWRITE_TEXT_METRICS m{};
		const auto hr = layout->GetMetrics(&m);

		if (SUCCEEDED(hr))
		{
			const rectd bb{ bounds.left + m.left, bounds.top + m.top, m.width, m.height };
			dc->draw_rounded_rect(bb.round().inflate(2), bg, dc->padding1);
		}
	}

	layout->Draw(nullptr, tr,
		static_cast<float>(bounds.left),
		static_cast<float>(bounds.top));
}
