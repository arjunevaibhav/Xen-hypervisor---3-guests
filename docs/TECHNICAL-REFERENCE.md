# Tinker Board R2.0 (RK3288) – Yocto + Xen Mixed-Criticality System

**Complete project documentation: from the first boot log to the automotive bench**

| | |
|---|---|
| Board | ASUS Tinker Board R2.0 – Rockchip RK3288 (RK3288W), 4 × Cortex-A17, 2 GB RAM |
| Build system | Yocto Project 4.0 "kirkstone" (Poky) |
| Hypervisor | Xen 4.16.1 (ARM32) |
| Operating systems | Yocto Linux 5.15 (dom0), Android 7.0 AOSP (guest), FreeRTOS 11.1 (real-time guest) |
| Project layer | `~/yocto-rk3288/meta-tinker-xen` |
| Test evidence | `~/yocto-rk3288/diagnostics/` (REPORT.md sections A–Z, UART logs, bitbake logs) |
| Period | 2026-10-22 to 2026-10-25 |
| Final image | `tinker-xen-test-image-tinker-board-20261025094250.rootfs.wic`, sha256 `99a3fec640201d6b8e74c675a8d039f611ef7c67394d18ed555a84a18e5f9412` (flashed and verified) |

---

## Contents

1. [What this project is](#1-what-this-project-is)
2. [Final result – what you get](#2-final-result--what-you-get)
3. [Hardware and host setup](#3-hardware-and-host-setup)
4. [Software stack](#4-software-stack)
5. [System architecture](#5-system-architecture)
6. [Directory trees](#6-directory-trees)
7. [Build environment setup](#7-build-environment-setup)
8. [Project history, stage by stage](#8-project-history-stage-by-stage)
9. [Component reference (code and configuration)](#9-component-reference-code-and-configuration)
10. [Build and flash procedure](#10-build-and-flash-procedure)
11. [Operating the system (commands and expected output)](#11-operating-the-system-commands-and-expected-output)
12. [Testing and verification](#12-testing-and-verification)
13. [All errors and fixes](#13-all-errors-and-fixes)
14. [Known limitations](#14-known-limitations)
15. [Recovery plan](#15-recovery-plan)
16. [Working rules and safety](#16-working-rules-and-safety)
17. [Future work](#17-future-work)
18. [Appendix](#18-appendix)

---

## 1. What this project is

The goal was to run several operating systems **at the same time** on one cheap ARM board, each
isolated from the others by a hypervisor, the way modern cars combine an infotainment system, a
control computer and a safety-critical real-time controller on one chip
("mixed-criticality system").

```
            one board, one SD card, power on → everything starts by itself

   ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
   │  Yocto Linux     │  │  Android 7.0     │  │  FreeRTOS 11.1   │
   │  (control, dom0) │  │  (apps, headless)│  │  (hard real time)│
   └──────────────────┘  └──────────────────┘  └──────────────────┘
   ┌──────────────────────────────────────────────────────────────┐
   │                    Xen 4.16 hypervisor                       │
   └──────────────────────────────────────────────────────────────┘
   ┌──────────────────────────────────────────────────────────────┐
   │    ASUS Tinker Board R2.0 – RK3288, 4 × Cortex-A17, 2 GB     │
   └──────────────────────────────────────────────────────────────┘
```

The RK3288 was never supported by Xen: the bootloader could not start a hypervisor, Xen did not
know the CPU, could not start the other CPU cores, could not reboot the board, and the Linux
kernel broke Xen's clock. All of that had to be found and fixed (Section 8). The project ends with
an automotive bench application: a simulated vehicle on a CAN bus, a read-only gateway into the
real-time OS, real-time safety checks, and an instrument-cluster dashboard for HDMI.

---

## 2. Final result – what you get

### 2.1 What runs on the board

| Domain | OS | CPUs | Memory | Scheduler | Job |
|---|---|---|---|---|---|
| Domain-0 | Yocto Linux 5.15 (Poky kirkstone) | 1 vCPU (Pool-0: pCPU 0-2) | 512 MB | credit2 | Control OS: Xen tools, network, CAN gateway, dashboard, watchdog |
| android1 | Android 7.0 AOSP (Linux 4.14 guest kernel) | 2 vCPUs (Pool-0) | 768 MB | credit2 | Application OS, headless, adb over network |
| rtos1 | FreeRTOS 11.1 | 1 vCPU, alone on pCPU 3 | 16 MB | RTDS (200 µs every 1 ms) | Hard real-time: 10 ms control task, vehicle safety checks |

### 2.2 Features and their status

| Feature | Status |
|---|---|
| Power on → U-Boot → Xen → dom0 → guests start, no key pressed | ✅ tested |
| Xen on all 4 CPUs (PSCI added to U-Boot) | ✅ tested |
| `reboot` / `poweroff` from dom0 | ✅ tested |
| Hardware watchdog resets a hung system (~43 s) | ✅ tested |
| FreeRTOS 10 ms task: 9958–10040 µs while Android runs | ✅ tested |
| Android boots (`sys.boot_completed=1`), adb from dom0 | ✅ tested |
| Guest network 192.168.100.0/24, NAT rule in dom0 | ✅ tested (internet itself untested: no Ethernet) |
| Temperatures (CPU/GPU) and hardware over-temperature shutdown | ✅ tested |
| Automotive bench: simulated CAN → FreeRTOS, 0 lost frames, gaps ≤ 35 ms | ✅ tested |
| Overspeed, redline and signal-loss alerts in FreeRTOS | ✅ tested |
| Instrument-cluster dashboard (HDMI framebuffer) | ✅ tested on a RAM framebuffer, ⏳ no monitor yet |
| Ignition (ACC) power-off service | ⏳ built, disabled, needs wiring |
| Real CAN bus (listen-only) | ⏳ built, needs a USB CAN adapter |
| Android screen + touch on HDMI | ⚠️ built, **disabled** (guest freezes with software rendering) |
| Sound in dom0 (onboard USB codec) | ✅ detected as ALSA card 0 |

### 2.3 What you see on the console

```
(XEN) Xen version 4.16.1 (arjune@) (arm-poky-linux-gnueabi-gcc (GCC) 11.5.0) debug=n Sun Oct 25 09:43:12 UTC 2026
(XEN) Using PSCI v1.0
(XEN) Using scheduler: SMP Credit Scheduler rev2 (credit2)
(XEN) Brought up 4 CPUs
[    0.000000] Linux version 5.15.201-yocto-standard ...
[    0.000000] OF: fdt: Machine model: Rockchip RK3288 Asus Tinker Board
[    0.000000] Xen 4.16 support found
Starting hardware watchdog keep-alive (/dev/watchdog, every 5s)
Setting up bridge xenbr0 (192.168.100.1/24)
Starting Xen guest rtos1
Starting Xen guest android1
Starting vehicle bench on vcan0 (simulated vehicle)
Starting cluster-hdmi (waits for /dev/fb0, i.e. an HDMI monitor)

Poky (Yocto Project Reference Distro) 4.0.35 tinker-board /dev/hvc0

tinker-board login:
```

```
root@tinker-board:~# xl list
Name                                        ID   Mem VCPUs	State	Time(s)
Domain-0                                     0   512     1     r-----     423.3
rtos1                                        1    16     1     -b----       8.9
android1                                     2   767     2     r-----     458.9
```

FreeRTOS reports (from `xl console rtos1` or `/var/log/xen/console/guest-rtos1.log`):

```
[rtos] 10 ms task: 1000 periods, min 9958 us, max 10040 us; heartbeats 469, queue messages 186 (report 47)
[vehicle] 10s: frames 1000, lost 0, bad 0, max gap 25000 us,  speed 135 km/h, rpm 4590; alerts: overspeed 8, redline 15, signal lost 23
[vehicle] ALERT engine over redline: 6293 rpm (limit 6000)
[vehicle] redline cleared: 5694 rpm
```

### 2.4 Expected HDMI output

With a monitor plugged in, `cluster-hdmi` starts by itself and draws the instrument cluster:
speed in km/h, rpm (with a bar), gear, and four lamps – OVERSPEED, REDLINE, SIGNAL, RTOS – driven by
the FreeRTOS verdicts, plus a status line. Snapshot taken **on the board** (RAM framebuffer, no
monitor connected): 135 km/h, OVERSPEED red, SIGNAL and RTOS green.

![Dashboard snapshot from the board](images/cluster-dashboard-board.png)

Android itself is headless (no picture on HDMI), see Section 14.

---

## 3. Hardware and host setup

### 3.1 Hardware

| Item | Detail |
|---|---|
| Board | ASUS Tinker Board R2.0, RK3288W (U-Boot reports it; the CRU node is renamed `rockchip,rk3288w-cru`) |
| Boot medium | micro-SD card, 32 GB (29.7 GiB). U-Boot sees it as `mmc 1`; Linux as `mmcblk0`/`mmcblk1` |
| Serial adapter | CP210x USB-UART, 3.3 V, appears as `/dev/ttyUSB0` on the PC |
| Power | 5 V / 3 A recommended (two unexplained silent hangs under full load, see Section 14) |
| Optional | HDMI monitor, Ethernet cable, USB CAN adapter (gs_usb/candleLight, PEAK, slcan), ACC wiring for ignition |

### 3.2 Serial console wiring (the important one)

The console is **UART2**, not UART1. Pins 8/10 are UART1 and were the first, wrong guess (board →
PC worked, PC → board did not).

```
  Tinker Board 40-pin header                 USB-UART adapter (3.3 V!)
  ────────────────────────────               ─────────────────────────
  pin 32  UART2 TX  (GPIO7_C7)  ───────────►  RX
  pin 33  UART2 RX  (GPIO7_C6)  ◄───────────  TX
  pin 34  GND                   ────────────  GND
```

Terminal: `picocom -b 115200 /dev/ttyUSB0` (or minicom), 115200 8N1, no flow control.
Under Xen this one UART is shared: Xen prints `(XEN) ...`, dom0 logs in on `hvc0`, and
`Ctrl-a` three times switches the input between Xen and dom0.

### 3.3 Host PC

Ubuntu, kernel 6.8, user `arjune` (groups `dialout`, `sudo`). Yocto build directory
`~/yocto-rk3288/build`, ~65 GB free at the start. Tools: `bmaptool`, `picocom`, Python 3 with
`pyserial` (used for all automated board tests over the UART).

---

## 4. Software stack

### 4.1 Versions

| Layer / component | Version / commit | Source |
|---|---|---|
| poky | kirkstone, `445a622392` | git.yoctoproject.org |
| meta-openembedded | kirkstone, `ce8539c941` (meta-oe, meta-networking, meta-python, meta-filesystems) | |
| meta-virtualization | kirkstone, `92b6abcd` (Xen 4.16) | |
| meta-arm | kirkstone, `c3e9fb12` (toolchain) | |
| meta-rockchip | kirkstone, `1e27f83` (MACHINE `tinker-board`) | |
| **meta-tinker-xen** | this project (priority 10) | `~/yocto-rk3288/meta-tinker-xen` |
| U-Boot | 2022.01, `tinker-rk3288_defconfig` + 2 patches | |
| Xen | 4.16.1 stable `f265444922` + 5 patches, release config | |
| dom0 kernel | linux-yocto 5.15.201, `multi_v7_defconfig` + 1 patch + 3 fragments | |
| Android guest kernel | Linux 4.14.336 LTS + 3 patches + `android-guest.cfg` | kernel.org |
| Android user space | AOSP 7.0 `sdk_phone_armv7-userdebug` NYC 3245079 (`armeabi-v7a-24_r07.zip`) | dl.google.com |
| FreeRTOS | FreeRTOS-Kernel V11.1.0 `dbf70559`, ARM_CA9 GCC port | github.com/FreeRTOS |
| Toolchain | GCC 11.5.0, binutils 2.38 | Yocto |
| Init system | SysVinit + BusyBox 1.35 | Yocto |

### 4.2 Stack diagram

```
 ┌─────────────────────────────── user space ───────────────────────────────┐
 │ dom0 (Yocto Linux 5.15)            │ android1             │ rtos1        │
 │  xl, xenstored, xenconsoled        │  init, zygote,       │  FreeRTOS    │
 │  QEMU (device model, off)          │  system_server,      │  tasks:      │
 │  adb, iptables, bridge-utils       │  surfaceflinger,     │   rt10ms     │
 │  vehicle-sim, can2rtos             │  adbd (TCP 5555)     │   vehicle    │
 │  cluster-hdmi, android-hdmi(off)   │                      │   heartbeat  │
 │  hw-watchdog, ignition-watch(off)  │                      │   producer/  │
 │  can-utils, alsa-utils, libgpiod   │                      │   consumer   │
 ├────────────────────────────────────┼──────────────────────┼──────────────┤
 │ Linux 5.15 kernel: Xen backends    │ Linux 4.14 kernel:   │ startup.S,   │
 │ (blkback, netback, gntdev), bridge,│ Xen frontends (blk,  │ vGIC, virtual│
 │ vcan/gs_usb/peak/slcan, dw_wdt,    │ net, console, fb,    │ timer, PV    │
 │ rockchip drm/hdmi, snd-usb-audio   │ kbd), binder 32-bit  │ console ring │
 ├────────────────────────────────────┴──────────────────────┴──────────────┤
 │ Xen 4.16.1 (EL2 / HYP): credit2 Pool-0 (CPU 0-2), RTDS pool "rt" (CPU 3), │
 │ vGICv2, event channels, grant tables, RK3288 platform (reset)            │
 ├──────────────────────────────────────────────────────────────────────────┤
 │ U-Boot 2022.01: non-secure HYP entry, SoC opened to non-secure,          │
 │ resident PSCI 1.0 monitor (CPU_ON/OFF, SYSTEM_RESET) in reserved RAM     │
 ├──────────────────────────────────────────────────────────────────────────┤
 │ RK3288 boot ROM → TPL/SPL (idbloader, SD sector 64)                       │
 └──────────────────────────────────────────────────────────────────────────┘
```

---

## 5. System architecture

### 5.1 CPU and scheduler layout

```
          pCPU 0          pCPU 1          pCPU 2       │      pCPU 3
   ┌───────────────────────────────────────────────┐   │ ┌──────────────────┐
   │ cpupool "Pool-0", scheduler credit2           │   │ │ cpupool "rt",    │
   │                                               │   │ │ scheduler RTDS   │
   │  Domain-0 (1 vCPU)   android1 (2 vCPUs)       │   │ │  rtos1 (1 vCPU)  │
   │                                               │   │ │  period 1000 µs  │
   └───────────────────────────────────────────────┘   │ │  budget  200 µs  │
                                                        │ │  extratime yes   │
                                                        │ └──────────────────┘
```

Why the split: RTDS on all 4 CPUs crashed Xen 4.16 (Section 8, Stage 13 and Stage 26). Credit2 is
better for general-purpose loads; RTDS gives the RTOS a guaranteed reservation on a CPU nobody else
uses. `rtos1-start` creates the pool at boot:

```
xl cpupool-cpu-remove Pool-0 3
xl cpupool-create 'name="rt"' 'sched="rtds"' 'cpus="3"'
xl create /etc/xen/rtos1.cfg                    # pool = "rt"
xl sched-rtds -d rtos1 -v all -p 1000 -b 200 -e 1
```

### 5.2 Boot flow

```
 power on
   │
   ▼
 RK3288 boot ROM ── reads SD sector 64 ──► TPL (DDR init) ► SPL ► U-Boot proper (sector 16384)
                                                                   │
                       distro_bootcmd scans mmc 1:6 (FAT "boot")   │
                       no /extlinux/extlinux.conf → /boot.scr ◄────┘
                                                   │
 boot.scr (= xen-boot.cmd):                        ▼
   setenv bootm_boot_mode nonsec                   load /rk3288-tinker.dtb, /xen, /zImage
   fdt: DMAC → non-secure, rsvmem U-Boot area, rm cpu-thermal cooling-maps,
        /chosen xen,xen-bootargs + module@4000000 (dom0 zImage + bootargs)
   bootz ${xen_addr} - ${fdt_addr_r}
                                                   │
 U-Boot armv7 nonsec switch:                       ▼
   psci_board_init(): SGRF/TZPC → non-secure, power off CPU 1-3
   install PSCI monitor (secure, reserved RAM), add /psci to DT
   SCR.NS=1, HSCTLR=0x30c50830, ERET to HYP ────► Xen 4.16 (HYP mode)
                                                     │  PSCI CPU_ON ×3 → 4 CPUs
                                                     │  RK3288 platform, credit2
                                                     ▼
                                                   dom0 Linux 5.15 (console hvc0)
                                                     │ SysVinit rc5.d:
                                                     │  S05 hw-watchdog
                                                     │  S79 xenbr0 (bridge + NAT)
                                                     │  S80 xencommons (xenstored, xenconsoled)
                                                     │  S90 xen-guests → rtos1-start, xl create android1
                                                     │  S92 vehicle-bench (vcan0, vehicle-sim, can2rtos)
                                                     │  S93 cluster-hdmi (waits for /dev/fb0)
                                                     │  S94 ignition-watch (ENABLED=0 → exits)
                                                     ▼
                                                   login: on hvc0

 If /xen is missing or bootz returns → boot.scr runs
   sysboot mmc 1:6 any ${scriptaddr} /native/extlinux.conf   (Linux without Xen)
```

### 5.3 Memory and address map (important values)

| What | Address / value |
|---|---|
| DDR | 2 GiB at 0x00000000 |
| Xen load address (`xen_addr`) | 0x02000000 |
| dom0 zImage (`dom0_addr`, `/chosen/module@4000000`) | 0x04000000 |
| U-Boot reserved area (kept from Xen with `fdt rsvmem`) | 0x79e00000 – 0x7fffffff |
| GIC-400 | 0xffc00000 (GICD +0x1000, GICC +0x2000) |
| UART2 (console) | 0xff690000, reg-shift 2 |
| CRU (clock/reset) | 0xff760000; GLB_SRST_FST at +0x1b0, value 0xfdb9 |
| PMU | 0xff730000 (PWRDN_CON +0x08, PWRDN_ST +0x0c) |
| Boot ROM mailbox (SRAM) | 0xff700000: +4 magic 0xdeadbeaf, +8 entry address |
| SGRF / TZPC | 0xff740000 / 0xffb00000 |
| Bus DMAC | secure 0xffb20000 → non-secure 0xff600000 |
| FreeRTOS guest RAM | 0x40000000, 16 MB; entry 0x40008000 |
| Guest vGIC | GICD 0x03001000, GICC 0x03002000; virtual timer PPI 27 |
| Arch timer | 24 MHz (CNTFRQ 0x016e3600), driven by RK3288 timer7 |

### 5.4 Network

```
              PC / LAN (optional, eth0 DHCP)
                       │
           ┌───────────┴─────────────────────────────────────────┐
           │ dom0  eth0 ── routing + iptables MASQUERADE ──┐      │
           │                                               │      │
           │              xenbr0 192.168.100.1/24 ◄────────┘      │
           │                │                                     │
           │             vif2.0                                   │
           └────────────────┼─────────────────────────────────────┘
                            │ (Xen netfront/netback)
                     android1 eth0 192.168.100.3
                     default route via 192.168.100.1, DNS 8.8.8.8 / 1.1.1.1
                     adbd TCP 5555  ◄── "adb connect 192.168.100.3:5555" from dom0
```

### 5.5 Automotive data flow

```
 ┌──────────── dom0 (Linux) ───────────────────────────────────────────────┐
 │                                                                          │
 │ vehicle-sim ──CAN 0x0C0 speed, 0x0C1 rpm, every 10 ms──► vcan0           │
 │ (or a real car bus: can0 in LISTEN-ONLY mode, SIM=0)        │            │
 │                                                             ├──► cluster-hdmi ─► /dev/fb0 (HDMI)
 │                                         can2rtos (CAN_RAW,  │       ▲
 │                                         receive only) ◄─────┘       │ reads verdicts
 │                                              │                       │
 │                       "V <seq> <speed> <rpm>\n" into rtos1's        /var/log/xen/console/
 │                        console pty (from xenstore)                   guest-rtos1.log
 │                                              │                       ▲
 │                                         xenconsoled ────────────────┘ (XENCONSOLED_TRACE=guest)
 └──────────────────────────────────────────────┼───────────────────────────┘
                                                │ shared ring page + event channel
 ┌──────────── rtos1 (FreeRTOS, pCPU 3, RTDS) ──▼───────────────────────────┐
 │ vVehicleTask every 5 ms: parse frames, count lost/bad, max gap,          │
 │   ALERT overspeed  > 120 km/h (clear < 115)                              │
 │   ALERT redline    > 6000 rpm (clear < 5700)                             │
 │   SIGNAL LOST      no frame for 100 ms (fail-safe: data marked invalid)  │
 │   10 s report                                                            │
 │ vRealTimeTask every 10 ms: measures its own period (min/max µs)          │
 └──────────────────────────────────────────────────────────────────────────┘
```

Safety principle: nothing in this system ever transmits on a vehicle bus. `can2rtos` only receives;
a real interface is configured `listen-only on`, so the controller does not even send ACK bits.

---

## 6. Directory trees

### 6.1 Project directory

```
~/yocto-rk3288/
├── poky/                     Yocto core (kirkstone)
├── meta-openembedded/        meta-oe, meta-networking, meta-python, meta-filesystems
├── meta-virtualization/      Xen recipes
├── meta-arm/                 toolchain
├── meta-rockchip/            MACHINE tinker-board, rockchip.wks
├── meta-tinker-xen/          ★ THIS PROJECT'S LAYER (all changes live here)
├── build/
│   ├── conf/local.conf       MACHINE = "tinker-board", DISTRO_FEATURES += "virtualization xen"
│   ├── conf/bblayers.conf    (backup: bblayers.conf.pre-meta-tinker-xen)
│   └── tmp/deploy/images/tinker-board/
│       ├── tinker-xen-test-image-tinker-board.wic(.bmap)   → card image
│       ├── xen-tinker-board                                → Xen binary
│       ├── zImage, rk3288-tinker.dtb, u-boot.*, idbloader.img
│       ├── android-system.img, android-userdata.img
│       └── freertos-xen-guest.bin/.elf, android-guest-zImage
├── diagnostics/              REPORT.md (A–Z), BOARD-TESTS.md, uart-*.log, bitbake-*.log,
│   └── baseline/             known-good original image + original U-Boot binaries
└── docs/
    ├── PROJECT-DOCUMENTATION.md   (this file)
    └── images/cluster-dashboard-board.png
```

### 6.2 The layer `meta-tinker-xen`

```
meta-tinker-xen/
├── conf/layer.conf                       priority 10, LAYERSERIES_COMPAT kirkstone
├── COPYING.MIT
├── README                                usage, recipes, limitations, recovery
├── wic/
│   └── tinker-xen.wks                    card layout (no extlinux; android partitions)
├── recipes-bsp/
│   ├── u-boot/
│   │   ├── u-boot_2022.01.bbappend
│   │   └── files/
│   │       ├── 0001-rockchip-rk3288-allow-booting-in-non-secure-HYP-mode.patch
│   │       └── 0002-rockchip-rk3288-add-PSCI-to-start-the-secondary-CPUs.patch
│   └── xen-boot-script/
│       ├── xen-boot-script.bb            mkimage → xen-boot.scr (= /boot.scr)
│       └── files/{xen-boot.cmd, native-extlinux.conf}
├── recipes-extended/
│   ├── xen/
│   │   ├── xen_4.16.bbappend             patches + release/debug config fragment
│   │   ├── xen-tools_4.16.bbappend       guest console logs (XENCONSOLED_TRACE)
│   │   └── files/
│   │       ├── 0001-xen-arm32-add-Cortex-A12-and-Cortex-A17-processor-info.patch
│   │       ├── 0002-xen-arm32-emulate-L2CTLR-for-guests.patch
│   │       ├── 0003-xen-arm-add-Rockchip-RK3288-platform-with-reset.patch
│   │       ├── 0004-xen-sched-fix-race-in-RTDS-scheduler.patch            (upstream backport)
│   │       ├── 0005-xen-sched-try-harder-to-find-a-runnable-unit-in-rt_schedule.patch (backport)
│   │       ├── tinker-rk3288-release.cfg
│   │       └── tinker-rk3288-early-printk.cfg
│   ├── xenbr0/                           bridge 192.168.100.1/24 + NAT (S79)
│   ├── xen-guests/                       guest autostart (S90/K10), hw-watchdog (S05), USB hub udev rule
│   ├── vehicle-bench/                    vehicle-sim.c, can2rtos.c, vehicle-bench.init (S92)
│   ├── cluster-hdmi/                     cluster-hdmi.c (dashboard), init (S93)
│   ├── ignition-watch/                   ACC → clean poweroff (S94, disabled)
│   └── android-hdmi/                     VNC client for Android's PV screen (disabled)
├── recipes-kernel/linux/
│   ├── linux-yocto_5.15.bbappend
│   └── files/
│       ├── 0001-ARM-rockchip-rk3288-keep-timer7-running-if-already-started.patch
│       ├── xen-dom0-bridge.cfg           CONFIG_BRIDGE=y
│       ├── xen-dom0-display.cfg          CONFIG_FB_VIRTUAL=m
│       └── xen-dom0-can.cfg              vcan, gs_usb, peak_usb, slcan
├── recipes-rtos/freertos-xen-guest/
│   ├── freertos-xen-guest.bb
│   └── files/{startup.S, guest.ld, main.c, FreeRTOSConfig.h, Makefile, rtos1.cfg, rtos1-start}
├── recipes-android/
│   ├── linux-android-guest/              Linux 4.14.336 guest kernel
│   │   ├── linux-android-guest_4.14.336.bb
│   │   └── files/{android-guest.cfg, 0001-timerfd..., 0002-xen-kbdfront..., 0003-xen-fbfront...}
│   └── android-guest/                    AOSP 7.0 image adapted to Xen
│       ├── android-guest.bb
│       └── files/{android1.cfg, init.xen.rc, fstab.xen, xen-net.sh}
├── recipes-core/
│   ├── images/tinker-xen-test-image.bb   the image
│   └── sysvinit/                         no getty on the Xen-owned UART
└── recipes-devtools/qemu/qemu_%.bbappend QEMU only as Xen device model (i386 target)
```

### 6.3 SD card layout (`tinker-xen.wks`, GPT)

```
 sector    size     partition            content
 ───────── ──────── ──────────────────── ──────────────────────────────────────────
        64   3.9 MB p1 loader1           TPL/SPL (idbloader.img)
      8064    64 KB p2 reserved1
      8192     4 MB p3 reserved2
     16384     4 MB p4 loader2           U-Boot 2022.01 with HYP + PSCI patches
     24576     4 MB p5 atf               (unused)
     32768   146 MB p6 boot (FAT, active)  /xen  /zImage  /rk3288-tinker.dtb
                                           /boot.scr  /xen-boot.scr  /native/extlinux.conf
    330956   571 MB p7 root (ext4)        dom0 root filesystem
   1499410   1.8 GB p8 android-system     Android /system  (xvda of android1, read-only)
   5169426   550 MB p9 android-data       Android /data    (xvdb of android1)
```

### 6.4 Important files on the running board (dom0)

```
/boot/                         (= SD p6) xen, zImage, rk3288-tinker.dtb, boot.scr
/etc/xen/rtos1.cfg             FreeRTOS guest config (pool "rt")
/etc/xen/android1.cfg          Android guest config
/var/lib/xen/rtos1/freertos-xen-guest.bin
/var/lib/xen/android/{zImage, ramdisk.img}
/usr/sbin/rtos1-start          creates cpupool rt, starts rtos1, sets RTDS budget
/usr/bin/{vehicle-sim, can2rtos, cluster-hdmi, android-hdmi}
/usr/sbin/ignition-watch
/etc/init.d/{hw-watchdog, xenbr0, xencommons, xen-guests, vehicle-bench, cluster-hdmi, ignition-watch, android-hdmi}
/etc/default/{xen-guests, vehicle-bench, ignition-watch, xencommons}
/etc/udev/rules.d/50-tinker-usb-hub.rules
/var/log/xen/console/guest-rtos1.log, guest-android1.log
/var/log/{vehicle-bench.log, cluster-hdmi.log, ignition-watch.log}
```

---

## 7. Build environment setup

### 7.1 Steps (from an empty directory)

```bash
mkdir ~/yocto-rk3288 && cd ~/yocto-rk3288
git clone -b kirkstone git://git.yoctoproject.org/poky
git clone -b kirkstone https://github.com/openembedded/meta-openembedded
git clone -b kirkstone git://git.yoctoproject.org/meta-virtualization
git clone -b kirkstone git://git.yoctoproject.org/meta-arm
git clone -b kirkstone git://git.yoctoproject.org/meta-rockchip
# meta-tinker-xen: copy this project's layer here

source poky/oe-init-build-env build
bitbake-layers add-layer ../meta-openembedded/meta-oe
bitbake-layers add-layer ../meta-arm/meta-arm-toolchain ../meta-arm/meta-arm
bitbake-layers add-layer ../meta-rockchip
bitbake-layers add-layer ../meta-openembedded/meta-filesystems
bitbake-layers add-layer ../meta-openembedded/meta-python
bitbake-layers add-layer ../meta-openembedded/meta-networking
bitbake-layers add-layer ../meta-virtualization
bitbake-layers add-layer ../meta-tinker-xen
```

### 7.2 `build/conf/local.conf` (non-default lines)

```
MACHINE = "tinker-board"
DISTRO_FEATURES:append = " virtualization xen"
```

Optional: `XEN_TINKER_DEBUG = "1"` builds the debug Xen (asserts, early printk on UART2, guest
CONSOLEIO output).

### 7.3 `build/conf/bblayers.conf` (result)

```
BBLAYERS ?= " \
  /home/arjune/yocto-rk3288/poky/meta \
  /home/arjune/yocto-rk3288/poky/meta-poky \
  /home/arjune/yocto-rk3288/poky/meta-yocto-bsp \
  /home/arjune/yocto-rk3288/meta-openembedded/meta-oe \
  /home/arjune/yocto-rk3288/meta-arm/meta-arm-toolchain \
  /home/arjune/yocto-rk3288/meta-arm/meta-arm \
  /home/arjune/yocto-rk3288/meta-rockchip \
  /home/arjune/yocto-rk3288/meta-openembedded/meta-filesystems \
  /home/arjune/yocto-rk3288/meta-openembedded/meta-python \
  /home/arjune/yocto-rk3288/meta-openembedded/meta-networking \
  /home/arjune/yocto-rk3288/meta-virtualization \
  /home/arjune/yocto-rk3288/meta-tinker-xen \
  "
```

### 7.4 Build

```bash
cd ~/yocto-rk3288 && source poky/oe-init-build-env build
bitbake tinker-xen-test-image
```

Expected end of the output (exit code 0):

```
NOTE: Tasks Summary: Attempted 4354 tasks of which 4257 didn't need to be rerun and all succeeded.
```

Single recipes can be rebuilt on their own, e.g. `bitbake xen`, `bitbake u-boot`,
`bitbake freertos-xen-guest`, `bitbake vehicle-bench`. The first full build takes several hours and
downloads the Android SDK system image (~400 MB) and kernel sources.

> Note (side effect seen on day 1): enabling `DISTRO_FEATURES xen` together with meta-virtualization
> adds kernel fragments (xen.scc, docker.scc, lxc.scc, vswitch.scc, …) to linux-yocto. The kernel grew
> from 10,002,944 to 10,379,776 bytes. This is expected and harmless.

---

## 8. Project history, stage by stage

Each stage: **goal → what was done → errors seen → cause → fix → result → evidence**. The letters in
brackets are the matching sections of `diagnostics/REPORT.md`.

### Stage 1 – Baseline inventory (A) – 2026-10-23

- **Goal:** know exactly what exists before changing anything.
- **Done:** recorded layer versions, MACHINE, kernel, U-Boot, Xen and WKS (table in Section 4.1);
  saved the known-good image to `diagnostics/baseline/`:
  `core-image-minimal-tinker-board-20261022043621.rootfs.wic`,
  sha256 `d9cdb9a5127b7d0bbd1151fc6430331c84a9b525d49f6f90653af40092598702`, bmap verified
  (12 ranges, 0 mismatches).
- **Rule set:** all changes go into a new layer; nothing in poky/meta-rockchip/meta-virtualization
  is edited.

### Stage 2 – The "Linux freeze" after `mmcblk0: p1 … p9` (B, H)

- **Symptom (user's log):** boot stopped right after `mmcblk0: p1 p2 p3 p4 p5 p6 p7 p8 p9`.
- **Analysis:** the Yocto image has **7** partitions and U-Boot **2022.01**; the log showed **9**
  partitions and U-Boot **2024.01**. So the card was not this Yocto image.
- **Found later on the board:** the card held another build (Poky 5.0.21 scarthgap, kernel 6.6,
  Xen 4.18.1-pre, U-Boot 2024.01 with NS-HYP patches) whose own notes documented a dom0 hang ~2 s
  into boot. That hang was found and fixed in Stage 8 (timer7).
- **Fix:** flash the real Yocto image. The card was erased at the user's request without a backup.

### Stage 3 – Xen prerequisites check (C)

| Requirement | Finding |
|---|---|
| CPU virtualization extensions | Cortex-A17 has them |
| Xen must be entered in non-secure HYP mode | **Blocker**: U-Boot for RK3288 had no NONSEC/VIRT (only LS1021A, sunxi, tegra124, exynos, mx7, stm32mp) |
| SMP under Xen | needs PSCI; RK3288 DT uses `rockchip,rk3066-smp` → CPU0 only |
| GIC-400 with GICH/GICV | OK |
| Arch timer 24 MHz | OK (`arm,cpu-registers-not-fw-configured`) |
| Xen console on UART2 | OK (ns16550 matches `snps,dw-apb-uart`) |
| Xen image format | ARM32 Xen has a zImage header; boot with `bootz` |

### Stage 4 – First test image and a debug Xen (D, E, F/G)

- **Done:** created `meta-tinker-xen`; `xen_4.16.bbappend` with
  `tinker-rk3288-early-printk.cfg` (`CONFIG_DEBUG=y`, early UART 8250 at 0xff690000, reg-shift 2)
  so Xen failures become visible; new image `tinker-xen-test-image` (copy of core-image-minimal +
  `/xen`), so the known-good image is never overwritten.
- **Build:** `bitbake xen` and `bitbake tinker-xen-test-image`: exit 0, 0 warnings.

### Stage 5 – First hardware run (H) – UART wiring and the confirmed blocker

- **Error 1:** board → PC text visible, but typing did nothing. **Cause:** adapter TX wired to pin 10
  (UART1). **Fix:** use UART2: pins 32 (TX), 33 (RX), 34 (GND).
- **Result:** Linux 5.15 login, 4 CPUs, 2005 MB, and the key line:

  ```
  CPU: All CPU(s) started in SVC mode.
  ```
  (secure SVC mode, not HYP → confirms the blocker).
- **Xen attempt from RAM (U-Boot prompt):**

  ```
  - Xen must be entered in NS Hyp mode -
  - Please update the bootloader -
  - Boot failed -
  ```
- Also learned: the SD card is `mmc 1` in U-Boot; U-Boot reserves 0x79f801f8–0x7fffffff.

### Stage 6 – U-Boot non-secure HYP port (I)

- **Goal:** U-Boot hands over to Xen in non-secure HYP mode.
- **Done (U-Boot patch 0001):** RK3288 selects `CPU_V7_HAS_NONSEC`/`CPU_V7_HAS_VIRT`; GIC base
  0xffc00000; `psci_board_init()` opens the SoC to the non-secure world (as the RK3288 TF-A port
  does); empty SMP hooks; **secure boot stays the default**, `bootm_boot_mode=nonsec` is only set by
  the Xen boot script. Original binaries kept in `baseline/*.2022.01-orig`.
- **Boot script:** `xen-boot.scr` (loads Xen + dom0, builds `/chosen`).

### Stage 7 – Getting Xen and dom0 to boot (J) – six problems in a row

| # | Symptom on the UART | Cause (evidence) | Fix |
|---|---|---|---|
| 1 | `Xen must be entered in NS Hyp mode` | no NONSEC/VIRT in RK3288 U-Boot | U-Boot 0001 |
| 2 | Silent hang after `Starting kernel ...` | HSCTLR reset value 0x70cd087f (MMU + Thumb exceptions on); debug markers stopped at "E" | U-Boot 0001: write HSCTLR = 0x30c50830 before ERET |
| 3 | (non-secure access faults) | SoC peripherals still secure-only | U-Boot 0001: SGRF/TZPC setup in `psci_board_init()` |
| 4 | `Missing processor info: 410fc0d1` | Xen had no Cortex-A12/A17 proc_info (also missing upstream) | Xen 0001 |
| 5 | dom0 Oops in `rk3288_suspend_init` | L2CTLR read traps (HCR.TIDCP), Xen injected UNDEF | Xen 0002: emulate L2CTLR like KVM |
| 6 | dom0 abort at 0xffb23fe0 | secure bus-DMAC window used from non-secure | boot script: `fdt set /dma-controller@ffb20000 reg <0x0 0xff600000 0x0 0x4000>` |

### Stage 8 – The famous "~2 s dom0 hang" (J)

- **Symptom:** dom0 always stopped ~2 s into boot (around the GMAC PHY reset).
- **Evidence:** Xen debug dump: vCPU idle, `NOW=-6148914817469028622` (Xen's time negative).
- **Cause:** Linux's `rockchip_timer_init()` stops and reloads **timer7**, which drives the ARM
  architected counter. The counter jumped back to zero under Xen → Xen's timers never fired.
- **Fix:** dom0 kernel patch 0001 – keep timer7 if it is already running (U-Boot starts it).
- **Result:** `/sys/hypervisor/type` = `xen`, dom0 login on hvc0, 483 MB, 1 vCPU
  (`uart-xen-card.log`, image `…20261023155925`).

### Stage 9 – First guest domain (K) – 2026-10-24

- **Done:** xen-tools (xl, xenstored, xenconsoled), backend modules, a BusyBox guest `guest1`.
- **Output:** `xl info` → xen_version 4.16.1, total_memory 2048; `xl list` → Domain-0 512 MB +
  guest1 127 MB; guest: `Machine model: XENVM-4.16`.
- **Seen once:** board reset (`Reset cause: POR`) exactly at `xl create`; not reproducible;
  likely a power dip → use 5 V / 3 A.
- Harmless: `qemu-xen is unavailable`.

### Stage 10 – Guest networking (L)

- **Done:** `CONFIG_BRIDGE=y` (dom0 fragment), `xenbr0` init script (192.168.100.1/24),
  `vif = ['bridge=xenbr0']`.
- **Result:** guest 192.168.100.2; ping both ways 4/4, ~0.66 ms.
- Harmless messages: `gnttab_mark_dirty not implemented yet`,
  `INIT: Id "S2" respawning too fast` (fixed later in Stage 19).

### Stage 11 – FreeRTOS as a Xen guest (M)

- **Why FreeRTOS:** Zephyr's Xen board (xenvm) is AArch64 only; this is a 32-bit system.
- **Design:** ARM32 zImage header (magic 0x016f2818, entry 0x40008000), vGICv2 at
  0x03001000/0x03002000, 1 kHz tick from the virtual timer (PPI 27) with **absolute** CNTV_CVAL,
  identity-mapped MMU with caches, WFI idle hook, console via `CONSOLEIO_write` hypercall (debug Xen).
- **Bugs fixed on the board:**

| Symptom | Cause | Fix |
|---|---|---|
| repeated/stale console lines | MMU off → uncached writes, Xen reads cached | enable MMU + caches |
| period 10.15 ms instead of 10 ms | relative TVAL re-arm adds IRQ latency each tick | absolute `CVAL += period` |
| rtos1 at 100 % CPU, 20 ms spikes | idle task spinning | idle hook executes `WFI` |
| `configASSERT` fails on correct setup | port's GIC priority probe reads 0xFF on Xen's vGIC | `configASSERT` off |

- **Result:** 10 ms task 9.99–10.01 ms (±8 µs) with dom0 idle; up to 19 ms late while a Linux
  guest booted (all on one CPU, credit2).

### Stage 12 – RTDS real-time scheduler (N)

- **Done:** `sched=rtds`, `rtos1-start` gives rtos1 200 µs every 1 ms
  (`xl sched-rtds -d rtos1 -v all -p 1000 -b 200 -e 1`; xl.cfg in 4.16 has no RTDS budget key).

| Situation | credit2 | RTDS defaults | RTDS 200 µs / 1 ms |
|---|---|---|---|
| idle | ±8 µs | ±8 µs | ±10–20 µs |
| Linux guest booting | up to 19.9 ms | up to 18.6 ms | 12.4 ms once, else ≤ ±115 µs |
| dom0 100 % busy | – | ±7 µs | ±4–7 µs |

- **New problem found:** dom0 `reboot` → endless `Xen: Platform reset did not work properly!`.

### Stage 13 – Reboot under Xen (O)

- **Cause:** no PSCI and no RK3288 platform code in Xen 4.16.
- **Fix:** Xen 0003 – new platform `rockchip.c`, `.reset` writes 0xfdb9 to CRU GLB_SRST_FST.
- **Error in the first version:** `rockchip.c:39: Cannot find the RK3288 CRU node in DT`.
  **Cause:** on the RK3288W U-Boot renames the CRU to `rockchip,rk3288w-cru`. **Fix:** match both.
- **Result:** `(XEN) Platform: Rockchip RK3288`; `reboot` → `Reset cause: RST` (a real soft reset).

### Stage 14 – Autoboot Xen from the card (P)

- **Done:** own WKS `tinker-xen.wks` without `loader=u-boot` → no `/extlinux/extlinux.conf`, so
  U-Boot's distro boot runs `/boot.scr` (= xen-boot.scr). `/native/extlinux.conf` keeps Linux without
  Xen. If `bootz` returns, the script falls back to native Linux. **No `saveenv`, U-Boot environment
  untouched.**
- **Tests:** power on → `Found U-Boot script /boot.scr` → Xen → dom0 ✔; reboot → Xen again ✔;
  stop autoboot + `sysboot mmc 1:6 any ${scriptaddr} /native/extlinux.conf` → Linux without Xen ✔.

### Stage 15 – SMP with PSCI (Q)

- **Before:** `Failed to bring up CPU 1 (error -19)` → Xen on 1 CPU.
- **Fix:** U-Boot 0002 – RK3288 selects `ARCH_SUPPORT_PSCI`; new `psci.c` with VERSION, FEATURES,
  CPU_ON, CPU_OFF, AFFINITY_INFO, MIGRATE_INFO_TYPE, SYSTEM_RESET. CPU_ON = core reset + PMU power-up
  + boot ROM mailbox (0xff700000: +8 entry, +4 0xdeadbeaf, `sev`).
- **Error in run 1:** only 2 CPUs; CPU2/3 returned `ALREADY_ON (-4)`. **Cause:** all parked cores
  wait on the same mailbox, so the first CPU_ON released all of them. **Fix:**
  `rk3288_psci_cpus_off()` in `psci_board_init()` powers cores 1–3 off first (as Linux and TF-A do).
- **Safety:** tested by sending U-Boot over the UART (`loady 0x01000000`, `go`) – nothing written to
  the card until it worked.
- **Result:** `(XEN) Using PSCI v1.0`, `Brought up 4 CPUs`, `xl info` nr_cpus 4; reboot via PSCI
  SYSTEM_RESET.

### Stage 16 – Headless Android 7.0 guest (R, T, U)

- **Why 7.0:** the newest ARM32 AOSP images Google publishes; they need the old 32-bit binder ABI
  (`CONFIG_ANDROID_BINDER_IPC_32BIT`), removed in Linux 4.18 → a separate 4.14 guest kernel.
  Building AOSP from source (~250 GB) did not fit the PC.
- **Done:** `linux-android-guest` (4.14.336, Xen frontends, binder 32-bit, ashmem, SELinux, cgroups,
  netfilter, vfb), `android-guest` (SDK image + ramdisk overlay `init.xen.rc`, `fstab.xen`,
  `default.prop`; emulator GLES libs removed, `libGLES_android.so` renamed `libGLES_soft.so`),
  partitions p8/p9 as xvda/xvdb, IP 192.168.100.3, adbd on TCP 5555.
- **Errors and fixes on the board:**

| Run | Error | Cause | Fix |
|---|---|---|---|
| 1 | system_server loop: `F EventHub: Could not register INotify for /dev/input` | no input devices → no /dev/input | `init.xen.rc`: `mkdir /dev/input 0755 root input` |
| 2 | system_server loop: `NullPointerException` in `AlarmManagerService$AlarmHandler` | no /dev/alarm in 4.14; system_server lacks CAP_WAKE_ALARM (added in Android 8) → timerfd alarm clocks -EPERM | guest kernel 0001: allow alarm clocks for gid 1000 |
| 2 | `usb 1-1: reset high-speed USB device` repeating | dom0 adb server wakes the GL850G hub, its resume fails | udev rule: hub `power/control = on` |
| 3 | **Xen panic** `Assertion '!unit_on_replq(svc)' failed at rt.c:587` | RTDS on all 4 CPUs, Xen 4.16 bug (properly fixed in Stage 26) | credit2 Pool-0 + RTDS pool `rt` on CPU 3 |

- **Result (run 4):** `SurfaceFlinger: Boot is finished (100948 ms)`, `sys.boot_completed=1`,
  77 packages, 0 system_server crashes, adb works; rtos1 ±2 µs idle, ≤ ±57 µs while Android boots.

### Stage 17 – Remove the demo guest (S)

On request, `guest1` (`xen-guest-demo`, `tinker-xen-guest-initramfs`) was deleted completely.

### Stage 18 – Quieter FreeRTOS console (V)

Three lines per second made the shared UART unusable for typing. Now one line every 10 s
(`REPORT_PERIODS = 1000`). Live workaround on older cards: `xl debug-keys G` then `xl debug-keys -`
twice.

### Stage 19 – Production hardening (W, X)

| Item | Problem | Fix | Verified |
|---|---|---|---|
| Release Xen | debug Xen slow/noisy; release build dropped RTDS | `tinker-rk3288-release.cfg`: DEBUG off, `EXPERT`, `UNSUPPORTED`, `SCHED_RTDS` on | `debug=n`, 4 CPUs |
| Thermal | dom0 has no physical CPU nodes → cooling maps make TSADC defer forever → no temperature, no HW shutdown | boot script `fdt rm /thermal-zones/cpu-thermal/cooling-maps` | 41.8 °C / 40.0 °C |
| FreeRTOS PV console | CONSOLEIO needs debug Xen | ring page (HVMOP_get_param CONSOLE_PFN/EVTCHN) + EVTCHNOP_send | `xl console rtos1` |
| Android network | no default route / DNS | `xen-net.sh` (route via .1, netd network 60000 + DNS); dom0 ip_forward + MASQUERADE | route OK |
| Android xen-net service | `Service xen-net does not have a SELinux domain defined` | `seclabel u:r:su:s0` | 192.168.100.3 up |
| Watchdog | hung system stays hung | `hw-watchdog` keeps dw_wdt alive every 5 s | reset after 43 s when stopped |
| Guest autostart | manual xl commands | `/etc/init.d/xen-guests` (S90/K10) | autostart after reboot |
| UART getty | `INIT: Id "S2" respawning too fast` | `start_getty`: no getty on ttyS2 under Xen | quiet |

Final verification (X): reboot → guests stopped first, `Reset cause: RST`, Xen with 4 CPUs, guests
autostarted, Android `boot_completed` 76 s after dom0 login, FreeRTOS 9961–10038 µs; poweroff →
`reboot: Power down`.

### Stage 20 – Screen, touch and sound (Y) – experimental, disabled

- **Built:** QEMU as Xen device model (VNC on 127.0.0.1:5900), `android-hdmi` (VNC client → dom0
  /dev/fb0 + touch/mouse/keyboard forwarding), guest kernel 0002 (Xen pointer as touchscreen) and
  0003 (16 bpp fbfront), DRM/HDMI and ALSA in dom0.
- **Errors:**

| Error | Cause | Fix / status |
|---|---|---|
| SurfaceFlinger `no suitable EGLConfig` | 32 bpp xenfb | 0003: 16 bpp RGB565 |
| apps abort in RenderThread `Failed to choose config` | HWUI needs GLES 2.0, only GLES 1.x software renderer | `ro.kernel.qemu=0` turns HWUI off… |
| …but then the guest **freezes 1–2 min after boot** | vCPUs idle in `cpu_v7_do_idle`, virtual timer IRQ 27 stuck active in the list registers | **not solved** |

- **Decision (user, option A):** back to stable headless; screen/touch kept in the layer but off.
- Stability afterwards: 10 min, 0 system_server crashes, FreeRTOS 9961–10038 µs.

### Stage 21 – Automotive bench (Z)

- **Built:** `vehicle-sim` (60 s drive cycle on vcan0), `can2rtos` (receive-only gateway),
  FreeRTOS `vVehicleTask`, CAN modules + can-utils.
- **Results:** ~1000 frames/10 s, 0 lost, 0 bad; overspeed/redline alerts raised and cleared;
  pause → `SIGNAL LOST`, resume → `SIGNAL OK again`; 10 ms task 9970–10029 µs.

### Stage 22 – Instrument-cluster dashboard (Z)

- **Built:** `cluster-hdmi` (framebuffer drawing, built-in 5×7 font, CAN + FreeRTOS verdicts).
- **Error:** RTOS lamp had no data: `/var/log/xen/console` did not exist (xenconsoled does not create
  it; /var/log is volatile). **Fix:** xen-tools bbappend adds the volatile directory and
  `XENCONSOLED_TRACE=guest`.
- **Result:** snapshot on the board (Section 2.4).

### Stage 23 – Frame bursts (Z)

- **Symptom:** FreeRTOS max gap ~105 ms (signal-loss limit 100 ms) while `candump vcan0` showed
  ≤ 29 ms.
- **Cause:** FreeRTOS sent an event-channel notify after **every** console read (up to 200/s);
  xenconsoled rate-limits a guest to `RATE_LIMIT_ALLOWANCE 30` events per `RATE_LIMIT_PERIOD 200` ms
  and then ignores it until the period ends (`tools/console/daemon/io.c`).
- **Fix:** notify only when the input ring was nearly full; `can2rtos` reports its own 10 s timing.
- **Result:** FreeRTOS max gap 20–35 ms, dom0 side 17–33 ms, 0 lost, 0 dropped.

### Stage 24 – Ignition power-off (Z)

`ignition-watch`: reads ACC on header pin 7 (gpiochip0 line 17) with libgpiod; ACC off for 10 s →
`poweroff` (guests stopped first); ACC back on cancels; nothing happens until ACC was seen on once.
Disabled by default. Not tested (nothing wired).

### Stage 25 – Xen panic after flashing (Z)

Right after flashing the image with Stages 21–24, about one minute after boot:

```
(XEN) CPU3: Unexpected Trap: Data Abort
(XEN) ----[ Xen-4.16.1  arm32  debug=n  Not tainted ]----
(XEN) CPU:    3
(XEN) PC:     0023ae60 rt.c#repl_timer_handler+0x1cc/0x2ac
(XEN)      R0: 00100100 R1: 0000000a R2: ffffffff R3: 00000000
(XEN)      HDFAR: 00100138
(XEN) Panic on CPU 3:
(XEN) CPU3: Unexpected Trap: Data Abort
(XEN) Reboot in five seconds...
```

- **Analysis:** `addr2line` on `xen-syms`: `deadline_queue_insert` ← `repl_timer_handler`,
  rt.c:1517. R0 = 0x00100100 = `LIST_POISON1` → a list entry that was already deleted is still linked:
  the RTDS replenishment queue is corrupted. rt.c:587 (the Stage 16 assertion) is
  `ASSERT(!unit_on_replq(svc))` in `replq_insert` → same bug.
- **Root cause** (upstream commit 73c62927f64e, Oct 2022): "When a domain gets paused the unit
  runnable state can change to 'not runnable' without the scheduling lock being involved… RTDS will
  remove the unit from the run queue, but doesn't do so for the replenish queue, leading to hitting
  an ASSERT() in replq_insert() later when the domain is unpaused again." Our Xen 4.16.1 predates it.
- **Reproduced on purpose:** `xl pause rtos1; usleep 3000; xl unpause rtos1` in a loop → same panic
  within 250 rounds (`uart-rtds-repro-old.log`).
- **Fix:** Xen patches 0004 (73c62927f64e) and 0005 (0f1dec89d324, retry `runq_pick`) backported.
- **Mistake during this stage (no damage):** an Enter sent during "Hit any key to stop autoboot"
  stopped U-Boot; the next Linux commands landed at the U-Boot prompt (`Unknown command`). None writes
  anything; resumed with `boot`. Lesson: after power-on, only listen until `login:`.

### Stage 26 – Final verification (Z) – 2026-10-25

| Check | Result |
|---|---|
| Boot | U-Boot → Xen → login by itself, no panic |
| Xen | compiled Sun Oct 25 09:43:12 UTC 2026, `/boot/xen` 917560 bytes, sha256 `6a53ae892f2dc3bce53696a0c1b1275e111ce7e8b01c539d72107f0027a77750` |
| Crash test | 3000 pause/unpause rounds, **no panic** (old Xen: < 250) |
| Guests | Domain-0, rtos1, android1 running |
| Vehicle frames | 1000/10 s, 0 lost, max gap 25–35 ms |
| FreeRTOS 10 ms task | 9958–10040 µs |
| Guest logs | `/var/log/xen/console/guest-rtos1.log`, `guest-android1.log` |
| ignition-watch | installed (`S94ignition-watch`), not running (ENABLED=0), `gpioget gpiochip0 17` = 0 |

---

## 9. Component reference (code and configuration)

Only the key parts are reproduced here; the full sources are in the layer (paths given).

### 9.1 U-Boot

`recipes-bsp/u-boot/u-boot_2022.01.bbappend`:

```
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append:tinker-board = " \
    file://0001-rockchip-rk3288-allow-booting-in-non-secure-HYP-mode.patch \
    file://0002-rockchip-rk3288-add-PSCI-to-start-the-secondary-CPUs.patch \
    "
```

**Patch 0001 – non-secure HYP boot** (key parts):

```c
/* arch/arm/cpu/armv7/nonsec_virt.S – before the exception return to HYP */
#ifdef CONFIG_ARMV7_VIRT
	cmp	r6, #HYP_MODE
	ldreq	r4, =0x30c50830			@ HSCTLR RES1 bits only
	mcreq	p15, 4, r4, c1, c0, 0		@ write HSCTLR
	isb
#endif
```

```c
/* arch/arm/mach-rockchip/rk3288/rk3288.c */
bool armv7_boot_nonsec_default(void) { return false; }   /* secure stays default */

void psci_board_init(void)              /* open the SoC to non-secure, as TF-A does */
{
	writel(1, CORE_AXI_SECURITY0);                        /* GIC */
	writel(0xffe0ffe0, SGRF_BASE + SGRF_SOC_CON(2));      /* bus masters/peripherals */
	writel(0x003f003f, SGRF_BASE + SGRF_SOC_CON(3));
	writel(0xffff0000, SGRF_BASE + SGRF_SOC_CON(4));
	writel(0x00ff0000, SGRF_BASE + SGRF_SOC_CON(5));
	writel(BIT(15) | BIT(31), SGRF_BASE + SGRF_SOC_CON(21)); /* whole DDR non-secure */
	writel(0xff, TZPC_BASE + TZPC_DECPROT1SET);           /* DMA non-secure + reset */
	writel(0xff, TZPC_BASE + TZPC_DECPROT2SET);
	...
}
```

```
# arch/arm/mach-rockchip/Kconfig
config ROCKCHIP_RK3288
	select CPU_V7_HAS_NONSEC
	select CPU_V7_HAS_VIRT
# include/configs/rk3288_common.h
#define CONFIG_ARM_GIC_BASE_ADDRESS	0xffc00000
```

**Patch 0002 – PSCI** (`arch/arm/mach-rockchip/rk3288/psci.c`, runs in the secure section):

```c
s32 __secure psci_cpu_on(u32 function_id, u32 target_cpu, u32 pc, u32 context_id)
{
	s32 cpu = rk3288_target_cpu(target_cpu);
	if (cpu < 0)                                   return ARM_PSCI_RET_INVAL;
	if (psci_state[cpu] == PSCI_AFFINITY_LEVEL_ON) return ARM_PSCI_RET_ALREADY_ON;
	...
	psci_save(cpu, pc, context_id);
	psci_set_state(cpu, PSCI_AFFINITY_LEVEL_ON_PENDING);
	if (rk3288_cpu_power_on(cpu)) { ... return ARM_PSCI_RET_INTERNAL_FAILURE; }

	rk3288_mdelay(1);                                   /* boot ROM reaches its WFE loop */
	writel((u32)&psci_cpu_entry, BOOTROM_MAILBOX_PC);   /* 0xff700008 */
	writel(BOOTROM_MAGIC, BOOTROM_MAILBOX_MAGIC);       /* 0xdeadbeaf at 0xff700004 */
	dsb();
	asm volatile("sev");
	return ARM_PSCI_RET_SUCCESS;
}

void __secure psci_system_reset(void)
{
	writel(CRU_GLB_SRST_FST_VALUE, CRU_GLB_SRST_FST);   /* 0xfdb9 → 0xff7601b0 */
	...
}

void rk3288_psci_cpus_off(void)   /* called from psci_board_init(): park cores 1-3 powered off */
{
	writel(0, BOOTROM_MAILBOX_MAGIC);
	for (cpu = 1; cpu < RK3288_NR_CPUS; cpu++) {
		writel(BIT(cpu) | BIT(cpu) << 16, CRU_SOFTRST_CON0);
		setbits_le32(PMU_PWRDN_CON, BIT(cpu));
	}
	...
}
```

### 9.2 Boot script `recipes-bsp/xen-boot-script/files/xen-boot.cmd`

```
setenv bootm_boot_mode nonsec
setenv xen_addr 0x02000000
setenv dom0_addr 0x04000000
load mmc 1:6 ${fdt_addr_r} /rk3288-tinker.dtb
load mmc 1:6 ${xen_addr} /xen
load mmc 1:6 ${dom0_addr} /zImage
setenv dom0_size ${filesize}

fdt addr ${fdt_addr_r}
fdt resize 1024
fdt set /dma-controller@ffb20000 reg <0x0 0xff600000 0x0 0x4000>
fdt rsvmem add 0x79e00000 0x06200000
fdt rm /thermal-zones/cpu-thermal/cooling-maps
fdt set /chosen \#address-cells <1>
fdt set /chosen \#size-cells <1>
fdt set /chosen xen,xen-bootargs "console=dtuart dtuart=serial2 dom0_mem=512M dom0_max_vcpus=1 sched=credit2 loglvl=all guest_loglvl=all"
fdt mknod /chosen module@4000000
fdt set /chosen/module@4000000 compatible "xen,linux-zimage" "xen,multiboot-module"
fdt set /chosen/module@4000000 reg <${dom0_addr} 0x${dom0_size}>
fdt set /chosen/module@4000000 bootargs "console=hvc0 earlycon=xenboot root=PARTLABEL=root rootwait rw rootfstype=ext4 clk_ignore_unused"

bootz ${xen_addr} - ${fdt_addr_r}

echo "Xen did not start: booting Linux without Xen"
setenv bootm_boot_mode sec
sysboot mmc 1:6 any ${scriptaddr} /native/extlinux.conf
```

`native-extlinux.conf`:

```
default native
label native
   kernel /zImage
   fdtdir /
append root=PARTLABEL=root rootwait console=tty1 console=ttyS2,115200n8 rw rootfstype=ext4 init=/sbin/init
```

### 9.3 Xen

`recipes-extended/xen/xen_4.16.bbappend`:

```
XEN_TINKER_DEBUG ??= "0"
SRC_URI:append:tinker-board = " \
    ${@'file://tinker-rk3288-early-printk.cfg' if d.getVar('XEN_TINKER_DEBUG') == '1' else 'file://tinker-rk3288-release.cfg'} \
    file://0001-xen-arm32-add-Cortex-A12-and-Cortex-A17-processor-info.patch \
    file://0002-xen-arm32-emulate-L2CTLR-for-guests.patch \
    file://0003-xen-arm-add-Rockchip-RK3288-platform-with-reset.patch \
    file://0004-xen-sched-fix-race-in-RTDS-scheduler.patch \
    file://0005-xen-sched-try-harder-to-find-a-runnable-unit-in-rt_schedule.patch \
    "
```

`tinker-rk3288-release.cfg`:

```
# CONFIG_DEBUG is not set
CONFIG_EXPERT=y
CONFIG_UNSUPPORTED=y
CONFIG_SCHED_RTDS=y
```

`tinker-rk3288-early-printk.cfg` (debug):

```
CONFIG_DEBUG=y
CONFIG_EARLY_UART_CHOICE_8250=y
CONFIG_EARLY_UART_BASE_ADDRESS=0xff690000
CONFIG_EARLY_UART_8250_REG_SHIFT=2
```

| Patch | What it does | Upstream status |
|---|---|---|
| 0001 | proc_info for Cortex-A12 (MIDR 0x410FC0D0, RK3288 reports 0x410fc0d1) and A17; init = set ACTLR.SMP like A15 | Pending |
| 0002 | emulate L2CTLR (p15,1,c9,c0,2): return HW value with the domain's CPU count, ignore writes | Pending |
| 0003 | `xen/arch/arm/platforms/rockchip.c`: platform "Rockchip RK3288", `.reset` = 0xfdb9 → CRU+0x1b0; matches `rockchip,rk3288-cru` and `rockchip,rk3288w-cru` | Pending |
| 0004 | `rt_schedule()`: also `replq_remove()` a picked unit that is not runnable | Backport 73c62927f64e |
| 0005 | `rt_schedule()`: loop over the run queue until a runnable unit is found | Backport 0f1dec89d324 |

Patch 0002 core:

```c
case HSR_CPREG32(L2CTLR):
    if ( psr_mode_is_user(regs) )
        return inject_undef_exception(regs, hsr);
    if ( cp32.read )
    {
        uint32_t l2ctlr = READ_CP32(L2CTLR);
        l2ctlr &= ~(3U << 24);
        l2ctlr |= ((v->domain->max_vcpus - 1) & 3U) << 24;
        set_user_reg(regs, regidx, l2ctlr);
    }
    break;
```

Patches 0004 + 0005 result in `xen/common/sched/rt.c`:

```c
        while ( true )
        {
            snext = runq_pick(ops, cpumask_of(sched_cpu), cur_cpu);
            if ( snext == NULL )
            {
                snext = rt_unit(sched_idle_unit(sched_cpu));
                break;
            }
            if ( unit_runnable_state(snext->unit) )
                break;

            q_remove(snext);
            replq_remove(ops, snext);      /* the missing line that caused the panic */
        }
```

`xen-tools_4.16.bbappend`:

```
do_install:append:tinker-board() {
    sed -i 's|^#XENCONSOLED_TRACE=.*|XENCONSOLED_TRACE=guest|' ${D}${sysconfdir}/default/xencommons
    echo "d root root 0755 /var/log/xen/console none" >> ${D}${sysconfdir}/default/volatiles/99_xen
}
```

### 9.4 dom0 kernel (linux-yocto 5.15)

```
SRC_URI:append:tinker-board = " \
    file://0001-ARM-rockchip-rk3288-keep-timer7-running-if-already-started.patch \
    file://xen-dom0-bridge.cfg \
    file://xen-dom0-display.cfg \
    file://xen-dom0-can.cfg \
    "
```

Patch 0001 (`arch/arm/mach-rockchip/rockchip.c`):

```c
			if (!(readl(reg_base + 0x30) & 1)) {     /* only if timer7 is not running */
				writel(0, reg_base + 0x30);
				writel(0xffffffff, reg_base + 0x20);
				writel(0xffffffff, reg_base + 0x24);
				writel(1, reg_base + 0x30);
				dsb();
			}
```

Fragments: `CONFIG_BRIDGE=y`; `CONFIG_FB_VIRTUAL=m`;
`CONFIG_CAN_VCAN=m CONFIG_CAN_GS_USB=m CONFIG_CAN_PEAK_USB=m CONFIG_CAN_SLCAN=m`.

### 9.5 Image recipe `recipes-core/images/tinker-xen-test-image.bb`

```
require recipes-core/images/core-image-minimal.bb
WKS_FILE = "tinker-xen.wks"
IMAGE_BOOT_FILES:append = " xen-${MACHINE};xen xen-boot.scr xen-boot.scr;boot.scr \
    native-extlinux.conf;native/extlinux.conf"
do_image_wic[depends] += "xen:do_deploy xen-boot-script:do_deploy android-guest:do_deploy"
IMAGE_INSTALL:append = " \
    xen-tools-xencommons \
    kernel-module-xen-gntdev kernel-module-xen-gntalloc \
    kernel-module-xen-blkback kernel-module-xen-netback kernel-module-xen-wdt \
    xen-tools-scripts-network xen-tools-scripts-block xenbr0 \
    freertos-xen-guest \
    android-guest android-tools \
    xen-guests xen-guests-watchdog \
    qemu android-hdmi \
    kernel-module-rockchipdrm kernel-module-dw-hdmi kernel-module-vfb \
    kernel-module-snd-usb-audio alsa-utils-aplay alsa-utils-amixer alsa-utils-speakertest \
    vehicle-bench can-utils cluster-hdmi ignition-watch \
    kernel-module-vcan kernel-module-gs-usb kernel-module-peak-usb kernel-module-slcan \
    "
IMAGE_ROOTFS_EXTRA_SPACE = "65536"
```

### 9.6 FreeRTOS guest (`recipes-rtos/freertos-xen-guest`)

Files: `startup.S` (zImage header, vectors, stacks), `guest.ld` (link at 0x40008000), `main.c`
(platform + tasks, 835 lines), `FreeRTOSConfig.h`, `Makefile`, `rtos1.cfg`, `rtos1-start`.
FreeRTOS-Kernel V11.1.0 is fetched by the recipe and used unmodified.

zImage header in `startup.S`:

```asm
_start:
	.rept	8
	mov	r0, r0
	.endr
	b	reset			@ 0x20
	.word	0x016f2818		@ 0x24: zImage magic
	.word	_start			@ 0x28: absolute load/run address (entry)
	.word	_end			@ 0x2c: end address
	.word	0x04030201		@ 0x30: little-endian marker
```

Tasks (`main.c`):

```c
xTaskCreate( vRealTimeTask,  "rt10ms",    configMINIMAL_STACK_SIZE * 2, NULL, 4, NULL );
xTaskCreate( vHeartbeatTask, "heartbeat", configMINIMAL_STACK_SIZE * 2, NULL, 3, NULL );
xTaskCreate( vProducerTask,  "producer",  configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL );
xTaskCreate( vConsumerTask,  "consumer",  configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL );
xTaskCreate( vVehicleTask,   "vehicle",   configMINIMAL_STACK_SIZE * 2, NULL, 3, NULL );
```

Console input with the burst fix:

```c
static uint32_t console_read( char * buf, uint32_t size )
{
    ...
    while( cons != prod && n < size )
        buf[ n++ ] = ring->in[ cons++ & ( sizeof( ring->in ) - 1 ) ];
    ring->in_cons = cons;
    /* notify xenconsoled only when the ring was nearly full (rate limit 30 / 200 ms) */
    if( n && ( prod - ( cons - n ) ) > sizeof( ring->in ) - 256 )
        ( void ) xen_hypercall2( __HYPERVISOR_event_channel_op, EVTCHNOP_send,
                                 ( uint32_t ) &ulConsEvtchn );
    return n;
}
```

Vehicle checks (`vVehicleTask`, every 5 ms):

```c
#define VEHICLE_PERIOD_MS       5
#define VEHICLE_TIMEOUT_MS      100     /* no frame for this long: signal lost */
#define OVERSPEED_KMH100        12000   /* 120.00 km/h */
#define REDLINE_RPM             6000

if( !overspeed && speed > OVERSPEED_KMH100 )            → "ALERT overspeed"
else if( overspeed && speed < OVERSPEED_KMH100 - 500 )  → "overspeed cleared"
if( !redline && rpm > REDLINE_RPM )                      → "ALERT engine over redline"
else if( redline && rpm < REDLINE_RPM - 300 )            → "redline cleared"
if( have_signal && now - last_rx > 100 ms )              → "SIGNAL LOST ... speed/rpm invalid"
```

`rtos1.cfg`:

```
name   = "rtos1"
kernel = "/var/lib/xen/rtos1/freertos-xen-guest.bin"
memory = 16
vcpus  = 1
pool   = "rt"
```

### 9.7 Android guest

`android1.cfg`:

```
name = "android1"
kernel = "/var/lib/xen/android/zImage"
ramdisk = "/var/lib/xen/android/ramdisk.img"
extra = "console=hvc0 androidboot.hardware=xen androidboot.console=hvc0 androidboot.selinux=permissive video=vfb:640x480-16@60 vfb.videomemorysize=4194304"
memory = 768
vcpus = 2
disk = [ 'phy:/dev/disk/by-partlabel/android-system,xvda,r',
         'phy:/dev/disk/by-partlabel/android-data,xvdb,w' ]
vif = [ 'bridge=xenbr0' ]
#vfb = [ 'vnc=1,vnclisten=127.0.0.1,vncdisplay=0' ]     (experimental screen, off)
```

`init.xen.rc`:

```
on early-init
    mount debugfs debugfs /sys/kernel/debug mode=755
on init
    mkdir /dev/input 0755 root input
on fs
    mount_all /fstab.xen
on boot
    setprop service.adb.tcp.port 5555
    setprop ro.radio.noril yes
    start xen-net
service xen-net /system/bin/sh /xen-net.sh
    seclabel u:r:su:s0
    user root
    group root
    oneshot
    disabled
```

`fstab.xen`:

```
/dev/block/xvda    /system      ext4    ro                                         wait
/dev/block/xvdb    /data        ext4    noatime,nosuid,nodev,errors=panic          wait,check
```

`xen-net.sh`:

```sh
ifconfig eth0 192.168.100.3 netmask 255.255.255.0 up
ip route add default via 192.168.100.1 dev eth0
# wait for netd, then a resolver network for DNS
ndc network create 60000
ndc resolver setnetdns 60000 "" 8.8.8.8 1.1.1.1
ndc network default set 60000
```

How the image is built (`android-guest.bb`, `do_compile`): ramdisk = SDK ramdisk + a second cpio
with `default.prop` (+ `ro.sf.lcd_density=160`), `fstab.xen`, `init.xen.rc`, `xen-net.sh`;
`system.img` edited with `debugfs`: emulator EGL/GLES libs removed, `libGLES_android.so` →
`libGLES_soft.so`; `e2fsck -fn` check. License `CLOSED` (Android SDK License: development use only).

Guest kernel (`linux-android-guest_4.14.336.bb`): `multi_v7_defconfig` + `android-guest.cfg`
(XEN, HVC_XEN, blk/net/fb/kbd frontends, ANDROID_BINDER_IPC + _32BIT, ASHMEM, SELinux, cgroups,
netfilter/NAT, FB_VIRTUAL, LPAE, no modules); `do_configure` fails the build if any required option
is missing. Patches: 0001 timerfd alarm for gid 1000, 0002 kbdfront pointer = touchscreen,
0003 fbfront 16 bpp.

Patch 0001 core:

```c
#define ANDROID_AID_SYSTEM	1000
static bool timerfd_may_use_alarm(void)
{
	return capable(CAP_WAKE_ALARM) ||
	       in_egroup_p(KGIDT_INIT(ANDROID_AID_SYSTEM));
}
```

### 9.8 dom0 services

| Init script | Order | Job | Config |
|---|---|---|---|
| `hw-watchdog` | S05 | opens /dev/watchdog (dw_wdt), writes every 5 s, never closes cleanly | – |
| `xenbr0` | S79 | bridge 192.168.100.1/24, ip_forward, `iptables -t nat ... MASQUERADE` | – |
| `xencommons` | S80 | xenstored, xenconsoled (`--log=guest`) | `/etc/default/xencommons` |
| `xen-guests` | S90 / K10 | start rtos1 (`rtos1-start`), android1 and media1; stop in reverse (`xl shutdown`, 30 s, then destroy) | `/etc/default/xen-guests` |
| `vehicle-bench` | S92 / K08 | vcan0 + vehicle-sim + can2rtos loop; real bus listen-only | `/etc/default/vehicle-bench` |
| `cluster-hdmi` | S93 | waits for /dev/fb0, runs the dashboard (ENABLED=0 since Part 6: display-map runs it) | `/etc/default/cluster-hdmi` |
| `display-map` | S93 / K07 | one viewer per screen (HDMI + USB displays), see 9.10 | `/etc/default/display-map` |
| `ignition-watch` | S94 / K05 | ACC GPIO → poweroff (ENABLED=0) | `/etc/default/ignition-watch` |
| `android-hdmi` | – | Android PV screen on HDMI (ENABLED=0) | `/etc/default/android-hdmi` |

USB hub rule (`50-tinker-usb-hub.rules`):

```
ACTION=="add", SUBSYSTEM=="usb", ATTR{idVendor}=="05e3", ATTR{idProduct}=="0608", ATTR{power/control}="on"
```

Getty fix (`start_getty`):

```sh
if [ "$2" != "hvc0" ] && grep -qs xen /sys/hypervisor/type; then
    sleep 1000
    exit 0
fi
```

QEMU (`qemu_%.bbappend`): `QEMU_TARGETS = "i386"`, `PACKAGECONFIG = "fdt pie xen"` – only the Xen
device model (xen-tools expects `/usr/bin/qemu-system-i386` on every architecture).

### 9.9 Automotive programs

**vehicle-sim** (`recipes-extended/vehicle-bench/files/vehicle-sim.c`) – 60 s cycle:

| Time | Speed |
|---|---|
| 0–20 s | accelerate 0 → 135 km/h |
| 20–35 s | cruise 135 km/h (above the 120 limit) |
| 35–50 s | brake to 0 |
| 50–60 s | stand still (800 rpm idle) |

Gearbox: limits 25/70/95/115/130 km/h, ratios 160/90/62/48/40/34 rpm per km/h (late shift in
2nd gear → redline). Frames every 10 ms, uint16 big endian: `0x0C0` speed in 0.01 km/h, `0x0C1` rpm.
`SIGUSR1` pauses/resumes (signal-loss test).

**can2rtos** – receive-only gateway:

```c
struct can_filter flt[2] = {
    { .can_id = ID_SPEED,  .can_mask = CAN_SFF_MASK },
    { .can_id = ID_ENGINE, .can_mask = CAN_SFF_MASK },
};
/* rtos1's console pty from xenstore: /local/domain/<id>/console/tty */
n = snprintf(line, sizeof(line), "V %u %u %u\n", ++seq, f.data[0] << 8 | f.data[1], rpm);
write(con, line, n);        /* EAGAIN → drop (the next frame is newer) */
/* every 10 s: "can2rtos: 10s: frames N, max gap X ms, dropped D" */
```

**vehicle-bench.init** – real bus in listen-only mode:

```sh
ip link set $IFACE type can bitrate $BITRATE listen-only on || {
    echo "vehicle-bench: cannot set $IFACE to listen-only, not starting"; exit 1; }
```

**cluster-hdmi** (`recipes-extended/cluster-hdmi/files/cluster-hdmi.c`, 551 lines): mmaps
`/dev/fb0`, switches the console to graphics mode, own 5×7 font, reads CAN (receive only) and the
FreeRTOS console log; trims guest logs above 2 MB. Options: `-f fb`, `-i iface`, `-l log`,
`-S file.ppm` (snapshot after 3 s, ¼ scale), `-g WxH` (draw into memory), `-D` (demo values).

**ignition-watch** (`/usr/sbin/ignition-watch`):

```sh
v=$(gpioget $CHIP $LINE)
if [ "$v" = "$ACC_ON_LEVEL" ]; then seen_on=1; off_since=0          # ACC on
elif [ $seen_on = 1 ]; then
    [ $off_since = 0 ] && off_since=$(now)                            # ACC just went off
    [ $(( $(now) - off_since )) -ge "$OFF_DELAY" ] && { sync; poweroff; }
fi
```

### 9.10 Part 6: four domains, a screen per OS, Android 10 (2026-10-27)

**Status: built, not tested on hardware.** Android 10 (LineageOS 17.1): source synced, first build
started.

Build 2026-10-27 14:02: `bitbake tinker-xen-test-image`, 4540 tasks, no warnings/errors.
`tinker-xen-test-image-tinker-board.wic` 3,543,864,320 bytes, sha256 `0fe2d29426b6153e…`.
GPT: p6 boot 145.6 MiB, p7 root 577.8 MiB, p8 android-system 1.8 GiB, p9 android-data 550 MiB,
**p10 media-root 298.3 MiB** (`media-guest-image-tinker-board.ext4`, 312,797,184 bytes).
Verified in the rootfs: no ffmpeg/libav in dom0; `display-map`, `media-guest`,
`kernel-module-udl`, `/boot/zImage → zImage-5.15.201-yocto-standard`;
`XEN_GUESTS="rtos1 android1 media1"`; `BUDGET=1000`; boot.scr `dom0_mem=384M`.

**Size reduction, build 2026-10-27 14:54** (4540 tasks, all succeeded):
`tinker-xen-test-image-tinker-board.wic` **1,571,959,808 bytes** (was 3.54 GB), sha256 `27cb7ba8c8d2b29f…`.
GPT: p6 boot **48.0 MiB** (`--fixed-size 48M`), p7 root **166.0 MiB** (rootfs 72 MB), p8
android-system **612.8 MiB** (642,617,344 bytes), p9 android-data 550 MiB, p10 media-root
**106.3 MiB** (111,470,592 bytes).

| Change | File |
|---|---|
| remove `${datadir}/qemu/*` except `keymaps` (220 MB of firmware) | `recipes-devtools/qemu/qemu_%.bbappend` (`do_install:append`) |
| `IMAGE_ROOTFS_EXTRA_SPACE` 65536 → 32768 | `tinker-xen-test-image.bb` |
| `IMAGE_ROOTFS_EXTRA_SPACE` 262144 → 65536 | `media-guest-image.bb` |
| boot `--size 114688K` → `--fixed-size 48M` | `wic/tinker-xen.wks` |
| system.img: `e2fsck -fy`, `resize2fs -M`, grow to +10 %, `dd if=/dev/null … seek=N` (no `truncate` in HOSTTOOLS), `e2fsck -fy` (fixes resize inode 7 `i_size`), `e2fsck -fn` | `android-guest.bb` |
| `dom0_mem=320M` | `xen-boot.cmd` |
| media1 `memory = 192` | `media1.cfg` |
| rtos1 `memory = 4`; `LENGTH = 4M - 32K`; `GUEST_RAM_MB 4` (`_end` = 0x4002ad18, 172 KB) | `rtos1.cfg`, `guest.ld`, `main.c` |

#### Resource plan

| Domain | Memory | vCPUs | CPUs | Scheduler | Display |
|---|---|---|---|---|---|
| Domain-0 | **320 MB** (`dom0_mem=320M`; 512 → 384 → 320) | 1 | Pool-0 (0–2) | credit2 | owns HDMI + USB displays |
| rtos1 | **4 MB** (was 16) | 1 | CPU 3, pool `rt` | RTDS, **period 1000 µs, budget 1000 µs** (was 200), extratime 1 | via cluster-hdmi (dom0) |
| android1 | 512 MB (was 768; ~768 again for Android 10) | 2 | `cpus = "0-2"` | credit2 | vfb, VNC :5900 (still commented out for Android 7) |
| media1 | **192 MB** (was 256) | 2 | `cpus = "0-2"` | credit2 | vfb 800×480, VNC :5901 |

Total **1028 MB** (≈ 1.28 GB with Android 10 at 768 MB) of 2 GB, minus the 98 MB U-Boot reservation.

`rtos1.cfg` is **not** pinned with `cpus = "3"`: the `rt` pool already confines it, and a hard
affinity outside Pool-0 at create time (before the pool move) could fail the create.

#### dom0 image (`tinker-xen-test-image.bb`)

- Removed: `ffmpeg`, `nature-video` (recipe deleted; the clip moved to `media-player`).
- Added: `media-guest`, `display-map`, `kernel-module-udl`.
- `do_image_wic[depends] += "media-guest-image:do_image_complete"`.
- The SSH setup moved into `classes/tinker-ssh.bbclass` (`inherit tinker-ssh`), shared with media1.

#### Kernel (`xen-dom0-display.cfg`)

```
CONFIG_DRM_UDL=m      # DisplayLink DL-1x0/DL-1x5 (USB 2.0) adapters
```

DL-3xxx/DL-5xxx/DL-6xxx need the closed `evdi` + DisplayLinkManager stack: not supported.

#### media1 guest

| File | Content |
|---|---|
| `recipes-core/images/media-guest-image.bb` | `core-image-minimal` + `media-player ffmpeg`; `IMAGE_FSTYPES = "ext4"`; `IMAGE_ROOTFS_EXTRA_SPACE = "262144"`; `inherit tinker-ssh`; postprocess: hostname `media1`, static `eth0` `MEDIA_GUEST_IP ?= "192.168.100.4"`/24 gw .1, plain `/etc/resolv.conf` (8.8.8.8) |
| `recipes-extended/media-guest/files/media1.cfg` | `kernel = "/boot/zImage"` (dom0's 5.15: blk/net/fb/kbd frontends built in); `extra = "console=hvc0 root=/dev/xvda rw rootwait rootfstype=ext4 xen_fbfront.video=2,800,480"`; `disk = [ 'phy:/dev/disk/by-partlabel/media-root,xvda,w' ]`; `vif = [ 'bridge=xenbr0' ]`; `vfb = [ 'vnc=1,vnclisten=127.0.0.1,vncdisplay=1' ]` |
| `recipes-extended/media-player/` | `media-play [-f fbdev] [-d secs] [-l] file\|dir…`: ffmpeg `-f fbdev`, `-pix_fmt bgra`, scale+pad to the framebuffer; pictures with `-loop 1 -framerate 1 -t SECS`. `media-player` init (S94): loops `/home/root/media`, or `/usr/share/media-player/sixes-river-800x480.mp4` while empty |
| `wic/tinker-xen.wks` | `part --source rawcopy --sourceparams="file=media-guest-image-${MACHINE}.ext4" --part-name media-root` (partition 10) |
| `xen-guests` | `XEN_GUESTS="rtos1 android1 media1"`; RDEPENDS `media-guest` |
| `xen-consoles` | 4th tmux pane: `xl console media1` |

#### display-map (`recipes-extended/display-map`)

`/etc/default/display-map`:

```
SCREENS="hdmi=rearcam:vnc:5900 usb1=vnc:5901 usb2=cluster usb3=console"
INPUT_hdmi="/dev/input/event*"          # other screens: none unless INPUT_<screen> is set
```

- Screen → framebuffer: `hdmi` = the fb whose `/sys/class/graphics/fbN/name` contains `rockchip`;
  `usbN` = the N-th fb whose name contains `udl`, sorted by `readlink -f fbN/device` (USB port path).
- Sources: `vnc:<port>` → `android-hdmi -f <fb> -p <port> -I <glob>`; `cluster` →
  `cluster-hdmi -f <fb> -i $IFACE`; `console` → `con2fb <fb> 1` (`FBIOPUT_CON2FBMAP`), then waits
  until the display goes away; `rearcam:vnc:<port>` / `rearcam:cluster` → `rear-camera -f <fb>
  -i $IFACE -- <viewer>`; `rearcam` → `rear-camera` alone (see 9.11).
- One background loop per screen: waits for the fb, restarts the viewer, re-resolves after an unplug.
  Log: `/var/log/display-map.log`. Init S93 / K07.
- Writes `ENABLED=0` to `/etc/default/android-hdmi` and `/etc/default/cluster-hdmi` (the
  single-screen services).

Viewer changes:

- `android-hdmi`: new `-I <glob>|none` (input devices to forward; default `/dev/input/event*`).
- `android-hdmi` and `cluster-hdmi`: switch tty1 to `KD_GRAPHICS` only if
  `FBIOGET_CON2FBMAP` for console 1 equals the minor of their own fb (`console_on()`).

#### Android 10 (LineageOS 17.1): host setup and sync

| Item | Value |
|---|---|
| Host | Ubuntu 22.04.5, 8 threads, 15 GB RAM, **16 GB swap** (`/swapfile`, was 2 GB) |
| Packages | LineageOS build list + `python2` (2.7.18) + `git-lfs` 3.0.2 |
| Tree | `~/lineage-17.1`, **58 GB** after sync |
| Init | `repo init -u https://github.com/LineageOS/android.git -b lineage-17.1 --depth=1 -g default,-darwin,-mips,-notdefault` |
| Sync | `repo sync -c -j8 --no-tags --no-clone-bundle --optimized-fetch --prune` |
| Local manifest | `.repo/local_manifests/tinker-xen.xml`: `remove-project` for `android_external_chromium-webview_prebuilt_{arm64,x86,x86_64}` |
| WebView ARM | re-synced, `git lfs pull` → `webview.apk` 98,126,751 bytes |
| Target | `lineage_arm-userdebug` (`vendor/lineage/build/target/product/lineage_arm.mk`: inherits `aosp_arm.mk`, `TARGET_USES_64_BIT_BINDER := true`) |

Work still to do for the Xen guest:

1. Plain `lineage_arm-userdebug` build (`source build/envsetup.sh; lunch lineage_arm-userdebug; m -j8`).
2. Guest kernel: Android 10 userspace needs the 64-bit binder ABI (`CONFIG_ANDROID_BINDER_IPC_32BIT`
   off) and `CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"`; the 4.14 base is an
   Android 10 launch kernel version.
3. Graphics without a GPU: SwiftShader (GLES) plus a composer/gralloc that works on the Xen PV
   framebuffer. Highest risk; no known Android 10 ARM32 software-rendering setup.
4. Vendor init/fstab for Xen (xvda system, vendor, xvdb data, hvc0 console, eth0 192.168.100.3),
   a new `android-guest` recipe and wks partitions (`android-vendor`).

Expected sizes: build output 60–90 GB; card: system 1.5–2 GB, vendor 0.1–0.3 GB, data ~2 GB → whole
card ~5–5.5 GB (8 GB minimum, 16 GB recommended).

Rejected: **Android 17** (64-bit only, RAM/GPU/kernel requirements, ~400 GB / 32–64 GB host).
Fallbacks: Android 9 (from source; Replicant 9 ran software-rendered on ARMv7) or 7.1.1
(`armeabi-v7a-25_r18`, Google APIs, the last prebuilt ARM32 image).

### 9.11 Rear camera and audio pipeline (2026-10-27)

**Status: host-tested (logic, rendering, mixing), not tested on hardware.**

**CAN:** new frame `0x0C2` gear, DLC 2: byte 0 selector `'P' 'R' 'N' 'D'`, byte 1 forward gear
1–6 (0 otherwise). `vehicle-sim` cycle: 50–52 s stand (D), 52–58 s reverse 0→4→0 km/h, 58–60 s P;
SIGUSR2 (`vehicle-bench reverse`) holds R at 3 km/h. `cluster-hdmi` shows the selector when it is
not D (falls back to the speed-derived gear after 1 s without 0x0C2) and redraws its static layer
on SIGCONT; `android-hdmi` requests a full (non-incremental) VNC update on SIGCONT. can2rtos and
FreeRTOS ignore 0x0C2.

**`rear-camera`** (`recipes-extended/rear-camera`): `rear-camera [-f fb] [-i if] [-d /dev/video0]
[-s 640x480] [-r 15] [-n] [-T] [-g WxH] [-- viewer args]`. Forks/execs the viewer; on reverse
(0x0C2 byte 0 == 'R', or SIGUSR1 manual, or -T) SIGSTOPs it and waits (`waitpid WUNTRACED`) until
it has stopped, opens V4L2 (YUYV only, 4 mmap buffers, MJPEG-only cameras refused), draws nearest-
neighbour scaled, aspect-kept, mirrored (-n off) BT.601 YUYV→RGB at ≤ 15 fps, fixed guide lines
(3 bands) and a banner. Exit: 1 s hold after R ends, or 1 s without 0x0C2 → STREAMOFF, SIGCONT.
Camera errors/unplug → colour bars + "NO CAMERA", reopen every 2 s. Viewer exit → rear-camera
exits 1 (display-map restarts). SIGTERM → SIGTERM + SIGCONT to the viewer. Reverse chime via
`/run/audio-mixer/chime` (O_NONBLOCK; no-op when the mixer is down). display-map sources
`rearcam:cluster`, `rearcam:vnc:<port>`, `rearcam`; default `usb2=rearcam:cluster`.
Kernel: `CONFIG_USB_VIDEO_CLASS=m` was already set; image adds `kernel-module-uvcvideo`.

**`audio-mixer`** (`recipes-extended/audio-mixer`, init S91): ALSA `snd_pcm_set_params` S16_LE
stereo 48 kHz, 80 ms latency, 20 ms (960-frame) mix periods; the blocking `snd_pcm_writei` paces
the loop (clock_nanosleep when no card; card reopened every 2 s, alsa-lib error output silenced).

| Source | Transport | Format | Buffering |
|---|---|---|---|
| media (media1) | UDP 192.168.100.1:5601, only from .4 | S16LE 2 ch 48 kHz | > 300 ms buffered → drop to 100 ms |
| android (android1) | TCP 192.168.100.1:5602, only from .3, one connection (new replaces old), SO_RCVBUF 16 KB | S16LE 2 ch 44.1 kHz | read only up to 100 ms ahead → TCP back-pressure paces the guest |
| chimes | FIFO `/run/audio-mixer/chime` (O_RDWR), one name per line; `audio-mixer -c NAME` | synthesised sine, 5 ms ramps | queue of 8, duplicates dropped |

Per source: 32768-frame ring, 60 ms prebuffer before playing, linear-interpolation resampling
(16.16 step), gain `-g media%,android%`. Chimes: `warn` 3× 880 Hz 160 ms, `reverse` 2× 1250 Hz
90 ms, `info` 660+880 Hz. Ducking: guests × 0.25 while a chime plays or is queued (one-pole,
~10 ms). `vehicle-chimes`: `tail -n 0 -F` of `guest-rtos1.log`; `ALERT `/`SIGNAL LOST` → warn,
` cleared`/`SIGNAL OK again` → info. Config `/etc/default/audio-mixer` (DEVICE, GAIN, CHIMES, OPTS).

**media1:** `media-play -a URL` (default `udp://192.168.100.1:5601?pkt_size=1920`, `""` = off). A
video with an audio stream (probed with `ffmpeg -i | grep Audio:`; an output without streams is an
ffmpeg error) gets a second output `-map 0:a:0 -ac 2 -ar 48000 -f s16le`; `-re` paces it.

**Board test 2026-10-27 (Step 40):** uvcvideo STREAMON oopsed in `dma_common_alloc_pages`
(`memset(page_address())` = NULL) because xen-swiotlb's `.alloc_pages` path took a CMA page and
dom0's CMA (64 MiB at 0x70000000, bank 1) is highmem; kernel 0003 frees a highmem CMA page and
allocates from lowmem. After the oops the stream mutex/USB device lock stays held: later USB
enumeration and `lsusb` hang until reboot. Codec card "Audio" (0bda:49f6): pcm0 is 8 kHz only,
pcm1/pcm2 44.1–192 kHz → `DEVICE=plughw:CARD=Audio,DEV=1`. Init `stop` used `killall
audio-mixer`, which matched the init script itself → pidof loop excluding `$$`. Android 7 has no
`mkfifo`/`netcat` links (toybox applets only).

**Android screen:** the 2026-10-25 "timer freeze" (section Y) was s2idle suspend after the 60 s
screen timeout (`PM: suspend entry (s2idle)` at 162 s in guest-android1.log); nothing wakes the
guest. Fix: `write /sys/power/wake_lock xen` in `on init`, `svc power stayon true` and
`screen_off_timeout 2147483647` at boot_completed (service xen-stayon). android1.cfg: vfb on,
`xen_fbfront.depth=16 xen_fbfront.video=2,800,480 qemu=0` (kernel cmdline `qemu=0` →
`ro.kernel.qemu=0` via init's emulator pass, HWUI off); default.prop `ro.config.low_ram=true`
(`ActivityManager.isHighEndGfx()` false → ImageWallpaper draws with Canvas, not GLES 2.0).
Verified live: 6+ min with the screen, launcher focused, no new suspend entries.
display-map default `hdmi=rearcam:vnc:5900 usb1=vnc:5901 usb2=cluster usb3=console`.

**android1 (Android 7):** the SDK image's only audio HAL is `audio.primary.goldfish.so`, which
writes 44.1 kHz stereo S16 to `/dev/eac` (O_RDWR); `ro.hardware=xen` and empty
`ro.product.board`/`ro.board.platform` meant no primary HAL loaded at all. The build adds
`/system/lib/hw/audio.primary.xen.so → audio.primary.goldfish.so` (debugfs `symlink`, e2fsck
clean). `init.xen.rc`: `on post-fs` `exec … /xen-audio.sh setup` makes `/dev/eac` a FIFO
(root:audio 0660) before audioserver starts; service `xen-audio` (su domain, SELinux permissive)
sets `tcp_wmem` to `4096 16384 32768` and runs `netcat 192.168.100.1 5602 < /dev/eac` in a loop.
The HAL's input stream would also read `/dev/eac` (recording would eat playback data; no mic
anyway). Latency ≈ pipe 64 KB (370 ms) + send buffer + 100 ms ring. Android 10 (Q goldfish HAL uses
tinyalsa) needs a different path: not done.

---

## 10. Build and flash procedure

### 10.1 Build

```bash
cd ~/yocto-rk3288 && source poky/oe-init-build-env build
bitbake tinker-xen-test-image
sha256sum tmp/deploy/images/tinker-board/tinker-xen-test-image-tinker-board.wic
```

### 10.2 Flash (on the PC – this erases the card)

```bash
lsblk                                   # find the card (29.7G), e.g. sda
ls -l /dev/sda                          # MUST start with "b" (block device)
sudo umount /dev/sda?*
sudo bmaptool copy ~/yocto-rk3288/build/tmp/deploy/images/tinker-board/tinker-xen-test-image-tinker-board.wic /dev/sda
```

**Always write to the whole disk (`/dev/sda`), never to a partition (`/dev/sda1`).**

Lesson learned: once `/dev/sda` was a *regular file* (card not inserted while flashing →
bmaptool created a 3.2 GB file in /dev). Check with `ls -l /dev/sda`:
`brw-rw----` = real card; `-rw-r--r--` = a file → `sudo rm /dev/sda`, reinsert the card. Only delete
it when the line starts with `-`.

Error seen once: `bmaptool: ERROR: ... Device or resource busy` → a partition was auto-mounted; run
the `umount` line again.

### 10.3 Verify the card (read-only)

```bash
sudo mount -o ro /dev/sda6 /mnt && ls -l /mnt/xen && sha256sum /mnt/xen; sudo umount /mnt
```

Expected for the final image: `917560` bytes, `6a53ae892f2dc3bce53696a0c1b1275e111ce7e8b01c539d72107f0027a77750`.

### 10.4 First boot

Insert the card, connect the UART, start `picocom -b 115200 /dev/ttyUSB0`, power on,
**do not press keys** until `tinker-board login:` appears. Log in as `root` (no password).

### 10.5 Hardware test after flashing (Step 40 image)

```
cat /sys/class/drm/card0-HDMI-A-1/status          connected
grep ^SCREENS /etc/default/display-map             SCREENS="hdmi=rearcam:vnc:5900 usb1=vnc:5901 usb2=cluster usb3=console"
tail -3 /var/log/display-map.log                   rear-camera: /dev/fb0 800x480 ... viewer started
                                                   android-hdmi: connected to 127.0.0.1:5900, guest 800x480
adb connect 192.168.100.3:5555
adb shell getprop sys.boot_completed               1
adb shell cat /sys/power/wake_lock                 xen
adb shell dumpsys power | grep -E "mWakefulness=|mStayOn="     Awake / true
adb shell dumpsys window | grep -m1 mCurrentFocus  com.android.launcher3/...Launcher
grep -A4 Mouse /proc/bus/input/devices             H: Handlers=eventN
ls -l /proc/$(pidof android-hdmi)/fd | grep event  the mouse's eventN is open
/etc/init.d/vehicle-bench reverse                  rear-camera: in reverse; /dev/video0 640x480 YUYV
/etc/init.d/vehicle-bench reverse                  rear-camera: out of reverse
dmesg | grep -i -A3 "Oops\|NULL pointer"           (nothing: patch 0003)
audio-mixer -c warn                                (chime)
grep -E "^rate|^state" /proc/asound/card*/pcm1p/sub0/hw_params /proc/asound/card*/pcm1p/sub0/status
                                                   rate: 48000 / state: RUNNING
amixer -c Audio sget PCM,1                         Playback 175 [100%] [on]
tail /var/log/audio-mixer.log                      playing on plughw:CARD=Audio,DEV=1; android: connected from 192.168.100.3
adb shell am start -a android.intent.action.VIEW -t audio/ogg \
  -d file:///system/media/audio/ringtones/Andromeda.ogg -n com.android.music/.AudioPreview
netstat -tn | grep 5602                            Recv-Q drains back to 0 after the ringtone
```

---

## 11. Operating the system (commands and expected output)

### 11.1 Console basics

| Action | How |
|---|---|
| Log in | `root`, no password (debug-tweaks image) |
| Switch UART input dom0 ↔ Xen | `Ctrl-a` three times |
| Leave a guest console | `Ctrl-]` |
| Stop autoboot | any key at "Hit any key to stop autoboot" |

### 11.2 Xen and guests

```
xl list                          Domain-0, rtos1, android1
xl info                          xen_version 4.16.1, nr_cpus 4
xl cpupool-list                  Pool-0 3 credit2 / rt 1 rtds
xl sched-rtds -d rtos1 -v all    rtos1 0 1000 200 yes
xl console rtos1                 [rtos] and [vehicle] lines
xl console android1              Android shell "generic:/ $"
xl dmesg | head                  Xen boot log (debug=n, PSCI, 4 CPUs)
/etc/init.d/xen-guests stop|start|status
```

Example:

```
root@tinker-board:~# xl sched-rtds -d rtos1 -v all; xl cpupool-list
Name                                ID VCPU    Period    Budget  Extratime
rtos1                                1    0      1000       200        yes
Name               CPUs   Sched     Active   Domain count
Pool-0               3   credit2       y          2
rt                   1      rtds       y          1
```

### 11.3 Android

```
adb connect 192.168.100.3:5555          connected to 192.168.100.3:5555
adb shell getprop sys.boot_completed    1
adb shell                               generic:/ $
adb logcat
adb kill-server                         (after use)
```

### 11.3a One HDMI display: which OS it shows

`sed -i 's/^SCREENS=.*/SCREENS="hdmi=<source>"/' /etc/default/display-map` then
`/etc/init.d/display-map restart`:

| OS | `<source>` | Expected in `/var/log/display-map.log` | Other checks |
|---|---|---|---|
| android1 | `rearcam:vnc:5900` | `connected to 127.0.0.1:5900`, `guest 800x480 on 800x480` | `adb shell getprop sys.boot_completed` → 1 |
| media1 | `vnc:5901` | `connected to 127.0.0.1:5901` | `xl list \| grep media1` |
| rtos1 + car | `rearcam:cluster` | `cluster-hdmi: /dev/fb0 800x480 32 bpp` | `grep vehicle /var/log/xen/console/guest-rtos1.log \| tail -3` |
| dom0 | `console` | `hdmi (/dev/fb0): console` | login prompt on the display (USB keyboard) |
| rear camera | (`rearcam:` sources) | `rear-camera: in reverse`, `/dev/video0 640x480 YUYV` | `/etc/init.d/vehicle-bench reverse` (toggle) |

Black screen: `cat /sys/class/drm/card0-HDMI-A-1/status` must be `connected`.

### 11.4 Automotive bench

```
candump vcan0                                 raw frames (0C0, 0C1)
grep vehicle /var/log/xen/console/guest-rtos1.log | tail
/etc/init.d/vehicle-bench pause               → "[vehicle] SIGNAL LOST ..."
/etc/init.d/vehicle-bench pause               → "[vehicle] SIGNAL OK again"
tail /var/log/vehicle-bench.log               can2rtos: 10s: frames 1001, max gap 24.9 ms, dropped 0
cluster-hdmi -g 1280x720 -S /tmp/c.ppm        dashboard snapshot without monitor
/etc/init.d/vehicle-bench reverse             hold reverse: rear camera on the cluster screen
candump vcan0,0C2:7FF                         gear frames: 0C2 [2] 52 00 = 'R'
tail /var/log/display-map.log                 rear-camera: in reverse / /dev/video0 640x480 YUYV
audio-mixer -c warn                           warning chime (also reverse, info)
tail /var/log/audio-mixer.log                 media: UDP ..., android: connected from 192.168.100.3
```

Real car (read-only): plug a USB CAN adapter, then in `/etc/default/vehicle-bench`:

```
IFACE=can0
SIM=0
BITRATE=500000
```

`/etc/init.d/vehicle-bench restart`. The CAN IDs and scaling are the bench's own; a real car uses
manufacturer-specific IDs, so `can2rtos` must be adapted. **Never connect vehicle-sim to a real
vehicle bus.**

### 11.5 Ignition

Wiring: ACC (12–14.4 V with spikes) **must not** reach the 3.3 V pin. Optocoupler (or 10 k/2.7 k
divider + 3.3 V zener) → header pin 7 (GPIO0_C1 = gpiochip0 line 17), external 10 k pull-down.

```
gpioget gpiochip0 17                 read the pin by hand first (1 = ACC on)
# /etc/default/ignition-watch: ENABLED=1 (ACC_ON_LEVEL=0 for an inverting optocoupler)
/etc/init.d/ignition-watch start
tail /var/log/ignition-watch.log
```

The board must be powered from permanent 12 V via a 5 V/3 A automotive DC-DC; poweroff halts the
board but does not cut 5 V, so a car install also needs a power-hold/timer relay.

### 11.6 System

```
cat /sys/class/thermal/thermal_zone{1,2}/temp      CPU, GPU in m°C (≈ 35000-42000)
speaker-test -c 2 -t sine                          sound (ALSA card 0)
reboot                                              → guests stopped, Reset cause: RST
poweroff                                            → "reboot: Power down"
```

### 11.7 Logs

`/var/log` is a link to `/var/volatile/log` (tmpfs): **all logs are lost at every reboot**. Save
them first (below). U-Boot and early Xen output exist only on the UART: start
`picocom -b 115200 /dev/ttyUSB0` before power-on to see them. Type or paste one short line at a
time: Xen buffers only 128 characters of UART input for dom0 (`SERIAL_RX_SIZE`), longer pastes
lose characters.

Boot (U-Boot → Xen → dom0):

```
xl dmesg                                       Xen log: PSCI, CPUs, schedulers, guest creation
cat /var/log/dmesg                             dom0 kernel log saved at boot
dmesg                                          dom0 kernel log now (dmesg -w: follow)
cat /var/log/boot                              init scripts ("Starting ..." lines)
```

Yocto Linux (dom0):

```
cat /var/log/messages                          syslog, all services (tail -f to follow)
cat /var/log/cluster-hdmi.log                  dashboard
cat /var/log/vehicle-bench.log                 vehicle-sim / can2rtos
cat /var/log/ignition-watch.log                ignition (when enabled)
cat /var/log/android-hdmi.log                  Android screen viewer (when enabled)
cat /var/log/xen/xen-hotplug.log               vif/disk hotplug scripts
```

FreeRTOS (rtos1), written by xenconsoled (`XENCONSOLED_TRACE=guest` in `/etc/default/xencommons`):

```
cat /var/log/xen/console/guest-rtos1.log       full console (tail -f to follow)
grep '10 ms task' /var/log/xen/console/guest-rtos1.log    timing reports
grep ALERT /var/log/xen/console/guest-rtos1.log           vehicle warnings
xl console rtos1                               live console (Ctrl-] leaves)
cat /var/log/xen/xl-rtos1.log                  xl (toolstack) log
```

Android (android1):

```
cat /var/log/xen/console/guest-android1.log    kernel + init console from boot
xl console android1                            live console (Ctrl-] leaves)
adb connect 192.168.100.3:5555                 needed before the adb lines
adb logcat -d                                  app/framework log (without -d: follow)
adb logcat -d -b crash                         crashes only
adb shell dmesg                                Android kernel log
adb shell getprop sys.boot_completed           1 = boot finished
cat /var/log/xen/xl-android1.log               xl log (.1, .2: earlier starts)
cat /var/log/xen/qemu-dm-android1.log          QEMU device model log
```

`Ctrl-C` stops the following forms (`tail -f`, `dmesg -w`, `adb logcat`). A frozen Android guest
makes `adb logcat` hang: `Ctrl-C` returns the prompt.

Save everything to the SD card before a reboot (one line at a time):

```
D=/home/root/logs-$(date +%H%M%S)
mkdir -p $D
cp -r /var/log/* $D/
xl dmesg > $D/xen-dmesg.txt
dmesg > $D/dom0-dmesg-now.txt
xl list > $D/xl-list.txt
adb connect 192.168.100.3:5555
adb logcat -d > $D/android-logcat.txt
sync; ls $D
```

On the PC (card in a reader): `/media/$USER/root/home/root/logs-*`.

---

## 12. Testing and verification

### 12.1 How tests were done

All board tests ran over the UART from the PC with small Python/pyserial scripts (listen, send a
command, collect the output into `diagnostics/uart-*.log`). Files were moved PC → board by sending
`printf '\ooo...' >> file` chunks and checking `md5sum`; board → PC by throttled `od` chunks (Xen's
serial buffer drops large bursts). BusyBox lacks `head -c`, `timeout` and `base64`, so `dd`, loops
and octal `printf` were used.

New Xen or U-Boot builds were first loaded **into RAM only** (`loady` at the U-Boot prompt) and
started with `bootz`/`go`, so nothing was written to the card before it was proven.

### 12.2 Test matrix

| # | Test | Method | Expected | Result | Log |
|---|---|---|---|---|---|
| 1 | Xen HYP entry | boot | no "NS Hyp" error | ✅ | uart-xen-card.log |
| 2 | dom0 boot | boot | login, `/sys/hypervisor/type`=xen | ✅ | uart-xen-card.log |
| 3 | SMP | `xl info` | nr_cpus 4 | ✅ | uart-xen-psci.log |
| 4 | Reboot | `reboot` | Reset cause: RST | ✅ | uart-xen-reset.log |
| 5 | Autoboot | power on, no key | Xen + guests | ✅ | uart-xen-autoboot.log |
| 6 | Native fallback | sysboot | Linux without Xen | ✅ | uart-xen-autoboot.log |
| 7 | Watchdog | stop keep-alive | reset ~43 s | ✅ | uart-watchdog-test.log |
| 8 | RTOS timing | 10 s reports | ≈ 10000 µs ± 60 | ✅ 9958–10040 | uart-rtds-repro-fixed.log |
| 9 | Android boot | getprop | boot_completed=1 | ✅ | uart-final-reboot-poweroff.log |
| 10 | adb | adb connect | connected | ✅ | uart-android-adb-net.log |
| 11 | Android stability | 10 min | 0 system_server crashes | ✅ | uart-stability.log |
| 12 | Thermal | sysfs | readings | ✅ 35–42 °C | uart-final-all.log |
| 13 | CAN → RTOS | 10 s reports | 1000 frames, 0 lost | ✅ | uart-vehicle-bench.log |
| 14 | Alerts | drive cycle | overspeed, redline raise/clear | ✅ | uart-vehicle-bench.log |
| 15 | Signal loss | pause | SIGNAL LOST / OK again | ✅ | uart-vehicle-bench.log |
| 16 | Frame gaps | 10 s reports | < 100 ms | ✅ 20–35 ms | uart-session.log |
| 17 | Dashboard | `-S` snapshot | picture with live values | ✅ | cluster-board2.png |
| 18 | RTDS race | 3000 × pause/unpause | no panic | ✅ (old Xen: panic < 250) | uart-rtds-repro-*.log |
| 19 | Poweroff | `poweroff` | guests stopped, power down | ✅ | uart-final-reboot-poweroff.log |
| 20 | Guest internet | ping 8.8.8.8 | reply | ⏳ no Ethernet | – |
| 21 | HDMI picture | monitor | dashboard on screen | ⏳ no monitor | – |
| 22 | Ignition | ACC off | poweroff after 10 s | ⏳ not wired | – |
| 23 | Real CAN | adapter | frames in listen-only | ⏳ no adapter | – |

---

## 13. All errors and fixes

| # | Error / symptom (exact text where available) | Root cause | Fix | Where |
|---|---|---|---|---|
| 1 | Boot stops after `mmcblk0: p1 … p9` | card held a different image (9 partitions, U-Boot 2024.01) | flash the Yocto image | Stage 2 |
| 2 | Typing on the UART does nothing | adapter on UART1 (pins 8/10) | UART2 pins 32/33/34 | Stage 5 |
| 3 | `CPU: All CPU(s) started in SVC mode.` / `Xen must be entered in NS Hyp mode` | no NONSEC/VIRT for RK3288 in U-Boot | U-Boot 0001 | Stage 6 |
| 4 | Silent hang after `Starting kernel ...` | HSCTLR reset value 0x70cd087f | HSCTLR = 0x30c50830 before ERET | U-Boot 0001 |
| 5 | non-secure access faults | SoC still secure | SGRF/TZPC in `psci_board_init` | U-Boot 0001 |
| 6 | `Missing processor info: 410fc0d1` | no A12/A17 proc_info | Xen 0001 | Stage 7 |
| 7 | dom0 Oops in `rk3288_suspend_init` | L2CTLR trap → UNDEF | Xen 0002 | Stage 7 |
| 8 | dom0 abort at 0xffb23fe0 | secure DMAC window | DT: DMAC at 0xff600000 | boot script |
| 9 | dom0 hang ~2 s, Xen `NOW` negative | Linux resets timer7 (arch counter) | kernel 0001 | Stage 8 |
| 10 | `Reset cause: POR` at `xl create` (once) | probably power dip | 5 V/3 A supply | Stage 9 |
| 11 | `INIT: Id "S2" respawning too fast` | getty on Xen-owned UART | start_getty | Stage 19 |
| 12 | FreeRTOS stale console lines | MMU/caches off | enable MMU + caches | Stage 11 |
| 13 | RTOS period 10.15 ms | relative TVAL | absolute CVAL | Stage 11 |
| 14 | rtos1 100 % CPU | idle spinning | WFI idle hook | Stage 11 |
| 15 | `Xen: Platform reset did not work properly!` | no reset method | Xen 0003 | Stage 13 |
| 16 | `Cannot find the RK3288 CRU node in DT` | RK3288W CRU renamed | match both compatibles | Stage 13 |
| 17 | `Failed to bring up CPU 1 (error -19)` | no PSCI | U-Boot 0002 | Stage 15 |
| 18 | only 2 CPUs, `ALREADY_ON (-4)` | all cores released by the first mailbox write | power cores off in `psci_board_init` | Stage 15 |
| 19 | `EventHub: Could not register INotify for /dev/input` | no /dev/input | `mkdir /dev/input` | Stage 16 |
| 20 | `NullPointerException` AlarmManagerService | timerfd alarm needs CAP_WAKE_ALARM | guest kernel 0001 | Stage 16 |
| 21 | `usb 1-1: reset high-speed USB device` | GL850G hub autosuspend + adb scan | udev rule | Stage 16 |
| 22 | `Assertion '!unit_on_replq(svc)' failed at rt.c:587` | RTDS race (Xen bug) | workaround: RTDS only on CPU 3; real fix #31 | Stage 16 |
| 23 | release Xen without RTDS | RTDS only default in debug; menu needs EXPERT | `CONFIG_EXPERT=y`, `CONFIG_SCHED_RTDS=y` | Stage 19 |
| 24 | no temperature readings | cooling maps → TSADC probe deferred | `fdt rm .../cooling-maps` | Stage 19 |
| 25 | `Service xen-net does not have a SELinux domain defined` | shell-script service without domain | `seclabel u:r:su:s0` | Stage 19 |
| 26 | `bmaptool: Device or resource busy` | auto-mounted partition | `umount` again | Section 10 |
| 27 | `/dev/sda` is a 3.2 GB regular file; `lsblk: /dev/sda: not a block device` | flashed with no card present | `rm` only when `ls -l` shows `-`; recreate node by reinserting card | Section 10 |
| 28 | SurfaceFlinger `no suitable EGLConfig` | 32 bpp xenfb | guest kernel 0003 (16 bpp) | Stage 20 |
| 29 | apps `Failed to choose config`, then guest freezes | no GLES 2.0; with software UI the vtimer IRQ stalls | **unsolved** → headless | Stage 20 |
| 30 | dashboard RTOS lamp without data | `/var/log/xen/console` missing | xen-tools bbappend volatile + TRACE | Stage 22 |
| 31 | FreeRTOS max frame gap ~105 ms | notify per read → xenconsoled rate limit 30/200 ms | notify only when ring nearly full | Stage 23 |
| 32 | `CPU3: Unexpected Trap: Data Abort` in `rt.c#repl_timer_handler` | unit left on RTDS replq after pause | Xen 0004 + 0005 (upstream backports) | Stage 25 |
| 33 | Linux commands at the U-Boot prompt (`Unknown command`) | Enter sent during autoboot countdown | `boot`; only listen until `login:` | Stage 25 |
| 34 | Board went silent twice under full load | suspected power supply | power-cycle; better PSU | Section 14 |
| 35 | Launcher3 / SystemUI crash-loop | HWUI needs GLES 2.0 | accepted (headless, adb works) | Section 14 |

---

## 14. Known limitations

- **Android is headless.** No GPU, display or sound for the guest: Xen 4.16 has no RK3288 IOMMU
  support (no device passthrough), no PV sound backend for Android 7. The only GLES is the 1.x
  software renderer; Android's UI toolkit needs GLES 2.0, so Launcher3 and SystemUI restart in a
  loop (system_server, services and adb are fine). The PV screen/touch path exists but freezes the
  guest. Android 7.0 is the newest prebuilt ARM32 image; SELinux is permissive.
- **License:** the Android SDK system image is under the Android SDK License Agreement –
  development use only, do not redistribute.
- **Bring-up security:** U-Boot opens the whole DDR and all peripherals to the non-secure world; the
  PSCI monitor lives in reserved but not TrustZone-protected RAM.
- **RTDS** only on CPU 3 (design choice; the scheduler bug itself is fixed now). RTOS jitter about
  ±2 µs idle, up to ±60 µs when Android is busy (shared L2 cache and memory bus).
- **No CPU frequency scaling** under Xen 4.16; CPUs stay at 500 MHz (APLL 1 GHz, U-Boot setting).
- **Automotive:** a bench and listen-only monitoring system, not certified (ISO 26262); it must not
  control anything in a car. CAN reaches FreeRTOS through dom0 (Linux), so the frame path has dom0
  latency (≤ ~35 ms measured); only FreeRTOS's checks are real-time.
- **Power:** two silent hangs under full load (4 CPUs, Android, QEMU, long UART transfers) until
  power-cycled; use a solid 5 V / 3 A supply with a short cable.
- **Not tested on hardware:** guest internet (no Ethernet), HDMI picture (no monitor), ignition (not
  wired), real CAN bus (no adapter).
- **Part 6 tested on hardware 2026-10-27** (`diagnostics/part6/RESULTS.md`): dom0 320 MB, android1
  512 MB, media1 192 MB, rtos1 4 MB, shrunk android-system (613 MB), RTDS 1000/1000 at boot
  (`xl sched-rtds -d rtos1 -v all`), media1 on dom0's kernel. **Still untested:** display-map on
  real displays, udl USB displays, HDMI pixel-clock patch 0002.
- **USB displays:** only DisplayLink DL-1x0/DL-1x5 (udl); no GPU, so all screens are CPU-copied
  (VNC raw encoding) at 500 MHz.
- **Android 10:** in progress; software rendering on ARM32 without a GPU is unproven.

---

## 15. Recovery plan

The board's own eMMC and bootloaders are never written; everything is on the SD card, so every
failure is fixed from the PC.

| Problem | What to do |
|---|---|
| A guest misbehaves | `xl destroy <name>; /etc/init.d/xen-guests start`. Don't autostart: `XEN_GUESTS=""` in `/etc/default/xen-guests` |
| Xen or dom0 hangs | the hardware watchdog resets the board after ~45 s; otherwise power-cycle |
| dom0 shell blocked, Xen alive | `Ctrl-a` ×3 (input to Xen), `r` = scheduler dump, `R` = reboot the machine; `Ctrl-a` ×3 back to dom0 |
| USB enumeration / `lsusb` hangs | a uvcvideo oops left the device lock held (before kernel 0003): reboot |
| Android unreachable 1–2 min after boot | s2idle suspend: check `/sys/power/wake_lock` (`xen`) and `PM: suspend entry` in `/var/log/xen/console/guest-android1.log` |
| Android screen off again | headless: drop `vfb` in `/etc/xen/android1.cfg`, extra `video=vfb:640x480-16@60 vfb.videomemorysize=4194304` instead of the xen_fbfront/qemu options |
| Xen does not boot | press a key at "Hit any key to stop autoboot", then `sysboot mmc 1:6 any ${scriptaddr} /native/extlinux.conf` (Linux without Xen). boot.scr also falls back by itself |
| Debug a Xen boot | at the U-Boot prompt `loady ${xen_addr}` a debug Xen and run the xen-boot.cmd lines by hand; nothing is written |
| Android data broken | reflash the card (or `dd` android-userdata.img to p9) |
| Card does not boot | reflash with bmaptool; a known-good original image is in `diagnostics/baseline/` |

Never type `saveenv` at the U-Boot prompt.

---

## 16. Working rules and safety

Rules agreed at the start and followed throughout:

1. The SD card is never flashed automatically – the user flashes with bmaptool; the device is
   checked first (`ls -l /dev/sda` must start with `b`).
2. No destructive disk commands; no `saveenv`, `erase`, `write` or other persistent bootloader
   commands.
3. The bootloader is not replaced without a recovery plan: new U-Boot/Xen builds were tested from
   RAM (`loady`) before they went onto a card; the card is the only boot medium and can always be
   reflashed.
4. No claim of a hardware test without evidence: every ✅ in this document has a UART log.
5. Build directories, shared state and downloads were never deleted; generated `.config` files were
   never edited – changes go through config fragments and patches in the layer.
6. Automotive: simulation only on a virtual bus; a real car only listen-only; nothing transmits on
   a vehicle bus.

---

## 17. Future work

| Item | Notes |
|---|---|
| Test the dashboard on a real HDMI monitor | should appear automatically |
| Test guest internet | needs an Ethernet cable at boot |
| Wire and test ignition-watch | optocoupler on pin 7, power-hold relay |
| Real CAN adapter | candleLight/PEAK, listen-only, adapt IDs to the car |
| Android display | needs GLES 2.0 (virtio-gpu/virgl not available in Xen 4.16 ARM) or a fix for the vtimer freeze |
| Android 10 guest | build `lineage_arm-userdebug`, binder-64 guest kernel, SwiftShader + PV-framebuffer composer (9.10) |
| Part 6 bring-up | flash with Android 7 first; check xl list, RTOS timing, media1, display-map, temperature |
| Newer Xen (4.17+) | upstream RTDS fixes, cpupool improvements; would drop patches 0004/0005 |
| Upstreaming | Xen 0001/0002/0003, kernel 0001 are upstream candidates |
| Security hardening | keep secure DDR regions, put the PSCI monitor in TrustZone RAM |
| CAN directly into FreeRTOS | device passthrough of a CAN controller would remove dom0 latency (needs IOMMU) |

---

## 18. Appendix

### 18.1 Image history

| Date / time | Image (`tinker-xen-test-image-tinker-board-…rootfs.wic`) | sha256 (start) | Contents |
|---|---|---|---|
| 2026-10-22 | `core-image-minimal-…20261022043621` | d9cdb9a5 | original baseline |
| 10-23 09:58 | 20261023095857 | e066744b | first test image, debug Xen |
| 10-23 11:10 | 20261023111057 | 0bc653ca | U-Boot HYP |
| 10-23 15:59 | 20261023155925 | 7e957065 | Xen + dom0 boots |
| 10-23 18:19 | 20261023181955 | 2762f72a | first guest |
| 10-24 05:58 | 20261024055832 | d3607e17 | networking |
| 10-24 07:27 | 20261024072751 | 72ef08b5 | FreeRTOS |
| 10-24 07:43 | 20261024074358 | 4654864b | RTDS |
| 10-24 12:44 | 20261024124409 | a8889f49 | reboot fix |
| 10-24 12:55 | 20261024125534 | 0e7ead10 | autoboot |
| 10-24 13:24 | 20261024132400 | 716468e1 | PSCI / SMP |
| 10-24 13:47 | 20261024134726 | 34f4e3f8 | Android |
| 10-24 14:01 | 20261024140152 | 80e38cf8 | guest1 removed |
| 10-24 14:32 | 20261024143227 | 172618c9 | Android fixes |
| 10-24 14:53 | 20261024145333 | 141d6f54 | cpupools |
| 10-24 15:13 | 20261024151313 | d27e5aaf | quiet RTOS |
| 10-24 16:33 | 20261024163357 | d0cdb27c | production hardening |
| 10-25 01:48 | 20261025014834 | 25b48f7c | final headless |
| 10-25 07:22 | 20261025072211 | e23e81e7 | stable headless after screen test |
| 10-25 08:31 | 20261025083123 | 59e8c585 | automotive bench + dashboard |
| 10-25 14:52 | (ignition build) | 0a623c4a | burst fix, ignition – hit the RTDS panic |
| **10-25 15:14** | **20261025094250** | **99a3fec6** | **RTDS fix – final, flashed and verified** |
| 10-27 16:19 | 20261027104724 | – | Part 6 + rear camera/audio (flashed; webcam oops, 8 kHz output) |
| **10-27 17:25** | **20261027115058** | **a1263bb0** | **kernel 0003, Android screen + keep-awake, 48 kHz output, audio fixes – to flash** |

### 18.2 Log index (`~/yocto-rk3288/diagnostics/`)

| Log | Content |
|---|---|
| REPORT.md | engineering report, sections A–Z |
| BOARD-TESTS.md | manual hardware test procedures |
| uart-boot.log | first Yocto boot, SVC mode |
| uart-xen-*.log | Xen bring-up, guest, net, RTDS, reset, autoboot, PSCI, Android, pools |
| uart-final-all.log, uart-final-reboot-poweroff.log | production verification |
| uart-stability.log | 10-minute stability run |
| uart-display-touch*.log, uart-android-screen*.log, uart-hang-*.log | screen experiment and freeze |
| uart-vehicle-bench.log | automotive bench test |
| uart-rtds-panic.log, uart-rtds-repro-old.log, uart-rtds-repro-fixed.log | RTDS panic and fix |
| uart-flash-rtdsfix.log | final boot |
| bitbake-*.log | every build, exit codes checked |
| baseline/ | original image and U-Boot binaries |

### 18.3 Glossary

| Term | Meaning |
|---|---|
| Xen | type-1 hypervisor; runs below all operating systems |
| dom0 / Domain-0 | the privileged Linux VM that controls Xen and owns the hardware |
| domU / guest | an unprivileged VM (rtos1, android1) |
| HYP mode | ARMv7 hypervisor CPU mode (EL2); Xen must start in it, non-secure |
| PSCI | ARM firmware interface to start/stop CPUs and reset the system |
| SGRF / TZPC | RK3288 security controllers deciding what is secure-only |
| CRU / PMU | clock & reset unit / power management unit |
| cpupool | a group of physical CPUs with its own Xen scheduler |
| credit2 | Xen's default fair-share scheduler |
| RTDS | Xen's real-time (EDF, period/budget) scheduler |
| PV console | paravirtual console: a shared ring page + event channel |
| event channel | Xen's virtual interrupt between domains |
| xenconsoled / xenstored | dom0 daemons for guest consoles / the configuration store |
| vcan | Linux virtual CAN bus |
| listen-only | CAN mode in which the controller never transmits, not even ACK |
| ACC | accessory/ignition line of a car (12 V when the key is on) |
| WKS / wic | Yocto's SD-card image layout file / image creator |
| bbappend | Yocto file that modifies another layer's recipe |
| bmaptool | fast, verified image writer using a block map |
