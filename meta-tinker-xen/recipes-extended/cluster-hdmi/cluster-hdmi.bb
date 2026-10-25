SUMMARY = "Instrument-cluster dashboard on the HDMI monitor (dom0 framebuffer)"
DESCRIPTION = "Draws speed, rpm and gear from the vehicle CAN bus (receive only) \
and the FreeRTOS guest's real-time warnings (overspeed, redline, signal loss, \
10 ms task timing) on /dev/fb0."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://cluster-hdmi.c file://cluster-hdmi.init"
S = "${WORKDIR}"

inherit update-rc.d

INITSCRIPT_NAME = "cluster-hdmi"
# after vehicle-bench (92)
INITSCRIPT_PARAMS = "start 93 2 3 4 5 . stop 07 0 1 6 ."

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -Wall -Wextra -o cluster-hdmi ${S}/cluster-hdmi.c
}

do_install() {
    install -d ${D}${bindir} ${D}${sysconfdir}/init.d
    install -m 0755 ${B}/cluster-hdmi ${D}${bindir}/cluster-hdmi
    install -m 0755 ${S}/cluster-hdmi.init ${D}${sysconfdir}/init.d/cluster-hdmi
}
