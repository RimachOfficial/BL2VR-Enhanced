// ui.h - the config panel widgets. Each widget edits + saves the matching ini key immediately.
#pragma once
#include "ini.h"
#include "imgui.h"
#include <cstdio>

static void FloatKey(IniFile& ini, const char* key, const char* label, float vmin, float vmax, const char* fmt = "%.3f") {
    float f = (float)ini.GetD(key, 0.0);
    if (ImGui::DragFloat(label, &f, (vmax - vmin) * 0.01f, vmin, vmax, fmt)) {
        char b[64]; snprintf(b, sizeof(b), fmt, f);
        ini.SetAndSave(key, b);
    }
}
static void IntKey(IniFile& ini, const char* key, const char* label, int vmin, int vmax) {
    int v = ini.GetI(key, 0);
    if (ImGui::DragInt(label, &v, 1, vmin, vmax)) {
        char b[32]; snprintf(b, sizeof(b), "%d", v);
        ini.SetAndSave(key, b);
    }
}
static void BoolKey(IniFile& ini, const char* key, const char* label) {
    bool b = ini.GetI(key, 0) != 0;
    if (ImGui::Checkbox(label, &b)) ini.SetAndSave(key, b ? "1" : "0");
}
static void ComboKey(IniFile& ini, const char* key, const char* label, const char* const* opts, int n) {
    std::string cur = ini.Get(key, opts[0]);
    int idx = 0;
    for (int i = 0; i < n; i++) if (cur == opts[i]) { idx = i; break; }
    if (ImGui::Combo(label, &idx, opts, n)) ini.SetAndSave(key, opts[idx]);
}

static void DrawUI(IniFile& ini, char* path, size_t pathsz) {
    ImGui::TextUnformatted("bl2vr_grip.ini - saved live, the mod hot-reloads in ~2s");
    ImGui::SetNextItemWidth(-120.0f);
    if (ImGui::InputText("##path", path, pathsz, ImGuiInputTextFlags_EnterReturnsTrue)) ini.Load(path);
    ImGui::SameLine();
    if (ImGui::Button("Reload")) ini.Load(path);

    if (ImGui::CollapsingHeader("Master", ImGuiTreeNodeFlags_DefaultOpen)) {
        BoolKey(ini, "enabled", "enabled");
        BoolKey(ini, "log", "log");
        IntKey(ini, "log_frames", "log_frames", 0, 10000);
    }
    if (ImGui::CollapsingHeader("Base smoothing", ImGuiTreeNodeFlags_DefaultOpen)) {
        BoolKey(ini, "smooth_hands", "smooth_hands");
        BoolKey(ini, "smooth_head", "smooth_head");
        FloatKey(ini, "min_cutoff", "min_cutoff (Hz)", 0.1f, 30.0f, "%.2f");
        FloatKey(ini, "beta", "beta", 0.0f, 20.0f, "%.2f");
        FloatKey(ini, "derivative_cutoff", "derivative_cutoff (Hz)", 0.1f, 30.0f, "%.2f");
        FloatKey(ini, "min_cutoff_rot", "min_cutoff_rot (Hz)", 0.1f, 30.0f, "%.2f");
        FloatKey(ini, "beta_rot", "beta_rot", 0.0f, 20.0f, "%.2f");
        FloatKey(ini, "predict_ms", "predict_ms", 0.0f, 100.0f, "%.1f");
    }
    if (ImGui::CollapsingHeader("Trigger (zoom) gate")) {
        static const char* tg[] = { "0 = ignore", "1 = stronger while held", "2 = only while held" };
        ComboKey(ini, "trigger_gate", "trigger_gate", tg, 3);
        FloatKey(ini, "trigger_threshold", "trigger_threshold", 0.0f, 1.0f, "%.2f");
        FloatKey(ini, "zoom_mult", "zoom_mult", 1.0f, 100.0f, "%.2f");
        FloatKey(ini, "zoom_mult_rot", "zoom_mult_rot", 0.0f, 100.0f, "%.2f");
    }
    if (ImGui::CollapsingHeader("Two-handed grip")) {
        BoolKey(ini, "twohand", "twohand");
        BoolKey(ini, "twohand_gate", "twohand_gate");
        static const char* btn[] = { "grip", "trigger" };
        ComboKey(ini, "twohand_button", "twohand_button", btn, 2);
        static const char* hh[] = { "offhand", "gun" };
        ComboKey(ini, "twohand_hand", "twohand_hand", hh, 2);
        static const char* gh[] = { "left", "right" };
        ComboKey(ini, "gun_hand", "gun_hand", gh, 2);
        static const char* ax[] = { "+x", "-x", "+y", "-y", "+z", "-z" };
        ComboKey(ini, "aim_axis", "aim_axis", ax, 6);
        FloatKey(ini, "twohand_min_dist", "twohand_min_dist", 0.0f, 2.0f, "%.3f");
        FloatKey(ini, "twohand_max_dist", "twohand_max_dist", 0.0f, 2.0f, "%.3f");
        FloatKey(ini, "twohand_blend", "twohand_blend", 0.0f, 1.0f, "%.2f");
    }
    if (ImGui::CollapsingHeader("Two-hand filter profile")) {
        FloatKey(ini, "twohand_min_cutoff", "twohand_min_cutoff", 0.1f, 30.0f, "%.2f");
        FloatKey(ini, "twohand_beta", "twohand_beta", 0.0f, 20.0f, "%.2f");
        FloatKey(ini, "twohand_min_cutoff_rot", "twohand_min_cutoff_rot", 0.1f, 30.0f, "%.2f");
        FloatKey(ini, "twohand_beta_rot", "twohand_beta_rot", 0.0f, 20.0f, "%.2f");
    }
    if (ImGui::CollapsingHeader("Catch-up")) {
        FloatKey(ini, "catchup_pos", "catchup_pos", 0.0f, 1000.0f, "%.0f");
        FloatKey(ini, "catchup_rot", "catchup_rot", 0.0f, 1000.0f, "%.0f");
    }
}
