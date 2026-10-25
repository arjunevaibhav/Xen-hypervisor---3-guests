SUMMARY = "Show a Xen guest's PV framebuffer on dom0's HDMI with touch, mouse and keyboard"
DESCRIPTION = "Minimal VNC client for QEMU's xenfb backend: draws the guest \
screen on /dev/fb0 (HDMI) and forwards dom0 evdev input (touchscreen, mouse, \
keyboard) to the guest's PV keyboard/pointer."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://android-hdmi.c file://android-hdmi.init"
S = "${WORKDIR}"

inherit update-rc.d

INITSCRIPT_NAME = "android-hdmi"
# after xen-guests (90)
INITSCRIPT_PARAMS = "start 91 2 3 4 5 . stop 09 0 1 6 ."

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -Wall -Wextra -o android-hdmi ${S}/android-hdmi.c
}

do_install() {
    install -d ${D}${bindir} ${D}${sysconfdir}/init.d
    install -m 0755 ${B}/android-hdmi ${D}${bindir}/android-hdmi
    install -m 0755 ${S}/android-hdmi.init ${D}${sysconfdir}/init.d/android-hdmi
}
