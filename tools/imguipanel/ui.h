#pragma once
#include "ini.h"
#include "game.h"
#include "imgui.h"
#include <cstdio>
#include <cstring>

// ---- widget helpers: friendly label + hover description ----
static void Desc(const char* d) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d); }

static void FloatKey(IniFile& ini, const char* key, const char* label, const char* d,
                     float vmin, float vmax, const char* fmt = "%.3f") {
    float f = (float)ini.GetD(key, 0.0);
    if (ImGui::DragFloat(label, &f, (vmax - vmin) * 0.01f, vmin, vmax, fmt)) {
        char b[64]; snprintf(b, sizeof(b), fmt, f); ini.SetAndSave(key, b);
    }
    Desc(d);
}
static void IntKey(IniFile& ini, const char* key, const char* label, const char* d, int vmin, int vmax) {
    int v = ini.GetI(key, 0);
    if (ImGui::DragInt(label, &v, 1, vmin, vmax)) { char b[32]; snprintf(b, sizeof(b), "%d", v); ini.SetAndSave(key, b); }
    Desc(d);
}
static void BoolKey(IniFile& ini, const char* key, const char* label, const char* d) {
    bool b = ini.GetI(key, 0) != 0;
    if (ImGui::Checkbox(label, &b)) ini.SetAndSave(key, b ? "1" : "0");
    Desc(d);
}
static void ComboKey(IniFile& ini, const char* key, const char* label, const char* d,
                     const char* const* opts, int n) {
    std::string cur = ini.Get(key, opts[0]);
    int idx = 0;
    for (int i = 0; i < n; i++) if (cur == opts[i]) { idx = i; break; }
    if (ImGui::Combo(label, &idx, opts, n)) ini.SetAndSave(key, opts[idx]);
    Desc(d);
}
static void Section(const char* t) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.62f, 0.78f, 1.0f, 1.0f), "%s", t);
    ImGui::Separator();
}

static void DrawUI(IniFile& ini, Patcher& p) {
    // ---- game / patch header (fixed) ----
    ImGui::Text("Game folder (auto-detected from Steam):");
    char w64[600] = { 0 };
    strncpy_s(w64, p.gameWin64.c_str(), sizeof(w64) - 1);
    ImGui::SetNextItemWidth(-80.0f);
    if (ImGui::InputText("##w64", w64, sizeof(w64))) {
        p.gameWin64 = w64; p.found = !p.gameWin64.empty();
        p.gameExe = p.found ? p.gameWin64 + "Borderlands2VR.exe" : "";
    }
    ImGui::SameLine();
    if (ImGui::Button("Detect")) {
        p.gameWin64 = FindGameFolder();
        p.found = !p.gameWin64.empty();
        p.gameExe = p.found ? p.gameWin64 + "Borderlands2VR.exe" : "";
        p.status = p.found ? "Found: " + p.gameWin64 : "Not found - set the path above.";
    }

    if (ImGui::Button("Patch game (install mod)")) { p.status = PatchGame(p.gameWin64, p.dllSrc, ini.raw).msg; }
    ImGui::SameLine();
    if (ImGui::Button("Create desktop shortcut")) { p.status = CreateShortcut(p.gameWin64).msg; }
    if (!p.status.empty()) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s", p.status.c_str());
    ImGui::Separator();

    // ---- config (scrolls) ----
    ImGui::BeginChild("##cfg", ImVec2(0, 0), false);

    { Section("Master switches");
      BoolKey(ini, "enabled", "Enabled", "Turn the whole mod on or off.");
      BoolKey(ini, "log", "Logging", "Write diagnostics to openvr_proxy.log next to the game.");
      IntKey(ini, "log_frames", "Log every N frames", "How often a sample line is written to the log.", 0, 10000);
    }
    { Section("Aim smoothing (base)");
      BoolKey(ini, "smooth_hands", "Smooth hands", "Filter the hand poses (this is the gun aim).");
      BoolKey(ini, "smooth_head", "Smooth head view", "Filter the head pose. Usually leave OFF - it adds view lag.");
      FloatKey(ini, "min_cutoff", "Position cutoff (Hz)", "How steady the hands are at rest. Lower = steadier, more lag.", 0.1f, 30.0f, "%.2f");
      FloatKey(ini, "beta", "Position beta", "How fast the filter opens up when you move.", 0.0f, 20.0f, "%.2f");
      FloatKey(ini, "derivative_cutoff", "Speed smoothing (Hz)", "How fast the speed estimate reacts. High = reacts to movement onset instantly.", 0.1f, 30.0f, "%.2f");
      FloatKey(ini, "min_cutoff_rot", "Rotation cutoff (Hz)", "Rotation steadiness - the knob that matters for aiming.", 0.1f, 30.0f, "%.2f");
      FloatKey(ini, "beta_rot", "Rotation beta", "How fast the aim opens up when you turn.", 0.0f, 20.0f, "%.2f");
      FloatKey(ini, "predict_ms", "Prediction (ms)", "Extra lookahead. Keep 0 - prediction is the one thing that can overshoot.", 0.0f, 100.0f, "%.1f");
    }
    { Section("Trigger (aim / zoom) gate");
      static const char* tg[] = { "0 = ignore", "1 = stronger while held", "2 = only while held" };
      ComboKey(ini, "trigger_gate", "Trigger gate", "When the stronger trigger profile applies. 1 is the usual choice.", tg, 3);
      FloatKey(ini, "trigger_threshold", "Trigger threshold", "How far the trigger must be pulled to count as held. Keep it low.", 0.0f, 1.0f, "%.2f");
      FloatKey(ini, "zoom_mult", "Aim position strength", "How much heavier position smoothing gets while the trigger is held.", 1.0f, 100.0f, "%.2f");
      FloatKey(ini, "zoom_mult_rot", "Aim rotation strength", "Same, for rotation - the important one while aiming.", 0.0f, 100.0f, "%.2f");
    }
    { Section("Two-handed grip");
      BoolKey(ini, "twohand", "Two-hand grip", "Lock the gun aim to the line between your two hands.");
      BoolKey(ini, "twohand_gate", "Require button", "Only engage while the button below is held.");
      static const char* btn[] = { "grip", "trigger" };
      ComboKey(ini, "twohand_button", "Two-hand button", "Which button engages two-hand mode.", btn, 2);
      static const char* hh[] = { "offhand", "gun" };
      ComboKey(ini, "twohand_hand", "Button hand", "Which controller owns that button. offhand avoids the game's reload bind.", hh, 2);
      static const char* gh[] = { "left", "right" };
      ComboKey(ini, "gun_hand", "Gun hand", "Which hand holds the gun. left = lefty mode.", gh, 2);
      static const char* ax[] = { "+x", "-x", "+y", "-y", "+z", "-z" };
      ComboKey(ini, "aim_axis", "Barrel axis", "Which local axis of the gun hand points down the barrel.", ax, 6);
      FloatKey(ini, "twohand_min_dist", "Min hand distance", "Ignore the grip if the hands are closer than this (metres).", 0.0f, 2.0f, "%.3f");
      FloatKey(ini, "twohand_max_dist", "Max hand distance", "Ignore the grip if the hands are further apart than this.", 0.0f, 2.0f, "%.3f");
      FloatKey(ini, "twohand_blend", "Snap strength", "How hard the aim snaps to the line. 1 = fully locked.", 0.0f, 1.0f, "%.2f");
    }
    { Section("Two-hand smoothing profile");
      FloatKey(ini, "twohand_min_cutoff", "Position cutoff (Hz)##2h", "Position steadiness while two-handing.", 0.1f, 30.0f, "%.2f");
      FloatKey(ini, "twohand_beta", "Position beta##2h", "How fast it opens up when you move while two-handing.", 0.0f, 20.0f, "%.2f");
      FloatKey(ini, "twohand_min_cutoff_rot", "Rotation cutoff (Hz)##2h", "Rotation steadiness while two-handing.", 0.1f, 30.0f, "%.2f");
      FloatKey(ini, "twohand_beta_rot", "Rotation beta##2h", "", 0.0f, 20.0f, "%.2f");
    }
    { Section("Catch-up");
      FloatKey(ini, "catchup_pos", "Position catch-up", "Extra stiffness per metre of lag. 0 = off. Kills a big lag without touching a small tremor.", 0.0f, 1000.0f, "%.0f");
      FloatKey(ini, "catchup_rot", "Rotation catch-up", "Same, per radian of lag.", 0.0f, 1000.0f, "%.0f");
    }

    ImGui::EndChild();
}
