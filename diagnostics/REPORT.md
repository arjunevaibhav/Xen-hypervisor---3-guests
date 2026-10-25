# Tinker Board R2.0 (RK3288) – Yocto + Xen diagnostic report

Date: 2026-10-23. Host-side inspection and builds only. **No hardware test was
performed by me.** Everything about the board itself comes from the log
excerpt you supplied.

Hardware tests to run: see `BOARD-TESTS.md` in this directory.

---------------------------------------------------------------------------
## A. Baseline inventory (verified)

| Item | Value |
|---|---|
| Distro | Poky kirkstone (all layers on branch `kirkstone`, no uncommitted changes) |
| MACHINE | `tinker-board` (`meta-rockchip/conf/machine/tinker-board.conf`) |
| Kernel | linux-yocto 5.15.201 (`b75d71b7f2`), `KBUILD_DEFCONFIG=multi_v7_defconfig`, zImage |
| U-Boot (recipe) | 2022.01, `tinker-rk3288_defconfig` |
| Xen | 4.16 stable (`f265444922`), `xen_4.16.bb` from meta-virtualization |
| WKS | `meta-rockchip/wic/rockchip.wks` (7 GPT partitions, idbloader at sector 64, u-boot at sector 16384) |
| Disk | 65 GB free on / |
| local.conf | only addition vs backup: `DISTRO_FEATURES:append = " virtualization xen"` |
| bblayers.conf | added meta-filesystems, meta-python, meta-networking, meta-virtualization (by you); meta-tinker-xen (by me) |

Layer git heads: poky 445a622392, meta-arm c3e9fb12, meta-openembedded ce8539c941,
meta-rockchip 1e27f83, meta-virtualization 92b6abcd.

Known-good image (untouched, a byte-identical copy is in `baseline/`):
`build/tmp/deploy/images/tinker-board/core-image-minimal-tinker-board-20261022043621.rootfs.wic`
sha256 `d9cdb9a5127b7d0bbd1151fc6430331c84a9b525d49f6f90653af40092598702`.
Its bmap verifies (12 ranges, 0 mismatches).

---------------------------------------------------------------------------
## B. Linux "freeze" after `mmcblk0: p1 … p9`

### What the image on the host contains (verified)
- 7 partitions. Root = partition 7, PARTUUID `852763f9-8e3b-46af-9d70-09a659745700`.
  That matches extlinux.conf `root=PARTUUID=852763f9…`.
- Boot FAT: `/zImage`, `/rk3288-tinker.dtb`, `/extlinux/extlinux.conf`.
- Rootfs: `/sbin/init -> init.sysvinit`, `/etc/inittab` starts a getty on ttyS2 at 115200.
- Kernel config: MMC_DW_ROCKCHIP=y, MMC_BLOCK=y, EFI_PARTITION=y, EXT4_FS=y,
  SERIAL_8250_DW=y, SERIAL_8250_CONSOLE=y, DEVTMPFS_MOUNT=y. All needed drivers are built in.
- console order: `console=tty1 console=ttyS2,115200n8`. The last console listed
  becomes /dev/console, so init/getty output goes to serial. This is OK.
- Embedded bootloader: **U-Boot TPL/SPL 2022.01**.

### What your board log shows
- `mmcblk0: p1 p2 p3 p4 p5 p6 p7 p8 p9` → **9 partitions**.
- Earlier logs: **U-Boot TPL/SPL 2024.01**.

### Conclusion
The card that produced that log is **not a whole-disk copy of this Yocto WIC**
(wrong partition count, different bootloader version). Possible explanations:
another distribution's image is on the card; the WIC was written to a
partition (e.g. /dev/sdb1) instead of the whole disk (/dev/sdb); or the board
was booting from another medium. This cannot be decided without the evidence below.

Remaining plausible failure classes (none proven):
1. **Root device not found** (most consistent with the data). If extlinux names a
   PARTUUID that is not on that card, `rootwait` waits forever, silently, right
   after the partition scan. Look for "Waiting for root device" earlier in the full log.
2. Kernel/DTB mismatch from a different image on the card.
3. Console change (e.g. the other image sends its console only to HDMI/tty1).
4. Real hang or crash. A crash normally prints a trace; it would appear with
   `ignore_loglevel earlycon`.

Not plausible for the Yocto image itself: missing init, missing MMC/ext4
drivers, wrong PARTUUID *inside this WIC*. All of these were verified OK.

Evidence still needed (procedures in BOARD-TESTS.md §0, §1, §3):
- the complete serial log from the first TPL line (~/minicom.cap is empty, 0 bytes, owned by root);
- `sfdisk -d` and the U-Boot version strings read from the actual card.

No boot configuration was changed for this issue.

---------------------------------------------------------------------------
## C. Xen on RK3288: prerequisites

| Requirement | Status | Evidence |
|---|---|---|
| CPU has virtualization extensions | Expected yes (Cortex-A17 implements them), **not yet observed on board** | Xen checks ID_PFR1 at boot (`xen/arch/arm/arm32/head.S:279`) |
| Xen must be entered in **non-secure Hyp mode** | **BLOCKER – not met** | `head.S:288-297` fails with "Xen must be entered in NS Hyp mode" |
| U-Boot can switch to NS Hyp | **No, for RK3288 in any mainline version** | `ARMV7_NONSEC`/`ARMV7_VIRT` depend on hidden `CPU_V7_HAS_NONSEC`/`CPU_V7_HAS_VIRT`, selected only by LS1021A, sunxi, tegra124, exynos, mx7, stm32mp. Not RK3288 in 2022.01 (local), v2024.01 or master (checked upstream). |
| Can I just enable those options? | **No** | They are not user-visible. Even if forced, `virt-v7.c:138` calls `smp_set_core_boot_addr()`, which RK3288 does not implement → link failure. Needs a real U-Boot port: SMP boot address/pen for RK3288, secure-RAM placement, ideally PSCI. |
| SMP under Xen | CPU0 only | RK3288 DT uses `enable-method = "rockchip,rk3066-smp"`. Xen only supports PSCI or per-platform code (`xen/arch/arm/platform.c:98`). Xen 4.16 has no Rockchip platform. |
| GIC | OK | `arm,gic-400` with GICH/GICV regions present in rk3288.dtsi |
| Arch timer | OK with caveat | `arm,armv7-timer`, `clock-frequency = <24000000>`, `arm,cpu-registers-not-fw-configured` (firmware does not program CNTFRQ). U-Boot has COUNTER_FREQUENCY 24 MHz. |
| Xen console UART | OK | Xen ns16550 driver matches `snps,dw-apb-uart` (`drivers/char/ns16550.c:1707`); UART2 @ 0xff690000, reg-shift 2 |
| Xen image format | **Correct** | ARM32 Xen has a zImage header (`head.S:141`, magic 0x016f2818). `file` saying "zImage" is expected. Boot it with `bootz`. |
| Dom0 kernel | Builds with CONFIG_XEN=y, XEN_DOM0=y, HVC_XEN=y, blk/net backends =m | Not proven to work as Dom0 (cannot be until Xen runs) |
| CONFIG_ARM_LPAE | off | Not required by Linux Kconfig (`arch/arm/Kconfig:1590`). Xen docs suggest LPAE for 32-bit Dom0; revisit only after Hyp entry works. **Deliberately not changed.** |
| RAM / reservations | 2 GiB @ 0x0; `dma-unusable@fe000000` only matters on 4 GiB boards | rk3288-tinker.dtsi, rk3288.dtsi |

**Bottom line:** with the U-Boot built here (2022.01), and with any mainline U-Boot
including the 2024.01 seen on the board, Xen cannot start on RK3288. The
CPU is left in secure SVC mode. Fixing that means porting U-Boot NONSEC/HYP
(and preferably PSCI) to RK3288, or finding a secure monitor (e.g. OP-TEE) that
hands off in NS-Hyp. That is bootloader replacement work. Per your rules it is
**not done** until a recovery plan exists (you can always recover an SD-booted
Tinker by re-flashing the card, since the board has no SPI flash bootloader to
brick, but confirm the board has no eMMC that boots first).

Quick hardware confirmation (BOARD-TESTS.md §2): `dmesg | grep -i "started in"`.
"SVC mode" confirms the blocker; "HYP mode" would overturn it.

---------------------------------------------------------------------------
## D. Changes made (all in a new project-owned layer)

Layer: `~/yocto-rk3288/meta-tinker-xen` (priority 10), added with
`bitbake-layers add-layer`. bblayers.conf backup:
`build/conf/bblayers.conf.pre-meta-tinker-xen`.

1. `recipes-extended/xen/xen_4.16.bbappend` and
   `recipes-extended/xen/files/tinker-rk3288-early-printk.cfg`
   - Problem: the release Xen had no early console, so on this board every Xen
     failure (including the Hyp-mode blocker) was invisible.
   - Change: `CONFIG_DEBUG=y`, early printk via 8250 at 0xff690000, reg-shift 2.
     Applied only for MACHINE tinker-board (`SRC_URI:append:tinker-board`).
   - Check: `bitbake-layers show-appends` lists it; the resulting `.config` contains
     all 4 options; the binary contains the "NS Hyp mode" message.

2. `recipes-core/images/tinker-xen-test-image.bb`
   - A separate image (core-image-minimal + `/xen` on the boot partition), so the
     known-good core-image-minimal output is never overwritten. The default
     extlinux entry still boots Linux directly; Xen is only started by hand.

Nothing was changed in poky, meta-rockchip, meta-virtualization, U-Boot or kernel metadata.

### Important side effect you should know about (not caused by these changes)
Enabling `DISTRO_FEATURES xen` + meta-virtualization earlier today added kernel
fragments (`xen.scc`, `docker.scc`, `lxc.scc`, `vswitch.scc`, `ebtables.scc`, …)
to linux-yocto's SRC_URI. `bitbake-diffsigs` confirms this. The kernel was
reconfigured at 13:19 but only compiled during my image build. So:
- **old known-good WIC** → kernel built Oct 22 *without* these fragments (zImage 10,002,944 bytes);
- **new test image** → kernel *with* them (zImage 10,379,776 bytes).
If the new image's direct-Linux entry behaves differently from the old one, this kernel change is the first suspect.

---------------------------------------------------------------------------
## E. Build results (exit codes checked, artifacts exist)

| Command | Result | Log |
|---|---|---|
| `bitbake xen` | exit 0, 0 warnings / 0 errors | `diagnostics/bitbake-xen.log` |
| `bitbake tinker-xen-test-image` | exit 0, 0 warnings / 0 errors; 103 of 3746 tasks rerun (kernel + image) | `diagnostics/bitbake-tinker-xen-test-image.log` |

U-Boot was **not** rebuilt; there is no justified U-Boot change yet.

---------------------------------------------------------------------------
## F/G. New image

`build/tmp/deploy/images/tinker-board/tinker-xen-test-image-tinker-board-20261023095857.rootfs.wic`
- size 190,781,440 bytes (182 MiB); sha256 `e066744b9925b8a14822d79ce22ee07ce1381de5c05d41da402e646cfb799907`
- .bmap sha256 `81f7b574167d2f5733ffa0ac190027a83d72508056a0aa179e2a6bd5cbaa0563`
- boot (p6) PARTUUID `eeb23e71-d806-4f97-b0d4-543f88ef28db`: `/zImage`, `/rk3288-tinker.dtb`, `/xen`, `/extlinux/extlinux.conf`
- root (p7) PARTUUID `15ebdc28-677e-4a2d-83a8-93e63cb4754c`, which matches extlinux `root=PARTUUID=15ebdc28-…` ✔
- embedded U-Boot TPL/SPL 2022.01 (same as before)
- `/xen` inside the image is identical to the deployed `xen-tinker-board` (sha256 e823dd23…)
- other checksums: `diagnostics/new-image-SHA256SUMS`

Xen boot status: **EXPERIMENTAL / expected to fail** with "Xen must be entered in
NS Hyp mode" until the bootloader blocker (section C) is solved. The manual
U-Boot commands are in BOARD-TESTS.md §4. Android/RTOS guests are out of scope
until Dom0 boots.

### Back up the current SD card first (host)
    lsblk -o NAME,SIZE,MODEL,TRAN          # identify the card, e.g. /dev/sdb (~29.7G)
    sudo umount /dev/sdX?* 2>/dev/null
    sudo dd if=/dev/sdX of=~/tinker-sd-backup-$(date +%F).img bs=4M status=progress conv=fsync
    sha256sum ~/tinker-sd-backup-*.img > ~/tinker-sd-backup.sha256

(Needs ~30 GB free. You have ~65 GB.)

### Flash the test image (double-check /dev/sdX; this ERASES the card)
    cd ~/yocto-rk3288/build/tmp/deploy/images/tinker-board
    sudo bmaptool copy tinker-xen-test-image-tinker-board-20261023095857.rootfs.wic /dev/sdX
    sync

Write to the whole device (/dev/sdb), never a partition (/dev/sdb1).

### Restore
Known-good Yocto image:

    cd ~/yocto-rk3288/diagnostics/baseline
    sudo bmaptool copy core-image-minimal-tinker-board-20261022043621.rootfs.wic /dev/sdX

The card exactly as it was before (whatever is on it now):

    sudo dd if=~/tinker-sd-backup-YYYY-MM-DD.img of=/dev/sdX bs=4M status=progress conv=fsync

---------------------------------------------------------------------------
## Next steps (in order)
1. Send the full serial log and the card inspection (BOARD-TESTS §0, §1).
2. Flash the test image. Confirm the direct-Linux entry reaches a login on ttyS2.
   Run BOARD-TESTS §2 ("started in SVC/HYP mode").
3. Optional: BOARD-TESTS §4 to see Xen's Hyp-mode error on real hardware.
4. Decide on the bootloader route (U-Boot RK3288 NONSEC/HYP port, possibly with
   PSCI). Only after that: Dom0 tuning (LPAE question, dom0_mem, generated
   boot.scr or extlinux Xen entry).

---------------------------------------------------------------------------
## H. Hardware results (2026-10-23, tinker-xen-test-image flashed to the SD card)

Note: the card previously held a different, non-kirkstone build (Poky 5.0.21
scarthgap, kernel 6.6.151, Xen 4.18.1-pre, boot.scr saying U-Boot was patched
"0001-0005" for NS-HYP, U-Boot TPL/SPL 2024.01, 9 partitions). That was the
source of the earlier "freeze" log. Its boot.scr documented a Dom0 hang ~2 s
into headless boots. The card was erased without a backup, at the user's request.

Serial capture: `diagnostics/uart-boot.log` (24151 bytes)
- U-Boot TPL/SPL/proper 2022.01 → extlinux → Linux 5.15.201 → `tinker-board login:` ✔
- `root=PARTUUID=15ebdc28-…` matched; `Run /sbin/init` at 3.99 s ✔
- 4 CPUs brought up by Linux ✔
- **`CPU: All CPU(s) started in SVC mode.`** → confirms the Xen blocker on real hardware.
- PC→board direction: no echo to Enter/"root" (also autoboot was not interrupted).
  Board→PC works. → wiring/adapter TX problem (adapter TX → board pin 10).
- UART wiring that works: adapter RX → pin 32 (UART2 TX, GPIO7_C7), adapter TX → pin 33
  (UART2 RX, GPIO7_C6), GND → pin 34. Pins 8/10 are UART1, NOT the console.
  Login over UART confirmed (root@tinker-board, 4 CPUs, 2005 MB).
- U-Boot: SD card is `mmc 1` (not mmc 0). U-Boot reserves 0x79f801f8-0x7fffffff.
- **Xen attempt on hardware** (RAM-only, `diagnostics/uart-xen-attempt.log`): xen, dtb and
  zImage loaded from mmc 1:6, /chosen module node set, `bootz`. Xen early printk output:
  `- Xen must be entered in NS Hyp mode -` / `- Please update the bootloader -` / `- Boot failed -`.
  → **Blocker confirmed on real hardware.** Next work item: U-Boot NONSEC/HYP port for RK3288.

---------------------------------------------------------------------------
## I. U-Boot HYP port (step 1)

Patch: `meta-tinker-xen/recipes-bsp/u-boot/files/0001-rockchip-rk3288-allow-booting-in-non-secure-HYP-mode.patch`
(applied by `u-boot_2022.01.bbappend`, tinker-board only)
- Kconfig: ROCKCHIP_RK3288 selects CPU_V7_HAS_NONSEC + CPU_V7_HAS_VIRT → ARMV7_NONSEC=y, ARMV7_VIRT=y
- rk3288_common.h: CONFIG_ARM_GIC_BASE_ADDRESS 0xffc00000 (GICD 0xffc01000, GICC 0xffc02000)
- rk3288.c: armv7_boot_nonsec_default() = false (secure stays default), empty
  smp_set_core_boot_addr/smp_kick_all_cpus/smp_waitloop (secondaries stay in boot ROM)
- Verified in the ELF: GIC literal 0xffc00000, CNTFRQ 0x016e3600 (24 MHz), MVBAR set, secure default returns 0
- `bitbake u-boot`: exit 0, no warnings. Original binaries: `baseline/*.2022.01-orig`

Boot script: `meta-tinker-xen/recipes-bsp/xen-boot-script` → `/xen-boot.scr` on the boot partition
(sets bootm_boot_mode=nonsec only for that boot; root=PARTLABEL=root; dom0_mem=512M, 1 vCPU)

Image: `tinker-xen-test-image-tinker-board-20261023111057.rootfs.wic`
sha256 0bc653ca00b2d72a3f24fd8b7ea4b2cf16e34f603c97837737198bde7d89efb3,
root PARTUUID ad2f7b45-02dd-483a-a425-d6a32a1e22cc (matches extlinux), loader2 == patched u-boot.bin.

Known limitation: only CPU0 runs in HYP mode. Plain Linux in nonsec mode would need `nosmp`
(the secondaries would start in secure mode). Linux SMP + Xen SMP need PSCI (step 2).

---------------------------------------------------------------------------
## J. Xen + Linux dom0 boots on the board (2026-10-23)

Result: `/sys/hypervisor/type` = xen, dom0 login on hvc0, 483 MB, 1 vCPU.
Log: `diagnostics/uart-xen-card.log`. Image: `tinker-xen-test-image-tinker-board-20261023155925.rootfs.wic`
(sha256 7e95706505e9818ab24ae76933c85d217b43709b487b1f70459bf08ee2bbaebb).

Start Xen (RAM-only, from the U-Boot prompt):
    load mmc 1:6 ${scriptaddr} /xen-boot.scr; source ${scriptaddr}
Normal autoboot still boots plain Linux in secure mode (unchanged).

Problems found and fixed, in the order they appeared (all in meta-tinker-xen):
| Symptom | Cause (evidence) | Fix |
|---|---|---|
| Xen: "must be entered in NS Hyp mode" | RK3288 U-Boot had no NONSEC/VIRT | U-Boot 0001: select NONSEC/VIRT, SMP hooks, GIC base |
| Silent hang after `Starting kernel` | HSCTLR reset value 0x70cd087f (MMU + Thumb exceptions on); debug markers stopped at "E" | U-Boot 0001: write HSCTLR=0x30c50830 before ERET |
| (needed for NS) | SoC still secure-only | U-Boot 0001: psci_board_init() SGRF/TZPC setup from TF-A rk3288 |
| Xen: "Missing processor info: 410fc0d1" | no Cortex-A12/A17 proc_info (also missing upstream) | Xen 0001 |
| dom0 Oops in rk3288_suspend_init | L2CTLR trap not emulated | Xen 0002: emulate L2CTLR (like KVM) |
| dom0 abort at 0xffb23fe0 | secure bus-DMAC window used from NS | xen-boot.scr: point dmac_bus_s at 0xff600000 |
| dom0 hang ~2 s (the original "freeze") | dom0 reset timer7 → arch counter reset → Xen NOW negative, timers never fire (Xen dump: vCPU in idle, NOW=-6148914817469028622) | kernel 0001: keep timer7 if already running |

Also kept: `fdt rsvmem` of U-Boot's area (harmless; turned out not to be the cause).

Remaining limitations / next steps:
- Xen uses only CPU0 (no PSCI for RK3288). Next: PSCI in U-Boot for SMP.
- No xen-tools (`xl`) in the image yet, so no guest domains can be created.
- Xen is a debug build with early printk (board-specific).
- Upstream candidates: Xen 0001/0002, kernel 0001.

---------------------------------------------------------------------------
## K. First guest (domU) runs next to dom0 (2026-10-24)

Image: `tinker-xen-test-image-tinker-board-20261023181955.rootfs.wic`
(sha256 2762f72a5a20ccaf641955235e9006010fb86623b6e48840543ba612cb42b294). Log: `diagnostics/uart-xen-guest.log`.

Added in meta-tinker-xen:
- `recipes-core/images/tinker-xen-guest-initramfs.bb`: BusyBox-only cpio.gz for the guest
- `recipes-extended/xen-guest-demo`: /etc/xen/guest1.cfg + /var/lib/xen/guest1/{zImage,initramfs.cpio.gz}
- test image: xen-tools-xencommons (xl, xenstored, xenconsoled, S80xencommons), Xen backend modules,
  xen-guest-demo, IMAGE_ROOTFS_EXTRA_SPACE=64 MB (root partition now 139 MB, ~100 MB free)

On the board (dom0):
    xl info      → xen_version 4.16.1, total_memory 2048, free_memory 1357, nr_cpus 1
    xl create /etc/xen/guest1.cfg
    xl list      → Domain-0 512 MB, guest1 127 MB, 1 vCPU each
    xl console guest1   (leave with Ctrl-])  → guest: "Machine model: XENVM-4.16",
                                               /sys/hypervisor/type = xen, MemTotal 105164 kB
    xl destroy guest1

Notes:
- First attempt: the board reset (U-Boot "Reset cause: POR") at the moment `xl create` was typed,
  before the shell even echoed it. Repeat run had no reset. Cause not established; POR suggests a
  power dip. Use a solid 5 V / 3 A supply. If it recurs, capture it and investigate.
- libxl prints "qemu-xen is unavailable": harmless for this guest (no emulated devices).
- QEMU is built (xen-tools dependency) but not installed in the image.
- Guest has no disk and no network yet (CONFIG_BRIDGE off in dom0).

Next steps: networking (bridge + vif), persistent guest disk, an RTOS guest (e.g. Zephyr xenvm),
SMP via PSCI, release (non-debug) Xen, a boot menu entry for Xen.

---------------------------------------------------------------------------
## L. Guest networking (host-only bridge) works (2026-10-24)

Image: `tinker-xen-test-image-tinker-board-20261024055832.rootfs.wic`
(sha256 d3607e1778be597b379da27dec87f61f094cc2ca1dc582dc83ad2452d19f26ed). Log: `diagnostics/uart-xen-net.log`.

Changes in meta-tinker-xen:
- `recipes-kernel/linux/files/xen-dom0-bridge.cfg`: CONFIG_BRIDGE=y (STP/LLC selected)
- `recipes-extended/xenbr0`: init script S79xenbr0 → bridge xenbr0 = 192.168.100.1/24 (eth0 not bridged)
- guest1.cfg: `vif = [ 'bridge=xenbr0' ]`
- image: + xen-tools-scripts-network (vif-bridge, pulls bridge-utils)

Result on the board: vif1.0 attached to xenbr0; guest eth0 192.168.100.2/24;
guest → dom0 ping 4/4 (~0.67 ms), dom0 → guest ping 4/4 (~0.66 ms).

Harmless messages seen: "qemu-xen is unavailable", "gnttab_mark_dirty not implemented yet",
"INIT: Id "S2" respawning too fast" (getty on ttyS2, which belongs to Xen; dom0's console is hvc0).

---------------------------------------------------------------------------
## M. FreeRTOS guest (RTOS) runs next to dom0 and a Linux guest (2026-10-24)

Zephyr's Xen board (xenvm) is ARMv8-A/AArch64 only, so the RTOS is FreeRTOS-Kernel
V11.1.0 (commit dbf70559, MIT), ARM_CA9 GCC port, unmodified.
Recipe: `meta-tinker-xen/recipes-rtos/freertos-xen-guest` (startup.S, guest.ld, main.c,
FreeRTOSConfig.h, Makefile, rtos1.cfg). Installed as /var/lib/xen/rtos1/freertos-xen-guest.bin
+ /etc/xen/rtos1.cfg (16 MB, 1 vCPU, no devices).

Guest design:
- ARM32 zImage header (magic 0x016f2818, entry 0x40008000 = guest RAM base + 0x8000)
- Xen virtual GICv2 (GICD 0x03001000, GICC 0x03002000), 32 priority levels
- 1 kHz tick from the virtual timer (PPI 27), absolute CNTV_CVAL
- identity-mapped MMU + caches (RAM normal WBWA, rest device), WFI idle hook
- console: CONSOLEIO_write hypercall (needs debug Xen: CONFIG_VERBOSE_DEBUG); output on the
  Xen console as "(dN) [rtos] ..."
- configASSERT off: the port's GIC priority-bit probe reads 0xFF on Xen's vGIC (Xen stores
  all 8 bits, vgic-v2.c), which would fail on a correct setup

Bugs found on the board and fixed:
| Symptom | Cause | Fix |
|---|---|---|
| repeated/stale console lines | guest MMU off → uncached writes, Xen reads cached | enable MMU + caches |
| period 10.15 ms instead of 10 ms | relative TVAL re-arm adds IRQ latency every tick | absolute CVAL += period |
| rtos1 at 100 % CPU, 20 ms spikes | idle task spinning | idle hook WFI |

Result (log `diagnostics/uart-xen-rtos2.log`): 10 ms task 9.99–10.01 ms (±8 µs) with dom0
idle; rtos1 ≈3.5 % CPU; `xl list` showed Domain-0, rtos1 and guest1 together. While the Linux
guest booted, the RTOS was delayed up to ~19 ms (one shared pCPU, credit2 scheduler), then
returned to ±8 µs. Debug Xen also has RTDS/null/ARINC653 schedulers built in (next step).

Image with the fixed guest: `tinker-xen-test-image-tinker-board-20261024072751.rootfs.wic`
(sha256 72ef08b5751f21c94f624a4bd26d1145df4afd714721011e2b705d70a39cee3a, root PARTUUID
2b42c95f-0bbc-4a02-8683-cba5857d98f4). Not flashed yet; the tested binary (sha256 f0adea2a…)
was sent to dom0 /tmp over the UART.

---------------------------------------------------------------------------
## N. RTDS real-time scheduler for the RTOS guest (2026-10-24)

Xen boot argument `sched=rtds` (debug Xen has CONFIG_SCHED_RTDS=y; "experimental" in 4.16).
RTDS = EDF with per-vCPU period/budget; defaults 10 ms / 4 ms with extratime.
rtos1 gets 200 us every 1 ms: `xl sched-rtds -d rtos1 -v all -p 1000 -b 200 -e 1`
(xl needs -p, -b and -e given the same number of times; xl.cfg in 4.16 has no RTDS budget key).
Logs: `diagnostics/uart-xen-rtds.log` (defaults), `diagnostics/uart-xen-rtds2.log` (200 us/1 ms).

Worst 10 ms period per 100-period report (target 10000 us):
| Situation | credit2 | RTDS defaults | RTDS 200 us / 1 ms |
|---|---|---|---|
| idle | ±8 us | ±8 us | ±10–20 us |
| Linux guest1 being created/booting | up to 19.9 ms (≈5 s) | up to 18.6 ms | 12.4 ms once, else ≤ ±115 us |
| dom0 100 % busy (md5sum /dev/zero) | not measured | ±7 us | ±4–7 us |
The remaining 2.4 ms spike coincides with `xl create` building guest1 (long, non-preemptible
hypercalls on the only pCPU).

Project changes:
- xen-boot.cmd: `sched=rtds` in xen,xen-bootargs
- freertos-xen-guest: /usr/sbin/rtos1-start (xl create + RTDS reservation)

Image: `tinker-xen-test-image-tinker-board-20261024074358.rootfs.wic`
(sha256 4654864b1eb24b0ae8e664ac3838ab20abebb7fd07792fa9daa7aa07a568c3a9, root PARTUUID
c201de0c-f4ed-41b7-b98d-5978b864c614). Not flashed yet (tests used manual U-Boot commands).

New finding: `reboot` in dom0 under Xen hangs with "Xen: Platform reset did not work properly!"
(no PSCI, no RK3288 platform code in Xen 4.16) → power-cycle needed. Fix idea: Xen platform
reset via the RK3288 CRU global soft reset register.
Also: the CP210x adapter re-enumerates on the PC when the board is power-cycled.

## O. `reboot` under Xen works (2026-10-24)

Cause: Xen 4.16 has no RK3288 platform code and the board has no PSCI, so
`machine_restart()` looped on "Platform reset did not work properly!".
Fix: `meta-tinker-xen/recipes-extended/xen/files/0003-xen-arm-add-Rockchip-RK3288-platform-with-reset.patch`
(new `xen/arch/arm/platforms/rockchip.c`): `.reset` writes 0xfdb9 to CRU GLB_SRST_FST
(CRU + 0x1b0), the same register Linux's rockchip restart handler uses.
The first version only matched `rockchip,rk3288-cru` and printed
"rockchip.c:39: Cannot find the RK3288 CRU node in DT": on the RK3288W, U-Boot
(`ft_rk3288w_setup`) renames the CRU node to `rockchip,rk3288w-cru`. The patch now matches both.

Hardware test (`diagnostics/uart-xen-reset.log`, Xen sent with loady, sched=rtds):
- `(XEN) Platform: Rockchip RK3288`
- dom0 `reboot` → "reboot: Restarting system" → U-Boot TPL with `Reset cause: RST`
  (the cold boot before showed `Reset cause: POR`): a real soft reset, no power-cycle.

Image with the fix: `tinker-xen-test-image-tinker-board-20261024124409.rootfs.wic`
(sha256 a8889f49e4e5b78c67dee144c9c00bc6418f2f5539e1561ae2769c9d606a1f88). Its boot
partition's `xen` is byte-identical to the Xen tested above. Not flashed yet.

Card test after flashing this image (`diagnostics/uart-xen-full-card.log`): Xen started from the
card with xen-boot.scr; dom0 (Xen 4.16.1, scheduler rtds, 1 pCPU); guest1 ping 4/4 both ways;
rtos1 at 200 us/1 ms, 10 ms task 9990–10008 us; three domains at once; dom0 `reboot` →
`Reset cause: RST`. Board left at the U-Boot prompt.

## P. The card starts Xen automatically at power-on (2026-10-24)

U-Boot's default `bootcmd` (`distro_bootcmd`) scans the bootable partition (mmc 1:6) for
`/extlinux/extlinux.conf` first, then `/boot.scr`. Nothing is saved in the U-Boot environment
(no saveenv) and U-Boot itself is unchanged.
- `meta-tinker-xen/wic/tinker-xen.wks`: rockchip.wks without `loader=u-boot`, so wic no longer
  generates /extlinux/extlinux.conf (WKS_FILE in tinker-xen-test-image.bb).
- The boot partition gets `/boot.scr` (= xen-boot.scr) and `/native/extlinux.conf` (Linux
  without Xen, root=PARTLABEL=root).
- xen-boot.cmd: if `bootz` returns (Xen missing or failed), it boots native Linux via
  `sysboot mmc 1:6 any ${scriptaddr} /native/extlinux.conf`.
Recovery: press a key during "Hit any key to stop autoboot", then either the sysboot line above
(Linux without Xen) or `load mmc 1:6 ${scriptaddr} /xen-boot.scr; source ${scriptaddr}` (Xen).
Image: `tinker-xen-test-image-tinker-board-20261024125534.rootfs.wic`
(sha256 0e7ead1044c7f936f3917fde55e1fa7fdd1b25a84e53dc659d753e517dbd41a6); /xen identical to
the Xen tested in section O.
Flashed and tested (`diagnostics/uart-xen-autoboot.log`), no key pressed except for step 3:
1. power-on → "Found U-Boot script /boot.scr" → Xen → dom0 login (`/sys/hypervisor/type` = xen)
2. dom0 `reboot` → `Reset cause: RST` → boot.scr → Xen → dom0 login
3. stop autoboot, `sysboot mmc 1:6 any ${scriptaddr} /native/extlinux.conf` → Linux 5.15 without
   Xen (no /sys/hypervisor/type, root=PARTLABEL=root) → `reboot` → Xen again by itself

## Q. SMP: Xen runs on all 4 CPUs through PSCI (2026-10-24, tested from RAM)

Before: no PSCI firmware → Xen "Failed to bring up CPU 1/2/3 (error -19)", 1 pCPU.
Fix: `meta-tinker-xen/recipes-bsp/u-boot/files/0002-rockchip-rk3288-add-PSCI-to-start-the-secondary-CPUs.patch`
- RK3288 selects ARCH_SUPPORT_PSCI → U-Boot's ARMv7 PSCI monitor (PSCI 1.0, SMC), which stays
  resident in U-Boot's reserved RAM; U-Boot adds /psci + enable-method="psci" for nonsec boots only.
- `arch/arm/mach-rockchip/rk3288/psci.c`: VERSION, FEATURES, CPU_ON, CPU_OFF, AFFINITY_INFO,
  MIGRATE_INFO_TYPE, SYSTEM_RESET. CPU_ON = core reset + PMU power-up + boot ROM mailbox
  (SRAM 0xff700000: +8 entry, +4 0xdeadbeaf, sev), as Linux platsmp.c / TF-A rk3288 pmu.c.
  The new core runs psci_cpu_entry → _nonsec_init (GIC, NSACR, CNTFRQ, HSCTLR) → HYP → Xen.
- psci_board_init() powers off cores 1-3 first. Run 1 without it: all parked cores followed the
  first mailbox release, so CPU2/3 returned ALREADY_ON (-4) and only 2 CPUs came up
  (`diagnostics/uart-xen-psci-run1.log`).
- Secure (plain Linux) boot is unchanged: no /psci node, Linux uses rockchip,rk3066-smp.
- The section is in U-Boot's secure text (checked with objdump: no calls outside it).

Test (`diagnostics/uart-xen-psci.log`): U-Boot sent with loady to 0x01000000 and started with
go (nothing written to the card), autoboot → boot.scr → Xen:
- `(XEN) Using PSCI v1.0`, `Bringing up CPU1/2/3`, `Brought up 4 CPUs`; `xl info` nr_cpus 4
- dom0 on pCPU0, guest1 on pCPU1, rtos1 on pCPU2 (xl vcpu-list); rtos1 10 ms task 9998–10002 us
  (one report 9972–10037 us during guest activity)
- dom0 `reboot` → Xen PSCI SYSTEM_RESET → `Reset cause: RST`
Image: `tinker-xen-test-image-tinker-board-20261024132400.rootfs.wic`
(sha256 716468e1d52e3daf655beb54dbc5707759750e09f74cf6894b43224140f78517); loader2 = the U-Boot
tested above. TPL/SPL contain no PSCI/nonsec code. Not flashed yet.
Recovery if the card does not boot: reflash it (e.g. `diagnostics/baseline/core-image-minimal-*.wic`).

## R. Headless Android 7.0 as a Xen guest (2026-10-24, built, not yet tested on the board)

Why Android 7.0: the newest ARM32 AOSP images Google publishes (SDK system images) are API 24/25.
They use the old 32-bit binder ABI (CONFIG_ANDROID_BINDER_IPC_32BIT, also set in the SDK's own
kernel-ranchu), removed from Linux in 4.18 → dom0's 5.15 cannot run them. Building AOSP from
source (~250 GB) does not fit the 54 GB free on the build PC.
- `recipes-android/linux-android-guest` (4.14.336 LTS, multi_v7_defconfig + android-guest.cfg):
  Xen front-ends, binder 32-bit, ashmem, SELinux, cgroups, netfilter, vfb (RAM framebuffer).
- `recipes-android/android-guest`: AOSP `armeabi-v7a-24_r07.zip` (sdk_phone_armv7-userdebug
  7.0 NYC 3245079, sha256 42690977…c91d). Ramdisk = SDK ramdisk + overlay cpio (init.xen.rc,
  fstab.xen, default.prop + ro.sf.lcd_density). system.img: emulator GLES libs removed,
  libGLES_android.so renamed libGLES_soft.so (outside the emulator the EGL loader skips it).
  /etc/xen/android1.cfg: 768 MB, 2 vCPUs, xvda/xvdb = SD partitions android-system/android-data,
  vif on xenbr0, cmdline androidboot.hardware=xen, selinux=permissive, video=vfb:640x480-16.
- Guest IP 192.168.100.3, adbd on TCP 5555; dom0 has adb (meta-oe android-tools).
- License: the SDK image is under the Android SDK License Agreement → LICENSE = "CLOSED",
  development use only.
Image: `tinker-xen-test-image-tinker-board-20261024134726.rootfs.wic` (2.8 GB,
sha256 34f4e3f80b4eb5745b2a78b281e582661782b64eb6a84012e77adcd39a8308ad), also contains the
PSCI U-Boot of section Q. New partitions p8 android-system (1.8 GB), p9 android-data (550 MB).

## S. Example Linux guest "guest1" removed (2026-10-24)

On request: recipes `xen-guest-demo` (guest1.cfg, dom0 zImage copy) and
`tinker-xen-guest-initramfs` (BusyBox initramfs) deleted from meta-tinker-xen and removed from
the image. Nothing else used them; xenbr0 stays for Android (192.168.100.3).
Image: `tinker-xen-test-image-tinker-board-20261024140152.rootfs.wic`
(sha256 80e38cf8f7dc3049356269f2632e17ed84a0914241ad636a25fd62a4f4191824); otherwise as in R.

## T. Android guest, first board runs (2026-10-24)

Run 1 (`diagnostics/uart-xen-android.log`, card image 20261024140152): Xen 4 CPUs, guest kernel
4.14 on 2 vCPUs, blkfront xvda/xvdb, netfront, vfb fb0, Android init, servicemanager,
surfaceflinger, adbd up; eth0 192.168.100.3 (dom0 ping 3/3). system_server crash loop (~12 s):
`F EventHub: Could not register INotify for /dev/input` (no input devices → no /dev/input).
Fix: init.xen.rc `mkdir /dev/input` (ramdisk replaced live in dom0, md5 checked).
Run 2 (`uart-xen-android2.log`): adb from dom0 works (`connected to 192.168.100.3:5555`).
system_server still loops: NullPointerException in AlarmManagerService$AlarmHandler
(PendingIntent.send) – the "Failed to open alarm driver. Falling back to a handler" path.
Cause (AOSP 7.0 init_timerfd): no /dev/alarm in 4.14, and system_server lacks CAP_WAKE_ALARM
(added in Android 8) → timerfd_create(CLOCK_*_ALARM) = -EPERM; as root all clocks work
(tiny static test binary over adb).
Fix: guest kernel patch `0001-timerfd-allow-alarm-clocks-for-the-Android-system-group.patch`
(also accept gid 1000 for alarm clocks).
Image: `tinker-xen-test-image-tinker-board-20261024143227.rootfs.wic`
(sha256 172618c993e40d96d901353d05d1ecdc03cb5c1fad4c886ca05a1c12e84f92d6).
Note: dom0's adb server (android-tools 5.1.1) resets a USB device ("usb 1-1: reset") while it
runs; stop it with `adb kill-server` after use.

## U. Headless Android boots completely; RTOS on its own CPU (2026-10-24)

Run 3 (card 20261024143227, sched=rtds on all 4 CPUs): Xen panic while creating android1:
`Assertion '!unit_on_replq(svc)' failed at rt.c:587` (replq_insert ← rt_unit_wake ← vcpu_wake ←
domain_unpause). RTDS is experimental in 4.16; it had only ever run on 1 pCPU before.
New layout: `sched=credit2` for Pool-0 (CPUs 0-2: dom0, Android); cpupool `rt` = CPU 3 with
RTDS only for rtos1 (`rtos1-start` creates it, rtos1.cfg `pool = "rt"`).
Run 4 (`diagnostics/uart-xen-pools.log`; Xen started by hand with sched=credit2, pools by hand):
- Pool-0 credit2 3 CPUs (dom0 + android1), rt rtds 1 CPU (rtos1, 1000 us / 200 us, extratime)
- Android: `SurfaceFlinger: Boot is finished (100948 ms)`, `sys.boot_completed` = 1,
  resumed activity com.android.launcher3/.Launcher, 77 packages, 0 system_server crashes,
  bootanim exited 0; adb from dom0 works
- rtos1 10 ms task: ±2 us with Android idle; 9944–10057 us (≤ ±57 us) over 414 reports while
  Android booted (shared L2/memory, not scheduling)
Image with these changes built in: `tinker-xen-test-image-tinker-board-20261024145333.rootfs.wic`
(sha256 141d6f5420737158fc3314d3e3a6349bfffab5d4ce25a94bb67a388e0ff49777). Not flashed yet.

## V. Quieter FreeRTOS console output (2026-10-24, built, not yet run on the board)

On request: the rtos1 output (three lines per second) made the shared Xen console unusable
for typing. main.c now prints one line every 10 s (REPORT_PERIODS = 1000 periods):
`[rtos] 10 ms task: 1000 periods, min X us, max Y us; heartbeats N, queue messages M (report R)`.
The heartbeat and producer/consumer tasks still run, they only count.
Live workaround on an older card: `xl debug-keys G` then `xl debug-keys -` twice (guest log
rate-limited; `xl debug-keys +` twice restores it). `xl set-parameters guest_loglvl=...`
fails on this arm32 build ("Bad address").
Image: `tinker-xen-test-image-tinker-board-20261024151313.rootfs.wic`
(sha256 d27e5aaf4d17e9113e1e5837b991a19d72c81d35b2e16dbfab64f32335e2acbe), otherwise as in U.

## W. Remaining items built (2026-10-24)

Tested live on the board first (nothing written to the card), then all built into one image:
- Release Xen (`files/tinker-rk3288-release.cfg`: DEBUG off, EXPERT + UNSUPPORTED + SCHED_RTDS on;
  XEN_TINKER_DEBUG = "1" selects the old debug fragment). Loaded with loady: `debug=n`,
  PSCI v1.0, 4 CPUs, credit2 (`diagnostics/uart-xen-release.log`).
- Thermal: dom0 has no physical CPU nodes, so cpu-thermal's cooling maps made the TSADC probe
  defer forever (no readings, no hardware over-temperature shutdown). xen-boot.cmd now does
  `fdt rm /thermal-zones/cpu-thermal/cooling-maps`: cpu-thermal 41.8 °C, gpu 40.0 °C, enabled.
- FreeRTOS PV console (HVMOP_get_param CONSOLE_PFN/EVTCHN, ring page mapped normal memory,
  EVTCHNOP_send; CONSOLEIO fallback): UART 0 [rtos] lines; `xl console rtos1` shows one line
  per 10 s, 9997–10002 us (`diagnostics/uart-rtos-pvconsole.log`).
- adb USB resets: the onboard GL850G hub (05e3:0608) fails runtime resume when adb's usbfs scan
  wakes it. `echo on > .../1-1/power/control` → 0 resets; now a udev rule.
- Android network: default route via 192.168.100.1 works for root and app uids (main table; the
  guest kernel has no policy routing, netd's addInterface fails). DNS: netd network 60000 as
  resolver context (`/xen-net.sh`). Dom0: ip_forward + MASQUERADE in xenbr0 (iptables, nf_nat).
- Hardware watchdog (dw_wdt): opening /dev/watchdog and stopping → board reset after 43 s
  (`diagnostics/uart-watchdog-test.log`). hw-watchdog (S05) feeds it every 5 s.
- Guest autostart: /etc/init.d/xen-guests (S90/K10) with /etc/default/xen-guests.
- start_getty: no getty on ttyS2 under Xen (fixes "INIT: Id "S2" respawning too fast").
- README with build/flash/usage/limitations/recovery plan in meta-tinker-xen.
CPU clock under Xen: armclk 500 MHz (APLL 1 GHz, U-Boot's setting; no cpufreq in Xen 4.16).
Image: `tinker-xen-test-image-tinker-board-20261024163357.rootfs.wic`
(sha256 d0cdb27c3ea8f3a90cdfcaefb7ed376f3af2029c5ea008da9b084484cb502010).

## X. Final verification (2026-10-25)

Card flashed with the W image. `diagnostics/uart-final-all.log`: PASS autoboot, guest
autostart, rtos1 + android1 running, rt pool CPU 3 1000/200 us, temperatures 35.0/34.1 °C,
watchdog keep-alive, NAT rule + ip_forward, UART quiet (0 [rtos] lines, no getty respawn),
FreeRTOS PV console (9957–10034 us while Android boots), 0 adb USB resets. "release Xen" and
"4 CPUs" were reported FAIL only because the test joined after Xen's first lines; `xl dmesg`:
`debug=n`, `Brought up 4 CPUs`.
Real failure: Android had booted (sys.boot_completed=1) but had no network: Android 7 init
refused the new shell-script service ("Service xen-net does not have a SELinux domain
defined"). Fix: `seclabel u:r:su:s0` for xen-net in init.xen.rc (ramdisk md5 b9d5c8c4…),
installed live in dom0 with md5 check; then via `/etc/init.d/xen-guests stop/start` Android
came up with 192.168.100.3, default route via dom0, launcher, adb.
`diagnostics/uart-final-reboot-poweroff.log` (all PASS): reboot → guests stopped first,
Reset cause RST, boot.scr, release Xen with 4 CPUs, guests autostarted, no getty respawn,
Android boot_completed over the network with no manual step (76 s after dom0 login),
launcher, FreeRTOS 9961–10038 us; poweroff → guests stopped, "reboot: Power down".
Not verified: guest internet through NAT and DNS (no Ethernet cable).
Final image: `tinker-xen-test-image-tinker-board-20261025014834.rootfs.wic`
(sha256 25b48f7c561a7774d97a21b901f310d1eff4bd73293cf8349f454918be503aef). It differs from
the flashed W image only by the Android ramdisk, which is already installed on the card.

## Y. Screen, touch and sound (2026-10-25)

Built (meta-tinker-xen): QEMU as Xen device model (qemu_%.bbappend: i386 target, xen only),
android-hdmi (VNC client: guest PV screen on dom0 /dev/fb0 + evdev touch/mouse/keys, -t X,Y
tap), Rockchip DRM/HDMI + ALSA/snd-usb-audio + alsa-utils in dom0, vfb module for testing,
guest kernel patches 0002 (xen-kbdfront pointer as touchscreen: BTN_TOUCH + INPUT_PROP_DIRECT)
and 0003 (xen-fbfront 16 bpp RGB565 mode, xen_fbfront.depth=16).
Verified on the board (uart-display-touch2.log, uart-android-screen*.log):
- dom0 sound: onboard USB codec = ALSA card 0. HDMI driver loads; no monitor connected, so no
  real HDMI picture was seen (dom0 /dev/fb0 appears only with a monitor).
- QEMU serves the guest vfb/vkbd on 127.0.0.1:5900; Android fb0 = xen 800x600 16 bpp;
  "Xen Virtual Pointer" = Classes 0x4, DeviceType touchScreen; android-hdmi on a 1024x768 RAM
  fb drew Android's picture (144065 non-zero bytes); a tap reached Android (BTN_TOUCH, ABS_X/Y).
Problems: 32 bpp xenfb → SurfaceFlinger "no suitable EGLConfig" (fixed by 0003). HWUI needs
GLES 2.0 but only the GLES 1.x software renderer exists → RenderThread aborts ("Failed to choose
config"). ro.config.low_ram only disables HWUI for persistent processes. ro.kernel.qemu=0 (+
qemu.gles=0) turns HWUI off in every process (android_view_DisplayListCanvas_isAvailable checks
only that the property exists; adbd/EGL loader only react to "1") – but then the guest freezes
1-2 min after boot: vCPUs idle in cpu_v7_do_idle (xenctx), virtual timer IRQ 27 left active in
the LRs, no timer interrupts → no network, console or sysrq. Same with 1 vCPU, with the RAM vfb
instead of xenfb, and with animations off. Not solved.
Decision (user, option A): back to the stable headless setup; screen/touch kept but disabled
(android1.cfg: vfb line commented, RAM vfb; android-hdmi ENABLED=0; no ro.kernel.qemu).
Stability (uart-stability.log): reboot + autostart, boot_completed 63 s, alive 10 min,
0 system_server crashes, FreeRTOS 9961–10038 us. Note: Launcher3 (77) and SystemUI (46)
crash-loop in RenderThread (same HWUI/GLES2 cause) – headless Android stays usable over adb.
Image: tinker-xen-test-image-tinker-board-20261025072211.rootfs.wic
(sha256 e23e81e71fd4b3acf7b31ff41735aac09d5e10fda72f804f56af4803e5206a43); the card was brought
to the same ramdisk/android1.cfg/android-hdmi files live (md5 checked).
Also seen: the board went silent twice (UART adapter re-enumerated, no output until a
power-cycle) under full load during long UART transfers – suspected power supply.

## Z. Automotive bench, dashboard, ignition (2026-10-25)

Built (meta-tinker-xen): vehicle-bench (vehicle-sim: 60 s drive cycle on vcan0, 0x0C0 speed
0.01 km/h + 0x0C1 rpm every 10 ms; can2rtos: receive-only CAN_RAW filter → "V seq speed rpm"
lines into rtos1's console pty; real bus only in listen-only mode), FreeRTOS vVehicleTask
(5 ms period: overspeed > 120 km/h, redline > 6000 rpm with hysteresis, signal lost after
100 ms, 10 s report), cluster-hdmi (fbdev dashboard: speed, rpm, lamps OVERSPEED / REDLINE /
SIGNAL / RTOS), dom0 CAN modules (vcan, gs_usb, peak_usb, slcan) + can-utils, ignition-watch
(GPIO ACC input → clean poweroff after 10 s, disabled by default, needs wiring).
Verified on the board (uart-vehicle-bench.log, uart-session.log):
- ~1000 frames per 10 s, 0 lost, 0 bad; overspeed and redline alerts raised and cleared;
  pause → "SIGNAL LOST", resume → "SIGNAL OK again"; 10 ms task timing 9970–10029 us.
- Dashboard snapshot from the board (vfb RAM fb, no monitor): live 135 km/h, OVERSPEED red,
  SIGNAL/RTOS green. /var/log/xen/console was missing (no guest log for the RTOS lamp):
  fixed in xen-tools bbappend (volatiles entry, XENCONSOLED_TRACE=guest).
- Frame bursts: FreeRTOS saw max gaps of ~105 ms (close to the 100 ms signal-loss limit)
  while candump on vcan0 showed ≤ 29 ms. Cause: the guest sent an event-channel notify after
  every console read (up to 200/s); xenconsoled rate-limits a guest to 30 events per 200 ms
  and then stops serving it until the period ends. Fix: notify only when the input ring was
  nearly full. After the fix (new binaries pushed live, md5 checked): FreeRTOS max gap
  20–35 ms, can2rtos (dom0 side) max gap 17–33 ms, 0 lost, 0 dropped.
- ignition-watch: built and in the image (disabled); not tested on hardware (no ACC wired).
- Xen panic after flashing that image (uart-session.log, uart-rtds-panic.log): ~1 min after
  boot, CPU3 "Unexpected Trap: Data Abort" at rt.c#repl_timer_handler+0x1cc (deadline_replq_insert,
  rt.c:1517), R0 = 0x00100100 (LIST_POISON1): corrupted RTDS replenishment queue. Same bug as
  the earlier debug ASSERT rt.c:587 (replq_insert: !unit_on_replq). Cause (upstream
  73c62927f64e, "xen/sched: fix race in RTDS scheduler"): a domain pause makes a unit
  not runnable without the scheduler lock; rt_schedule() then takes it off the run queue but
  not the replenishment queue, and the wake-up inserts it a second time. Reproduced on the old
  Xen: xl pause/unpause rtos1 in a loop panicked within 250 rounds (uart-rtds-repro-old.log).
  Fix: Xen 0004 (73c62927f64e) + 0005 (0f1dec89d324, retry runq_pick), backported to 4.16.1.
- With the fix (uart-rtds-repro-fixed.log): Xen compiled 09:43 UTC, /boot/xen sha256 6a53ae89…;
  3000 pause/unpause rounds without a panic; afterwards 1000 frames/10 s, 0 lost, max gap
  25–35 ms; 10 ms task 9958–10040 us; ignition-watch installed, not running (ENABLED=0),
  gpioget gpiochip0 17 = 0; /var/log/xen/console has guest-rtos1.log and guest-android1.log.
Image: tinker-xen-test-image-tinker-board-20261025094250.rootfs.wic
(sha256 99a3fec640201d6b8e74c675a8d039f611ef7c67394d18ed555a84a18e5f9412), flashed and verified.
