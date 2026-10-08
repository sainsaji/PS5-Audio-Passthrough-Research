// Ported from EVO Player (GPL-3.0), Surround Sound Studio (#106).
/*
 * evo_hui_surround.hpp - Surround Sound Studio (#106) on ps5-homebrew-ui.
 */
#ifndef EVO_HUI_SURROUND_HPP
#define EVO_HUI_SURROUND_HPP

#include "surround/surround_params.h"

#include "gfx/draw_list.hpp"
#include "ui/fonts.hpp"

#include <string>

namespace evo::kit
{

/* What EVO's kit screens share, reduced to what this view uses. */
struct Palette
{
    hui::gfx::Color bg_top = hui::gfx::Color::rgb(0x0a0d1c);
    hui::gfx::Color bg_bottom = hui::gfx::Color::rgb(0x04050b);
    hui::gfx::Color surface = hui::gfx::Color::rgb(0xffffff, 0.06f);
    hui::gfx::Color surface_sel = hui::gfx::Color::rgb(0xffffff, 0.14f);
    hui::gfx::Color border = hui::gfx::Color::rgb(0xffffff, 0.12f);
    hui::gfx::Color accent = hui::gfx::Color::rgb(0x4f8cff);
    hui::gfx::Color accent_soft = hui::gfx::Color::rgb(0x2a4c99);
    hui::gfx::Color accent_alt = hui::gfx::Color::rgb(0xb06cff);
    hui::gfx::Color text = hui::gfx::Color::rgb(0xffffff);
    hui::gfx::Color text_muted = hui::gfx::Color::rgb(0xffffff, 0.66f);
    hui::gfx::Color text_faint = hui::gfx::Color::rgb(0xffffff, 0.42f);
};

struct Context
{
    const hui::ui::Fonts &fonts;
    const Palette &palette;
    float time = 0.0f;
};

class SurroundScreen
{
  public:
    void set(const evo_rmlui_surround_params_t &params);
    void set_display_120(bool on) { is_120hz_ = on; }
    void enter() {}
    void update(float) {}
    void draw(hui::gfx::DrawList &list, const Context &ctx) const;

  private:
    void draw_stage(hui::gfx::DrawList &list, const Context &ctx) const;
    void draw_left(hui::gfx::DrawList &list, const Context &ctx) const;
    void draw_right(hui::gfx::DrawList &list, const Context &ctx) const;
    void draw_calibration(hui::gfx::DrawList &list, const Context &ctx) const;
    void draw_telemetry(hui::gfx::DrawList &list, const Context &ctx) const;

    std::string label_of(int ch) const;
    std::string name_of(int ch) const;

    evo_rmlui_surround_params_t p_{};
    std::string names_[EVO_RMLUI_SURROUND_SPEAKERS];
    std::string labels_[EVO_RMLUI_SURROUND_SPEAKERS];
    std::string cal_message_;
    bool is_120hz_ = false;
};

} // namespace evo::kit

#endif /* EVO_HUI_SURROUND_HPP */
