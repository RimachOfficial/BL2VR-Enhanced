# Security policy

## Reporting a vulnerability

If you find a security issue in this project (e.g. the proxy DLL loads arbitrary config, the
installer writes into a game directory, the panel executes shell operations), please report it
**privately**:

- Use GitHub's **Private vulnerability reporting** for this repository (Security tab →
  Report a vulnerability), or
- Open a minimal issue asking for a private channel, without details.

Please do not open a public issue for exploitable vulnerabilities. Expect an acknowledgement
within a reasonable time; this is a hobby project run by one person plus an AI pair programmer.

## Scope notes

- This mod runs **inside your own game process** by design (that is what a DLL proxy is).
  Only install binaries you built yourself or downloaded from this repository's official releases.
- The repository **never ships game files or third-party runtime DLLs**. If a download claims to
  be "BL2VR Enhanced" and contains `startup.upk`, `LibOVRRT64_1.dll`, or an `openvr_api.dll` you
  did not build — treat it as hostile.
- The mod does not touch the network, does not phone home, and writes only to its own log file
  and `bl2vr_grip.ini`.
