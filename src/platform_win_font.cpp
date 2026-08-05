// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: DirectWrite font rendering. Handles text layout, font loading,
// glyph rendering, and text measurement using DirectWrite API.

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
static constexpr auto icon_font_name = L"Segoe MDL2 Assets";

class resource_font_file_stream final : public IDWriteFontFileStream
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

resource_font_file_stream::resource_font_file_stream(const uint32_t resourceID) :
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
	const UINT64 fileOffset,
	const UINT64 fragmentSize,
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

std::string format_guid(REFGUID id)
{
	wchar_t sz[50]; // GUID string is typically 38 characters + null terminator
	const int result = StringFromGUID2(id, sz, _countof(sz));
	if (result == 0)
	{
		return "<invalid-guid>";
	}
	return str::utf16_to_utf8(sz);
}

class resource_font_file_loader final : public IDWriteFontFileLoader
{
public:
	resource_font_file_loader() : refCount_(0)
	{
	}

	virtual ~resource_font_file_loader() = default;

	ULONG refCount_;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
	{
		if (riid == __uuidof(IUnknown) ||
			riid == __uuidof(IDWriteFontFileLoader))
		{
			AddRef();
			*ppvObject = static_cast<IDWriteFontFileLoader*>(this);
			return S_OK;
		}

		df::log(__FUNCTION__, std::format("E_NOINTERFACE {}", format_guid(riid)));
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
		// do not delete
		return newCount;
	}

	HRESULT STDMETHODCALLTYPE CreateStreamFromKey(
		const void* fontFileReferenceKey, // [fontFileReferenceKeySize] in bytes
		const UINT32 fontFileReferenceKeySize,
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
			df::log(__FUNCTION__, std::format("Failed to load font resource ID {}", resource_id));
			delete stream;
			return E_FAIL;
		}

		*fontFileStream = SafeAcquire(stream);

		return S_OK;
	}
};

// Immortal singleton: registered with the shared DWrite factory and referenced (via
// CreateCustomFontFileReference) by custom font faces. It must outlive every DWrite object
// that depends on it, including during C++ static destruction whose order across translation
// units is undefined. It is therefore heap-allocated once and intentionally never freed
// (its Release() is already a deliberate no-op for the same reason).
static resource_font_file_loader& font_loader = *new resource_font_file_loader();

class resource_font_file_enumerator final : public IDWriteFontFileEnumerator
{
public:
	resource_font_file_enumerator() : refCount_(0), _index(0), _factory(nullptr), _collection_key(nullptr),
	                                  _collection_key_size(0)
	{
	}

	explicit resource_font_file_enumerator(IDWriteFactory* factory,
	                                       const void* collectionKey,
	                                       const UINT32 collectionKeySize) : refCount_(0), _index(0),
	                                                                         _factory(factory),
	                                                                         _collection_key(collectionKey),
	                                                                         _collection_key_size(collectionKeySize)
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
			*ppvObject = static_cast<IDWriteFontFileEnumerator*>(this);
			return S_OK;
		}

		df::log(__FUNCTION__, std::format("E_NOINTERFACE {}", format_guid(riid)));

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
		if (_index == 0) font_id = IDF_ICONS;

		if (font_id != 0)
		{
			ComPtr<IDWriteFontFile> current;

			const auto hr = _factory->CreateCustomFontFileReference(
				&font_id,
				sizeof(font_id),
				&font_loader,
				&current);

			df::log(__FUNCTION__, std::format("CreateCustomFontFileReference {:x}", static_cast<uint32_t>(hr)));

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

class resource_font_collection_loader final : public IDWriteFontCollectionLoader
{
public:
	resource_font_collection_loader() : refCount_(0)
	{
	}

	virtual ~resource_font_collection_loader() = default;

	ULONG refCount_;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
	{
		if (riid == __uuidof(IUnknown) ||
			riid == __uuidof(IDWriteFontCollectionLoader))
		{
			AddRef();
			*ppvObject = static_cast<IDWriteFontCollectionLoader*>(this);
			return S_OK;
		}

		df::log(__FUNCTION__, std::format("E_NOINTERFACE {}", format_guid(riid)));
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
		const UINT32 collectionKeySize,
		OUT IDWriteFontFileEnumerator** fontFileEnumerator
	) override
	{
		auto* const enumerator = new(std::nothrow) resource_font_file_enumerator(
			factory, collectionKey, collectionKeySize);
		if (enumerator == nullptr)
		{
			*fontFileEnumerator = nullptr;
			return E_OUTOFMEMORY;
		}
		*fontFileEnumerator = SafeAcquire(enumerator);
		return S_OK;
	}
};

// Immortal singleton (see font_loader above): heap-allocated once and never freed so it
// cannot be destroyed before the DWrite objects that depend on it during process teardown.
static resource_font_collection_loader& font_collection_loader = *new resource_font_collection_loader();


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

	if (FAILED(hr))
	{
		df::log(__FUNCTION__, std::format("Failed to find font family {} - FindFamilyName failed: {:x}",
		                                  str::utf16_to_utf8(font_name), static_cast<uint32_t>(hr)));
		return nullptr;
	}

	if (!exists)
	{
		df::log(__FUNCTION__, std::format("Failed to create font {} - font family not found in collection",
		                                  str::utf16_to_utf8(font_name)));
		return nullptr;
	}

	hr = font_collection->GetFontFamily(index, &family);
	if (FAILED(hr))
	{
		df::log(__FUNCTION__, std::format("Failed to create font {} - GetFontFamily failed: {:x}",
		                                  str::utf16_to_utf8(font_name), static_cast<uint32_t>(hr)));
		return nullptr;
	}

	hr = family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
	                                  DWRITE_FONT_STYLE_NORMAL, &font);
	if (FAILED(hr))
	{
		df::log(__FUNCTION__, std::format("Failed to create font {} - GetFirstMatchingFont failed: {:x}",
		                                  str::utf16_to_utf8(font_name), static_cast<uint32_t>(hr)));
		return nullptr;
	}

	hr = font->CreateFontFace(&font_face);
	if (FAILED(hr))
	{
		df::log(__FUNCTION__, std::format("Failed to create font {} - CreateFontFace failed: {:x}",
		                                  str::utf16_to_utf8(font_name), static_cast<uint32_t>(hr)));
		return nullptr;
	}

	hr = dwrite->CreateTextFormat(
		font_name,
		font_collection,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		static_cast<float>(font_height),
		L"", //locale
		&text_format);

	if (FAILED(hr))
	{
		df::log(__FUNCTION__, std::format("Failed to create font {} - CreateTextFormat failed: {:x}",
		                                  str::utf16_to_utf8(font_name), static_cast<uint32_t>(hr)));
		return nullptr;
	}

	df::log(__FUNCTION__, std::format("Created font {}", str::utf16_to_utf8(font_name)));
	return std::make_shared<font_renderer>(dwrite, font_face, text_format, font_height);
}


font_renderer_ptr factories::create_icon_font_face(const int font_height)
{
	font_renderer_ptr result;
	ComPtr<IDWriteFontCollection> custom_collection;

	const auto hr = dwrite->CreateCustomFontCollection(
		&font_collection_loader,
		&icon_font_collection_id,
		sizeof(icon_font_collection_id),
		&custom_collection);

	if (SUCCEEDED(hr))
	{
		result = create_font_renderer(dwrite.Get(), custom_collection.Get(), icon_font_name, font_height);
	}
	else
	{
		df::log(__FUNCTION__, std::format("CreateCustomFontCollection failed {:x}", static_cast<uint32_t>(hr)));
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

font_renderer_ptr factories::create_font_face(const wchar_t* font_name, const int font_height) const
{
	return create_font_renderer(dwrite.Get(), font_collection.Get(), font_name, font_height);
}

void factories::register_fonts() const
{
	if (dwrite)
	{
		auto hr = dwrite->RegisterFontFileLoader(&font_loader);
		df::assert_true(SUCCEEDED(hr));
		df::log(__FUNCTION__, std::format("RegisterFontFileLoader {:x}", static_cast<uint32_t>(hr)));
		hr = dwrite->RegisterFontCollectionLoader(&font_collection_loader);
		df::log(__FUNCTION__, std::format("RegisterFontCollectionLoader {:x}", static_cast<uint32_t>(hr)));
		df::assert_true(SUCCEEDED(hr));
	}
}

void factories::unregister_fonts() const
{
	if (dwrite)
	{
		dwrite->UnregisterFontFileLoader(&font_loader);
		dwrite->UnregisterFontCollectionLoader(&font_collection_loader);
	}
}

platform::glyph_fallback_probe platform::probe_glyph_fallback(const std::string_view primary_family,
                                                              const std::string_view fallback_family,
                                                              const char32_t code_point)
{
	glyph_fallback_probe result;

	ComPtr<IDWriteFactory> factory;

	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
		reinterpret_cast<IUnknown**>(factory.GetAddressOf()))) || !factory)
	{
		return result;
	}

	ComPtr<IDWriteFontCollection> system_fonts;
	factory->GetSystemFontCollection(system_fonts.GetAddressOf());
	if (!system_fonts) return result;

	const auto make_face = [&system_fonts](const std::string_view family) -> ComPtr<IDWriteFontFace>
	{
		const auto name = utf8_to_utf16(family);
		ComPtr<IDWriteFontFace> face;
		uint32_t index = 0;
		BOOL exists = FALSE;

		if (SUCCEEDED(system_fonts->FindFamilyName(name.c_str(), &index, &exists)) && exists)
		{
			ComPtr<IDWriteFontFamily> font_family;
			ComPtr<IDWriteFont> font;

			if (SUCCEEDED(system_fonts->GetFontFamily(index, font_family.GetAddressOf())) &&
				SUCCEEDED(font_family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
					DWRITE_FONT_STYLE_NORMAL, font.GetAddressOf())))
			{
				font->CreateFontFace(face.GetAddressOf());
			}
		}

		return face;
	};

	const auto primary = make_face(primary_family);
	const auto fallback = make_face(fallback_family);
	if (!primary || !fallback) return result;

	const auto cp = static_cast<uint32_t>(code_point);
	primary->GetGlyphIndices(&cp, 1, &result.primary_glyph);
	fallback->GetGlyphIndices(&cp, 1, &result.fallback_glyph);

	DWRITE_GLYPH_METRICS from_fallback{};
	result.fallback_metrics_ok = SUCCEEDED(
		fallback->GetDesignGlyphMetrics(&result.fallback_glyph, 1, &from_fallback));

	// The latent render_glyph bug: metrics read from the primary face for a glyph index
	// that belongs to the fallback face describe a different glyph, or fail outright.
	DWRITE_GLYPH_METRICS from_primary{};
	const auto primary_hr = primary->GetDesignGlyphMetrics(&result.fallback_glyph, 1, &from_primary);
	result.primary_metrics_differ = FAILED(primary_hr) ||
		from_primary.advanceWidth != from_fallback.advanceWidth ||
		from_primary.verticalOriginY != from_fallback.verticalOriginY;

	result.available = true;
	return result;
}

font_renderer_ptr factories::font_face(const ui::style::font_face type, const int base_font_size)
{
	// Shift as unsigned: `base_font_size << 16` on a signed int is undefined once the size exceeds
	// 32767, which a very large DPI plus the large-font setting can reach.
	const auto key = static_cast<uint32_t>(type) | static_cast<uint32_t>(base_font_size) << 16;
	const auto found = font_renderers.find(key);

	if (found != font_renderers.end())
	{
		return found->second;
	}

	if (!font_collection)
	{
		dwrite->GetSystemFontCollection(font_collection.GetAddressOf());
	}

	if (!font_collection)
	{
		// Could not obtain the system font collection yet - do not cache a negative
		// result so this key can be retried once the collection becomes available.
		return nullptr;
	}

	font_renderer_ptr result;

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
	default:
		break;
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

	// Issue #232: cache the outcome for this key - including a null result - so a
	// known-bad key (requested font and all fallbacks unavailable) is never retried.
	// This stops the per-draw software renderer path from repeatedly re-running the
	// font lookups and flooding the log with "font family not found" messages.
	font_renderers[key] = result;

	return result;
}

platform::font_cache_probe platform::probe_font_cache(const int base_font_size)
{
	font_cache_probe result;

	// A private factories instance: font_face only needs the text engine, so this never
	// touches the app's renderer or its cache.
	factories f;

	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
		reinterpret_cast<IUnknown**>(f.dwrite.GetAddressOf()))) || !f.dwrite)
	{
		return result;
	}

	constexpr auto face = ui::style::font_face::dialog;

	const auto first = f.font_face(face, base_font_size);
	if (!first) return result;

	result.entries_after_first = static_cast<int>(f.font_renderers.size());

	// Issue #232: the repeat must come from the cache, so no family lookup and no log line.
	const auto repeat = f.font_face(face, base_font_size);
	result.same_request_is_cached = repeat == first &&
		static_cast<int>(f.font_renderers.size()) == result.entries_after_first;

	// Issue #189: the size is part of the key, so a Large Font toggle cannot hand back the
	// old-size face (and with it the glyph cache built at that size).
	const auto resized = f.font_face(face, base_font_size * 2);
	result.size_change_is_distinct = resized && resized != first;

	const auto other_face = f.font_face(ui::style::font_face::code, base_font_size);
	result.face_change_is_distinct = other_face && other_face != first;

	f.reset_fonts();
	result.reset_clears_cache = f.font_renderers.empty();

	result.available = true;
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

// https://stackoverflow.com/questions/5995293/get-single-glyph-metrics-net

render_char_result font_renderer::render_glyph(const uint16_t glyph_index, const int spacing,
                                               const DWRITE_GLYPH_RUN* glyph_run) const
{
	render_char_result result{};

	const auto line_height = df::mul_div(_metrics.ascent + _metrics.descent + _metrics.lineGap, _font_size,
	                                     _metrics.designUnitsPerEm);
	const auto base_line_height = df::mul_div(_metrics.ascent, _font_size, _metrics.designUnitsPerEm);

	DWRITE_GLYPH_METRICS glyph_metrics{};

	// Query metrics from the glyph run's OWN face, not the primary font face
	// (_face). For a fallback run (e.g. Hangul rendered via Malgun Gothic) the
	// glyph_index belongs to that fallback face; querying _face here would fail
	// for out-of-range indices (dropping the glyph) or return a different glyph's
	// metrics. The line box below intentionally stays on _face for a consistent
	// baseline across mixed-font text.
	const auto glyph_face = glyph_run->fontFace ? glyph_run->fontFace : _face.Get();

	if (SUCCEEDED(glyph_face->GetDesignGlyphMetrics(&glyph_index, 1, &glyph_metrics)))
	{
		constexpr float glyph_advance = 0;

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
				RECT bbox2; // { 0, -char_height, char_width, 0 };
				bbox2.left = -spacing;
				bbox2.right = bbox.right + spacing; // -bbox.left) + spacing;
				bbox2.top = -base_line_height;
				bbox2.bottom = bbox2.top + line_height;

				const auto char_width = bbox2.right - bbox2.left;
				const auto char_top = df::mul_div(glyph_metrics.verticalOriginY, _font_size, _metrics.designUnitsPerEm);
				const auto buffer_len = char_width * line_height * 3;

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
								const auto dest = result.pixels.data() + y * char_width;
								auto src = buffer.get() + char_width * 3 * y;

								for (auto x = 0; x < char_width; ++x)
								{
									// Bounds check to prevent buffer overrun
									if (dest + x < result.pixels.data() + result.pixels.size() &&
										src + 2 < buffer.get() + buffer_len)
									{
										dest[x] = (*src + *(src + 1) + *(src + 2)) / 3;
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

sizei font_renderer::measure(const std::wstring_view text, const ui::style::text_style style, const int width,
                             int height) const
{
	if (text.empty() || text.size() > INT_MAX)
	{
		return {}; // Return empty result for invalid input
	}

	if (height == 0) height = 1000;

	ComPtr<IDWriteTextLayout> layout;
	auto hr = _factory->CreateTextLayout(text.data(), static_cast<int>(text.size()), _text_format.Get(), 0, 0, &layout);

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
			return {df::round_up(metrics.width), df::round_up(metrics.height)};
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
	default: ;
	}

	layout->SetWordWrapping(word_wrapping);
	layout->SetTextAlignment(text_alignment);
	layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	constexpr DWRITE_TRIMMING trimmingOpt = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
	layout->SetTrimming(&trimmingOpt, nullptr);
}

void text_layout_impl::update(const std::string_view text, const ui::style::text_style text_style)
{
	const auto textw = str::utf8_to_utf16(text);

	for (auto& m : _measured) m.valid = false;
	_measured_next = 0;

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

	for (const auto& m : _measured)
	{
		if (m.valid && m.limit.cx == cx && m.limit.cy == cy) return m.extent;
	}

	if (_layout)
	{
		_layout->SetMaxWidth(static_cast<float>(cx));
		_layout->SetMaxHeight(static_cast<float>(cy));

		DWRITE_TEXT_METRICS metrics{};
		const auto hr = _layout->GetMetrics(&metrics);

		if (SUCCEEDED(hr))
		{
			const sizei extent{df::round_up(metrics.width), df::round_up(metrics.height)};
			_measured[_measured_next] = {{cx, cy}, extent, true};
			_measured_next = (_measured_next + 1) % std::size(_measured);
			return extent;
		}
	}

	return {};
}

void font_renderer::draw(ui::draw_context* dc, IDWriteTextRenderer* tr, const std::wstring_view text,
                         const recti bounds, const ui::style::text_style style, const ui::color color,
                         const ui::color bg,
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
			const rectd bb{bounds.left + m.left, bounds.top + m.top, m.width, m.height};
			dc->draw_rounded_rect(bb.round().inflate(2), bg, dc->padding1);
		}
	}

	layout->Draw(nullptr, tr,
	             static_cast<float>(bounds.left),
	             static_cast<float>(bounds.top));
}
