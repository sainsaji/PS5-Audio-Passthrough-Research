// Surround Sound Studio - the Speaker Lab page.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from EVO Player's Surround Sound Studio (#106, SurroundTestScreen):
// per-speaker test tones, 5.1 / 7.1 auto tests, a 360 degree sweep, the 3D
// sound field (music panned around the room by a gain matrix) and free roam,
// and the DualSense microphone calibration. Plain multichannel PCM on an
// 8-channel port: it tests the speakers, not the bitstream path.
//
// Panes: the action list on the left, the speakers on the stage (D-pad moves
// by where they stand), the orb (the sound source) and the calibration report.
// L1 / R1 belong to the app's page switcher here, so the orb's height is on
// L2 / R2 (it was L1 / R1 in EVO).

#include "concepts/concepts.hpp"
#include "concepts/studio_audio.hpp"

#include "surround/spatial_field.hpp"
#include "surround/speaker_calibration.hpp"
#include "surround/surround_test_service.hpp"
#include "surround/surround_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace hui::concepts
{

namespace
{

namespace spatial = evo::spatial;
using evo::SpeakerCalibrationService;

constexpr float kTapStepM = 0.18f;
constexpr float kGlideMps = 3.6f;
constexpr float kStickMps = 3.2f;
constexpr float kHeightTapM = 0.10f;
constexpr float kHeightMps = 1.0f;
constexpr double kRepeatDelayMs = 250.0;
constexpr double kSweepDwellMs = 1300.0; // a whole 1.2 s tone and a breath
constexpr double kSweepRevSec = 12.0;
constexpr double kOrbitRadPerS = 0.55;
constexpr double kFlybySec = 7.0;
constexpr double kPi = 3.14159265358979323846;

const gfx::Color kAccent = gfx::Color::rgb(0x00cdff);

constexpr const char *kTechniques[] = {
    "An 8-channel PCM port: test tones per speaker, 5.1 and 7.1 sequences",
    "Distance-based amplitude panning (DBAP) across the room for the 3D sound field",
    "A 360 degree sweep that follows the speaker ring",
    "DualSense mic calibration: log sweeps and a matched filter give levels, distances, delays",
};

float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

float speaker_azimuth(bool is51, int ch)
{
    return spatial::azimuthDeg(spatial::speakerPosition(is51, ch));
}

class SpeakerLab final : public app::Concept
{
  public:
    explicit SpeakerLab(app::Context &context)
        : context_(context), surround_(std::make_unique<evo::SurroundTestService>()),
          cal_(std::make_unique<SpeakerCalibrationService>())
    {
        src_.y = -1.2f;
        studio_register(StudioPage::speaker_lab, &SpeakerLab::stop_all, this);
        update_field();
    }
    ~SpeakerLab() override
    {
        studio_unregister(StudioPage::speaker_lab);
        cal_->cancel();
        surround_->stop();
    }

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "speakerlab",
            "Speaker Lab",
            "Test every speaker, fly a sound around the room, calibrate with the DualSense mic",
            "src/concepts/speaker_lab.cpp",
            audio::SoundSet::glass,
            kAccent,
            kTechniques,
        };
        return kInfo;
    }

    void enter() override
    {
        studio_enter(StudioPage::speaker_lab);
        age_ = 0.0f;
        pane_ = Pane::actions;
        toast_.clear();
    }

    void update(const InputFrame &input, float dt, app::Feedback &feedback) override
    {
        age_ += dt;
        anim_ += dt;
        fb_ = &feedback;
        if (toast_time_ > 0.0f)
            toast_time_ -= dt;

        switch (pane_)
        {
        case Pane::orb:
            handle_orb(input);
            break;
        case Pane::calibration:
            handle_calibration(input);
            break;
        case Pane::speakers:
            handle_speakers(input);
            break;
        case Pane::actions:
            handle_actions(input);
            break;
        }
        step(input, dt);
        fb_ = nullptr;
    }

    void draw(app::Frame &frame) const override
    {
        frame.backdrop.mode = gfx::BackdropMode::aurora;
        frame.backdrop.colors[0] = gfx::Color::rgb(0x04050b);
        frame.backdrop.colors[1] = gfx::Color::rgb(0x0a0d1c);
        frame.backdrop.colors[2] = gfx::Color::rgb(0x0d3a5c);
        frame.backdrop.colors[3] = gfx::Color::rgb(0x3a1d6b);
        frame.backdrop.time = anim_;

        evo_rmlui_surround_params_t p;
        fill(&p);
        evo::kit::SurroundScreen view;
        view.set(p);
        const evo::kit::Context ctx{context_.fonts, palette_, anim_};
        gfx::DrawList &list = frame.scene;
        list.push_opacity(std::min(1.0f, age_ / 0.4f));
        view.draw(list, ctx);
        if (toast_time_ > 0.0f && !toast_.empty())
        {
            const float a = std::min(1.0f, toast_time_ / 0.3f);
            const ui::FontRef &font = context_.fonts.semibold;
            const float w = font.measure(toast_, 20) + 48.0f;
            const gfx::Rect r{960.0f - w * 0.5f, 962.0f, w, 44.0f};
            list.rounded_rect(r, 22, gfx::Color::rgb(0x0b1322, 0.92f * a));
            list.bordered_rect(r, 22, gfx::Color::rgb(0, 0.0f), 1.5f, kAccent.with_alpha(0.6f * a));
            ui::text(list, font, toast_, 960.0f, r.y + 29, 20, gfx::Color::rgb(0xffffff, a),
                     gfx::Align::center);
        }
        list.pop_opacity();
    }

  private:
    enum class Pane
    {
        actions,
        speakers,
        orb,
        calibration,
    };
    enum Flight
    {
        kManual = 0,
        kOrbit,
        kFlyby
    };
    enum Item // the action list (not hui::Action, the input)
    {
        kSoundField = 0,
        kCalibration,
        kFreeRoam,
        kRotation,
        kAuto51,
        kAuto71,
        kLayout,
        kSilence,
        kActionCount
    };

    static void stop_all(void *self)
    {
        auto *lab = static_cast<SpeakerLab *>(self);
        lab->stop_sweep();
        lab->cal_->cancel();
        lab->surround_->stop();
        lab->tone_follow_ = false;
        lab->pane_ = Pane::actions;
    }

    // ---------------------------------------------------------- feedback
    void cue(audio::Cue c)
    {
        if (fb_ != nullptr)
            fb_->play(c);
    }
    void toast(const char *title, const char *text)
    {
        toast_ = std::string(title) + "  -  " + text;
        toast_time_ = 2.6f;
    }

    bool is51() const
    {
        return surround_->is51Layout();
    }

    // ------------------------------------------------------------ input
    void handle_actions(const InputFrame &in)
    {
        if (in.is_pressed(Action::up))
            move_action(-1);
        else if (in.is_pressed(Action::down))
            move_action(1);
        else if (in.is_pressed(Action::right))
            switch_pane(1);
        if (in.is_pressed(Action::confirm))
            activate();
        if (in.is_pressed(Action::west))
            toggle_layout();
        if (in.is_pressed(Action::north) || in.is_pressed(Action::back))
            silence();
    }

    void handle_speakers(const InputFrame &in)
    {
        if (in.is_pressed(Action::up))
            navigate_speakers(0, -1);
        else if (in.is_pressed(Action::down))
            navigate_speakers(0, 1);
        else if (in.is_pressed(Action::left))
            navigate_speakers(-1, 0);
        else if (in.is_pressed(Action::right))
            navigate_speakers(1, 0);
        if (in.is_pressed(Action::confirm))
        {
            stop_sweep();
            play_channel(selected_speaker_);
        }
        if (in.is_pressed(Action::west))
            toggle_layout();
        if (in.is_pressed(Action::north))
            silence();
        if (in.is_pressed(Action::back))
            switch_pane(-1);
    }

    void handle_orb(const InputFrame &in)
    {
        if (in.is_pressed(Action::confirm))
        {
            if (tone_follow_)
            {
                if (surround_->isFieldPlaying())
                    surround_->stopField();
                else
                    surround_->startField();
                cue(audio::Cue::toggle);
            }
            else
            {
                play_channel(loudest_);
            }
        }
        if (in.is_pressed(Action::north))
        {
            set_flight(static_cast<Flight>((flight_ + 1) % 3));
            cue(audio::Cue::toggle);
            static const char *kNames[3] = {"Manual", "Orbit", "Flyby"};
            toast("FLIGHT MODE", kNames[flight_]);
        }
        if (in.is_pressed(Action::west))
        {
            src_ = spatial::Vec3();
            flight_ = kManual;
            trail_count_ = 0;
            cue(audio::Cue::back);
        }
        if (in.is_pressed(Action::jump_prev))
            src_.z = clampf(src_.z - kHeightTapM, -spatial::kMaxHeightM, spatial::kMaxHeightM);
        if (in.is_pressed(Action::jump_next))
            src_.z = clampf(src_.z + kHeightTapM, -spatial::kMaxHeightM, spatial::kMaxHeightM);
        if (in.is_pressed(Action::back) || in.is_pressed(Action::l3))
        {
            if (tone_follow_)
                surround_->stopField();
            switch_pane(-1);
        }
    }

    void handle_calibration(const InputFrame &in)
    {
        using Phase = SpeakerCalibrationService::Phase;
        const Phase phase = cal_->snapshot().phase;
        if (in.is_pressed(Action::confirm))
        {
            if (phase == Phase::Complete && cal_started_)
                leave_calibration();
            else if (!cal_->isRunning())
                start_calibration();
        }
        if (in.is_pressed(Action::north))
        {
            cal_->cancel();
            start_calibration();
        }
        if (in.is_pressed(Action::back))
            leave_calibration();
    }

    // ---------------------------------------------------------- navigation
    void move_action(int dir)
    {
        const int next = selected_action_ + dir;
        if (next < 0 || next >= kActionCount)
        {
            cue(audio::Cue::error);
            return;
        }
        selected_action_ = next;
        cue(audio::Cue::focus);
    }

    // By where the speakers stand: the nearest one inside a ~55 degree cone of
    // the pressed direction. Nothing further left hands focus back to the list.
    void navigate_speakers(int dir_x, int dir_y)
    {
        const bool l51 = is51();
        const spatial::Vec3 cur = spatial::speakerPosition(l51, selected_speaker_);
        int best = -1;
        float best_score = 1e9f;
        for (int ch = 0; ch < spatial::kChannels; ++ch)
        {
            if (ch == selected_speaker_ || !spatial::present(l51, ch))
                continue;
            const spatial::Vec3 p = spatial::speakerPosition(l51, ch);
            const float dx = p.x - cur.x;
            const float dy = p.y - cur.y;
            const float along = dx * dir_x + dy * dir_y;
            const float perp = std::fabs(dx * dir_y - dy * dir_x);
            if (along <= 0.05f || perp > along * 1.5f)
                continue;
            const float score = along + 2.0f * perp;
            if (score < best_score)
            {
                best_score = score;
                best = ch;
            }
        }
        if (best >= 0)
        {
            selected_speaker_ = best;
            cue(audio::Cue::focus);
        }
        else if (dir_x < 0)
        {
            switch_pane(-1);
        }
        else
        {
            cue(audio::Cue::error);
        }
    }

    void switch_pane(int dir)
    {
        if (dir > 0 && pane_ == Pane::actions)
        {
            if (!spatial::present(is51(), selected_speaker_))
                selected_speaker_ = 0;
            pane_ = Pane::speakers;
            cue(audio::Cue::focus);
        }
        else if (dir < 0 && pane_ != Pane::actions)
        {
            pane_ = Pane::actions;
            tone_follow_ = false;
            cue(audio::Cue::back);
        }
        else
        {
            cue(audio::Cue::error);
        }
    }

    // ------------------------------------------------------------ actions
    void activate()
    {
        switch (selected_action_)
        {
        case kSoundField:
            enter_orb(true, kOrbit);
            break;
        case kCalibration:
            stop_sweep();
            pane_ = Pane::calibration;
            cal_started_ = false;
            cue(audio::Cue::select);
            break;
        case kFreeRoam:
            enter_orb(false, kManual);
            break;
        case kRotation:
            start_sweep(2);
            break;
        case kAuto51:
            start_sweep(0);
            break;
        case kAuto71:
            start_sweep(1);
            break;
        case kLayout:
            toggle_layout();
            break;
        default:
            silence();
            break;
        }
    }

    void enter_orb(bool follow, Flight flight)
    {
        stop_sweep();
        if (follow && !surround_->startField())
        {
            toast("SURROUND TEST", "Audio port unavailable");
            cue(audio::Cue::error);
            return;
        }
        pane_ = Pane::orb;
        tone_follow_ = follow;
        trail_count_ = 0;
        set_flight(flight);
        cue(audio::Cue::select);
        toast(follow ? "3D SOUND FIELD" : "SPATIAL ORB",
              follow ? "Music follows the source - Triangle changes mode"
                     : "Left stick moves the source, L2 / R2 height");
    }

    void set_flight(Flight flight)
    {
        flight_ = flight;
        if (flight == kOrbit)
        {
            const float r = std::sqrt(src_.x * src_.x + src_.y * src_.y);
            orbit_radius_ = clampf(r, 1.2f, 3.0f);
            orbit_angle_ = r < 0.3f ? 0.0 : std::atan2(src_.x, -src_.y);
        }
        else if (flight == kFlyby)
        {
            flyby_t_ = 0.0;
        }
    }

    void toggle_layout()
    {
        const bool next51 = !surround_->is51Layout();
        surround_->set51Layout(next51);
        if (!spatial::present(next51, selected_speaker_))
            selected_speaker_ = 0;
        update_field();
        cue(audio::Cue::toggle);
        toast("SPEAKER LAYOUT", next51 ? "5.1 Surround (6 channels)" : "7.1 Surround (8 channels)");
    }

    void silence()
    {
        stop_sweep();
        tone_follow_ = false;
        surround_->stop();
        cue(audio::Cue::back);
        toast("SURROUND TEST", "Silenced");
    }

    void play_channel(int ch)
    {
        if (ch < 0)
            return;
        studio_enter(StudioPage::speaker_lab);
        cue(audio::Cue::select);
        surround_->triggerTone(surround_->is51Layout(), ch);
    }

    // Non-LFE speakers clockwise from FRONT LEFT, for the 360 sweep.
    int rotation_order(int *out) const
    {
        const bool l51 = is51();
        const float start = speaker_azimuth(l51, 0);
        int n = 0;
        for (int ch = 0; ch < spatial::kChannels; ++ch)
            if (ch != 3 && spatial::present(l51, ch))
                out[n++] = ch;
        std::sort(out, out + n,
                  [&](int a, int b)
                  {
                      const float ra = std::fmod(speaker_azimuth(l51, a) - start + 360.0f, 360.0f);
                      const float rb = std::fmod(speaker_azimuth(l51, b) - start + 360.0f, 360.0f);
                      return ra < rb;
                  });
        return n;
    }

    void start_sweep(int mode)
    {
        if (mode == 0)
            surround_->set51Layout(true);
        else if (mode == 1)
            surround_->set51Layout(false);
        // 360 is one continuous revolution panned by the gain matrix
        if (!(mode == 2 ? surround_->startField() : surround_->start()))
        {
            toast("SURROUND TEST", "Audio port unavailable");
            cue(audio::Cue::error);
            return;
        }
        if (mode != 2)
            surround_->stopField();
        sweep_mode_ = mode;
        sweep_angle_ = speaker_azimuth(surround_->is51Layout(), 0) * kPi / 180.0;
        sweep_travel_ = 0.0;
        sweep_step_ = 0;
        sweep_ms_ = 0.0;
        tone_follow_ = false;
        pane_ = Pane::actions;
        cue(audio::Cue::select);
        if (mode == 0)
            toast("AUTO TEST", "5.1 - 6 channel sequence");
        else if (mode == 1)
            toast("AUTO TEST", "7.1 - 8 channel sequence");
        else
            toast("360 SWEEP", "The source circles the listener");
    }

    void stop_sweep()
    {
        if (sweep_mode_ == 2)
            surround_->stopField();
        sweep_mode_ = -1;
        sweep_step_ = 0;
        sweep_ms_ = 0.0;
    }

    void start_calibration()
    {
        surround_->stop(); // nothing else may play during the run
        cal_started_ = cal_->start(surround_.get(), surround_->is51Layout());
        cue(cal_started_ ? audio::Cue::select : audio::Cue::error);
    }

    void leave_calibration()
    {
        cal_->cancel();
        surround_->stop();
        cal_started_ = false;
        pane_ = Pane::actions;
        cue(audio::Cue::back);
    }

    // ---------------------------------------------------------- per frame
    void ease_source_to(float x, float y, float z, double dt)
    {
        const float k = 1.0f - static_cast<float>(std::exp(-dt / 0.12));
        src_.x += (x - src_.x) * k;
        src_.y += (y - src_.y) * k;
        src_.z += (z - src_.z) * k;
    }

    void orb_motion(const InputFrame &in, double dt)
    {
        float mx = 0.0f;
        float my = 0.0f;
        const auto held = [&](Action a) { return (in.held & action_bit(a)) != 0; };
        const auto tapped = [&](Action a) { return in.is_pressed(a); };
        const float tap_x = (tapped(Action::right) ? 1.0f : 0.0f) - (tapped(Action::left) ? 1.0f : 0.0f);
        const float tap_y = (tapped(Action::down) ? 1.0f : 0.0f) - (tapped(Action::up) ? 1.0f : 0.0f);
        const float dir_x = (held(Action::right) ? 1.0f : 0.0f) - (held(Action::left) ? 1.0f : 0.0f);
        const float dir_y = (held(Action::down) ? 1.0f : 0.0f) - (held(Action::up) ? 1.0f : 0.0f);
        // D-pad: one step per tap, a glide once held (the stick is read below)
        if (!in.nav_from_stick && (tap_x != 0.0f || tap_y != 0.0f))
        {
            mx += tap_x * kTapStepM;
            my += tap_y * kTapStepM;
            dpad_held_ms_ = 0.0;
        }
        else if (dir_x != 0.0f || dir_y != 0.0f)
        {
            dpad_held_ms_ += dt * 1000.0;
            if (dpad_held_ms_ >= kRepeatDelayMs)
            {
                mx += dir_x * kGlideMps * static_cast<float>(dt);
                my += dir_y * kGlideMps * static_cast<float>(dt);
            }
        }
        mx += in.stick_x * kStickMps * static_cast<float>(dt);
        my += in.stick_y * kStickMps * static_cast<float>(dt);

        // L2 / R2 held: continuous height
        const bool down = held(Action::jump_prev);
        const bool up = held(Action::jump_next);
        if ((down || up) && !tapped(Action::jump_prev) && !tapped(Action::jump_next))
        {
            height_held_ms_ += dt * 1000.0;
            if (height_held_ms_ >= kRepeatDelayMs)
                src_.z = clampf(src_.z + ((up ? 1.0f : 0.0f) - (down ? 1.0f : 0.0f)) * kHeightMps *
                                             static_cast<float>(dt),
                                -spatial::kMaxHeightM, spatial::kMaxHeightM);
        }
        else
        {
            height_held_ms_ = 0.0;
        }

        // a hand on the controls takes the source back from the autopilot
        if ((mx != 0.0f || my != 0.0f) && flight_ != kManual)
            flight_ = kManual;
        if (flight_ == kOrbit)
        {
            orbit_angle_ = std::fmod(orbit_angle_ + kOrbitRadPerS * dt, 2.0 * kPi);
            ease_source_to(static_cast<float>(orbit_radius_ * std::sin(orbit_angle_)),
                           static_cast<float>(-orbit_radius_ * std::cos(orbit_angle_)), src_.z, dt);
        }
        else if (flight_ == kFlyby)
        {
            // in from the front, through the seat, out the back - then again
            flyby_t_ += dt / kFlybySec;
            if (flyby_t_ >= 1.0)
                flyby_t_ -= 1.0;
            ease_source_to(static_cast<float>(0.6 * std::sin(flyby_t_ * kPi * 1.5)),
                           static_cast<float>(-3.2 + 6.4 * flyby_t_), src_.z, dt);
        }
        else
        {
            src_.x += mx;
            src_.y += my;
        }
        const float r = std::sqrt(src_.x * src_.x + src_.y * src_.y);
        if (r > spatial::kMaxRadiusM)
        {
            src_.x *= spatial::kMaxRadiusM / r;
            src_.y *= spatial::kMaxRadiusM / r;
        }
    }

    void update_field()
    {
        loudest_ = spatial::panGains(src_, is51(), gains_);
    }

    void record_trail(double delta_ms)
    {
        trail_ms_ += delta_ms;
        if (trail_ms_ < 70.0)
            return;
        trail_ms_ = 0.0;
        const float gx = src_.x * spatial::kDpPerMeter;
        const float gy = src_.y * spatial::kDpPerMeter;
        if (trail_count_ > 0)
        {
            const float dx = gx - trail_x_[0];
            const float dy = gy - trail_y_[0];
            if (dx * dx + dy * dy < 16.0f)
            {
                --trail_count_; // parked: let the trail fade out
                return;
            }
        }
        for (int i = kTrail - 1; i > 0; --i)
        {
            trail_x_[i] = trail_x_[i - 1];
            trail_y_[i] = trail_y_[i - 1];
        }
        trail_x_[0] = gx;
        trail_y_[0] = gy;
        if (trail_count_ < kTrail)
            ++trail_count_;
    }

    void step(const InputFrame &in, float dt_s)
    {
        const double dt = std::min(static_cast<double>(dt_s), 0.1);
        const double delta_ms = dt * 1000.0;
        const bool l51 = is51();

        if (pane_ == Pane::orb)
        {
            orb_motion(in, dt);
        }
        else if (pane_ == Pane::calibration)
        {
            ease_source_to(0.0f, 0.0f, 0.0f, dt); // the mic at the seat
        }
        else if (sweep_mode_ == 2)
        {
            // clockwise at a steady rate, 85 % of the way out to the speaker ring
            const double inc = 2.0 * kPi * dt / kSweepRevSec;
            sweep_angle_ = std::fmod(sweep_angle_ + inc, 2.0 * kPi);
            sweep_travel_ += inc;
            int rot[spatial::kChannels];
            const int n = rotation_order(rot);
            double rad = 2.4;
            if (n > 1)
            {
                const double az = sweep_angle_ * 180.0 / kPi;
                for (int k = 0; k < n; ++k)
                {
                    const int a = rot[k];
                    const int b = rot[(k + 1) % n];
                    double aa = speaker_azimuth(l51, a);
                    double ab = speaker_azimuth(l51, b);
                    double x = az;
                    if (ab < aa)
                        ab += 360.0;
                    if (x < aa)
                        x += 360.0;
                    if (x >= aa && x <= ab)
                    {
                        const double t = ab > aa ? (x - aa) / (ab - aa) : 0.0;
                        const spatial::Vec3 pa = spatial::speakerPosition(l51, a);
                        const spatial::Vec3 pb = spatial::speakerPosition(l51, b);
                        rad = 0.85 * (std::sqrt(pa.x * pa.x + pa.y * pa.y) * (1.0 - t) +
                                      std::sqrt(pb.x * pb.x + pb.y * pb.y) * t);
                        break;
                    }
                }
            }
            src_.x = static_cast<float>(rad * std::sin(sweep_angle_));
            src_.y = static_cast<float>(-rad * std::cos(sweep_angle_));
            src_.z = 0.0f;
        }
        else if (sweep_mode_ == 0 || sweep_mode_ == 1 || pane_ == Pane::speakers)
        {
            const spatial::Vec3 p = spatial::speakerPosition(l51, selected_speaker_);
            ease_source_to(p.x * 0.7f, p.y * 0.7f, 0.0f, dt);
        }
        else
        {
            ease_source_to(0.0f, -1.2f, 0.0f, dt); // resting in front
        }
        update_field();
        record_trail(delta_ms);

        // 3D SOUND FIELD: music panned by the gain matrix, louder as it nears the seat
        if (pane_ == Pane::orb && tone_follow_ && surround_->isFieldPlaying())
        {
            const float master = clampf(1.0f / (0.75f + 0.25f * spatial::rangeM(src_)), 0.45f, 1.0f);
            surround_->setFieldGains(gains_, master);
        }

        if (sweep_mode_ < 0)
            return;
        if (sweep_mode_ == 2)
        {
            surround_->setFieldGains(gains_, 1.0f);
            if (loudest_ >= 0)
                selected_speaker_ = loudest_;
            if (sweep_travel_ >= 2.0 * kPi)
            {
                stop_sweep();
                toast("360 SWEEP", "Complete");
            }
            return;
        }
        int order[spatial::kChannels];
        const int count = spatial::physicalOrder(l51, order);
        if (count <= 0)
        {
            stop_sweep();
            return;
        }
        if (sweep_ms_ <= 0.0)
        {
            const int ch = order[sweep_step_ % count];
            surround_->triggerTone(l51, ch);
            selected_speaker_ = ch;
            sweep_ms_ = kSweepDwellMs;
        }
        sweep_ms_ -= delta_ms;
        if (sweep_ms_ <= 0.0 && ++sweep_step_ >= count)
        {
            stop_sweep();
            toast("SURROUND TEST", "Sweep complete");
        }
    }

    // ----------------------------------------------------------- the view
    void fill(evo_rmlui_surround_params_t *p) const
    {
        *p = {};
        const bool l51 = is51();
        const float ppm = spatial::kDpPerMeter;
        const bool orb = pane_ == Pane::orb;
        p->is_51_layout = l51 ? 1 : 0;
        p->selected_item = pane_ == Pane::speakers ? EVO_RMLUI_SURROUND_ACTIONS + selected_speaker_
                                                   : selected_action_;
        p->active_channel = surround_->isActive() ? surround_->getCurrentChannel() : -1;
        p->surround_mode = orb ? 2 : (sweep_mode_ >= 0 ? 1 : 0);
        p->view_mode = orb                            ? EVO_SURROUND_VIEW_ORB
                       : pane_ == Pane::calibration ? EVO_SURROUND_VIEW_CALIBRATION
                                                    : EVO_SURROUND_VIEW_STAGE;
        p->anim_time = anim_;
        p->orb_active = orb ? 1 : 0;
        p->flight_mode = flight_;
        p->tone_follow = tone_follow_ ? 1 : 0;
        p->field_playing = surround_->isFieldPlaying() ? 1 : 0;
        p->sweep_rotation = sweep_mode_ == 2 ? 1 : 0;
        p->nearest_channel = loudest_;
        p->px_per_m = ppm;
        p->orb_x = src_.x * ppm;
        p->orb_y = src_.y * ppm;
        p->orb_z_m = src_.z;
        p->azimuth_deg = spatial::azimuthDeg(src_);
        p->elevation_deg = spatial::elevationDeg(src_);
        p->distance_m = spatial::rangeM(src_);
        p->x_m = src_.x;
        p->y_m = -src_.y; // telemetry: +Y towards the screen
        p->source_dbfs = spatial::kToneDbfs;
        p->trail_count = trail_count_;
        for (int i = 0; i < trail_count_ && i < EVO_RMLUI_SURROUND_TRAIL; ++i)
        {
            p->trail_x[i] = trail_x_[i];
            p->trail_y[i] = trail_y_[i];
        }
        p->order_count = spatial::physicalOrder(l51, p->order);
        p->speaker_count = spatial::kChannels;
        for (int ch = 0; ch < spatial::kChannels; ++ch)
        {
            const spatial::Vec3 pos = spatial::speakerPosition(l51, ch);
            evo_rmlui_surround_speaker_t &s = p->speakers[ch];
            s.name = spatial::speakerName(l51, ch);
            s.label = spatial::speakerLabel(l51, ch);
            s.ch = ch;
            s.item_idx = EVO_RMLUI_SURROUND_ACTIONS + ch;
            s.dx = static_cast<int>(std::lround(pos.x * ppm));
            s.dy = static_cast<int>(std::lround(pos.y * ppm));
            s.hidden = spatial::present(l51, ch) ? 0 : 1;
            s.hz = ch == 3 ? 60.0 : 440.0;
            p->proximity[ch] = gains_[ch];
        }

        cal_snapshot_ = cal_->snapshot();
        const SpeakerCalibrationService::Snapshot &cal = cal_snapshot_;
        using Phase = SpeakerCalibrationService::Phase;
        switch (cal_started_ ? cal.phase : Phase::Idle)
        {
        case Phase::MicCheck:
            p->cal_phase = EVO_SURROUND_CAL_MIC_CHECK;
            break;
        case Phase::Measuring:
            p->cal_phase = EVO_SURROUND_CAL_MEASURING;
            break;
        case Phase::Analyzing:
            p->cal_phase = EVO_SURROUND_CAL_ANALYZING;
            break;
        case Phase::Complete:
            p->cal_phase = EVO_SURROUND_CAL_COMPLETE;
            break;
        case Phase::Error:
            p->cal_phase = EVO_SURROUND_CAL_ERROR;
            break;
        default:
            p->cal_phase = EVO_SURROUND_CAL_INTRO;
            break;
        }
        p->cal_step = cal.step;
        p->cal_total = cal.total > 0 ? cal.total : p->order_count;
        p->cal_channel = cal.channel;
        p->cal_verifying = cal.verifying ? 1 : 0;
        p->cal_noise_db = cal.noiseDb;
        p->cal_mic_level = cal.micLevel;
        p->cal_message = cal.message;
        for (int ch = 0; ch < spatial::kChannels; ++ch)
        {
            const SpeakerCalibrationService::ChannelResult &r = cal.results[ch];
            p->cal_measured[ch] = r.measured ? 1 : 0;
            p->cal_detected[ch] = r.detected ? 1 : 0;
            p->cal_trim_db[ch] = r.trimDb;
            p->cal_path_m[ch] = r.pathM;
            p->cal_delay_ms[ch] = r.delayMs;
        }
    }

    static constexpr int kTrail = 5;

    app::Context &context_;
    std::unique_ptr<evo::SurroundTestService> surround_;
    std::unique_ptr<SpeakerCalibrationService> cal_;
    mutable SpeakerCalibrationService::Snapshot cal_snapshot_; // keeps cal_message alive
    evo::kit::Palette palette_;
    app::Feedback *fb_ = nullptr;

    Pane pane_ = Pane::actions;
    int selected_action_ = 0;
    int selected_speaker_ = 0;
    spatial::Vec3 src_;
    int flight_ = kManual;
    bool tone_follow_ = false;
    double orbit_angle_ = 0.0;
    double orbit_radius_ = 2.2;
    double flyby_t_ = 0.0;
    float gains_[spatial::kChannels] = {};
    int loudest_ = -1;
    double dpad_held_ms_ = 0.0;
    double height_held_ms_ = 0.0;
    float trail_x_[kTrail] = {};
    float trail_y_[kTrail] = {};
    int trail_count_ = 0;
    double trail_ms_ = 0.0;
    int sweep_mode_ = -1; // -1 idle, 0 = 5.1, 1 = 7.1, 2 = rotation
    int sweep_step_ = 0;
    double sweep_ms_ = 0.0;
    double sweep_angle_ = 0.0;
    double sweep_travel_ = 0.0;
    bool cal_started_ = false;
    float age_ = 0.0f;
    float anim_ = 0.0f;
    std::string toast_;
    float toast_time_ = 0.0f;
};

} // namespace

std::unique_ptr<app::Concept> make_speaker_lab(app::Context &context)
{
    return std::make_unique<SpeakerLab>(context);
}

} // namespace hui::concepts
