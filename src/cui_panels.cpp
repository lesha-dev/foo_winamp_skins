#include "stdafx.h"
#include "cui_panels.h"

#include <columns_ui-sdk/ui_extension.h>

#include <algorithm>
#include <cmath>
#include <optional>

// Columns UI SDK 8.2's host font and colour overrides, declared here when the SDK in 3rdParty predates
// them. Columns UI versions without them never ask for the interface and keep their own fonts and colours.
#if !__has_include(<columns_ui-sdk/window_host_with_appearance_overrides.h>)
namespace uie {
class NOVTABLE window_host_with_appearance_overrides : public window_host {
public:
    [[nodiscard]] virtual ::cui::fonts::font::ptr get_font_override(GUID font_id) = 0;
    [[nodiscard]] virtual ::cui::callback_token::ptr on_font_overrides_changed(::cui::basic_callback::ptr callback) = 0;
    [[nodiscard]] virtual bool get_colour_override(GUID colour_client_id, ::cui::colours::colour_identifier_t colour_id, COLORREF& colour) = 0;
    [[nodiscard]] virtual ::cui::callback_token::ptr on_colour_overrides_changed(::cui::basic_callback::ptr callback) = 0;
    FB2K_MAKE_SERVICE_INTERFACE(window_host_with_appearance_overrides, window_host);
};
const GUID window_host_with_appearance_overrides::class_guid = { 0xafd8b526, 0x8f96, 0x423d, { 0x9d, 0xc5, 0x56, 0x04, 0x7d, 0x2d, 0x35, 0x55 } };
} // namespace uie
#endif

namespace wsz::cui {

namespace fonts = ::cui::fonts;

namespace {

constexpr GUID host_guid = { 0x98267af3, 0xe1e5, 0x4513, { 0x87, 0xfd, 0xa8, 0x57, 0xf5, 0x28, 0x2c, 0x56 } };

// Splitters and layouts need Columns UI's layout editing; toolbars belong in its rebar.
bool offered(const uie::window_ptr& w) {
    const unsigned type = w->get_type();
    if (!(type & (uie::type_panel | uie::type_playlist))) return false;
    if (type & (uie::type_splitter | uie::type_layout)) return false;
    uie::splitter_window_ptr splitter;
    return !w->service_query_t(splitter);
}

bool create_window(const GUID& guid, uie::window_ptr& out) {
    service_enum_t<uie::window> e;
    while (e.next(out))
        if (out->get_extension_guid() == guid) return true;
    out.release();
    return false;
}

float system_scale() {
    HDC dc = GetDC(nullptr);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : USER_DEFAULT_SCREEN_DPI;
    if (dc) ReleaseDC(nullptr, dc);
    return static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI;
}

// A GDI font as a Columns UI font: same face, weight and italics, and the same pixel height (DirectWrite
// sizes are DIPs, scaled by the system DPI). A non-antialiased LOGFONT renders aliased.
class GdiFont : public fonts::font {
public:
    explicit GdiFont(const LOGFONTW& lf) : m_lf(lf) {
        const float px = static_cast<float>(lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight > 0 ? lf.lfHeight : 12);
        m_size = px / system_scale();
        fb2k::std_api_try_get(m_manager); // Columns UI 3.0+
        if (lf.lfQuality == NONANTIALIASED_QUALITY)
            m_rendering = fb2k::service_new<fonts::rendering_options_impl>(DWRITE_RENDERING_MODE_ALIASED, false, true);
        else if (m_manager.is_valid())
            m_rendering = m_manager->get_default_rendering_options();
        if (!m_rendering.is_valid()) m_rendering = fb2k::service_new<fonts::rendering_options_impl>();
    }

    const wchar_t* family_name(fonts::font_family_model model) noexcept override {
        return model == fonts::font_family_model::typographic ? L"" : m_lf.lfFaceName;
    }
    DWRITE_FONT_WEIGHT weight() noexcept override {
        return static_cast<DWRITE_FONT_WEIGHT>(m_lf.lfWeight ? m_lf.lfWeight : FW_NORMAL);
    }
    DWRITE_FONT_STRETCH stretch() noexcept override { return DWRITE_FONT_STRETCH_NORMAL; }
    DWRITE_FONT_STYLE style() noexcept override { return m_lf.lfItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL; }
    float size() noexcept override { return m_size; }
    size_t axis_count() const noexcept override { return 0; }
    fonts::axis_value axis_value(size_t) const override { throw std::out_of_range("no font axes"); }
    LOGFONT log_font() noexcept override { return scaled(system_scale()); }
    LOGFONT log_font_for_dpi(unsigned dpi) noexcept override {
        return scaled(static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI);
    }
    HRESULT create_font_fallback(IDWriteFontFallback** out) noexcept override {
        *out = nullptr;
        return m_manager.is_valid() ? m_manager->get_default_font_fallback(out) : S_FALSE;
    }
    HRESULT create_text_format(IDWriteTextFormat** out, const wchar_t* locale) noexcept override {
        *out = nullptr;
        pfc::com_ptr_t<IDWriteFactory> factory;
        HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                         reinterpret_cast<IUnknown**>(factory.receive_ptr()));
        if (FAILED(hr)) return hr;
        pfc::com_ptr_t<IDWriteTextFormat> format;
        hr = factory->CreateTextFormat(m_lf.lfFaceName, nullptr, weight(), style(), stretch(), m_size,
                                       locale ? locale : L"", format.receive_ptr());
        if (FAILED(hr)) return hr;
        pfc::com_ptr_t<IDWriteFontFallback> fallback;
        if (create_font_fallback(fallback.receive_ptr()) == S_OK && fallback.is_valid()) {
            pfc::com_ptr_t<IDWriteTextFormat1> format1;
            if (SUCCEEDED(format->QueryInterface(__uuidof(IDWriteTextFormat1), reinterpret_cast<void**>(format1.receive_ptr()))))
                format1->SetFontFallback(fallback.get_ptr());
        }
        *out = format.detach();
        return S_OK;
    }
    fonts::rendering_options::ptr rendering_options() noexcept override { return m_rendering; }

private:
    LOGFONT scaled(float scale) const noexcept {
        LOGFONT lf = m_lf;
        lf.lfHeight = -static_cast<LONG>(std::lround(m_size * scale));
        return lf;
    }

    LOGFONTW m_lf;
    float m_size = 12.f;
    fonts::manager_v3::ptr m_manager;
    fonts::rendering_options::ptr m_rendering;
};

} // namespace

std::vector<PanelInfo> list_panels() {
    std::vector<PanelInfo> out;
    uie::window_ptr w;
    service_enum_t<uie::window> e;
    while (e.next(w)) {
        try {
            if (!offered(w)) continue;
            PanelInfo info{ w->get_extension_guid(), {}, {}, (w->get_type() & uie::type_playlist) != 0 };
            w->get_name(info.name);
            w->get_category(info.category);
            if (info.category.is_empty()) info.category = "Other";
            out.push_back(std::move(info));
        } catch (const std::exception&) {}
    }
    std::sort(out.begin(), out.end(), [](const PanelInfo& a, const PanelInfo& b) {
        if (int c = pfc::stricmp_ascii(a.category, b.category)) return c < 0;
        return pfc::stricmp_ascii(a.name, b.name) < 0;
    });
    return out;
}

bool find_panel(const GUID& guid, PanelInfo* out) {
    if (guid == pfc::guid_null) return false;
    for (auto& p : list_panels()) {
        if (p.guid != guid) continue;
        if (out) *out = p;
        return true;
    }
    return false;
}

// Callbacks a hosted panel registered; the returned token unregisters its callback.
class CallbackList {
public:
    ::cui::callback_token::ptr add(::cui::basic_callback::ptr callback) {
        const uint64_t id = ++m_last_id;
        auto list = m_list;
        list->emplace_back(id, std::move(callback));
        return fb2k::service_new<::cui::lambda_callback_token>([list, id] {
            list->erase(std::remove_if(list->begin(), list->end(), [id](const auto& c) { return c.first == id; }), list->end());
        });
    }
    void call() const {
        const auto callbacks = *m_list; // a callback may unregister itself
        for (const auto& c : callbacks) {
            try { (*c.second)(); } catch (const std::exception&) {}
        }
    }

private:
    std::shared_ptr<std::vector<std::pair<uint64_t, ::cui::basic_callback::ptr>>> m_list = std::make_shared<std::vector<std::pair<uint64_t, ::cui::basic_callback::ptr>>>();
    uint64_t m_last_id = 0;
};

// The host the panel talks to: a fixed rectangle laid out by our window, always visible. With Columns UI
// versions that support host appearance overrides, its list panels use our list font and colours when set.
class Host : public uie::window_host_with_appearance_overrides {
public:
    const GUID& get_host_guid() const override { return host_guid; }
    void on_size_limit_change(HWND, unsigned) override {}
    unsigned is_resize_supported(HWND) const override { return 0; }
    bool request_resize(HWND, unsigned, unsigned, unsigned) override { return false; }
    bool override_status_text_create(service_ptr_t<ui_status_text_override>&) override { return false; }
    bool is_visible(HWND) const override { return true; }
    bool is_visibility_modifiable(HWND, bool) const override { return false; }
    bool set_window_visibility(HWND, bool) override { return false; }
    // A single-instance panel moved to another host: it isn't ours to destroy any more.
    void relinquish_ownership(HWND) override { if (m_owner) m_owner->lost(); }

    fonts::font::ptr get_font_override(GUID) override { return m_font; }
    ::cui::callback_token::ptr on_font_overrides_changed(::cui::basic_callback::ptr callback) override {
        return m_font_callbacks.add(std::move(callback));
    }

    bool get_colour_override(GUID, ::cui::colours::colour_identifier_t colour_id, COLORREF& colour) override {
        if (!m_colours) return false;
        switch (colour_id) {
        case ::cui::colours::colour_text:
        case ::cui::colours::colour_active_item_frame: colour = m_colours->text; return true;
        case ::cui::colours::colour_selection_text:
        case ::cui::colours::colour_inactive_selection_text: colour = m_colours->selection_text; return true;
        case ::cui::colours::colour_background: colour = m_colours->background; return true;
        case ::cui::colours::colour_selection_background:
        case ::cui::colours::colour_inactive_selection_background: colour = m_colours->selection_background; return true;
        default: return false;
        }
    }
    ::cui::callback_token::ptr on_colour_overrides_changed(::cui::basic_callback::ptr callback) override {
        return m_colour_callbacks.add(std::move(callback));
    }

    void set_font(fonts::font::ptr font) {
        m_font = std::move(font);
        m_font_callbacks.call();
    }

    void set_colours(const ListColours* colours) {
        std::optional<ListColours> next;
        if (colours) next = *colours;
        if (next == m_colours) return;
        m_colours = next;
        m_colour_callbacks.call();
    }

    struct Owner { virtual void lost() = 0; };
    Owner* m_owner = nullptr;

private:
    fonts::font::ptr m_font;
    std::optional<ListColours> m_colours;
    CallbackList m_font_callbacks;
    CallbackList m_colour_callbacks;
};

struct Panel::Impl : Host::Owner {
    GUID guid;
    uie::window_ptr window;
    service_ptr_t<Host> host;
    HWND wnd = nullptr;

    void lost() override {
        window.release();
        wnd = nullptr;
    }
};

Panel::Panel(const GUID& guid) : m(std::make_unique<Impl>()) {
    m->guid = guid;
    m->host = fb2k::service_new<Host>();
    m->host->m_owner = m.get();
}

Panel::~Panel() { release(); }

bool Panel::create(HWND parent, const std::vector<uint8_t>& config) {
    if (m->wnd) return true;
    uie::window_ptr w;
    if (!create_window(m->guid, w)) {
        FB2K_console_formatter() << "foo_winamp_skins: Columns UI panel " << pfc::print_guid(m->guid) << " is not available";
        return false;
    }
    try {
        if (!w->is_available(m->host)) {
            FB2K_console_formatter() << "foo_winamp_skins: Columns UI panel " << pfc::print_guid(m->guid) << " is already in use";
            return false;
        }
        if (!config.empty()) {
            try {
                stream_reader_memblock_ref reader(config.data(), config.size());
                w->set_config(&reader, config.size(), fb2k::noAbort);
            } catch (const std::exception&) {} // unreadable settings: the panel's defaults
        }
        RECT rc{};
        GetClientRect(parent, &rc);
        m->wnd = w->create_or_transfer_window(parent, m->host, ui_helpers::window_position_t(rc));
    } catch (const std::exception& e) {
        FB2K_console_formatter() << "foo_winamp_skins: Columns UI panel failed: " << e.what();
        m->wnd = nullptr;
    }
    if (!m->wnd) return false;
    m->window = w;
    ShowWindow(m->wnd, SW_SHOWNA);
    return true;
}

HWND Panel::hwnd() const { return m->wnd; }

void Panel::set_list_font(HFONT font) {
    LOGFONTW lf{};
    if (font && GetObjectW(font, sizeof(lf), &lf) == sizeof(lf)) {
        try {
            m->host->set_font(fb2k::service_new<GdiFont>(lf));
            return;
        } catch (const std::exception&) {}
    }
    m->host->set_font({});
}

void Panel::set_list_colours(const ListColours* colours) { m->host->set_colours(colours); }

bool Panel::config(std::vector<uint8_t>& out) const {
    if (!m->window.is_valid()) return false;
    try {
        stream_writer_memblock writer;
        m->window->get_config(&writer, fb2k::noAbort);
        out.assign(writer.m_data.get_ptr(), writer.m_data.get_ptr() + writer.m_data.get_size());
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void Panel::release() {
    if (m->window.is_valid()) {
        try { m->window->destroy_window(); } catch (const std::exception&) {}
    }
    m->window.release();
    m->wnd = nullptr;
}

} // namespace wsz::cui
