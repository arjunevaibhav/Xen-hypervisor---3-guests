SUMMARY = "Linux 4.14 kernel for the headless Android 7 Xen guest"
DESCRIPTION = "Guest kernel for the Android 7.0 ARM32 user space, which uses \
the old 32-bit binder ABI (CONFIG_ANDROID_BINDER_IPC_32BIT). That option was \
removed in Linux 4.18, so dom0's 5.15 kernel cannot run it. Built from \
multi_v7_defconfig plus android-guest.cfg, with everything built in."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=d7810fab7487fb0aad327b76f1be7cd7"

COMPATIBLE_MACHINE = "tinker-board"
PACKAGE_ARCH = "${MACHINE_ARCH}"

SRC_URI = " \
    https://cdn.kernel.org/pub/linux/kernel/v4.x/linux-${PV}.tar.xz \
    file://android-guest.cfg \
    file://0001-timerfd-allow-alarm-clocks-for-the-Android-system-group.patch \
    file://0002-xen-kbdfront-present-the-absolute-pointer-as-a-touchscreen.patch \
    file://0003-xen-fbfront-add-a-16-bpp-RGB565-mode.patch \
    "
SRC_URI[sha256sum] = "0820fdb7971c6974338081c11fbf2dc869870501e7bdcac4d0ed58ba1f57b61c"

S = "${WORKDIR}/linux-${PV}"
B = "${WORKDIR}/build"

DEPENDS = "bc-native bison-native flex-native"

inherit kernel-arch deploy

# Not the machine kernel: no kernel packaging, just a zImage for xl
EXTRA_OEMAKE = " \
    -C ${S} O=${B} ARCH=${ARCH} CROSS_COMPILE=${TARGET_PREFIX} \
    CC='${KERNEL_CC}' LD='${KERNEL_LD}' AR='${KERNEL_AR}' \
    HOSTCC='${BUILD_CC}' HOSTCFLAGS='${BUILD_CFLAGS}' HOSTLDFLAGS='${BUILD_LDFLAGS}' \
    "

do_configure() {
    oe_runmake multi_v7_defconfig
    ${S}/scripts/kconfig/merge_config.sh -m -O ${B} ${B}/.config ${WORKDIR}/android-guest.cfg
    oe_runmake olddefconfig
    # fail early if an option the Android guest needs did not stick
    for opt in XEN HVC_XEN XEN_BLKDEV_FRONTEND XEN_NETDEV_FRONTEND \
               ANDROID_BINDER_IPC ANDROID_BINDER_IPC_32BIT ASHMEM \
               SECURITY_SELINUX EXT4_FS_SECURITY FB_VIRTUAL PM_WAKELOCKS \
               XEN_FBDEV_FRONTEND INPUT_XEN_KBDDEV_FRONTEND; do
        grep -q "^CONFIG_$opt=y" ${B}/.config || bbfatal "CONFIG_$opt is not set"
    done
}

do_compile() {
    oe_runmake zImage
}

do_install() {
    install -d ${D}${localstatedir}/lib/xen/android
    install -m 0644 ${B}/arch/arm/boot/zImage ${D}${localstatedir}/lib/xen/android/zImage
}

do_deploy() {
    install -m 0644 ${B}/arch/arm/boot/zImage ${DEPLOYDIR}/android-guest-zImage
    install -m 0644 ${B}/.config ${DEPLOYDIR}/android-guest-zImage.config
}
addtask deploy after do_compile before do_build

FILES:${PN} = "${localstatedir}/lib/xen/android/zImage"
INSANE_SKIP:${PN} = "arch"
