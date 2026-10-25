SUMMARY = "FreeRTOS demo running as a 32-bit ARM Xen guest (domU)"
DESCRIPTION = "FreeRTOS-Kernel (ARM_CA9 GCC port) on Xen's virtual GICv2 and \
virtual timer, packaged as an ARM32 zImage plus an xl config for dom0."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://FreeRTOS-Kernel/LICENSE.md;md5=7ae2be7fb1637141840314b51970a9f7"

COMPATIBLE_MACHINE = "tinker-board"
PACKAGE_ARCH = "${MACHINE_ARCH}"

# FreeRTOS-Kernel V11.1.0
SRCREV = "dbf70559b27d39c1fdb68dfb9a32140b6a6777a0"
SRC_URI = " \
    git://github.com/FreeRTOS/FreeRTOS-Kernel.git;protocol=https;nobranch=1;destsuffix=FreeRTOS-Kernel \
    file://Makefile \
    file://guest.ld \
    file://startup.S \
    file://main.c \
    file://FreeRTOSConfig.h \
    file://rtos1.cfg \
    file://rtos1-start \
    "

S = "${WORKDIR}"
B = "${WORKDIR}"

inherit deploy

# Bare metal: use our own flags, not the Linux userspace CFLAGS/TUNE_CCARGS
do_compile() {
    oe_runmake -C ${S} \
        CC="${HOST_PREFIX}gcc ${TOOLCHAIN_OPTIONS}" \
        OBJCOPY="${HOST_PREFIX}objcopy" \
        KERNEL=${S}/FreeRTOS-Kernel
}

do_install() {
    install -d ${D}${sysconfdir}/xen ${D}${localstatedir}/lib/xen/rtos1 ${D}${sbindir}
    install -m 0644 ${S}/rtos1.cfg ${D}${sysconfdir}/xen/rtos1.cfg
    install -m 0644 ${B}/freertos-xen-guest.bin ${D}${localstatedir}/lib/xen/rtos1/
    install -m 0755 ${S}/rtos1-start ${D}${sbindir}/rtos1-start
}

do_deploy() {
    install -m 0644 ${B}/freertos-xen-guest.bin ${B}/freertos-xen-guest.elf ${DEPLOYDIR}/
}
addtask deploy after do_compile before do_build

FILES:${PN} = "${sysconfdir}/xen/rtos1.cfg ${localstatedir}/lib/xen/rtos1 ${sbindir}/rtos1-start"
