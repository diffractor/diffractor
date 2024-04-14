// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include <Exdisp.h>
#include <Mshtml.h>
#include <Mshtmhst.h>
#include <exdispid.h>

class web_window_impl :
	public IOleClientSite,
	public IOleInPlaceSite,
	public IStorage,
	public DWebBrowserEvents2,
	public IDocHostUIHandler,
	public ui::web_window
{
public:
	web_window_impl(const std::u8string_view start_url, ui::web_events* events) : _events(events), _start_url(start_url)
	{
		_bounds = {-300, -300, 300, 300};
	}

	~web_window_impl() override
	{
		df::assert_true(_ref_count == 0);
	}

	void Create(HWND hwnd)
	{
		_wnd_parent = hwnd;

		if (CreateBrowser())
		{
			Navigate(_start_url);
		}
	}

	DWORD _cp_cookie = 0;

	bool CreateBrowser()
	{
		HRESULT hr;
		hr = OleCreate(CLSID_WebBrowser,
		               IID_IOleObject, OLERENDER_DRAW, nullptr, this, this,
		               &_ole_object);

		if (FAILED(hr))
		{
			MessageBox(nullptr, L"Cannot create oleObject CLSID_WebBrowser",
			           L"Error",
			           MB_ICONERROR);
			return FALSE;
		}

		hr = _ole_object->SetClientSite(this);
		hr = OleSetContainedObject(_ole_object.Get(), TRUE);

		RECT posRect;
		SetRect(&posRect, -300, -300, 300, 300);
		hr = _ole_object->DoVerb(OLEIVERB_INPLACEACTIVATE,
		                         nullptr, this, -1, _wnd_parent, &posRect);
		if (FAILED(hr))
		{
			MessageBox(nullptr, L"oleObject->DoVerb() failed",
			           L"Error",
			           MB_ICONERROR);
			return FALSE;
		}

		hr = _ole_object.As(&_web_browser2);

		if (FAILED(hr))
		{
			MessageBox(nullptr, L"oleObject->QueryInterface(&webBrowser2) failed",
			           L"Error",
			           MB_ICONERROR);
			return FALSE;
		}

		ComPtr<IConnectionPointContainer> pCPC;
		ComPtr<IConnectionPoint> pCP;
		hr = _web_browser2.As(&pCPC);

		if (SUCCEEDED(hr))
		{
			hr = pCPC->FindConnectionPoint(DIID_DWebBrowserEvents2, &pCP);
		}

		if (SUCCEEDED(hr))
		{
			hr = pCP->Advise(static_cast<IDispatch*>(this), &_cp_cookie);
		}

		_web_browser2->put_Silent(VARIANT_TRUE);

		//SetExternalDispatch(static_cast<IDispatch*>(this));

		//if (_web_browser2)
		//{
		//	DispEventAdvise(_browser.Get(), &DIID_DWebBrowserEvents2);
		//	_browser->put_Silent(VARIANT_TRUE);
		//}

		//ComPtr<IAxWinAmbientDispatch> spHost;
		//QueryHost(spHost.GetAddressOf());

		//if (spHost)
		//{
		//	spHost->put_DocHostFlags(DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_THEME | DOCHOSTUIFLAG_SCROLL_NO);
		//}

		return TRUE;
	}

	RECT PixelToHiMetric(const RECT& _rc)
	{
		static bool s_initialized = false;
		static int s_pixelsPerInchX, s_pixelsPerInchY;
		if (!s_initialized)
		{
			const HDC hdc = GetDC(nullptr);
			s_pixelsPerInchX = GetDeviceCaps(hdc, LOGPIXELSX);
			s_pixelsPerInchY = GetDeviceCaps(hdc, LOGPIXELSY);
			ReleaseDC(nullptr, hdc);
			s_initialized = true;
		}

		RECT rc;
		rc.left = MulDiv(2540, _rc.left, s_pixelsPerInchX);
		rc.top = MulDiv(2540, _rc.top, s_pixelsPerInchY);
		rc.right = MulDiv(2540, _rc.right, s_pixelsPerInchX);
		rc.bottom = MulDiv(2540, _rc.bottom, s_pixelsPerInchY);
		return rc;
	}

	// ----- Control methods -----

	void GoBack()
	{
		_web_browser2->GoBack();
	}

	void GoForward()
	{
		_web_browser2->GoForward();
	}

	void Refresh()
	{
		_web_browser2->Refresh();
	}

	void Navigate(std::u8string_view szUrl)
	{
		const bstr_t url(szUrl);
		variant_t flags;
		flags.v.vt = VT_UI4;
		flags.v.uintVal = 0x02u; //navNoHistory
		_web_browser2->Navigate(url, &flags.v, nullptr, nullptr, nullptr);
	}

	// ----- IUnknown -----

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
	                                         void** ppvObject) override
	{
		if (riid == __uuidof(IUnknown))
		{
			(*ppvObject) = static_cast<IOleClientSite*>(this);
		}
		else if (riid == __uuidof(IOleInPlaceSite))
		{
			(*ppvObject) = static_cast<IOleInPlaceSite*>(this);
		}
		else if (riid == __uuidof(IDispatch))
		{
			(*ppvObject) = static_cast<IDispatch*>(this);
		}
		else if (riid == __uuidof(DWebBrowserEvents2))
		{
			(*ppvObject) = static_cast<DWebBrowserEvents2*>(this);
		}
		else if (riid == __uuidof(IDocHostUIHandler))
		{
			(*ppvObject) = static_cast<IDocHostUIHandler*>(this);
		}
		else
		{
			return E_NOINTERFACE;
		}

		AddRef();
		return S_OK;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return ++_ref_count;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		return --_ref_count;
	}

	// ---------- IOleWindow ----------

	HRESULT STDMETHODCALLTYPE GetWindow(
		__RPC__deref_out_opt HWND* phwnd) override
	{
		(*phwnd) = _wnd_parent;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(
		BOOL fEnterMode) override
	{
		return E_NOTIMPL;
	}

	// ---------- IOleInPlaceSite ----------

	HRESULT STDMETHODCALLTYPE CanInPlaceActivate() override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnInPlaceActivate() override
	{
		OleLockRunning(_ole_object.Get(), TRUE, FALSE);
		_ole_object.As(&_ole_in_place_object);
		_ole_in_place_object->SetObjectRects(&_bounds, &_bounds);

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnUIActivate() override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetWindowContext(
		__RPC__deref_out_opt IOleInPlaceFrame** ppFrame,
		__RPC__deref_out_opt IOleInPlaceUIWindow** ppDoc,
		__RPC__out LPRECT lprcPosRect,
		__RPC__out LPRECT lprcClipRect,
		__RPC__inout LPOLEINPLACEFRAMEINFO lpFrameInfo) override
	{
		const HWND hwnd = _wnd_parent;

		if (ppFrame) *ppFrame = nullptr;
		if (ppDoc) *ppDoc = nullptr;

		(*lprcPosRect).left = _bounds.left;
		(*lprcPosRect).top = _bounds.top;
		(*lprcPosRect).right = _bounds.right;
		(*lprcPosRect).bottom = _bounds.bottom;
		*lprcClipRect = *lprcPosRect;

		lpFrameInfo->fMDIApp = false;
		lpFrameInfo->hwndFrame = hwnd;
		lpFrameInfo->haccel = nullptr;
		lpFrameInfo->cAccelEntries = 0;

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Scroll(
		SIZE scrollExtant) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE OnUIDeactivate(
		BOOL fUndoable) override
	{
		return S_OK;
	}

	HWND GetControlWindow() const
	{
		if (_wnd_control != nullptr)
			return _wnd_control;

		if (_ole_in_place_object == nullptr)
			return nullptr;

		_ole_in_place_object->GetWindow(&_wnd_control);
		return _wnd_control;
	}

	HRESULT STDMETHODCALLTYPE OnInPlaceDeactivate() override
	{
		_wnd_control = nullptr;
		_ole_in_place_object.Reset();

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DiscardUndoState() override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE DeactivateAndUndo() override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE OnPosRectChange(
		__RPC__in LPCRECT lprcPosRect) override
	{
		return E_NOTIMPL;
	}

	// ---------- IOleClientSite ----------

	HRESULT STDMETHODCALLTYPE SaveObject() override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE GetMoniker(
		DWORD dwAssign,
		DWORD dwWhichMoniker,
		__RPC__deref_out_opt IMoniker** ppmk) override
	{
		if ((dwAssign == OLEGETMONIKER_ONLYIFTHERE) &&
			(dwWhichMoniker == OLEWHICHMK_CONTAINER))
			return E_FAIL;

		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE GetContainer(
		__RPC__deref_out_opt IOleContainer** ppContainer) override
	{
		return E_NOINTERFACE;
	}

	HRESULT STDMETHODCALLTYPE ShowObject() override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnShowWindow(
		BOOL fShow) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE RequestNewObjectLayout() override
	{
		return E_NOTIMPL;
	}

	// ----- IStorage -----

	HRESULT STDMETHODCALLTYPE CreateStream(
		__RPC__in_string const OLECHAR* pwcsName,
		DWORD grfMode,
		DWORD reserved1,
		DWORD reserved2,
		__RPC__deref_out_opt IStream** ppstm) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE OpenStream(
		const OLECHAR* pwcsName,
		void* reserved1,
		DWORD grfMode,
		DWORD reserved2,
		IStream** ppstm) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE CreateStorage(
		__RPC__in_string const OLECHAR* pwcsName,
		DWORD grfMode,
		DWORD reserved1,
		DWORD reserved2,
		__RPC__deref_out_opt IStorage** ppstg) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE OpenStorage(
		__RPC__in_opt_string const OLECHAR* pwcsName,
		__RPC__in_opt IStorage* pstgPriority,
		DWORD grfMode,
		__RPC__deref_opt_in_opt SNB snbExclude,
		DWORD reserved,
		__RPC__deref_out_opt IStorage** ppstg) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE CopyTo(
		DWORD ciidExclude,
		const IID* rgiidExclude,
		__RPC__in_opt SNB snbExclude,
		IStorage* pstgDest) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE MoveElementTo(
		__RPC__in_string const OLECHAR* pwcsName,
		__RPC__in_opt IStorage* pstgDest,
		__RPC__in_string const OLECHAR* pwcsNewName,
		DWORD grfFlags) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE Commit(
		DWORD grfCommitFlags) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE Revert() override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE EnumElements(
		DWORD reserved1,
		void* reserved2,
		DWORD reserved3,
		IEnumSTATSTG** ppenum) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE DestroyElement(
		__RPC__in_string const OLECHAR* pwcsName) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE RenameElement(
		__RPC__in_string const OLECHAR* pwcsOldName,
		__RPC__in_string const OLECHAR* pwcsNewName) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE SetElementTimes(
		__RPC__in_opt_string const OLECHAR* pwcsName,
		__RPC__in_opt const FILETIME* pctime,
		__RPC__in_opt const FILETIME* patime,
		__RPC__in_opt const FILETIME* pmtime) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE SetClass(
		__RPC__in REFCLSID clsid) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE SetStateBits(
		DWORD grfStateBits,
		DWORD grfMask) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE Stat(
		__RPC__out STATSTG* pstatstg,
		DWORD grfStatFlag) override
	{
		return E_NOTIMPL;
	}

	HRESULT __stdcall
	ShowContextMenu(DWORD dwID, POINT* ppt, IUnknown* pcmdtReserved, IDispatch* pdispReserved) override
	{
		return S_OK;
	}

	HRESULT __stdcall GetHostInfo(DOCHOSTUIINFO* pInfo) override
	{
		pInfo->dwFlags = DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_THEME | DOCHOSTUIFLAG_SCROLL_NO;
		return S_OK;
	}

	HRESULT __stdcall ShowUI(DWORD dwID, IOleInPlaceActiveObject* pActiveObject, IOleCommandTarget* pCommandTarget,
	                         IOleInPlaceFrame* pFrame, IOleInPlaceUIWindow* pDoc) override
	{
		return S_OK;
	}

	HRESULT __stdcall HideUI() override
	{
		return S_OK;
	}

	HRESULT __stdcall UpdateUI() override
	{
		return S_OK;
	}

	HRESULT __stdcall EnableModeless(BOOL fEnable) override
	{
		return S_OK;
	}

	HRESULT __stdcall OnDocWindowActivate(BOOL fActivate) override
	{
		return S_OK;
	}

	HRESULT __stdcall OnFrameWindowActivate(BOOL fActivate) override
	{
		return S_OK;
	}

	HRESULT __stdcall ResizeBorder(LPCRECT prcBorder, IOleInPlaceUIWindow* pUIWindow, BOOL fRameWindow) override
	{
		return S_OK;
	}

	HRESULT __stdcall TranslateAcceleratorW(LPMSG lpMsg, const GUID* pguidCmdGroup, DWORD nCmdID) override
	{
		return S_FALSE;
	}

	HRESULT __stdcall GetOptionKeyPath(LPOLESTR* pchKey, DWORD dw) override
	{
		return S_FALSE;
	}

	HRESULT __stdcall GetDropTarget(IDropTarget* pDropTarget, IDropTarget** ppDropTarget) override
	{
		return E_NOTIMPL;
	}

	HRESULT __stdcall GetExternal(IDispatch** ppDispatch) override
	{
		*ppDispatch = static_cast<IDispatch*>(this);
		return S_OK;
	}

	HRESULT __stdcall TranslateUrl(DWORD dwTranslate, LPWSTR pchURLIn, LPWSTR* ppchURLOut) override
	{
		return E_NOTIMPL;
	}

	HRESULT __stdcall FilterDataObject(IDataObject* pDO, IDataObject** ppDORet) override
	{
		return E_NOTIMPL;
	}


	HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* pctinfo) override
	{
		if (pctinfo) *pctinfo = 0;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo** ppTInfo) override
	{
		return S_OK;
	}

	static constexpr int id_select_place = 2;

	HRESULT STDMETHODCALLTYPE GetIDsOfNames(const IID& riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid,
	                                        DISPID* rgDispId) override
	{
		for (auto i = 0u; i < cNames; i++)
		{
			if (_wcsicmp(rgszNames[i], L"select_place") == 0)
			{
				rgDispId[i] = id_select_place;
			}
			else
			{
				rgDispId[i] = 0;
			}
		}

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Invoke(DISPID dispIdMember, const IID& riid, LCID lcid, WORD wFlags,
	                                 DISPPARAMS* pDispParams, VARIANT* pVarResult, EXCEPINFO* pExcepInfo,
	                                 UINT* puArgErr) override
	{
		if (dispIdMember == DISPID_BEFORENAVIGATE2)
		{
			//pVarResult->vt = VT_BOOL;
			//pVarResult->boolVal = VARIANT_FALSE;

			/* = _events->before_navigate(_bstr_t(pDispParams->rgvarg[5].bstrVal),
				0,
				_bstr_t(pDispParams->rgvarg[3].bstrVal),
				NULL,
				_bstr_t(""sv),
				NULL);*/
			return S_OK;
		}
		if (dispIdMember == DISPID_NAVIGATECOMPLETE2)
		{
			_events->navigation_complete(str::utf16_to_utf8(pDispParams->rgvarg[0].bstrVal));
			return S_OK;
		}
		if (dispIdMember == id_select_place)
		{
			if (pDispParams && pDispParams->cArgs == 2)
			{
				const auto& a1 = pDispParams->rgvarg[2];
				const auto& a2 = pDispParams->rgvarg[1];

				if (a2.vt == VT_R8 &&
					a1.vt == VT_R8)
				{
					// location was selected
					_events->select_place(a1.dblVal, a2.dblVal);
				}
			}

			return S_OK;
		}


		return E_NOTIMPL;
	}

	std::any handle() const override
	{
		return GetControlWindow();
	}

	void destroy() override
	{
		if (_web_browser2)
		{
			_web_browser2->Stop();
		}

		if (_ole_in_place_object)
		{
			_ole_in_place_object->InPlaceDeactivate();
		}

		if (_ole_object)
		{
			_ole_object->Close(OLECLOSE_NOSAVE);
			OleSetContainedObject(_ole_object.Get(), FALSE);
			_ole_object->SetClientSite(nullptr);
			CoDisconnectObject(_ole_object.Get(), 0);
		}

		_ole_object.Reset();
		_ole_in_place_object.Reset();
		_web_browser2.Reset();

		df::assert_true(_ref_count == 0);
	}

	void enable(bool enable) override
	{
	}

	std::u8string window_text() const override
	{
		return {};
	}

	void window_text(const std::u8string_view val) override
	{
	}

	void focus() override
	{
	}

	sizei measure(int cx) const override
	{
		return {};
	}

	bool is_visible() const override
	{
		return IsWindowVisible(GetControlWindow());
	}

	bool has_focus() const override
	{
		return GetFocus() == GetControlWindow();
	}

	recti window_bounds() const override
	{
		RECT rc;
		GetClientRect(GetControlWindow(), &rc);
		return {rc.left, rc.top, rc.right, rc.bottom};
	}

	void window_bounds(const recti bounds, bool visible) override
	{
		_bounds = {bounds.left, bounds.top, bounds.right, bounds.bottom};

		{
			const RECT hiMetricRect = PixelToHiMetric(_bounds);
			SIZEL sz;
			sz.cx = hiMetricRect.right - hiMetricRect.left;
			sz.cy = hiMetricRect.bottom - hiMetricRect.top;
			_ole_object->SetExtent(DVASPECT_CONTENT, &sz);
		}

		if (_ole_in_place_object != nullptr)
		{
			_ole_in_place_object->SetObjectRects(&_bounds, &_bounds);
		}
	}

	void show(bool show) override
	{
		ShowWindow(GetControlWindow(), show ? SW_SHOW : SW_HIDE);
	}

	void options_changed() override
	{
	}

	static ComPtr<IHTMLDocument2> safe_get_doc(ComPtr<IWebBrowser2> browser)
	{
		ComPtr<IDispatch> pDisp;

		if (browser && SUCCEEDED(browser->get_Document(&pDisp)) && pDisp)
		{
			ComPtr<IHTMLDocument2> pDoc;

			if (pDisp && SUCCEEDED(pDisp.As(&pDoc)))
			{
				return pDoc;
			}
		}

		return nullptr;
	}

	void eval_in_browser(const std::u8string_view script) const override
	{
		const auto doc2 = safe_get_doc(_web_browser2);

		if (doc2)
		{
			const bstr_t javascript(L"javascript"sv);
			ComPtr<IHTMLWindow2> pWin2;

			if (SUCCEEDED(doc2->get_parentWindow(&pWin2)))
			{
				try
				{
					const bstr_t bstr_script(script);
					variant_t v;
					pWin2->execScript(bstr_script, javascript, &v.v);
				}
				catch (std::exception& e)
				{
					df::trace(e.what());
				}
				catch (...)
				{
					df::trace(u8"Browser Exception"sv);
				}
			}
		}
	}

protected:
	ComPtr<IOleObject> _ole_object;
	ComPtr<IOleInPlaceObject> _ole_in_place_object;
	ComPtr<IWebBrowser2> _web_browser2;

	std::atomic<int> _ref_count = 0;

	RECT _bounds = {};

	HWND _wnd_parent = nullptr;
	mutable HWND _wnd_control = nullptr;

	ui::web_events* _events = nullptr;
	std::u8string _start_url;
};
