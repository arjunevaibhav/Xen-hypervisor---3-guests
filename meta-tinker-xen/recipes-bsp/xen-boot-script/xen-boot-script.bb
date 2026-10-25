SUMMARY = "U-Boot script that starts Xen with Linux as dom0 (Tinker Board)"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

COMPATIBLE_MACHINE = "tinker-board"

DEPENDS = "u-boot-mkimage-native"

SRC_URI = "file://xen-boot.cmd file://native-extlinux.conf"

S = "${WORKDIR}"

inherit deploy nopackages

do_compile() {
    mkimage -A arm -O linux -T script -C none -n "Xen boot (Tinker RK3288)" \
        -d ${S}/xen-boot.cmd ${B}/xen-boot.scr
}

do_deploy() {
    install -m 0644 ${B}/xen-boot.scr ${DEPLOYDIR}/xen-boot.scr
    install -m 0644 ${S}/native-extlinux.conf ${DEPLOYDIR}/native-extlinux.conf
}

addtask deploy after do_compile before do_build
