// game.h - find the BL2VR install, patch it (drop the proxy DLL + ini), and create a
// desktop shortcut that launches the enhanced game. Header-only, no ImGui dependency.
#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <windows.h>
#include <shlobj.h>

struct Patcher {
    std::string gameWin64;   // ...\Borderlands 2 VR\Binaries\Win64 folder
    std::string gameExe;     // ...\Borderlands2VR.exe
    std::string dllSrc;      // bundled openvr_api.dll (next to this tool)
    std::string status;      // last detect/patch/shortcut message
    bool statusOk = true;    // status color: green (ok) / red (error)
    bool found = false;
};

// absolute path to this tool's own directory (where the bundled openvr_api.dll sits)
static std::string ExeDir() {
    char buf[MAX_PATH] = { 0 };
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf);
    size_t i = s.find_last_of("\\/");
    return (i == std::string::npos) ? "" : s.substr(0, i + 1);
}

static std::string RegGetString(HKEY root, const char* sub, const char* val) {
    HKEY k = nullptr; char buf[512] = { 0 }; DWORD n = sizeof(buf);
    if (RegOpenKeyExA(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return "";
    RegQueryValueExA(k, val, nullptr, nullptr, (BYTE*)buf, &n);
    RegCloseKey(k);
    return buf;
}

// every Steam library root (install folder + the libraryfolders.vdf list + defaults)
static std::vector<std::string> SteamLibraries() {
    std::vector<std::string> v;
    std::string sp = RegGetString(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath");
    if (!sp.empty()) v.push_back(sp);
    if (!sp.empty()) {
        std::ifstream f(sp + "\\steamapps\\libraryfolders.vdf");
        std::string line;
        while (std::getline(f, line)) {
            size_t a = line.find("\"path\"");
            if (a == std::string::npos) continue;
            size_t q1 = line.find('"', a + 6);
            size_t q2 = (q1 == std::string::npos) ? std::string::npos : line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos)
                v.push_back(line.substr(q1 + 1, q2 - q1 - 1));
        }
    }
    v.push_back("C:\\Program Files (x86)\\Steam");
    v.push_back("C:\\Program Files\\Steam");
    return v;
}

static bool FileExists(const std::string& p) {
    return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// returns ...\Borderlands 2 VR\Binaries\Win64\ (with trailing backslash), or "" if not found
static std::string FindGameFolder() {
    for (auto& root : SteamLibraries()) {
        std::string w = root + "\\steamapps\\common\\Borderlands 2 VR\\Binaries\\Win64\\";
        if (FileExists(w + "Borderlands2VR.exe")) return w;
    }
    return "";
}

struct PatchResult { bool ok = false; std::string msg; };

static PatchResult PatchGame(const std::string& win64, const std::string& dllSrc, const std::string& iniText) {
    PatchResult r;
    if (win64.empty()) { r.msg = "Game folder not found - click Detect."; return r; }
    if (!FileExists(win64 + "Borderlands2VR.exe")) { r.msg = "Borderlands2VR.exe not found in " + win64; return r; }
    if (!FileExists(dllSrc)) { r.msg = "openvr_api.dll not found next to this tool: " + dllSrc; return r; }
    if (!CopyFileA(dllSrc.c_str(), (win64 + "openvr_api.dll").c_str(), FALSE)) {
        r.msg = "Could not write openvr_api.dll (is the game running? close it first).";
        return r;
    }
    std::ofstream o(win64 + "bl2vr_grip.ini", std::ios::binary);
    if (!o) { r.msg = "Could not write bl2vr_grip.ini."; return r; }
    o.write(iniText.data(), (std::streamsize)iniText.size());
    o.close();
    r.ok = true;
    r.msg = "Patched: openvr_api.dll + bl2vr_grip.ini written.";
    return r;
}

static PatchResult CreateShortcut(const std::string& win64) {
    PatchResult r;
    std::string exe = win64 + "Borderlands2VR.exe";
    if (!FileExists(exe)) { r.msg = "Borderlands2VR.exe not found."; return r; }

    char desk[MAX_PATH] = { 0 };
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desk))) {
        r.msg = "Could not find the desktop folder."; return r;
    }
    std::string lnk = std::string(desk) + "\\BL2VR (enhanced).lnk";

    HRESULT hr = E_FAIL;
    CoInitialize(nullptr);
    IShellLinkA* link = nullptr;
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkA, (void**)&link))) {
        link->SetPath(exe.c_str());
        link->SetWorkingDirectory(win64.c_str());
        link->SetDescription("Borderlands 2 VR with the BL2VR grip + aim smoothing enhancement");
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, (void**)&pf))) {
            wchar_t wlnk[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, lnk.c_str(), -1, wlnk, MAX_PATH);
            hr = pf->Save(wlnk, TRUE);
            pf->Release();
        }
        link->Release();
    }
    CoUninitialize();
    if (FAILED(hr)) { r.msg = "Could not create the shortcut."; return r; }
    r.ok = true;
    r.msg = "Desktop shortcut created: BL2VR (enhanced)";
    return r;
}
