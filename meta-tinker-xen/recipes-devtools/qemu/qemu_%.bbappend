# Dom0 runs QEMU only as Xen's device backend: guest framebuffers with
# keyboard/pointer (served over VNC) and the dom0 disk backend that
# xencommons starts. xen-tools expects /usr/bin/qemu-system-i386 for that,
# whatever the CPU, so build just that target, without SDL/GTK/OpenGL.
QEMU_TARGETS:class-target:tinker-board = "i386"
PACKAGECONFIG:class-target:tinker-board = "fdt pie xen"
