# Dom0 runs QEMU only as Xen's device backend: guest framebuffers with
# keyboard/pointer (served over VNC) and the dom0 disk backend that
# xencommons starts. xen-tools expects /usr/bin/qemu-system-i386 for that,
# whatever the CPU, so build just that target, without SDL/GTK/OpenGL.
QEMU_TARGETS:class-target:tinker-board = "i386"
PACKAGECONFIG:class-target:tinker-board = "fdt pie xen"

# The firmware images in /usr/share/qemu (PC BIOS, EDK2, OpenSBI, SLOF,
# OpenBIOS ...) are for emulated machines: Xen's PV backends never load
# them. They were 220 MB of dom0's 291 MB. Keep only the VNC keymaps.
do_install:append:class-target:tinker-board() {
    find ${D}${datadir}/qemu -mindepth 1 -maxdepth 1 ! -name keymaps -exec rm -rf {} +
}
