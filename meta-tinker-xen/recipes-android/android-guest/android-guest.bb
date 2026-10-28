SUMMARY = "Android 7.0 (AOSP, ARM32) as a Xen guest, with a screen"
DESCRIPTION = "Takes the AOSP Android 7.0 armeabi-v7a system image from the \
Android SDK, adapts it to Xen (init.xen.rc, fstab.xen, software GLES) and \
provides the guest's ramdisk, disk images and xl config. The kernel is \
linux-android-guest."
# The SDK system image is distributed under the Android SDK License Agreement
# (see NOTICE.txt in the zip for the open source parts): development use only.
LICENSE = "CLOSED"

COMPATIBLE_MACHINE = "tinker-board"
PACKAGE_ARCH = "${MACHINE_ARCH}"

SRC_URI = " \
    https://dl.google.com/android/repository/sys-img/android/armeabi-v7a-24_r07.zip;subdir=sdk \
    file://fstab.xen \
    file://init.xen.rc \
    file://xen-net.sh \
    file://xen-audio.sh \
    file://android1.cfg \
    "
SRC_URI[sha256sum] = "42690977fe4b97b0643da11a2b42742b2e3d5b8849dcdd6893e0b92d6618c91d"

SDK = "${WORKDIR}/sdk/armeabi-v7a"
S = "${WORKDIR}"
B = "${WORKDIR}/build"

DEPENDS = "e2fsprogs-native cpio-native"
RDEPENDS:${PN} = "linux-android-guest"

inherit deploy

do_compile() {
    rm -rf ${B}/extra && mkdir -p ${B}/extra

    # Ramdisk: the SDK ramdisk plus a second cpio archive with the Xen
    # files. The kernel unpacks both; files in the second one win.
    install -m 0640 ${S}/fstab.xen ${B}/extra/fstab.xen
    install -m 0750 ${S}/init.xen.rc ${B}/extra/init.xen.rc
    install -m 0750 ${S}/xen-net.sh ${B}/extra/xen-net.sh
    install -m 0750 ${S}/xen-audio.sh ${B}/extra/xen-audio.sh
    zcat ${SDK}/ramdisk.img | cpio -i --to-stdout default.prop > ${B}/extra/default.prop
    echo "ro.sf.lcd_density=160" >> ${B}/extra/default.prop
    # Low-RAM mode: SystemUI's wallpaper then draws in software instead of
    # with GLES 2.0 (which doesn't exist here, it crashed SystemUI)
    echo "ro.config.low_ram=true" >> ${B}/extra/default.prop
    # For the PV screen (android1.cfg) apps must draw in software (the only
    # GLES is the 1.x software renderer, HWUI needs GLES 2.0): Android 7
    # turns HWUI off everywhere when ro.kernel.qemu is *defined*, which the
    # kernel option qemu=0 in android1.cfg does (ro.kernel.qemu=0).
    chmod 0644 ${B}/extra/default.prop
    (cd ${B}/extra && printf '%s\n' default.prop fstab.xen init.xen.rc xen-net.sh xen-audio.sh | \
        cpio -o -H newc -R 0:0 --quiet) | gzip -9n > ${B}/xen-overlay.cpio.gz
    cat ${SDK}/ramdisk.img ${B}/xen-overlay.cpio.gz > ${B}/ramdisk.img

    # system.img: outside the emulator, Android's EGL loader skips
    # libGLES_android.so (the software renderer) and would load the
    # emulator's host-GPU libraries. Remove those and give the software
    # renderer a name the loader accepts.
    cp ${SDK}/system.img ${B}/android-system.img
    debugfs -w ${B}/android-system.img -f - <<CMDS
rm /lib/egl/libEGL_emulation.so
rm /lib/egl/libGLESv1_CM_emulation.so
rm /lib/egl/libGLESv2_emulation.so
ln /lib/egl/libGLES_android.so /lib/egl/libGLES_soft.so
unlink /lib/egl/libGLES_android.so
symlink /lib/hw/audio.primary.xen.so audio.primary.goldfish.so
CMDS
    e2fsck -fn ${B}/android-system.img
    debugfs -R "ls /lib/egl" ${B}/android-system.img | grep -q libGLES_soft.so
    debugfs -R "stat /lib/hw/audio.primary.xen.so" ${B}/android-system.img | grep -q symlink

    # The SDK image is 1.8 GB with ~575 MB used, and the guest mounts it
    # read-only: shrink it to its contents plus 10 %
    e2fsck -fy ${B}/android-system.img || [ $? -le 1 ]
    resize2fs -M ${B}/android-system.img
    blocks=$(dumpe2fs -h ${B}/android-system.img 2>/dev/null | sed -n 's/^Block count: *//p')
    resize2fs ${B}/android-system.img $(expr $blocks \* 110 / 100)
    # cut the file to the new size (truncate is not a bitbake host tool)
    dd if=/dev/null of=${B}/android-system.img bs=4096 seek=$(expr $blocks \* 110 / 100) 2>/dev/null
    # the shrink leaves the reserved resize inode (7) with its old size
    e2fsck -fy ${B}/android-system.img || [ $? -le 1 ]
    e2fsck -fn ${B}/android-system.img

    cp ${SDK}/userdata.img ${B}/android-userdata.img
}

do_install() {
    install -d ${D}${sysconfdir}/xen ${D}${localstatedir}/lib/xen/android
    install -m 0644 ${S}/android1.cfg ${D}${sysconfdir}/xen/android1.cfg
    install -m 0644 ${B}/ramdisk.img ${D}${localstatedir}/lib/xen/android/ramdisk.img
}

# The disk images are too big for the root filesystem: wic writes them to
# their own SD card partitions (tinker-xen.wks)
do_deploy() {
    install -m 0644 ${B}/android-system.img ${B}/android-userdata.img ${DEPLOYDIR}/
}
addtask deploy after do_compile before do_build

FILES:${PN} = "${sysconfdir}/xen/android1.cfg ${localstatedir}/lib/xen/android/ramdisk.img"
