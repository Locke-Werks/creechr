// BusyDetector — "is the user probably in a call right now".
//
// windows tracks which apps hold the microphone or webcam in the
// CapabilityAccessManager consent store (HKCU, world-readable): an
// app entry with LastUsedTimeStart set and LastUsedTimeStop == 0 is
// using the device at this moment. teams, zoom, meet, discord, obs
// all show up. misses screen-share-without-mic, catches everything
// that matters for the "he yeeted my calculator during the standup"
// failure mode.
//
// pure HKCU registry reads, no privileges, no hooks, polled at 0.2Hz.
#pragma once

namespace cr {

class BusyDetector
{
public:
    BusyDetector() = default;

    // re-read the consent store. cheap, but not every-tick cheap;
    // call it every few seconds. returns the new state.
    bool refresh();

    bool busy() const { return m_busy; }

private:
    bool m_busy = false;
};

} // namespace cr
