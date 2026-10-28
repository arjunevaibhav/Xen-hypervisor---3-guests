# Xen Hypervisor — 3 Guests

## Tinker Board R2.0 (RK3288): Yocto + Xen Mixed-Criticality System

Xen 4.16 on an ASUS Tinker Board R2.0 running three operating systems at once:

- Yocto Linux 5.15 (dom0)
- Headless Android 7.0
- FreeRTOS 11.1 on its own CPU core with the RTDS real-time scheduler

The project also includes an automotive CAN bench and an HDMI instrument-cluster dashboard.

## Project Documentation

- `docs/PROJECT-DOCUMENTATION.md` — Beginner's guide explaining the project step by step.
- `docs/TECHNICAL-REFERENCE.md` — Technical details, code, addresses, and checksums.
- `meta-tinker-xen/` — Yocto layer containing the project changes. See its README.
- `diagnostics/` — Engineering report (`REPORT.md`, sections A–Z), board test logs, and build logs.

## Build Environment

The build uses Yocto Kirkstone with Poky, meta-openembedded, meta-virtualization, meta-arm, and meta-rockchip.

For setup and build instructions, see `docs/PROJECT-DOCUMENTATION.md`, Chapter 4.

## Android Image

The Android system image is downloaded during the build from Google's SDK repository. It is not included in this repository. Refer to the applicable Android SDK license terms.
