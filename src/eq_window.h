#pragma once

#include "eq.h"
#include "skin_window.h"

namespace wsz {

class EqWindow : public SkinWindow {
public:
    static constexpr int width = 275, height = 116, shade_height = 14;

    void set_double_size(bool on);
    void set_shade(bool on);
    bool shade() const { return m_shade; }
    void save_position();

    // Refreshes from eq state / playback (volume, balance shown in shade mode).
    void refresh() { invalidate(); }

protected:
    SIZE skin_size() const override { return { width, m_shade ? shade_height : height }; }
    int scale() const override { return m_scale; }
    void render(Canvas& c) override;
    bool is_control(POINT p) const override;
    void on_mouse_down(POINT p, bool dbl) override;
    void on_mouse_move(POINT p) override;
    void on_mouse_up(POINT p) override;
    void on_mouse_leave_capture() override;
    void on_wheel(int delta) override;
    void on_caption_dblclick() override { set_shade(!m_shade); }
    void on_position_saved(int x, int y) override;
    void on_visibility_changed(bool visible) override;
    const char* region_name() const override { return m_shade ? nullptr : "equalizer"; }

private:
    enum class Part { none, close, shade, on, autob, presets, preamp, band0, volume = band0 + eq::band_count, balance };
    Part hit(POINT p) const;
    const wchar_t* tooltip_at(POINT p) const override;
    void drag_to(POINT p);
    void show_presets_menu();
    void render_normal(Canvas& c);
    void render_shade(Canvas& c);

    int m_scale = 1;
    bool m_shade = false;
    Part m_pressed = Part::none;
    bool m_inside = false;
};

} // namespace wsz
