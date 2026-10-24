// profiles.h - feel presets for bl2vr_grip.ini.
// A profile covers ONLY the 19 feel keys (filters, trigger weight, stock, catch-up).
// Hardware keys (gun_hand, aim_axis, buttons, distances) are the player's own setup
// and are never touched. "Custom" is derived: no profile's key set matches exactly.
#pragma once
#include "ini.h"
#include <cmath>
#include <cstdio>

struct ProfileKV { const char* key; float val; };

struct Profile {
    const char* name;
    const char* desc;
    ProfileKV kv[19];
};

// All profiles obey the project rules: no sub-1-Hz result cutoffs (the 1 Hz stock floor
// is the documented exception), predict_ms always 0, trigger gate on at 0.25.
// Default = the shipped ini, verbatim.
inline Profile kProfiles[] = {
    { "Default", "The shipped tuning. Balanced steadiness and response - start here.",
      { {"min_cutoff",4.5f}, {"beta",2.0f}, {"derivative_cutoff",6.0f},
        {"min_cutoff_rot",3.0f}, {"beta_rot",1.5f}, {"predict_ms",0.0f},
        {"trigger_gate",1.0f}, {"trigger_threshold",0.25f}, {"zoom_mult",2.0f}, {"zoom_mult_rot",1.5f},
        {"twohand_min_cutoff",3.0f}, {"twohand_beta",2.2f},
        {"twohand_min_cutoff_rot",2.0f}, {"twohand_beta_rot",1.5f},
        {"catchup_pos",0.0f}, {"catchup_rot",0.0f},
        {"stock_cutoff",2.5f}, {"stock_beta",2.0f}, {"stock_zoom_mult",2.5f} } },

    { "Steady", "Heavy resistance to every movement. Leaves ~19% of an 8 Hz hand tremor (measured, "
                "tools/tremor_sweep.cpp) at a cost of ~106 ms lag, which catch-up keeps honest. "
                "Best for long-range and long sessions.",
      { {"min_cutoff",2.0f}, {"beta",1.0f}, {"derivative_cutoff",4.0f},
        {"min_cutoff_rot",1.0f}, {"beta_rot",0.5f}, {"predict_ms",0.0f},
        {"trigger_gate",1.0f}, {"trigger_threshold",0.25f}, {"zoom_mult",3.0f}, {"zoom_mult_rot",1.0f},
        {"twohand_min_cutoff",2.0f}, {"twohand_beta",1.0f},
        {"twohand_min_cutoff_rot",1.0f}, {"twohand_beta_rot",0.5f},
        {"catchup_pos",90.0f}, {"catchup_rot",80.0f},
        {"stock_cutoff",1.5f}, {"stock_beta",1.0f}, {"stock_zoom_mult",1.5f} } },

    { "Hybrid", "The middle path: noticeably steadier than Default, still quick to follow "
                "a swinging aim. Good all-rounder for mixed loadouts.",
      { {"min_cutoff",3.0f}, {"beta",1.5f}, {"derivative_cutoff",5.0f},
        {"min_cutoff_rot",2.25f}, {"beta_rot",1.2f}, {"predict_ms",0.0f},
        {"trigger_gate",1.0f}, {"trigger_threshold",0.25f}, {"zoom_mult",2.5f}, {"zoom_mult_rot",1.5f},
        {"twohand_min_cutoff",2.5f}, {"twohand_beta",1.5f},
        {"twohand_min_cutoff_rot",1.8f}, {"twohand_beta_rot",1.2f},
        {"catchup_pos",30.0f}, {"catchup_rot",25.0f},
        {"stock_cutoff",2.0f}, {"stock_beta",1.5f}, {"stock_zoom_mult",2.0f} } },

    { "Responsive", "Light filtering, fast wake-up. Leaves ~60% of an 8 Hz hand tremor through at "
                    "~27 ms lag - for players who hate any lag and trust their own hands.",
      { {"min_cutoff",6.0f}, {"beta",3.0f}, {"derivative_cutoff",8.0f},
        {"min_cutoff_rot",6.0f}, {"beta_rot",3.0f}, {"predict_ms",0.0f},
        {"trigger_gate",1.0f}, {"trigger_threshold",0.25f}, {"zoom_mult",1.5f}, {"zoom_mult_rot",1.2f},
        {"twohand_min_cutoff",4.5f}, {"twohand_beta",2.5f},
        {"twohand_min_cutoff_rot",5.0f}, {"twohand_beta_rot",3.0f},
        {"catchup_pos",0.0f}, {"catchup_rot",0.0f},
        {"stock_cutoff",3.5f}, {"stock_beta",3.0f}, {"stock_zoom_mult",1.5f} } },

    { "Sniper", "Default in the hip; extreme weight the moment you touch the trigger or "
                "brace with two hands. Precision aiming, still usable on the move.",
      { {"min_cutoff",4.5f}, {"beta",2.0f}, {"derivative_cutoff",6.0f},
        {"min_cutoff_rot",3.0f}, {"beta_rot",1.5f}, {"predict_ms",0.0f},
        {"trigger_gate",1.0f}, {"trigger_threshold",0.25f}, {"zoom_mult",4.0f}, {"zoom_mult_rot",2.5f},
        {"twohand_min_cutoff",2.5f}, {"twohand_beta",1.2f},
        {"twohand_min_cutoff_rot",1.5f}, {"twohand_beta_rot",0.8f},
        {"catchup_pos",80.0f}, {"catchup_rot",70.0f},
        {"stock_cutoff",1.5f}, {"stock_beta",0.8f}, {"stock_zoom_mult",2.0f} } },

    { "Locked", "Precision experiment. Uses cutoffs BELOW the project's normal 1 Hz floor (~10% of "
                "an 8 Hz tremor left, ~200 ms lag) and relies on a strong catch-up term to stop the "
                "drift that the floor exists to prevent. Try it scoped in; if it floats after a big "
                "sweep, raise the cutoffs or drop catch-up.",
      { {"min_cutoff",1.2f}, {"beta",0.4f}, {"derivative_cutoff",4.0f},
        {"min_cutoff_rot",0.8f}, {"beta_rot",0.3f}, {"predict_ms",0.0f},
        {"trigger_gate",1.0f}, {"trigger_threshold",0.25f}, {"zoom_mult",3.0f}, {"zoom_mult_rot",2.0f},
        {"twohand_min_cutoff",1.2f}, {"twohand_beta",0.4f},
        {"twohand_min_cutoff_rot",0.8f}, {"twohand_beta_rot",0.3f},
        {"catchup_pos",160.0f}, {"catchup_rot",150.0f},
        {"stock_cutoff",1.0f}, {"stock_beta",0.6f}, {"stock_zoom_mult",1.2f} } },
};
inline const int kProfileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);

// write every key of a profile into the ini (numeric, "%g"-formatted)
inline void ApplyProfile(IniFile& ini, const Profile& pr) {
    char b[64];
    for (const ProfileKV& kv : pr.kv) {
        snprintf(b, sizeof(b), "%.6g", kv.val);
        ini.SetAndSave(kv.key, b);
    }
}

// does the ini currently match this profile's key set? (numeric compare, 1e-4 tolerance)
inline bool ProfileMatches(IniFile& ini, const Profile& pr) {
    for (const ProfileKV& kv : pr.kv) {
        const double v = ini.GetD(kv.key, -1e9);
        if (std::fabs(v - (double)kv.val) > 1e-4) return false;
    }
    return true;
}

// index of the active profile, or -1 (= Custom)
inline int ActiveProfile(IniFile& ini) {
    for (int i = 0; i < kProfileCount; i++)
        if (ProfileMatches(ini, kProfiles[i])) return i;
    return -1;
}
