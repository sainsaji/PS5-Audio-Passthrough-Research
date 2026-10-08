// Surround Sound Studio - silent stand-ins for the PC preview.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The preview has no console audio and no microphone. The tone engine accepts
// every request and reports the channel as playing; calibration fails with a
// message, so both panes can be drawn.

#include "surround/speaker_calibration.hpp"
#include "surround/surround_test_service.hpp"

#include <cstdio>

namespace evo
{

SurroundTestService::SurroundTestService() = default;
SurroundTestService::~SurroundTestService() = default;
bool SurroundTestService::start()
{
    m_running.store(true);
    return true;
}
void SurroundTestService::stop()
{
    m_running.store(false);
    m_active.store(false);
    m_fieldOn.store(false);
}
void SurroundTestService::triggerTone(bool is51Layout, int channelIndex)
{
    m_is51Layout = is51Layout;
    m_currentChannel.store(channelIndex);
    m_active.store(true);
}
bool SurroundTestService::playPcm(const int16_t *, size_t)
{
    return false;
}
bool SurroundTestService::startField()
{
    m_fieldOn.store(true);
    return true;
}
void SurroundTestService::stopField()
{
    m_fieldOn.store(false);
}
void SurroundTestService::setFieldGains(const float *, float)
{
}

SpeakerCalibrationService::~SpeakerCalibrationService() = default;
bool SpeakerCalibrationService::start(ISurroundTestService *, bool is51)
{
    std::lock_guard<std::mutex> lock(m_lock);
    m_snap = Snapshot();
    m_snap.is51 = is51;
    m_snap.phase = Phase::Error;
    std::snprintf(m_snap.message, sizeof(m_snap.message), "NO MICROPHONE IN THE PC PREVIEW");
    return true;
}
void SpeakerCalibrationService::cancel()
{
}
SpeakerCalibrationService::Snapshot SpeakerCalibrationService::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_snap;
}
bool SpeakerCalibrationService::buildProfile(evo_speaker_cal_t *) const
{
    return false;
}

} // namespace evo
