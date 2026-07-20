// user-tunable knobs. stored as an ini you can open in notepad
// (%APPDATA%\creechr\creechr.ini), not the registry, because a config
// file you can see matches the open-the-log-folder ethos.
//
// deliberately tiny. the mischief level scales exactly five gates and
// nothing else; every other constant in this codebase stays hardcoded
// because its character, not configuration. env vars (CREECHR_*)
// remain dev overrides and beat anything in here.
#pragma once

namespace cr {

struct Settings {
    enum class Mischief { Calm, Normal, Gremlin };

    Mischief mischief = Mischief::Normal;

    // per-target-kind consent. calm mode wins over all of these.
    bool stealWindows = true;
    bool stealCursor  = true;
    bool stealUia     = true;   // taskbar buttons and friends
    bool stealDom     = true;   // browser bits via the extension

    bool calmWhenInCall  = true;   // mic/camera in use -> no crimes
    bool hideOnFullscreen = true;
    bool adaptiveTick = true;
    bool ghostMode = false;        // cant touch him (reserved for v1.2)

    void load();
    void save() const;

    // the five settings-scaled gates. calm = heists never fire.
    int heistMeanIntervalMs() const; // expected ms between attempts, 0 = never
    int inputIdleGateMs() const;     // required hands-off-ms before a heist
    int heistCooldownMs() const;     // floor between attempts
    int stashWaitMinMs() const;      // boredom timer minimum
    int stashWaitRangeMs() const;    // boredom timer random range

    bool anyStealEnabled() const
    {
        return stealWindows || stealCursor || stealUia || stealDom;
    }
};

} // namespace cr
