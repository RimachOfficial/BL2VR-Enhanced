// Pose smoothing payload that runs INSIDE the game, injected via our
// openvr_api.dll proxy (the game always loads that one).
//
// It waits for Virtual Desktop's Oculus runtime to be loaded, then installs an
// inline hook on its ovr_GetTrackingState and runs the controller poses through
// a One Euro filter. Oculus SDK, not OpenXR - BL2VR has no OpenXR code.
#pragma once
#include <cstdint>

void Bl2Log(const char* fmt, ...);   // implemented in proxy.cpp
void InstallOvrHookThread();          // run this on its own thread