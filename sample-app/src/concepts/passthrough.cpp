// Passthrough Lab - the one screen of the app.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lists the bundled test clips (AC-3, E-AC-3, DTS), what the HDMI device says
// it can decode, and plays the selected clip as a bitstream so the receiver,
// not the console, decodes it. Every number the console returns is on screen,
// so a run doubles as a hardware test.

#include "concepts/concepts.hpp"

#include "core/save_file.hpp"
#include "core/tween.hpp"
#include "passthrough/bitstream.hpp"
#include "passthrough/iec61937.hpp"
#include "platform/ps5/system.hpp"
#include "ui/glyphs.hpp"
#include "ui/motion.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace hui::concepts
{

namespace
{

using gfx::Color;
using gfx::Rect;

const Color kInk = Color::rgb(0xf2f4ff);
const Color kMuted = Color::rgb(0x8d94b8);
const Color kPanel = Color::rgb(0x12152b, 0.82f);
const Color kRow = Color::rgb(0x1a1f3d, 0.9f);
const Color kAccent = Color::rgb(0x6ee7ff);
const Color kGood = Color::rgb(0x7cf0a8);
const Color kWarn = Color::rgb(0xffc56b);
const Color kBad = Color::rgb(0xff6b8a);

constexpr const char *kClipDir = "/app0/assets/clips";

constexpr float kListX = 96.0f;
constexpr float kListY = 292.0f;
constexpr float kListW = 980.0f;
constexpr float kRowH = 100.0f;
constexpr float kRowGap = 12.0f;
constexpr float kSideX = 1124.0f;
constexpr float kSideW = 700.0f;

constexpr const char *kTechniques[] = {
    "sceAudioOutExOpen, then sceAudioOutExConfigureOutput: the order Sony's media core uses",
    "IEC 61937 bursts built on the console: AC-3, E-AC-3 at 192 kHz, DTS types I to III",
    "The receiver's own format list, read from HDMI with sceAudioOutSysGetHdmiMonitorInfo",
    "Every return code on screen, so a run is also a hardware test",
};

constexpr app::TourStep kTour[] = {
    {0.6f, 0, Direction::none, "clips"},
    {0.3f, 0, Direction::down},
    {0.3f, 0, Direction::down, "focus-dts"},
};

struct Clip
{
    std::string file;
    pt::Codec codec = pt::Codec::unknown;
    int kbps = 0;
    bool verified = false; // confirmed on hardware by the research
};

const char *short_name(pt::Codec codec);

// The first `count` bytes of a file. (save::read_file's limit is a maximum
// file size, not a read length: it fails on anything bigger.)
std::vector<std::uint8_t> read_head(const std::string &path, std::size_t count)
{
    std::vector<std::uint8_t> head(count);
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return {};
    head.resize(std::fread(head.data(), 1, count, file));
    std::fclose(file);
    return head;
}

// Reads the first frame only: enough for the codec and the bitrate.
Clip describe(const std::string &file)
{
    Clip clip;
    clip.file = file;
    const std::vector<std::uint8_t> head = read_head(std::string(kClipDir) + "/" + file, 16384);
    clip.codec = pt::detect(head);
    const pt::Frame f = pt::parse_frame(clip.codec, head, 0);
    if (f.size > 0 && f.samples > 0)
        clip.kbps = static_cast<int>(f.size * 8 * 48000 / static_cast<std::size_t>(f.samples) / 1000);
    // Confirmed on a PS5 Pro, FW 12.70, 2026-10-08.
    clip.verified = clip.codec == pt::Codec::ac3 || clip.codec == pt::Codec::eac3 ||
                    clip.codec == pt::Codec::dts;
    sys::log("[PT] clip %s codec=%s kbps=%d head=%zu", file.c_str(), short_name(clip.codec),
             clip.kbps, head.size());
    return clip;
}

// index.txt lists the clips the repository ships; index.local.txt, if
// present, lists clips cut from your own files (never committed).
void read_index(const char *name, std::vector<Clip> *clips)
{
    std::string index;
    if (!save::read_file(std::string(kClipDir) + "/" + name, &index, 4096))
        return;
    std::size_t start = 0;
    while (start < index.size())
    {
        std::size_t end = index.find('\n', start);
        if (end == std::string::npos)
            end = index.size();
        std::string line = index.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty())
            clips->push_back(describe(line));
        start = end + 1;
    }
}

std::vector<Clip> load_clips()
{
    std::vector<Clip> clips;
    read_index("index.txt", &clips);
    read_index("index.local.txt", &clips);
    return clips;
}

Rect row_rect(int index)
{
    return {kListX, kListY + static_cast<float>(index) * (kRowH + kRowGap), kListW, kRowH};
}

const char *short_name(pt::Codec codec)
{
    switch (codec)
    {
    case pt::Codec::ac3:
        return "AC-3";
    case pt::Codec::eac3:
        return "E-AC-3";
    case pt::Codec::dts:
        return "DTS";
    case pt::Codec::aac:
        return "AAC";
    case pt::Codec::unknown:
        break;
    }
    return "?";
}

class PassthroughLab final : public app::Concept
{
  public:
    explicit PassthroughLab(app::Context &context)
        : context_(context), player_(pt::make_player()), clips_(load_clips())
    {
        sink_ = player_->query_sink();
        focus_ring_.snap(row_rect(0));
    }

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "passthrough",
            "Passthrough Lab",
            "Dolby and DTS bitstream over HDMI, decoded by your receiver",
            "src/concepts/passthrough.cpp",
            audio::SoundSet::glass,
            kAccent,
            kTechniques,
        };
        return kInfo;
    }

    void enter() override
    {
        age_ = 0.0f;
    }

    void update(const InputFrame &input, float dt, app::Feedback &feedback) override
    {
        age_ += dt;
        clock_ += dt;
        const int count = static_cast<int>(clips_.size());

        if (input.nav == Direction::up && focus_ > 0)
        {
            --focus_;
            feedback.play(audio::Cue::focus);
        }
        else if (input.nav == Direction::down && focus_ + 1 < count)
        {
            ++focus_;
            feedback.play(audio::Cue::focus);
        }
        else if ((input.nav == Direction::up || input.nav == Direction::down) && !input.nav_repeat)
        {
            feedback.play(audio::Cue::error);
        }

        if (input.is_pressed(Action::confirm) && focus_ < count)
            play(feedback);
        if (input.is_pressed(Action::back) && player_->busy())
        {
            player_->stop();
            feedback.play(audio::Cue::back);
        }
        if (input.is_pressed(Action::west))
        {
            loop_ = !loop_;
            feedback.play(audio::Cue::toggle);
        }
        if (input.is_pressed(Action::north) && !player_->busy())
        {
            sink_ = player_->query_sink();
            feedback.play(audio::Cue::notify);
        }

        // A finished run frees the thread; the screen keeps its numbers.
        if (player_->busy() && player_->state() != pt::PlayState::playing)
            player_->stop();

        focus_ring_.target(row_rect(focus_));
        focus_ring_.update(dt, 22.0f);
        live_.target = player_->state() == pt::PlayState::playing ? 1.0f : 0.0f;
        live_.update(dt, 8.0f);
    }

    void draw(app::Frame &frame) const override
    {
        frame.backdrop.mode = gfx::BackdropMode::aurora;
        frame.backdrop.colors[0] = Color::rgb(0x070914);
        frame.backdrop.colors[1] = Color::rgb(0x10153a);
        frame.backdrop.colors[2] = Color::rgb(0x1d4e89);
        frame.backdrop.colors[3] = Color::rgb(0x5b2a86);
        frame.backdrop.time = clock_;

        gfx::DrawList &list = frame.scene;
        const ui::Fonts &fonts = context_.fonts;
        const float intro = tween::cubic_out(std::min(1.0f, age_ / 0.6f));
        list.push_opacity(intro);

        ui::text(list, fonts.display, "Passthrough Lab", 96, 150, 64, kInk);
        ui::text(list, fonts.regular,
                 "Dolby and DTS sent over HDMI untouched. Your receiver does the decoding.", 98,
                 198, 26, kMuted);
        ui::text(list, fonts.semibold, "TEST CLIPS", kListX, 262, 18, kAccent, gfx::Align::left,
                 3.0f);

        draw_clips(list);
        draw_receiver(list);
        draw_now_playing(list);

        const ui::Hint hints[] = {
            {ui::Button::cross, "Play as bitstream"},
            {ui::Button::circle, "Stop"},
            {ui::Button::square, loop_ ? "Loop: on" : "Loop: off"},
            {ui::Button::triangle, "Read receiver again"},
        };
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), hints, 4, 96, false);

        list.pop_opacity();
    }

    std::span<const app::TourStep> tour() const override
    {
        return kTour;
    }

  private:
    void play(app::Feedback &feedback)
    {
        const Clip &clip = clips_[static_cast<std::size_t>(focus_)];
        if (clip.codec == pt::Codec::unknown)
        {
            sys::log("[PT] play %s refused: codec not recognised", clip.file.c_str());
            feedback.play(audio::Cue::error);
            return;
        }
        player_->stop();
        std::string bytes;
        if (!save::read_file(std::string(kClipDir) + "/" + clip.file, &bytes, 64u << 20))
        {
            sys::log("[PT] play %s refused: could not read the clip", clip.file.c_str());
            feedback.play(audio::Cue::error);
            return;
        }
        sys::log("[PT] play %s (%zu bytes) loop=%d", clip.file.c_str(), bytes.size(), loop_ ? 1 : 0);
        std::vector<std::uint8_t> stream(bytes.begin(), bytes.end());
        // The cue plays before the console mutes PCM for the bitstream.
        feedback.play(audio::Cue::launch);
        playing_ = focus_;
        player_->start(std::move(stream), clip.codec, loop_);
    }

    void draw_clips(gfx::DrawList &list) const
    {
        const ui::Fonts &fonts = context_.fonts;
        if (clips_.empty())
        {
            ui::text(list, fonts.regular, "No clips found in /app0/assets/clips", kListX, kListY + 40,
                     26, kBad);
            return;
        }
        const Rect ring = focus_ring_.value();
        list.glow(ring, 22, 26, kAccent.with_alpha(0.35f));
        for (int i = 0; i < static_cast<int>(clips_.size()); ++i)
        {
            const Clip &clip = clips_[static_cast<std::size_t>(i)];
            const Rect r = row_rect(i);
            list.rounded_rect(r, 22, kRow);
            const bool live = i == playing_ && player_->state() == pt::PlayState::playing;

            // codec badge
            const Rect badge{r.x + 20, r.y + 18, 140, 64};
            list.rounded_rect(badge, 16, live ? kGood : Color::rgb(0x2a3266));
            ui::text(list, fonts.semibold, short_name(clip.codec), badge.cx(), badge.y + 42, 24,
                     live ? Color::rgb(0x08101c) : kInk, gfx::Align::center);

            ui::text(list, fonts.semibold, pt::codec_name(clip.codec), r.x + 184, r.y + 44, 26, kInk);
            char detail[128];
            const pt::Carrier carrier = pt::carrier_for(clip.codec);
            std::snprintf(detail, sizeof(detail), "%s  -  %d kbps  -  mode %d, %d kHz carrier",
                          clip.file.c_str(), clip.kbps, carrier.mode, carrier.sample_rate / 1000);
            ui::text(list, fonts.regular, detail, r.x + 184, r.y + 78, 19, kMuted);

            // status pills: console side, receiver side
            const bool listed = sink_.ok && sink_.supports(pt::coding_for(clip.codec));
            pill(list, r.x + r.w - 20, r.y + 14, clip.verified ? "VERIFIED" : "UNTESTED",
                 clip.verified ? kGood : kWarn);
            pill(list, r.x + r.w - 20, r.y + 56,
                 !sink_.ok ? "RECEIVER ?" : listed ? "RECEIVER OK" : "NOT LISTED",
                 !sink_.ok ? kMuted : listed ? kGood : kBad);
        }
        list.bordered_rect(ring, 22, Color::rgb(0x000000, 0.0f), 3.0f, kAccent);
    }

    void pill(gfx::DrawList &list, float right, float top, const char *label, Color color) const
    {
        const ui::FontRef &font = context_.fonts.semibold;
        const float w = font.measure(label, 15, 1.5f) + 28.0f;
        const Rect r{right - w, top, w, 30};
        list.rounded_rect(r, 15, color.with_alpha(0.16f));
        list.bordered_rect(r, 15, Color::rgb(0x000000, 0.0f), 1.5f, color.with_alpha(0.7f));
        ui::text(list, font, label, r.cx(), r.y + 21, 15, color, gfx::Align::center, 1.5f);
    }

    void draw_receiver(gfx::DrawList &list) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const Rect card{kSideX, 236, kSideW, 330};
        list.shadow({card.x, card.y + 10, card.w, card.h}, 26, 30, Color::rgb(0x000000, 0.5f));
        list.rounded_rect(card, 26, kPanel);
        ui::text(list, fonts.semibold, "YOUR RECEIVER", card.x + 32, card.y + 48, 18, kAccent,
                 gfx::Align::left, 3.0f);
        if (!sink_.ok)
        {
            char line[96];
            std::snprintf(line, sizeof(line), "HDMI info unavailable (0x%08x)",
                          static_cast<unsigned>(sink_.rc));
            ui::text(list, fonts.regular, line, card.x + 32, card.y + 100, 24, kBad);
            return;
        }
        ui::text(list, fonts.semibold, sink_.name.empty() ? "HDMI device" : sink_.name.c_str(),
                 card.x + 32, card.y + 98, 32, kInk);
        ui::text(list, fonts.regular, "Formats it says it can decode:", card.x + 32, card.y + 140,
                 20, kMuted);
        float x = card.x + 32;
        float y = card.y + 168;
        for (const pt::SinkFormat &f : sink_.formats)
        {
            char label[48];
            std::snprintf(label, sizeof(label), "%s %dch", pt::coding_name(f.coding), f.channels);
            const float w = fonts.regular.measure(label, 19) + 28.0f;
            if (x + w > card.x + card.w - 32)
            {
                x = card.x + 32;
                y += 46;
            }
            const bool ours = f.coding == 2 || f.coding == 6 || f.coding == 7 || f.coding == 10;
            list.rounded_rect({x, y, w, 36}, 18, (ours ? kAccent : kMuted).with_alpha(0.16f));
            ui::text(list, fonts.regular, label, x + w * 0.5f, y + 25, 19, ours ? kInk : kMuted,
                     gfx::Align::center);
            x += w + 10;
        }
    }

    void draw_now_playing(gfx::DrawList &list) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const Rect card{kSideX, 596, kSideW, 336};
        list.shadow({card.x, card.y + 10, card.w, card.h}, 26, 30, Color::rgb(0x000000, 0.5f));
        list.rounded_rect(card, 26, kPanel);
        if (live_.value > 0.01f)
            list.glow(card, 26, 30, kGood.with_alpha(0.25f * live_.value));
        ui::text(list, fonts.semibold, "BITSTREAM", card.x + 32, card.y + 48, 18, kAccent,
                 gfx::Align::left, 3.0f);

        const pt::PlayState state = player_->state();
        const char *title = "Nothing playing";
        Color color = kMuted;
        switch (state)
        {
        case pt::PlayState::idle:
            break;
        case pt::PlayState::playing:
            title = "Playing - check your receiver";
            color = kGood;
            break;
        case pt::PlayState::done:
            title = "Finished";
            color = kInk;
            break;
        case pt::PlayState::failed:
            title = "Failed";
            color = kBad;
            break;
        }
        ui::text(list, fonts.semibold, title, card.x + 32, card.y + 100, 32, color);
        if (state == pt::PlayState::idle)
        {
            ui::text(list, fonts.regular, "Pick a clip and press Cross.", card.x + 32, card.y + 146,
                     22, kMuted);
            ui::text(list, fonts.regular, "Each speaker beeps in turn: FL, FR, C, LFE, SL, SR.",
                     card.x + 32, card.y + 180, 22, kMuted);
            return;
        }
        if (state == pt::PlayState::failed)
            ui::text(list, fonts.regular, player_->error().c_str(), card.x + 32, card.y + 140, 22,
                     kBad);

        char line[128];
        std::snprintf(line, sizeof(line), "ExOpen        0x%08x",
                      static_cast<unsigned>(player_->open_rc()));
        ui::text(list, fonts.mono, line, card.x + 32, card.y + 186, 22, kInk);
        std::snprintf(line, sizeof(line), "ExConfigure   0x%08x",
                      static_cast<unsigned>(player_->config_rc()));
        ui::text(list, fonts.mono, line, card.x + 32, card.y + 218, 22, kInk);
        std::snprintf(line, sizeof(line), "bursts %llu   errors %llu   %.1f s",
                      static_cast<unsigned long long>(player_->bursts()),
                      static_cast<unsigned long long>(player_->output_errors()),
                      player_->seconds());
        ui::text(list, fonts.mono, line, card.x + 32, card.y + 250, 22, kInk);
        if (state == pt::PlayState::playing)
            ui::text(list, fonts.regular, "The console mutes its own sounds while this plays.",
                     card.x + 32, card.y + 300, 20, kMuted);
    }

    app::Context &context_;
    std::unique_ptr<pt::Player> player_;
    std::vector<Clip> clips_;
    pt::Sink sink_;
    int focus_ = 0;
    int playing_ = -1;
    bool loop_ = false;
    float age_ = 0.0f;
    float clock_ = 0.0f;
    ui::SpringRect focus_ring_;
    tween::Spring live_;
};

} // namespace

std::unique_ptr<app::Concept> make_passthrough(app::Context &context)
{
    return std::make_unique<PassthroughLab>(context);
}

} // namespace hui::concepts
