#include "stdafx.h"
#include "drop_target.h"

#include <shlobj.h>

namespace wsz {

class DropTarget::Impl : public IDropTarget {
public:
    Impl(HWND wnd, Handler* handler) : m_wnd(wnd), m_handler(handler) {
        CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_helper));
    }
    void detach() { m_handler = nullptr; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IDropTarget) {
            *out = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --m_refs;
        if (!n) {
            if (m_helper) m_helper->Release();
            delete this;
        }
        return n;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* obj, DWORD, POINTL pt, DWORD* effect) override {
        m_accepted = DROPEFFECT_NONE;
        if (m_handler) {
            DWORD e = DROPEFFECT_NONE;
            if (playlist_incoming_item_filter::get()->process_dropped_files_check_ex(obj, &e)) m_accepted = pick(e, *effect);
        }
        *effect = over(pt);
        if (m_helper) {
            POINT p = { pt.x, pt.y };
            m_helper->DragEnter(m_wnd, obj, &p, *effect);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL pt, DWORD* effect) override {
        *effect = over(pt);
        if (m_helper) {
            POINT p = { pt.x, pt.y };
            m_helper->DragOver(&p, *effect);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        if (m_handler) m_handler->drop_leave();
        if (m_helper) m_helper->DragLeave();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* obj, DWORD, POINTL pt, DWORD* effect) override {
        *effect = over(pt);
        if (m_helper) {
            POINT p = { pt.x, pt.y };
            m_helper->Drop(obj, &p, *effect);
        }
        if (!m_handler) return S_OK;
        m_handler->drop_leave();
        if (*effect == DROPEFFECT_NONE) return S_OK;
        Sink sink = m_handler->drop(client(pt));
        if (!sink) {
            *effect = DROPEFFECT_NONE;
            return S_OK;
        }
        playlist_incoming_item_filter_v2::get()->process_dropped_files_async(obj,
            playlist_incoming_item_filter_v2::op_flag_delay_ui, m_wnd, process_locations_notify::create(std::move(sink)));
        return S_OK;
    }

private:
    // One effect out of what foobar suggests and the source allows; tracks are always copied in.
    static DWORD pick(DWORD wanted, DWORD allowed) {
        for (DWORD e : { DWORD(DROPEFFECT_COPY), DWORD(DROPEFFECT_LINK), DWORD(DROPEFFECT_MOVE) })
            if (wanted & allowed & e) return e;
        for (DWORD e : { DWORD(DROPEFFECT_COPY), DWORD(DROPEFFECT_LINK), DWORD(DROPEFFECT_MOVE) })
            if (allowed & e) return e;
        return DROPEFFECT_NONE;
    }
    POINT client(POINTL pt) const {
        POINT p = { pt.x, pt.y };
        ScreenToClient(m_wnd, &p);
        return p;
    }
    DWORD over(POINTL pt) {
        if (!m_handler || m_accepted == DROPEFFECT_NONE) return DROPEFFECT_NONE;
        return m_handler->drop_over(client(pt)) ? m_accepted : DROPEFFECT_NONE;
    }

    ULONG m_refs = 1;
    HWND m_wnd;
    Handler* m_handler;
    IDropTargetHelper* m_helper = nullptr;
    DWORD m_accepted = DROPEFFECT_NONE;
};

void DropTarget::attach(HWND wnd, Handler* handler) {
    detach();
    OleInitialize(nullptr); // no-op when foobar2000 already did it on this thread
    m_impl = new Impl(wnd, handler);
    if (SUCCEEDED(RegisterDragDrop(wnd, m_impl))) m_wnd = wnd;
    else {
        m_impl->Release();
        m_impl = nullptr;
    }
}

void DropTarget::detach() {
    if (!m_impl) return;
    RevokeDragDrop(m_wnd);
    m_impl->detach();
    m_impl->Release();
    m_impl = nullptr;
    m_wnd = nullptr;
}

void shell_drop(metadb_handle_list_cref items) {
    if (!items.get_count()) return;
    auto store = fb2k::configStore::get();
    const bool enqueue = store->getConfigBool("core.shell.equeueDefault");
    auto pm = playlist_manager::get();
    size_t pl = SIZE_MAX;
    if (store->getConfigBool("core.shell.playlistOverride.enabled")) {
        // The playlist name of "Always send new files to playlist"; foobar keeps it as a plain variable.
        pfc::string8 name = store->getConfigString("cfg_var.48C0DAF0-456C-48BB-8EA1-62B3F90B567C", "Default")->c_str();
        if (name.is_empty()) name = "Default";
        pl = pm->find_or_create_playlist(name);
    } else {
        pl = pm->get_active_playlist();
        if (pl == SIZE_MAX) pl = pm->create_playlist_autoname();
    }
    if (pl == SIZE_MAX) return;
    if (enqueue) {
        pm->playlist_add_items(pl, items, pfc::bit_array_false());
        return;
    }
    pm->playlist_undo_backup(pl);
    pm->playlist_clear(pl);
    pm->playlist_add_items(pl, items, pfc::bit_array_false());
    pm->set_active_playlist(pl);
    pm->playlist_set_selection(pl, pfc::bit_array_true(), pfc::bit_array_one(0));
    pm->playlist_set_focus_item(pl, 0);
    pm->playlist_execute_default_action(pl, 0);
}

} // namespace wsz
