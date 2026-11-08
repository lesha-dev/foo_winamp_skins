#pragma once

#include <functional>

namespace wsz {

// OLE drop target taking whatever foobar2000 itself takes (files from Explorer, tracks from its own
// panels). foobar resolves the drop asynchronously and applies its incoming-file settings (sorting,
// restrict/exclude masks); the resolved tracks go to the sink returned by `drop`.
class DropTarget {
public:
    using Sink = std::function<void(metadb_handle_list_cref)>;
    struct Handler {
        virtual ~Handler() = default;
        // Cursor over the window (client coordinates): false refuses the drop there.
        virtual bool drop_over(POINT) { return true; }
        virtual void drop_leave() {}
        // The drop happened at the point; return where its tracks should go (empty refuses it).
        virtual Sink drop(POINT) = 0;
    };

    ~DropTarget() { detach(); }
    void attach(HWND wnd, Handler* handler);
    void detach();

private:
    class Impl;
    HWND m_wnd = nullptr;
    Impl* m_impl = nullptr;
};

// The main window's drop: like opening files from Explorer, per Preferences > Shell Integration
// ("Enqueue" as the default action, "Always send new files to playlist").
void shell_drop(metadb_handle_list_cref items);

} // namespace wsz
