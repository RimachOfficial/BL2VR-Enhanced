#pragma once
#include "ini.h"
#include "game.h"
#include "theme.h"
#include "profiles.h"
#include "imgui.h"
#include "imgui_internal.h"   // ImGuiWindow / GetCurrentWindow for the custom widgets
#include <cfloat>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Custom widgets: InvisibleButton + ImDrawList (decide behaviour, draw it).
// ---------------------------------------------------------------------------

// Full-row toggle: label left, pill switch right, the whole row is the hitbox.
// Returns true on click (value already flipped in *v).
static bool ToggleRow(const char* label, bool* v, const char* desc = nullptr) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const float w    = ImGui::GetContentRegionAvail().x;
    const float h    = ImGui::GetFrameHeight() + 4.0f * g_dpi;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(w, h));
    if (clicked) *v = !*v;
    const bool hovered = ImGui::IsItemHovered();
    if (desc && hovered) ImGui::SetTooltip("%s", desc);

    ImDrawList* dl = window->DrawList;
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(g_fonts.body, ImGui::GetFontSize(),
                ImVec2(pos.x + 2.0f * g_dpi, pos.y + (h - ts.y) * 0.5f),
                ImGui::GetColorU32(hovered ? kText : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)), label);

    const float ph = 20.0f * g_dpi, pw = ph * 1.75f;
    const ImVec2 p0(pos.x + w - pw, pos.y + (h - ph) * 0.5f);
    const ImVec2 p1(p0.x + pw, p0.y + ph);
    ImVec4 bg = *v ? kAccent : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    if (hovered) { bg.x += 0.08f; bg.y += 0.08f; bg.z += 0.08f; }
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(bg), ph * 0.5f);
    const float kr = ph * 0.5f - 3.0f * g_dpi;
    const float kx = *v ? (p1.x - 3.0f * g_dpi - kr) : (p0.x + 3.0f * g_dpi + kr);
    dl->AddCircleFilled(ImVec2(kx, (p0.y + p1.y) * 0.5f), kr,
                        ImGui::GetColorU32(ImVec4(0.96f, 0.96f, 0.97f, 1.0f)));
    return clicked;
}

// Accent-bar section header in bold.
static void SectionHeader(const char* t) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f * g_dpi);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeightWithSpacing();
    window->DrawList->AddRectFilled(ImVec2(pos.x, pos.y + 1.0f),
                                    ImVec2(pos.x + 3.0f * g_dpi, pos.y + h - 2.0f),
                                    ImGui::GetColorU32(kAccent), 2.0f);
    ImGui::Dummy(ImVec2(11.0f * g_dpi, h));
    ImGui::SameLine();
    ImGui::PushFont(g_fonts.bold);
    ImGui::TextUnformatted(t);
    ImGui::PopFont();
}

// Sidebar navigation item: active = accent bar + tinted row.
static bool NavItem(const char* label, bool active, float width) {
    const float h = 34.0f * g_dpi;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) {
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h),
                          ImGui::GetColorU32(ImVec4(0.26f, 0.18f, 0.055f, 0.60f)), 6.0f * g_dpi);
        dl->AddRectFilled(ImVec2(pos.x + 2.0f, pos.y + 7.0f * g_dpi),
                          ImVec2(pos.x + 5.0f * g_dpi, pos.y + h - 7.0f * g_dpi),
                          ImGui::GetColorU32(kAccent), 2.0f);
    } else if (hovered) {
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h),
                          ImGui::GetColorU32(ImVec4(1, 1, 1, 0.04f)), 6.0f * g_dpi);
    }
    const ImVec4 col = active ? kAccent : (hovered ? kText : kTextDim);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(g_fonts.body, ImGui::GetFontSize(),
                ImVec2(pos.x + 14.0f * g_dpi, pos.y + (h - ts.y) * 0.5f),
                ImGui::GetColorU32(col), label);
    return clicked;
}

// Accent-filled primary action button.
static bool PrimaryButton(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.76f, 0.28f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.87f, 0.59f, 0.09f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.07f, 0.06f, 0.03f, 1.00f));
    const bool r = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    return r;
}

// ---------------------------------------------------------------------------
// ini-backed helpers (same save semantics as before: write only on change)
// ---------------------------------------------------------------------------
static void Desc(const char* d) { if (d && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d); }

static void FloatKey(IniFile& ini, const char* key, const char* label, const char* d,
                     float vmin, float vmax, const char* fmt = "%.2f") {
    ImGui::PushID(key);                     // unique scope: several sliders share the "##v" label
    float f = (float)ini.GetD(key, 0.0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##v", &f, vmin, vmax, fmt)) {
        char b[64]; snprintf(b, sizeof(b), fmt, f); ini.SetAndSave(key, b);
    }
    Desc(d);
    ImGui::PopID();
}
static void IntKey(IniFile& ini, const char* key, const char* label, const char* d, int vmin, int vmax) {
    ImGui::PushID(key);
    int v = ini.GetI(key, 0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##v", &v, vmin, vmax)) { char b[32]; snprintf(b, sizeof(b), "%d", v); ini.SetAndSave(key, b); }
    Desc(d);
    ImGui::PopID();
}
static void BoolKey(IniFile& ini, const char* key, const char* label, const char* d) {
    ImGui::PushID(key);
    bool b = ini.GetI(key, 0) != 0;
    if (ToggleRow(label, &b, d)) ini.SetAndSave(key, b ? "1" : "0");
    ImGui::PopID();
}
static void ComboKey(IniFile& ini, const char* key, const char* label, const char* d,
                     const char* const* opts, const char* const* vals, int n) {
    ImGui::PushID(key);
    std::string cur = ini.Get(key, vals[0]);
    int idx = 0;
    for (int i = 0; i < n; i++) {
        const size_t L = strlen(vals[i]);
        if (cur.compare(0, L, vals[i]) == 0 && (cur.size() == L || cur[L] == ' ')) { idx = i; break; }
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##v", &idx, opts, n)) ini.SetAndSave(key, vals[idx]);
    Desc(d);
    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------
enum class Page { Profiles, Install, Smoothing, Trigger, Grip, Gunstock, About };
static Page g_page = Page::Profiles;

// Profile card: full-width row, name + active badge + wrapped description. Click = apply.
static bool ProfileCard(const Profile& pr, bool active) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const float w    = ImGui::GetContentRegionAvail().x;
    const float padX = 16.0f * g_dpi;
    const float wrapW = w - padX * 2.0f - (active ? 70.0f * g_dpi : 0.0f);   // leave room for the ACTIVE chip
    const ImVec2 nameSize = ImGui::CalcTextSize(pr.name);
    const ImVec2 descSize = ImGui::CalcTextSize(pr.desc, nullptr, false, wrapW);
    const float h = 12.0f * g_dpi + nameSize.y + 5.0f * g_dpi + descSize.y + 12.0f * g_dpi;
    const ImVec2 pos = ImGui::GetCursorScreenPos();

    const bool clicked = ImGui::InvisibleButton(pr.name, ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h),
                      ImGui::GetColorU32(active ? ImVec4(0.26f, 0.18f, 0.055f, 0.45f)
                                                : ImVec4(1, 1, 1, hovered ? 0.05f : 0.025f)),
                      8.0f * g_dpi);
    if (active) {
        dl->AddRectFilled(ImVec2(pos.x + 2.0f, pos.y + 10.0f * g_dpi),
                          ImVec2(pos.x + 5.0f * g_dpi, pos.y + h - 10.0f * g_dpi),
                          ImGui::GetColorU32(kAccent), 2.0f);
    }
    // name (bold); dim it a little when not active and not hovered
    dl->AddText(g_fonts.bold, g_fonts.bold->FontSize, ImVec2(pos.x + padX, pos.y + 12.0f * g_dpi),
                ImGui::GetColorU32(active ? kAccent : (hovered ? kText : ImVec4(0.80f, 0.81f, 0.83f, 1.0f))),
                pr.name);
    if (active) {
        const char* chip = "ACTIVE";
        const ImVec2 cs = ImGui::CalcTextSize(chip);
        dl->AddText(g_fonts.body, ImGui::GetFontSize(),
                    ImVec2(pos.x + w - cs.x - padX, pos.y + 14.0f * g_dpi),
                    ImGui::GetColorU32(kAccent), chip);
    }
    // wrapped description under the name
    ImGui::RenderTextWrapped(ImVec2(pos.x + padX, pos.y + 12.0f * g_dpi + nameSize.y + 5.0f * g_dpi),
                             pr.desc, nullptr, wrapW);
    return clicked;
}

static void PageProfiles(IniFile& ini, Patcher& p) {
    ImGui::PushFont(g_fonts.title); ImGui::TextUnformatted("Profiles"); ImGui::PopFont();
    ImGui::TextColored(kTextDim, "One click sets every feel value. Hardware setup (hands, buttons, axis) is never touched.");

    const int active = ActiveProfile(ini);
    ImGui::Spacing();

    for (int i = 0; i < kProfileCount; i++) {
        const bool isActive = (i == active);
        ImGui::PushID(i);
        if (ProfileCard(kProfiles[i], isActive)) {
            ApplyProfile(ini, kProfiles[i]);
            p.status = std::string("Profile applied: ") + kProfiles[i].name;
            p.statusOk = true;
        }
        ImGui::PopID();
        ImGui::Spacing();
    }

    // Custom slot: shown when hand-tuned values match no profile
    if (active < 0) {
        ImGui::PushID("custom");
        ImGui::Spacing();
        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float h = 12.0f * g_dpi + ImGui::GetTextLineHeight() + 5.0f * g_dpi
                        + ImGui::CalcTextSize("Hand-tuned values on the other pages. Pick a profile above to replace them.").y
                        + 12.0f * g_dpi;
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h),
                          ImGui::GetColorU32(ImVec4(1, 1, 1, 0.025f)), 8.0f * g_dpi);
        dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.10f)),
                    8.0f * g_dpi, 0, 1.0f * g_dpi);
        dl->AddText(g_fonts.bold, g_fonts.bold->FontSize, ImVec2(pos.x + 16.0f * g_dpi, pos.y + 12.0f * g_dpi),
                    ImGui::GetColorU32(kTextDim), "Custom");
        ImGui::RenderTextWrapped(ImVec2(pos.x + 16.0f * g_dpi, pos.y + 12.0f * g_dpi + ImGui::GetTextLineHeight() + 5.0f * g_dpi),
                                 "Hand-tuned values on the other pages. Pick a profile above to replace them.",
                                 nullptr, w - 32.0f * g_dpi);
        ImGui::PopID();
    }
}

static std::string IniPathFor(const Patcher& p) {
    return p.found ? p.gameWin64 + "bl2vr_grip.ini" : "bl2vr_grip.ini";
}

static void PageInstall(IniFile& ini, Patcher& p) {
    ImGui::PushFont(g_fonts.title); ImGui::TextUnformatted("Install"); ImGui::PopFont();
    ImGui::TextColored(kTextDim, "Find the game, patch it, done. The mod never edits game files.");

    SectionHeader("Game");
    ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Game folder");
    ImGui::SameLine();
    char w64[600] = { 0 };
    strncpy_s(w64, p.gameWin64.c_str(), sizeof(w64) - 1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##w64", w64, sizeof(w64))) {
        p.gameWin64 = w64; p.found = !p.gameWin64.empty();
        p.gameExe = p.found ? p.gameWin64 + "Borderlands2VR.exe" : "";
    }
    Desc("Auto-detected from Steam. Editable if your library lives elsewhere.");
    if (ImGui::Button("Detect from Steam")) {
        p.gameWin64 = FindGameFolder();
        p.found = !p.gameWin64.empty();
        p.gameExe = p.found ? p.gameWin64 + "Borderlands2VR.exe" : "";
        p.status = p.found ? "Found: " + p.gameWin64 : "Not found - set the path above.";
        p.statusOk = p.found;
        ini.Load(IniPathFor(p));   // start editing the real ini where it lives
    }

    SectionHeader("Mod");
    if (PrimaryButton("Patch game (install mod)")) {
        PatchResult r = PatchGame(p.gameWin64, p.dllSrc, ini.raw); p.status = r.msg; p.statusOk = r.ok;
        ini.Load(IniPathFor(p));
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove mod (restore)")) {
        PatchResult r = RestoreGame(p.gameWin64); p.status = r.msg; p.statusOk = r.ok;
    }
    ImGui::SameLine();
    if (ImGui::Button("Launch game")) {
        PatchResult r = LaunchGame(p.gameWin64); p.status = r.msg; p.statusOk = r.ok;
    }
    Desc("Patch backs up the original DLL automatically. Remove puts the game fully back to stock. Launch runs the exe (use with Virtual Desktop).");
    if (ImGui::Button("Create desktop shortcut")) {
        PatchResult r = CreateShortcut(p.gameWin64); p.status = r.msg; p.statusOk = r.ok;
    }
    Desc("Makes a 'BL2VR (enhanced)' shortcut that launches the game directly.");

    SectionHeader("Diagnostics");
    BoolKey(ini, "log", "Logging", "Write diagnostics to openvr_proxy.log next to the game.");
    IntKey(ini, "log_frames", "Log every N frames", "How often a sample line is written to the log.", 0, 10000);
}

static void PageSmoothing(IniFile& ini) {
    ImGui::PushFont(g_fonts.title); ImGui::TextUnformatted("Smoothing"); ImGui::PopFont();
    ImGui::TextColored(kTextDim, "One Euro filter on the controller poses. Smoothing is the base of everything.");

    SectionHeader("Master");
    BoolKey(ini, "enabled", "Enabled", "Turn the whole mod on or off.");
    BoolKey(ini, "smooth_hands", "Smooth hands", "Filter the hand poses (this is the gun aim).");
    BoolKey(ini, "smooth_head", "Smooth head view", "Filter the head pose. Usually leave OFF - it adds view lag.");

    SectionHeader("Base profile");
    FloatKey(ini, "min_cutoff", "Position cutoff (Hz)", "How steady the hands are at rest. Lower = steadier, more lag. Keep 1.5-4.5 Hz.", 0.1f, 30.0f);
    FloatKey(ini, "beta", "Position beta", "How fast the filter opens up when you move.", 0.0f, 20.0f);
    FloatKey(ini, "derivative_cutoff", "Speed smoothing (Hz)", "How fast the speed estimate reacts. High = reacts to movement onset instantly.", 0.1f, 30.0f);
    FloatKey(ini, "min_cutoff_rot", "Rotation cutoff (Hz)", "Rotation steadiness - the knob that matters for aiming.", 0.1f, 30.0f);
    FloatKey(ini, "beta_rot", "Rotation beta", "How fast the aim opens up when you turn.", 0.0f, 20.0f);
    FloatKey(ini, "predict_ms", "Prediction (ms)", "Extra lookahead. Keep 0 - prediction is the one thing that can overshoot.", 0.0f, 100.0f, "%.1f");

    SectionHeader("Advanced");
    BoolKey(ini, "per_thread_filters", "Per-thread filter state",
            "Give each calling thread its own filters + clock. Turn ON only if openvr_proxy.log shows 't=' jumping between different times with different tids - that means the game asks for poses at different prediction horizons and one shared filter would mix timelines. Cost: each thread sees about half the samples, so the velocity estimate is a little noisier. Compare the 'jitter R-rot' line with it OFF and ON.");
}

static void PageTrigger(IniFile& ini) {
    ImGui::PushFont(g_fonts.title); ImGui::TextUnformatted("Trigger"); ImGui::PopFont();
    ImGui::TextColored(kTextDim, "The zoom / aim-down-sights feel: pull the trigger, smoothing gets heavier.");

    SectionHeader("Gate");
    static const char* tg[] = { "0 = ignore", "1 = stronger while held", "2 = only while held" };
    static const char* tgv[] = { "0", "1", "2" };
    ComboKey(ini, "trigger_gate", "Trigger gate", "When the stronger trigger profile applies. 1 is the usual choice.", tg, tgv, 3);
    static const char* zh[] = { "gun hand", "offhand", "either" };
    static const char* zhv[] = { "gun", "offhand", "either" };
    ComboKey(ini, "zoom_hand", "Trigger hand", "Which controller trigger drives this gate. either = whichever pulls harder - pick this if the game aim bind sits on the other hand.", zh, zhv, 3);
    FloatKey(ini, "trigger_threshold", "Trigger threshold", "How far the trigger must be pulled to count as held. Keep it low.", 0.0f, 1.0f);

    SectionHeader("Weight while held");
    FloatKey(ini, "zoom_mult", "Position strength", "How much heavier position smoothing gets while the trigger is held.", 1.0f, 100.0f);
    FloatKey(ini, "zoom_mult_rot", "Rotation strength", "Same, for rotation - the important one while aiming.", 0.0f, 100.0f);
}
static void PageGrip(IniFile& ini) {
    ImGui::PushFont(g_fonts.title); ImGui::TextUnformatted("Two-hand grip"); ImGui::PopFont();
    ImGui::TextColored(kTextDim, "Lock the aim to the line between your two hands.");

    SectionHeader("Engage");
    BoolKey(ini, "twohand", "Two-hand grip", "Master switch for the feature.");
    BoolKey(ini, "twohand_gate", "Require button", "Only engage while the button below is held.");
    static const char* btn[] = { "grip", "trigger" };
    ComboKey(ini, "twohand_button", "Button", "Which button engages two-hand mode.", btn, btn, 2);
    static const char* hh[] = { "offhand", "gun" };
    ComboKey(ini, "twohand_hand", "Button hand", "Which controller owns that button. offhand avoids the game reload bind.", hh, hh, 2);

    SectionHeader("Calibration");
    static const char* gh[] = { "left", "right" };
    ComboKey(ini, "gun_hand", "Gun hand", "Which hand holds the gun. left = lefty mode.", gh, gh, 2);
    static const char* ax[] = { "+x", "-x", "+y", "-y", "+z", "-z" };
    ComboKey(ini, "aim_axis", "Barrel axis", "Which local axis of the gun hand points down the barrel. Use the log helper if unsure.", ax, ax, 6);
    FloatKey(ini, "twohand_min_dist", "Min hand distance (m)", "Ignore the grip if the hands are closer than this.", 0.0f, 2.0f, "%.3f");
    FloatKey(ini, "twohand_max_dist", "Max hand distance (m)", "Ignore the grip if the hands are further apart than this.", 0.0f, 2.0f);
    FloatKey(ini, "twohand_blend", "Snap strength", "How hard the aim snaps to the line. 1 = fully locked.", 0.0f, 1.0f);
}

static void PageGunstock(IniFile& ini) {
    ImGui::PushFont(g_fonts.title); ImGui::TextUnformatted("Virtual gunstock"); ImGui::PopFont();
    ImGui::TextColored(kTextDim, "The hand-to-hand line has its own filter - that is the braced-stock feel.");

    SectionHeader("Stock");
    FloatKey(ini, "stock_cutoff", "Steadiness (Hz)", "How fast the aim follows the line between your hands. Lower = the stock leans the aim slowly instead of snapping. Floored at 1 Hz.", 1.0f, 30.0f);
    FloatKey(ini, "stock_beta", "Wake-up", "How fast the stock follows when you deliberately swing the aim.", 0.0f, 20.0f);
    FloatKey(ini, "stock_zoom_mult", "Trigger weight", "While the trigger is held the stock gets this many times heavier (1 Hz floor). This is the zoom feel in two-hand mode.", 1.0f, 20.0f, "%.1f");

    SectionHeader("Filter override (0 = base profile)");
    FloatKey(ini, "twohand_min_cutoff", "Position cutoff (Hz)##2h", "Hand-filter override while two-handing. 0 = use the base/trigger profile (recommended - the stock line has its own filter).", 0.0f, 30.0f);
    FloatKey(ini, "twohand_beta", "Position beta##2h", "How fast it opens up when you move while two-handing.", 0.0f, 20.0f);
    FloatKey(ini, "twohand_min_cutoff_rot", "Rotation cutoff (Hz)##2h", "0 = use the base/trigger profile (recommended).", 0.0f, 30.0f);
    FloatKey(ini, "twohand_beta_rot", "Rotation beta##2h", "How fast the rotation filter opens up when you turn while two-handing. 0 = use the base profile.", 0.0f, 20.0f);

    SectionHeader("Catch-up (0 = off)");
    FloatKey(ini, "catchup_pos", "Position", "Extra stiffness per metre of lag. Kills a big lag without touching a small tremor.", 0.0f, 1000.0f, "%.0f");
    FloatKey(ini, "catchup_rot", "Rotation", "Same, per radian of lag.", 0.0f, 1000.0f, "%.0f");
}

static void PageAbout() {
    ImGui::PushFont(g_fonts.title); ImGui::TextUnformatted("About"); ImGui::PopFont();
    ImGui::TextColored(kTextDim, "Two-handed virtual gunstock + motion smoothing for Borderlands 2 VR.");
    SectionHeader("Mod");
    ImGui::BulletText("Version 0.1.4");
    ImGui::BulletText("GPL-3.0 - free to use, share and improve");
    ImGui::BulletText("No game files are modified; Remove restores the stock game");
    ImGui::BulletText("Launch through Virtual Desktop (Oculus runtime path)");
    SectionHeader("Source");
    ImGui::BulletText("github.com/RimachOfficial/BL2VR-Enhanced");
    ImGui::BulletText("Downloads: github.com/RimachOfficial/BL2VR-Enhanced/releases");
    SectionHeader("Credits");
    ImGui::BulletText("One Euro filter - Casiez, Roussel & Vogel (CHI 2012)");
    ImGui::BulletText("MinHook (BSD-3-Clause) - Tsuda Kageyu");
    ImGui::BulletText("Dear ImGui (MIT) - Omar Cornut");
    ImGui::Spacing();
    ImGui::TextColored(kTextDim, "Built with AI pair programming.");
}
// ---------------------------------------------------------------------------
// Top level: sidebar + content inside the single borderless window.
// ---------------------------------------------------------------------------
static void DrawUI(IniFile& ini, Patcher& p) {
    const float top   = 46.0f * g_dpi;          // title strip height (main.cpp draws the title)
    const float sideW = 170.0f * g_dpi;
    const float pad   = 14.0f * g_dpi;
    const ImVec2 wpos = ImGui::GetWindowPos();
    const ImVec2 wsz  = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // sidebar background + divider
    dl->AddRectFilled(wpos, ImVec2(wpos.x + sideW, wpos.y + wsz.y), ImGui::GetColorU32(kSidebarBg));
    dl->AddLine(ImVec2(wpos.x + sideW, wpos.y + top), ImVec2(wpos.x + sideW, wpos.y + wsz.y),
                ImGui::GetColorU32(ImVec4(1, 1, 1, 0.06f)));

    // nav
    ImGui::SetCursorPos(ImVec2(6.0f * g_dpi, top + pad));
    const float navW = sideW - 12.0f * g_dpi;
    if (NavItem("Profiles", g_page == Page::Profiles, navW)) g_page = Page::Profiles;
    if (NavItem("Install",   g_page == Page::Install,   navW)) g_page = Page::Install;
    if (NavItem("Smoothing", g_page == Page::Smoothing, navW)) g_page = Page::Smoothing;
    if (NavItem("Trigger",   g_page == Page::Trigger,   navW)) g_page = Page::Trigger;
    if (NavItem("Grip",      g_page == Page::Grip,      navW)) g_page = Page::Grip;
    if (NavItem("Gunstock",  g_page == Page::Gunstock,  navW)) g_page = Page::Gunstock;
    ImGui::SetCursorPosY(wsz.y - top - 14.0f * g_dpi - 34.0f * g_dpi);
    if (NavItem("About", g_page == Page::About, navW)) g_page = Page::About;

    // content area (scrolls when needed)
    ImGui::SetCursorPos(ImVec2(sideW + pad, top + pad));
    ImGui::BeginChild("##content", ImVec2(wsz.x - sideW - pad * 2.0f, wsz.y - top - pad * 2.0f - 28.0f * g_dpi), false);
    switch (g_page) {
        case Page::Profiles:  PageProfiles(ini, p); break;
        case Page::Install:   PageInstall(ini, p); break;
        case Page::Smoothing: PageSmoothing(ini); break;
        case Page::Trigger:   PageTrigger(ini); break;
        case Page::Grip:      PageGrip(ini); break;
        case Page::Gunstock:  PageGunstock(ini); break;
        case Page::About:     PageAbout(); break;
    }
    ImGui::EndChild();

    // footer status bar: status left, hint right; truncate status (with ellipsis) so
    // the two can never collide, and hide the hint entirely on very narrow windows
    const float footY = wpos.y + wsz.y - 22.0f * g_dpi;
    dl->AddLine(ImVec2(wpos.x + sideW, footY - 6.0f * g_dpi), ImVec2(wpos.x + wsz.x, footY - 6.0f * g_dpi),
                ImGui::GetColorU32(ImVec4(1, 1, 1, 0.06f)));
    const char* hint = "config saves instantly - the mod hot-reloads (~2 s)";
    const ImVec2 hts = ImGui::CalcTextSize(hint);
    const float hintX = wpos.x + wsz.x - pad - hts.x;
    const float gap   = 16.0f * g_dpi;
    const float statusX = wpos.x + sideW + pad + 14.0f * g_dpi;
    // the hint is right-aligned, so it fits by itself unless the window is tiny; hide it
    // when it would leave no breathing room in the content area
    const bool drawHint = (hintX - (wpos.x + sideW + pad)) > gap;

    const bool hasStatus = !p.status.empty();
    dl->AddCircleFilled(ImVec2(wpos.x + sideW + pad + 4.0f * g_dpi, footY + 8.0f * g_dpi), 4.0f * g_dpi,
                        ImGui::GetColorU32(hasStatus ? (p.statusOk ? kOk : kErr) : kTextDim));
    if (hasStatus) {
        const float maxW = drawHint ? (hintX - gap - statusX)
                                    : (wpos.x + wsz.x - pad - statusX);
        // ellipsis-truncate to fit: shrink until the string (plus "...") is within maxW
        std::string s = p.status;
        if (maxW > 0) {
            while (!s.empty() && ImGui::CalcTextSize((s + "...").c_str()).x > maxW) s.pop_back();
            if (!s.empty() && s.size() < p.status.size()) s += "...";
        } else {
            s.clear();
        }
        dl->AddText(g_fonts.body, ImGui::GetFontSize(), ImVec2(statusX, footY),
                    ImGui::GetColorU32(p.statusOk ? kText : kTextDim), s.c_str());
    }
    if (drawHint) {
        dl->AddText(g_fonts.body, ImGui::GetFontSize(), ImVec2(hintX, footY),
                    ImGui::GetColorU32(kTextDim), hint);
    }
}



