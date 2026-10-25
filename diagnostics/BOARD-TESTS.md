# Board tests to run by hand (Tinker Board R2.0, RK3288)

None of these steps write to the board's bootloader or environment.
Never type `saveenv` at the U-Boot prompt. Everything below lives in RAM
and disappears on power-off.

## 0. Capture the serial console to a file (host, Ubuntu)

Your old capture `~/minicom.cap` is empty and owned by root (minicom was
started with sudo). Give your user access to the serial port once:

    sudo usermod -aG dialout $USER     # then log out and back in

Then capture every boot like this. Start the capture BEFORE powering the board:

    sudo apt install picocom
    picocom -b 115200 --logfile ~/tinker-boot-$(date +%Y%m%d-%H%M%S).log /dev/ttyUSB0

Quit picocom with Ctrl-A then Ctrl-X. Send the whole .log file, starting from
the first "U-Boot TPL" line.

## 1. Identify what is on the SD card (host, read-only)

Put the card in the host reader. Find its device name (example: /dev/sdb).
Check the SIZE column carefully; it must be ~29.7G:

    lsblk -o NAME,SIZE,TYPE,PARTUUID,PARTLABEL,FSTYPE,LABEL

Then, replacing sdX with your card (these commands only READ the card):

    sudo sfdisk -d /dev/sdX
    sudo dd if=/dev/sdX bs=512 skip=64 count=8000 status=none | strings | grep -E 'U-Boot (TPL|SPL) 20'
    sudo dd if=/dev/sdX bs=512 skip=16384 count=8192 status=none | strings | grep -E '^U-Boot 20' | sort -u

Why: the Yocto WIC contains U-Boot TPL/SPL 2022.01 and 7 partitions. Your
log showed TPL/SPL 2024.01 and 9 partitions (p1..p9), so that card was NOT a
whole-disk copy of the Yocto image.

## 2. If Linux boots to a shell: check the CPU boot mode

    dmesg | grep -iE 'started in|HYP|SVC'
    cat /proc/cmdline
    cat /proc/cpuinfo | head -20

"All CPU(s) started in SVC mode" means the bootloader did not enter Hyp
mode, so Xen cannot start. "started in HYP mode" means Xen's main
prerequisite is satisfied.

## 3. Diagnostic direct-Linux boot (only if Linux appears to freeze)

Interrupt U-Boot autoboot by pressing a key during "Hit any key", then:

    mmc list
    part list mmc 1

Note the partition number named "boot" (6 in the Yocto image) and the
PARTUUID of "root". Then (replace 6 and the PARTUUID with what you saw):

    load mmc 1:6 ${kernel_addr_r} /zImage
    load mmc 1:6 ${fdt_addr_r} /rk3288-tinker.dtb
    setenv bootargs "console=ttyS2,115200n8 earlycon=uart8250,mmio32,0xff690000 ignore_loglevel root=PARTUUID=<root-partuuid> rootwait rw rootfstype=ext4 init=/sbin/init"
    bootz ${kernel_addr_r} - ${fdt_addr_r}

Why each argument:
- only one console= (serial). This rules out output going to tty1/HDMI.
- earlycon + ignore_loglevel make every kernel message print.
- an explicit PARTUUID. If it is wrong you will see
  "Waiting for root device PARTUUID=..." and nothing more (rootwait waits forever).

What the result means:
- "Waiting for root device" and nothing after: wrong PARTUUID / wrong card contents.
- "VFS: Unable to mount root fs": filesystem/driver problem.
- "Run /sbin/init as init process" then silence: init/getty problem.
- "Kernel panic": send the full trace.

## 4. Xen experiment (only with tinker-xen-test-image on the card)

Expected result with the CURRENT U-Boot: Xen prints
"- Xen must be entered in NS Hyp mode -" and stops. That is the known
blocker (see REPORT.md, section C). This test confirms the blocker on real
hardware. Power-cycle to recover; nothing is changed permanently.

At the U-Boot prompt (boot partition is 6 in the Yocto image, root PARTUUID
from `part list mmc 1`):

    setenv xen_addr  0x02000000
    setenv dom0_addr 0x04000000
    load mmc 1:6 ${fdt_addr_r} /rk3288-tinker.dtb
    load mmc 1:6 ${xen_addr} /xen
    load mmc 1:6 ${dom0_addr} /zImage
    fdt addr ${fdt_addr_r}
    fdt resize 1024
    fdt set /chosen \#address-cells <1>
    fdt set /chosen \#size-cells <1>
    fdt set /chosen xen,xen-bootargs "console=dtuart dtuart=serial2 dom0_mem=512M dom0_max_vcpus=1 loglvl=all guest_loglvl=all"
    fdt mknod /chosen module@4000000
    fdt set /chosen/module@4000000 compatible "xen,linux-zimage" "xen,multiboot-module"
    fdt set /chosen/module@4000000 reg <${dom0_addr} 0x${filesize}>
    fdt set /chosen/module@4000000 bootargs "console=hvc0 earlycon=xenboot root=PARTUUID=<root-partuuid> rootwait rw rootfstype=ext4"
    bootz ${xen_addr} - ${fdt_addr_r}

Notes:
- The `fdt set ... reg` line must come straight after loading /zImage,
  because ${filesize} holds the size of the LAST file loaded.
- Address map (RAM is 0x00000000-0x7fffffff, 2 GiB):
  DTB 0x01f00000 (~40 KiB), Xen 0x02000000 (~1 MiB), Dom0 zImage 0x04000000
  (~10 MiB). The regions do not overlap. Xen relocates itself to the top of RAM.
- dom0_mem=512M and dom0_max_vcpus=1 are conservative. Xen can only start
  CPU0 on RK3288 anyway (no PSCI; see REPORT.md).
