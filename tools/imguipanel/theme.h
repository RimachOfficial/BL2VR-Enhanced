// theme.h - custom look for bl2vr_panel: colors, rounding, fonts, DPI scaling.
// Style follows the docs pattern (StyleColorsDark as base, then override), fonts load
// from the Windows system font so the exe ships no font data.
#pragma once
#include "imgui.h"

// ---- palette (amber accent = Borderlands loot rarity, on a neutral dark slate) ----
inline const ImVec4 kAccent      = ImVec4(1.00f, 0.69f, 0.13f, 1.00f);   // #FFB020
inline const ImVec4 kAccentDim   = ImVec4(0.55f, 0.38f, 0.07f, 1.00f);
inline const ImVec4 kText        = ImVec4(0.91f, 0.91f, 0.92f, 1.00f);
inline const ImVec4 kTextDim     = ImVec4(0.58f, 0.60f, 0.64f, 1.00f);
inline const ImVec4 kOk          = ImVec4(0.35f, 0.78f, 0.42f, 1.00f);
inline const ImVec4 kErr         = ImVec4(0.94f, 0.42f, 0.42f, 1.00f);
inline const ImVec4 kWindowBgC   = ImVec4(0.086f, 0.090f, 0.102f, 1.00f); // #161719
inline const ImVec4 kSidebarBg   = ImVec4(0.062f, 0.065f, 0.075f, 1.00f);
inline const ImVec4 kCardBg      = ImVec4(0.110f, 0.115f, 0.130f, 1.00f);

struct PanelFonts {
    ImFont* body   = nullptr;   // 17 px Segoe UI
    ImFont* bold   = nullptr;   // 17 px Segoe UI Bold
    ImFont* title  = nullptr;   // 22 px Segoe UI Bold
};
inline PanelFonts g_fonts;
inline float g_dpi = 1.0f;   // set once at startup from the monitor DPI

// base theme -> override -> scale every size for the monitor DPI
inline void ApplyTheme(float dpiScale) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();

    style.WindowRounding    = 0.0f;    // the app window is borderless; children carry the rounding
    style.ChildRounding     = 8.0f;
    style.FrameRounding     = 6.0f;
    style.GrabRounding      = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.PopupRounding     = 8.0f;
    style.TabRounding       = 6.0f;
    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.ItemSpacing       = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize     = 12.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                 = kText;
    c[ImGuiCol_TextDisabled]         = kTextDim;
    c[ImGuiCol_WindowBg]             = kWindowBgC;
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = kCardBg;
    c[ImGuiCol_Border]               = ImVec4(1, 1, 1, 0.06f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.145f, 0.150f, 0.170f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.185f, 0.192f, 0.215f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.225f, 0.232f, 0.258f, 1.00f);
    c[ImGuiCol_Button]               = ImVec4(0.165f, 0.170f, 0.192f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.220f, 0.226f, 0.252f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.270f, 0.277f, 0.305f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.260f, 0.180f, 0.055f, 1.00f); // amber tint family
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.380f, 0.262f, 0.078f, 1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.460f, 0.318f, 0.092f, 1.00f);
    c[ImGuiCol_CheckMark]            = kAccent;
    c[ImGuiCol_SliderGrab]           = kAccentDim;
    c[ImGuiCol_SliderGrabActive]     = kAccent;
    c[ImGuiCol_Separator]            = ImVec4(1, 1, 1, 0.07f);
    c[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.22f, 0.23f, 0.26f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.31f, 0.35f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.38f, 0.39f, 0.43f, 1.00f);
    c[ImGuiCol_NavHighlight]         = kAccent;

    style.ScaleAllSizes(dpiScale);
}

// system fonts, no bundled data; graceful fallback to the built-in font
inline void LoadFonts(ImGuiIO& io, float dpiScale) {
    const float bodyS  = 17.0f * dpiScale;
    const float titleS = 22.0f * dpiScale;
    io.FontDefault = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", bodyS);
    g_fonts.body = io.FontDefault ? io.FontDefault : io.Fonts->AddFontDefault();
    g_fonts.bold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", bodyS);
    if (!g_fonts.bold) g_fonts.bold = g_fonts.body;
    g_fonts.title = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", titleS);
    if (!g_fonts.title) g_fonts.title = g_fonts.bold;
}
