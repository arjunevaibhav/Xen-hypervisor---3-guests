# Part 6 board test – 2026-10-27

Image: `tinker-xen-test-image-tinker-board.wic`, 1,571,959,808 bytes, sha256 `27cb7ba8c8d2b29f…`
(Step 37 sizes). Flashed by the user; tested over UART (`boot-1.log`, `check-1.log`, `boot-2.log`,
`check-2.log`). No Ethernet cable (eth0: no DHCP lease), no HDMI monitor, no USB displays.

| Check | Result |
|---|---|
| Boot to `tinker-board login:` (cold boot and `reboot`) | ✅ |
| `xl list` | ✅ Domain-0 320, rtos1 4, android1 511, media1 191 |
| `xl cpupool-list` | ✅ Pool-0 3 CPUs credit2 (3 domains), rt 1 CPU rtds (1 domain) |
| rtos1 RTDS after boot (`xl sched-rtds -d rtos1 -v all`) | ✅ period 1000, budget 1000, extratime yes (boot-2) |
| rtos1 10 ms task (Android booting) | ✅ min 9952 µs, max 10049 µs |
| rtos1 vehicle checks | ✅ 1000 frames/10 s, lost 0, bad 0; overspeed/redline alerts work |
| dom0 memory (`free -m`) | ✅ 282 MB total, 45 used, 228 available |
| android1 | ✅ `sys.boot_completed=1` < 4 min; MemAvailable 282 MB of 496; /system 92 % (587 MB, ro), /data 40 MB used of 545 |
| media1 | ✅ boots dom0's kernel; 166 MB total, 118 available (64 MB is CMA); root 94 MB, 58 MB free |
| media1 network | ✅ ping 192.168.100.4 from dom0 |
| media1 player | ✅ ffmpeg running, fb0 800x480, two frames 1 s apart differ (video playing) |
| media1 screen served by QEMU | ✅ 127.0.0.1:5901 listening |
| display-map | ✅ started all four screens (waiting: no displays connected) |
| Shutdown order on `reboot` | ✅ media1 → android1 → rtos1 |
| Temperature (idle → Android boot) | ✅ 45.5–46.4 °C |
| `xl dmesg` errors/panics | ✅ none (only "Unhandled SMC 0x8600ff01" and vGICD ICACTIVER0 notes from the Linux guests) |

Notes:
- `xl sched-rtds -d rtos1` (without `-v all`) prints the domain default 10000/4000; the per-vCPU
  values are the real ones.
- media1's kernel reserves 64 MB CMA (from dom0's kernel config) that it doesn't need: `cma=0`
  on its command line would give it that memory back.
- Android's /data used only 40 MB after the first boot; it could be made smaller.

Not tested (no equipment): HDMI picture, USB displays (udl), VNC from the PC (needs Ethernet),
guest internet.
