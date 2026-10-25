# Tinker Board R2.0 (RK3288): Yocto + Xen mixed-criticality system

Xen 4.16 on an ASUS Tinker Board R2.0 running three operating systems at once:
Yocto Linux 5.15 (dom0), headless Android 7.0 and FreeRTOS 11.1 on its own CPU
core with the RTDS real-time scheduler, plus an automotive CAN bench and an
HDMI instrument-cluster dashboard.

- `docs/PROJECT-DOCUMENTATION.md` - beginner's guide, the whole story step by step
- `docs/TECHNICAL-REFERENCE.md` - full technical details (code, addresses, checksums)
- `meta-tinker-xen/` - the Yocto layer with all changes (see its README)
- `diagnostics/` - engineering report (REPORT.md, sections A-Z), board test logs, build logs

Build: Yocto kirkstone with poky, meta-openembedded, meta-virtualization,
meta-arm and meta-rockchip (setup in docs/PROJECT-DOCUMENTATION.md, Chapter 4).

The Android system image is downloaded by the build from Google's SDK
repository (Android SDK License, development use only) and is not part of
this repository.
