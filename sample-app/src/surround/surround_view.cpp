// Ported from EVO Player (GPL-3.0), Surround Sound Studio (#106).
/*
 * evo_hui_surround.cpp - Surround Sound Studio (#106) on ps5-homebrew-ui.
 *
 * Same state and same layout as assets/rml/surround.rml, drawn as shapes:
 * actions + monitor on the left, the 2.5D room stage in the middle, levels /
 * modes / output (or the calibration report) on the right.
 */
#include "surround/surround_view.hpp"
#include "ui/glyphs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace evo::kit
{

using ::hui::gfx::Align;
using ::hui::gfx::Color;
using ::hui::gfx::DrawList;
using ::hui::gfx::Rect;
namespace ui = ::hui::ui;

namespace
{
const Color kWhite = Color::rgb(0xffffff);
const Color kCyan = Color::rgb(0x00cdff);
const Color kYellow = Color::rgb(0xffcd00);
const Color kRed = Color::rgb(0xff4d4d);
const Color kPanel = Color::rgb(0x0b1322);
const Color kEdge = Color::rgb(0x1f3150);

constexpr float kOx = 152.0f;                 /* pane origin */
constexpr float kTop = 146.0f;
constexpr Rect kStage{kOx + 440.0f, kTop, 836.0f, 694.0f};
constexpr float kCx = kStage.x + 418.0f;      /* the sweet spot */
constexpr float kCy = kStage.y + 360.0f;

const char *kActionLabel[EVO_RMLUI_SURROUND_ACTIONS] = {
    "3D SOUND FIELD", "AUTO CALIBRATION (MIC)", "SPATIAL ORB - FREE ROAM", "360 ROTATION SWEEP",
    "AUTO TEST 5.1",  "AUTO TEST 7.1",          "SPEAKER LAYOUT",          "SILENCE / STOP"};
const char *kActionSub[EVO_RMLUI_SURROUND_ACTIONS] = {
    "INTERACTIVE SPATIAL AUDIO TEST", "DUALSENSE MICROPHONE CALIBRATION", "POSITIONING TEST",
    "CIRCULAR SURROUND TEST",         "6-CHANNEL SEQUENCE",               "8-CHANNEL SEQUENCE",
    "",                               "STOP ALL AUDIO OUTPUT"};

std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char *f, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

void label(DrawList &list, const ui::FontRef &font, const std::string &s, float x, float y, float size,
           Color c, Align a = Align::left, float tracking = 0.0f)
{
    ui::text(list, font, s, x, y, size, c, a, tracking);
}
} // namespace

void SurroundScreen::set(const evo_rmlui_surround_params_t &p)
{
    p_ = p;
    for (int i = 0; i < EVO_RMLUI_SURROUND_SPEAKERS; ++i)
    {
        names_[i] = (i < p.speaker_count && p.speakers[i].name) ? p.speakers[i].name : "";
        labels_[i] = (i < p.speaker_count && p.speakers[i].label) ? p.speakers[i].label : "";
        p_.speakers[i].name = names_[i].c_str();
        p_.speakers[i].label = labels_[i].c_str();
    }
    cal_message_ = p.cal_message ? p.cal_message : "";
    p_.cal_message = cal_message_.c_str();
}

std::string SurroundScreen::label_of(int ch) const
{
    return (ch >= 0 && ch < p_.speaker_count && ch < EVO_RMLUI_SURROUND_SPEAKERS) ? labels_[ch] : "-";
}

std::string SurroundScreen::name_of(int ch) const
{
    return (ch >= 0 && ch < p_.speaker_count && ch < EVO_RMLUI_SURROUND_SPEAKERS) ? names_[ch] : "-";
}

void SurroundScreen::draw(DrawList &list, const Context &ctx) const
{
    const ui::Fonts &fonts = ctx.fonts;
    const bool v_cal = p_.view_mode == EVO_SURROUND_VIEW_CALIBRATION;
    const bool v_orb = p_.view_mode == EVO_SURROUND_VIEW_ORB;
    const bool is51 = p_.is_51_layout != 0;

    /* header */
    list.rounded_rect({kOx, 46, 5, 62}, 2, is_120hz_ ? kCyan : ctx.palette.accent);
    label(list, fonts.display, "SURROUND SOUND STUDIO", kOx + 24, 86, 34, kWhite, Align::left, 0.5f);
    label(list, fonts.semibold,
          fmt("%s \xC2\xB7 %s \xC2\xB7 48 kHz \xC2\xB7 %s", is51 ? "5.1 SYSTEM" : "7.1 SYSTEM",
              is51 ? "6 CHANNELS" : "8 CHANNELS", is_120hz_ ? "120 HZ OUTPUT" : "60 HZ OUTPUT"),
          kOx + 24, 110, 15, is_120hz_ ? kCyan : ctx.palette.text_muted, Align::left, 1.2f);

    draw_left(list, ctx);
    draw_stage(list, ctx);
    if (v_cal)
        draw_calibration(list, ctx);
    else
        draw_right(list, ctx);
    draw_telemetry(list, ctx);

    /* footer hints */
    ui::Hint hints[6];
    int n = 0;
    auto add = [&](ui::Button b, const char *t, ui::Button second = ui::Button::none) {
        hints[n++] = ui::Hint{b, t, second};
    };
    if (v_cal)
    {
        add(ui::Button::cross, p_.cal_phase == EVO_SURROUND_CAL_COMPLETE ? "Next" : "Start");
        add(ui::Button::triangle, "Repeat");
        add(ui::Button::circle, "Back");
    }
    else if (v_orb)
    {
        add(ui::Button::cross, p_.tone_follow ? (p_.field_playing ? "Pause music" : "Play music") : "Test tone");
        add(ui::Button::left_stick, "Move source");
        add(ui::Button::l2, "Height", ui::Button::r2);
        add(ui::Button::triangle, "Change mode");
        add(ui::Button::square, "Reset position");
        add(ui::Button::circle, "Back");
    }
    else
    {
        add(ui::Button::cross, p_.selected_item >= EVO_RMLUI_SURROUND_ACTIONS ? "Test tone" : "Select");
        add(ui::Button::dpad, "2D navigate");
        add(ui::Button::square, "5.1 / 7.1");
        add(ui::Button::triangle, "Silence");
    }
    ui::HintLayout layout;
    layout.size = 30;
    layout.text_size = 20;
    layout.cy = 1030.0f;
    ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), hints, n, 1824.0f, true, layout);
}

/* ------------------------------------------------------------------ left */

void SurroundScreen::draw_left(DrawList &list, const Context &ctx) const
{
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &pal = ctx.palette;
    const bool v_cal = p_.view_mode == EVO_SURROUND_VIEW_CALIBRATION;
    const bool v_orb = p_.view_mode == EVO_SURROUND_VIEW_ORB;
    const bool v_stage = !v_orb && !v_cal;
    const bool is51 = p_.is_51_layout != 0;
    const int n_spk = std::min(p_.speaker_count, (int)EVO_RMLUI_SURROUND_SPEAKERS);
    const int running = v_cal ? 1 : (v_orb ? (p_.tone_follow ? 0 : 2) : -1);

    label(list, fonts.semibold, "TESTS", kOx, kTop - 8, 13, pal.text_faint, Align::left, 1.6f);
    for (int i = 0; i < EVO_RMLUI_SURROUND_ACTIONS; ++i)
    {
        Rect r{kOx, kTop + i * 58.0f, 420, 52};
        const bool focused = v_stage && !p_.rail_focused && p_.selected_item == i;
        const bool live = i == running;
        list.bordered_rect(r, 12, focused ? Color::rgb(0xffcd00, 0.10f) : kPanel, focused ? 2.0f : 1.0f,
                           focused ? kYellow : (live ? kCyan : kEdge));
        label(list, fonts.semibold, kActionLabel[i], r.x + 18, r.y + 22, 17, focused || live ? kWhite : pal.text_muted);
        std::string sub = (i == 6) ? std::string(is51 ? "CURRENT: 5.1 SURROUND" : "CURRENT: 7.1 SURROUND")
                                   : std::string(kActionSub[i]);
        label(list, fonts.regular, sub, r.x + 18, r.y + 42, 12, focused ? kYellow : (live ? kCyan : pal.text_faint),
              Align::left, 0.8f);
    }

    /* monitor */
    std::string title, l1, l2, status;
    bool live = false;
    auto lab = [&](int ch) { return label_of(ch); };
    if (v_cal)
    {
        title = "AUTO CALIBRATION";
        l1 = "RECEIVER: DUALSENSE MIC @ SWEET SPOT";
        l2 = fmt("SWEEPS: %d SPEAKERS + FRONT LEFT DRIFT CHECK", p_.cal_total);
        static const char *kPhase[] = {"READY", "MIC CHECK", "MEASURING", "ANALYZING", "COMPLETE", "FAILED"};
        const int ph = std::max(0, std::min(5, p_.cal_phase));
        status = fmt("STATUS: [ %s ]", kPhase[ph]);
        live = ph >= 1 && ph <= 3;
    }
    else if (v_orb)
    {
        static const char *kFlight[3] = {"MANUAL", "ORBIT", "FLYBY"};
        title = p_.tone_follow ? "3D SOUND FIELD" : "SPATIAL ORB - FREE ROAM";
        l1 = fmt("FLIGHT MODE: %s", kFlight[std::max(0, std::min(2, p_.flight_mode))]);
        l2 = (p_.nearest_channel >= 0 && p_.nearest_channel < n_spk)
                 ? fmt("LOUDEST: %s (%s) \xC2\xB7 %d%% GAIN", name_of(p_.nearest_channel).c_str(),
                       lab(p_.nearest_channel).c_str(), (int)(p_.proximity[p_.nearest_channel] * 100.0f + 0.5f))
                 : "NO SPEAKERS IN THIS LAYOUT";
        if (p_.tone_follow)
        {
            live = p_.field_playing != 0;
            status = live ? "STATUS: [ MUSIC PANNING ACROSS THE SPEAKERS ]" : "STATUS: [ PAUSED \xC2\xB7 CROSS TO PLAY ]";
        }
        else
        {
            live = p_.active_channel >= 0;
            status = live ? fmt("STATUS: [ TONE ON %s ]", lab(p_.active_channel).c_str())
                          : "STATUS: [ CROSS PLAYS THE LOUDEST SPEAKER ]";
        }
    }
    else if (p_.selected_item >= EVO_RMLUI_SURROUND_ACTIONS)
    {
        const int ch = p_.selected_item - EVO_RMLUI_SURROUND_ACTIONS;
        title = fmt("%s (%s)", name_of(ch).c_str(), lab(ch).c_str());
        l1 = fmt("TEST TONE: %.0f HZ \xC2\xB7 CHANNEL %d", ch < n_spk ? p_.speakers[ch].hz : 0.0, ch);
        l2 = "D-PAD MOVES BETWEEN SPEAKERS BY ROOM POSITION";
        live = p_.active_channel == ch;
        status = live ? "STATUS: [ EMITTING TONE ]" : "STATUS: [ CROSS TO TEST ]";
    }
    else
    {
        const int a = std::max(0, std::min(EVO_RMLUI_SURROUND_ACTIONS - 1, p_.selected_item));
        title = kActionLabel[a];
        l1 = (a == 6) ? std::string(is51 ? "LAYOUT: 5.1 (6 CHANNELS)" : "LAYOUT: 7.1 (8 CHANNELS)")
                      : std::string(kActionSub[a]);
        l2 = "D-PAD RIGHT: SELECT A SPEAKER ON THE STAGE";
        live = p_.surround_mode == 1;
        const int now = p_.sweep_rotation ? p_.nearest_channel : p_.active_channel;
        status = live ? fmt("STATUS: [ %s \xC2\xB7 %s ]", p_.sweep_rotation ? "SWEEPING" : "RUNNING", lab(now).c_str())
                      : "STATUS: [ IDLE ]";
    }
    Rect m{kOx, kTop + 8 * 58.0f + 14, 420, 694 - 8 * 58.0f - 14};
    list.bordered_rect(m, 14, kPanel, 1.0f, live ? kCyan : kEdge);
    label(list, fonts.semibold, title, m.x + 18, m.y + 34, 18, kWhite);
    label(list, fonts.regular, l1, m.x + 18, m.y + 62, 13, pal.text_muted, Align::left, 0.6f);
    label(list, fonts.regular, l2, m.x + 18, m.y + 84, 13, pal.text_muted, Align::left, 0.6f);
    label(list, fonts.semibold, status, m.x + 18, m.y + 112, 13, live ? kCyan : pal.text_faint, Align::left, 0.8f);
}

/* ----------------------------------------------------------------- stage */

void SurroundScreen::draw_stage(DrawList &list, const Context &ctx) const
{
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &pal = ctx.palette;
    const bool v_cal = p_.view_mode == EVO_SURROUND_VIEW_CALIBRATION;
    const bool v_orb = p_.view_mode == EVO_SURROUND_VIEW_ORB;
    const bool v_stage = !v_orb && !v_cal;
    const bool measuring = v_cal && p_.cal_phase == EVO_SURROUND_CAL_MEASURING;
    const int n_spk = std::min(p_.speaker_count, (int)EVO_RMLUI_SURROUND_SPEAKERS);
    const int emitting = v_cal ? (measuring ? p_.cal_channel : -1) : p_.active_channel;
    const double ppm = p_.px_per_m > 1.0f ? p_.px_per_m : 100.0f;

    list.bordered_rect(kStage, 22, Color::rgb(0x070d18), 1.0f, Color::rgb(0x1e3352));
    list.push_clip(kStage);

    /* perspective floor */
    {
        const int rows = 12, cols = 11;
        const float y0 = kStage.y + 64.0f, y1 = kStage.y + 694.0f - 10.0f;
        auto row_y = [&](int i) { return y0 + (y1 - y0) * std::pow((float)i / (rows - 1), 1.6f); };
        auto col_x = [&](int j, float t) { return kCx + (j - cols / 2) * (44.0f + 62.0f * t); };
        const Color grid = Color::rgb(0x1e3352, 0.55f);
        for (int i = 0; i < rows; ++i)
        {
            const float t = (float)i / (rows - 1);
            list.line(col_x(0, t), row_y(i), col_x(cols - 1, t), row_y(i), 1.0f, grid);
        }
        for (int j = 0; j < cols; ++j)
            list.line(col_x(j, 0.0f), row_y(0), col_x(j, 1.0f), row_y(rows - 1), 1.0f, grid);
    }

    for (int r = 3; r >= 1; --r)
    {
        const float rad = (float)(r * ppm);
        list.ring(kCx, kCy, rad, 1.0f, Color::rgb(0x2a4a70, 0.6f));
        label(list, fonts.regular, fmt("%d.0 m", r), kCx + 6, kCy - rad + 16, 11, pal.text_faint);
    }

    /* display bar + listener */
    list.rounded_rect({kStage.x + 238, kStage.y + 14, 360, 10}, 5, kCyan);
    label(list, fonts.semibold, "FRONT DISPLAY", kStage.x + 418, kStage.y + 46, 11, Color::rgb(0x54708f),
          Align::center, 1.4f);
    list.circle(kCx, kCy - 4.0f, 26.0f, Color::rgb(0x3d5a85, 0.35f));
    list.circle(kCx, kCy - 4.0f, 11.0f, v_cal ? kCyan : Color::rgb(0xe6f1ff));
    list.rounded_rect({kCx - 18, kCy + 8, 36, 20}, 8, v_cal ? kCyan : Color::rgb(0xe6f1ff));
    label(list, fonts.regular, "0-DEG REFERENCE", kCx, kCy + 66, 11, pal.text_faint, Align::center, 1.0f);

    /* speakers */
    for (int i = 0; i < n_spk; ++i)
    {
        const evo_rmlui_surround_speaker_t &spk = p_.speakers[i];
        if (spk.hidden)
            continue;
        const bool tone = emitting == spk.ch;
        const float gain = p_.proximity[i];
        const float level = tone ? 1.0f : (v_cal ? 0.0f : gain);
        const bool selected = v_stage && !p_.rail_focused && p_.selected_item == spk.item_idx;
        const float sx = kCx + spk.dx, sy = kCy + spk.dy;

        if (tone)
        { /* one wavefront travelling to the seat */
            const float ph = std::fmod(p_.anim_time * 1.2f + i * 0.13f, 1.0f);
            const float dx = kCx - sx, dy = kCy - sy;
            const float dist = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
            const float travel = 40.0f + (dist * 0.75f - 40.0f) * ph;
            list.ring(sx + dx / dist * travel, sy + dy / dist * travel, (40.0f + 70.0f * ph) * 0.5f, 2.0f,
                      kCyan.with_alpha(0.8f * (1.0f - ph)));
        }

        Rect cab{sx - 62, sy - 31, 124, 62};
        if (level > 0.1f || tone)
            list.glow(cab, 14, 18, kCyan.with_alpha(tone ? 0.45f : 0.3f * level));
        list.bordered_rect(cab, 12, tone ? Color::rgb(0x162846) : (level > 0.5f ? Color::rgb(0x111e33) : Color::rgb(0x0d1626)),
                           selected ? 2.0f : 1.0f,
                           selected ? kYellow : (tone ? kCyan : (level > 0.1f ? kCyan.with_alpha(0.2f + 0.7f * level) : Color::rgb(0x223751))));
        /* icon */
        const Color body = tone ? kCyan : (level > 0.5f ? Color::rgb(0x9fdcf2) : Color::rgb(0x6f88aa));
        const Color cone = tone ? kWhite : (level > 0.3f ? kCyan : Color::rgb(0x6f88aa));
        list.bordered_rect({cab.x + 10, cab.y + 10, 30, 42}, 6, Color::rgb(0x000000, 0.0f), 2.0f, body);
        list.circle(cab.x + 25, cab.y + 34, 8.0f, cone);
        list.circle(cab.x + 25, cab.y + 20, 3.5f, cone);
        label(list, fonts.semibold, labels_[i], cab.x + 48, cab.y + 28, 17, (tone || level > 0.6f) ? kCyan : kWhite);
        std::string sub = fmt("%.0f Hz", spk.hz);
        if (v_cal && p_.cal_phase == EVO_SURROUND_CAL_COMPLETE)
            sub = !p_.cal_detected[i] ? "MISSED" : (spk.ch == 3) ? fmt("%.1f ms", p_.cal_delay_ms[i])
                                                                  : fmt("%+.1f dB", p_.cal_trim_db[i]);
        else if (v_orb)
            sub = fmt("%d%%", (int)(gain * 100.0f + 0.5f));
        label(list, fonts.regular, sub, cab.x + 48, cab.y + 46, 12, tone ? kCyan : pal.text_faint);
        /* VU */
        const int lit = level <= 0.02f ? 0 : level < 0.25f ? 1 : level < 0.55f ? 2 : level < 0.8f ? 3 : 4;
        static const Color kVu[5] = {Color::rgb(0x18263a), Color::rgb(0x3ddc97), Color::rgb(0x00cdff),
                                     Color::rgb(0xffb020), Color::rgb(0xff4d4d)};
        for (int b = 1; b <= 4; ++b)
            list.rounded_rect({cab.x + cab.w - 14, cab.y + cab.h - 8 - b * 11.0f, 6, 8}, 2,
                              b <= lit ? kVu[b] : kVu[0]);
    }

    /* orb */
    const float gx = kCx + p_.orb_x, gy = kCy + p_.orb_y;
    const float z = p_.orb_z_m;
    const float lift = z * 40.0f;
    const float vy = gy - lift;
    const float scale = std::max(0.72f, std::min(1.28f, 1.0f + 0.16f * z));
    const float od = 26.0f * scale;
    const bool bright = v_orb || v_cal || p_.active_channel >= 0 || p_.field_playing;

    for (int i = 0; i < p_.trail_count && i < EVO_RMLUI_SURROUND_TRAIL && (v_orb || p_.sweep_rotation); ++i)
        list.circle(kCx + p_.trail_x[i], kCy + p_.trail_y[i] - lift, od * (0.62f - 0.09f * i) * 0.5f,
                    kCyan.with_alpha(0.34f * (1.0f - i / 5.0f)));

    /* dotted arc: listener -> orb, or speaker -> mic while measuring */
    float ax = kCx, ay = kCy - 18.0f;
    bool arc = v_orb;
    if (measuring && p_.cal_channel >= 0 && p_.cal_channel < n_spk)
    {
        ax = kCx + p_.speakers[p_.cal_channel].dx;
        ay = kCy + p_.speakers[p_.cal_channel].dy;
        arc = true;
    }
    const float adx = gx - ax, ady = vy - ay;
    const float alen = std::sqrt(adx * adx + ady * ady);
    if (arc && alen > 40.0f)
        for (int k = 0; k < 8; ++k)
        {
            const float t = (k + 1) / 9.0f;
            const float bulge = std::sin(t * 3.14159265f) * std::min(36.0f, alen * 0.14f);
            list.circle(ax + adx * t + (ady / alen) * bulge, ay + ady * t - (adx / alen) * bulge, 2.0f,
                        kCyan.with_alpha(0.15f + 0.6f * t));
        }

    const float sw = 30.0f + 6.0f * std::fabs(z);
    list.rounded_rect({gx - sw * 0.5f, gy - sw * 0.18f + 4.0f, sw, sw * 0.36f}, sw * 0.18f,
                      Color::rgb(0x000000, std::max(0.3f, 0.9f - 0.3f * std::fabs(z) / 1.5f) * 0.6f));
    if (std::fabs(lift) > 3.0f)
        list.line(gx, gy, gx, vy, 1.0f, kCyan.with_alpha(0.4f));
    if (bright && (v_orb || measuring || p_.active_channel >= 0 || p_.field_playing))
        for (int k = 0; k < 2; ++k)
        {
            const float ph = std::fmod(p_.anim_time * 0.9f + k * 0.5f, 1.0f);
            list.ring(gx, vy, (od * 1.2f + od * 2.8f * ph) * 0.5f, 2.0f, kCyan.with_alpha(0.45f * (1.0f - ph)));
        }
    list.circle(gx, vy, od * 1.15f, kCyan.with_alpha(bright ? 0.18f : 0.09f));
    list.circle(gx, vy, od * 0.5f, kCyan.with_alpha(bright ? 1.0f : 0.75f));
    list.circle(gx, vy, od * 0.19f, kWhite);
    std::string tag;
    if (v_cal)
        tag = "DUALSENSE MIC";
    else if (v_orb && std::fabs(z) >= 0.005f)
        tag = fmt("Z %+.2f m", z);
    if (!tag.empty())
        label(list, fonts.semibold, tag, gx, v_cal ? vy - od * 0.5f - 10 : vy + od * 0.5f + 20, 12, kCyan, Align::center, 1.0f);

    list.pop_clip();
}

/* ---------------------------------------------------------------- right */

void SurroundScreen::draw_right(DrawList &list, const Context &ctx) const
{
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &pal = ctx.palette;
    const float x = kOx + 1296.0f, w = 408.0f;
    const bool v_orb = p_.view_mode == EVO_SURROUND_VIEW_ORB;
    const int emitting = p_.active_channel;
    const bool is51 = p_.is_51_layout != 0;

    auto panel = [&](float top, float h, const char *title) {
        list.bordered_rect({x, kTop + top, w, h}, 14, kPanel, 1.0f, kEdge);
        label(list, fonts.semibold, title, x + 18, kTop + top + 26, 12, pal.text_faint, Align::left, 1.6f);
    };

    panel(0, 300, "CHANNEL LEVELS");
    for (int i = 0; i < p_.order_count && i < EVO_RMLUI_SURROUND_SPEAKERS; ++i)
    {
        const int ch = p_.order[i];
        const bool tone = emitting == ch;
        const float level = tone ? 1.0f : ((ch >= 0 && ch < EVO_RMLUI_SURROUND_SPEAKERS) ? p_.proximity[ch] : 0.0f);
        const float y = kTop + 40.0f + i * 31.0f;
        label(list, fonts.semibold, label_of(ch), x + 18, y + 16, 13, tone ? kCyan : kWhite);
        list.rounded_rect({x + 70, y + 6, 250, 10}, 5, Color::rgb(0x18263a));
        if (level > 0.01f)
            list.rounded_rect({x + 70, y + 6, 250 * clamp01(level), 10}, 5, tone ? kYellow : kCyan);
        label(list, fonts.regular, fmt("%d%%", (int)(level * 100.0f + 0.5f)), x + w - 18, y + 16, 13,
              tone ? kYellow : pal.text_muted, Align::right);
    }

    panel(312, 150, "CONTROLS");
    label(list, fonts.regular, v_orb ? "LEFT STICK  MOVE SOURCE" : "D-PAD  NAVIGATE STAGE", x + 18, kTop + 312 + 56, 14, pal.text_muted);
    label(list, fonts.regular, v_orb ? "L2 / R2  HEIGHT" : "CROSS  SELECT / TEST", x + 18, kTop + 312 + 84, 14, pal.text_muted);
    label(list, fonts.regular, v_orb ? "TRIANGLE  FLIGHT MODE" : "SQUARE  5.1 / 7.1", x + 18, kTop + 312 + 112, 14, pal.text_muted);

    panel(474, 150, "QUICK MODES");
    static const char *kModes[3] = {"MANUAL", "ORBIT", "FLYBY"};
    for (int i = 0; i < 3; ++i)
    {
        Rect r{x + 18 + i * 128.0f, kTop + 474 + 40, 116, 90};
        const bool sel = i == p_.flight_mode;
        const float a = v_orb ? 1.0f : 0.55f;
        list.bordered_rect(r, 12, kPanel, sel ? 2.0f : 1.0f, (sel ? kCyan : Color::rgb(0x223751)).with_alpha(a));
        label(list, fonts.semibold, kModes[i], r.cx(), r.cy() + 5, 14, (sel ? kWhite : pal.text_muted).with_alpha(a),
              Align::center, 1.0f);
    }

    panel(636, 148, "OUTPUT");
    label(list, fonts.regular,
          fmt("LAYOUT  %s \xC2\xB7 S16 \xC2\xB7 48 kHz", is51 ? "5.1 \xC2\xB7 6 CH" : "7.1 \xC2\xB7 8 CH"), x + 18,
          kTop + 636 + 56, 14, pal.text_muted);
    label(list, fonts.regular, is_120hz_ ? "DISPLAY  120 HZ \xC2\xB7 8.33 ms FRAMES" : "DISPLAY  60 HZ \xC2\xB7 16.67 ms FRAMES",
          x + 18, kTop + 636 + 84, 14, pal.text_muted);
    label(list, fonts.regular, p_.cal_profile_active ? "CALIBRATION  ACTIVE ON PLAYBACK" : "CALIBRATION  NOT SET",
          x + 18, kTop + 636 + 112, 14, p_.cal_profile_active ? kCyan : pal.text_faint);
}

void SurroundScreen::draw_calibration(DrawList &list, const Context &ctx) const
{
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &pal = ctx.palette;
    const float x = kOx + 1296.0f, w = 408.0f;
    list.bordered_rect({x, kTop, w, 784}, 14, kPanel, 1.0f, kEdge);
    label(list, fonts.semibold, "AUTO CALIBRATION", x + 18, kTop + 26, 12, pal.text_faint, Align::left, 1.6f);

    int cur = 0;
    switch (p_.cal_phase)
    {
    case EVO_SURROUND_CAL_MIC_CHECK: cur = 1; break;
    case EVO_SURROUND_CAL_MEASURING: cur = 2; break;
    case EVO_SURROUND_CAL_ANALYZING: cur = 3; break;
    case EVO_SURROUND_CAL_COMPLETE: cur = 5; break;
    default: break;
    }
    const bool failed = p_.cal_phase == EVO_SURROUND_CAL_ERROR;
    static const char *kStep[5] = {"PLACE", "MIC CHECK", "MEASURE", "ANALYZE", "DONE"};
    for (int s = 0; s < 5; ++s)
    {
        const bool done = s < cur, now = s == cur && !failed;
        const float y = kTop + 52.0f + s * 28.0f;
        list.circle(x + 26, y + 8, 6, done ? kCyan : (now ? kYellow : Color::rgb(0x2a4c5c)));
        label(list, fonts.semibold, kStep[s], x + 44, y + 13, 13, now ? kYellow : (done ? kWhite : pal.text_faint),
              Align::left, 1.0f);
    }

    std::string body;
    Color bc = kWhite;
    if (p_.cal_phase == EVO_SURROUND_CAL_INTRO)
        body = "Place your DualSense controller on your seat at ear level, facing the screen. Unmute its "
               "microphone, keep the room quiet, then press CROSS.";
    else if (p_.cal_phase == EVO_SURROUND_CAL_MEASURING)
        body = "A short sweep plays from each speaker in turn. Stay still and keep the room quiet.";
    else
    {
        body = cal_message_;
        if (failed)
            bc = kRed;
    }
    ui::paragraph(list, fonts.regular, body, x + 18, kTop + 212, 15, w - 36, 22, bc, 3);

    /* mic level */
    label(list, fonts.semibold, "MIC", x + 18, kTop + 296, 12, pal.text_faint, Align::left, 1.4f);
    list.rounded_rect({x + 60, kTop + 286, 220, 10}, 5, Color::rgb(0x18263a));
    list.rounded_rect({x + 60, kTop + 286, 220 * clamp01(p_.cal_mic_level), 10}, 5, kCyan);
    if (p_.cal_noise_db > -119.0f)
        label(list, fonts.regular, fmt("NOISE %.0f dBFS", p_.cal_noise_db), x + w - 18, kTop + 296, 12, pal.text_muted,
              Align::right);

    /* results table */
    const bool results = p_.cal_phase == EVO_SURROUND_CAL_COMPLETE;
    const float cols[4] = {18, 84, 196, 306};
    static const char *kHead[4] = {"CH", "LEVEL", "PATH", "DELAY"};
    for (int c = 0; c < 4; ++c)
        label(list, fonts.semibold, kHead[c], x + cols[c], kTop + 332, 11, pal.text_faint, Align::left, 1.2f);
    for (int i = 0; i < p_.order_count && i < EVO_RMLUI_SURROUND_SPEAKERS; ++i)
    {
        const int ch = p_.order[i];
        const bool now = p_.cal_phase == EVO_SURROUND_CAL_MEASURING && ch == p_.cal_channel;
        std::string c1 = "-", c2 = "-", c3 = "-";
        bool missed = false;
        if (results && ch >= 0 && ch < EVO_RMLUI_SURROUND_SPEAKERS)
        {
            if (p_.cal_detected[ch])
            {
                c1 = (ch == 3) ? "NO TRIM" : fmt("%+.1f dB", p_.cal_trim_db[ch]);
                c2 = fmt("+%.2f m", p_.cal_path_m[ch]);
                c3 = fmt("%.1f ms", p_.cal_delay_ms[ch]);
            }
            else
            {
                c1 = "NOT HEARD";
                missed = true;
            }
        }
        else if (now)
            c1 = "MEASURING";
        const float y = kTop + 356.0f + i * 28.0f;
        label(list, fonts.semibold, label_of(ch), x + cols[0], y + 14, 13, now ? kYellow : kWhite);
        label(list, fonts.regular, c1, x + cols[1], y + 14, 13, now ? kYellow : (missed ? kRed : pal.text_muted));
        label(list, fonts.regular, c2, x + cols[2], y + 14, 13, pal.text_muted);
        label(list, fonts.regular, c3, x + cols[3], y + 14, 13, pal.text_muted);
    }
    ui::paragraph(list, fonts.regular,
                  "LEVEL = trim that balances each speaker at the seat. PATH = extra distance to the nearest "
                  "speaker; DELAY aligns every arrival. The subwoofer's level is left alone - the controller mic "
                  "hears too little deep bass to judge it.",
                  x + 18, kTop + 600, 12, w - 36, 18, pal.text_faint, 6);
}

/* ------------------------------------------------------------- telemetry */

void SurroundScreen::draw_telemetry(DrawList &list, const Context &ctx) const
{
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &pal = ctx.palette;
    const Rect strip{kStage.x, kStage.y + kStage.h + 14, kStage.w, 70};
    list.bordered_rect(strip, 14, kPanel, 1.0f, kEdge);

    if (p_.view_mode != EVO_SURROUND_VIEW_CALIBRATION)
    {
        const std::string near_ = p_.nearest_channel >= 0 ? label_of(p_.nearest_channel) : "-";
        const std::string cells[8][2] = {
            {"AZIMUTH", fmt("%.0f\xC2\xB0", p_.azimuth_deg)},   {"ELEVATION", fmt("%.0f\xC2\xB0", p_.elevation_deg)},
            {"DISTANCE", fmt("%.1f m", p_.distance_m)},         {"SOURCE", fmt("%.1f dBFS", p_.source_dbfs)},
            {"X", fmt("%+.2f m", p_.x_m)},                      {"Y", fmt("%+.2f m", p_.y_m)},
            {"Z", fmt("%+.2f m", p_.orb_z_m)},                  {"LOUDEST", near_}};
        const float cw = strip.w / 8.0f;
        for (int i = 0; i < 8; ++i)
        {
            const float cx = strip.x + cw * (i + 0.5f);
            label(list, fonts.semibold, cells[i][0], cx, strip.y + 26, 10, pal.text_faint, Align::center, 1.2f);
            label(list, fonts.semibold, cells[i][1], cx, strip.y + 52, 15,
                  (i == 7 && p_.nearest_channel >= 0) ? kCyan : kWhite, Align::center);
        }
        return;
    }

    std::string line;
    float frac = 0.0f;
    Color color = kWhite;
    switch (p_.cal_phase)
    {
    case EVO_SURROUND_CAL_MIC_CHECK:
        line = "CHECKING MICROPHONE + ROOM NOISE...";
        frac = 0.05f;
        break;
    case EVO_SURROUND_CAL_MEASURING:
        line = p_.cal_verifying ? fmt("VERIFYING: %s (CLOCK DRIFT CHECK)...", name_of(p_.cal_channel).c_str())
                                : fmt("MEASURING: %s (%d/%d)...", name_of(p_.cal_channel).c_str(), p_.cal_step, p_.cal_total);
        frac = 0.1f + 0.8f * (p_.cal_total > 0 ? (float)(p_.cal_step - (p_.cal_verifying ? 0 : 1)) / p_.cal_total : 0.0f);
        color = kCyan;
        break;
    case EVO_SURROUND_CAL_ANALYZING:
        line = "CALCULATING LEVELS, DISTANCES AND DELAYS...";
        frac = 0.95f;
        break;
    case EVO_SURROUND_CAL_COMPLETE:
        line = p_.cal_applied ? "COMPLETE \xC2\xB7 APPLIED TO PLAYBACK" : "COMPLETE \xC2\xB7 SQUARE APPLIES IT TO PLAYBACK";
        frac = 1.0f;
        color = kCyan;
        break;
    case EVO_SURROUND_CAL_ERROR:
        line = "CALIBRATION STOPPED \xC2\xB7 TRIANGLE TO REPEAT";
        color = kRed;
        break;
    default:
        line = "PLACE THE DUALSENSE AT THE SWEET SPOT \xC2\xB7 CROSS TO START";
        break;
    }
    label(list, fonts.semibold, line, strip.x + 18, strip.y + 28, 16, color, Align::left, 0.6f);
    list.rounded_rect({strip.x + 18, strip.y + 42, strip.w - 36, 8}, 4, Color::rgb(0x18263a));
    if (frac > 0.0f)
        list.rounded_rect({strip.x + 18, strip.y + 42, (strip.w - 36) * clamp01(frac), 8}, 4, kCyan);
}

} // namespace evo::kit
