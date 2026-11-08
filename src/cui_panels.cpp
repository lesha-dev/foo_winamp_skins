#include "stdafx.h"
#include "cui_panels.h"

#define CUI_SDK_DWRITE_ENABLED 0
#include <columns_ui-sdk/ui_extension.h>

#include <algorithm>

namespace wsz::cui {

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

// The host the panel talks to: a fixed rectangle laid out by our window, always visible.
class Host : public uie::window_host {
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

    struct Owner { virtual void lost() = 0; };
    Owner* m_owner = nullptr;
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

Panel::Panel(const GUID& guid) : m(std::make_unique<Impl>()) { m->guid = guid; }

Panel::~Panel() { release(); }

bool Panel::create(HWND parent, const std::vector<uint8_t>& config) {
    if (m->wnd) return true;
    uie::window_ptr w;
    if (!create_window(m->guid, w)) {
        FB2K_console_formatter() << "foo_winamp_skins: Columns UI panel " << pfc::print_guid(m->guid) << " is not available";
        return false;
    }
    if (!m->host.is_valid()) {
        m->host = fb2k::service_new<Host>();
        m->host->m_owner = m.get();
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
