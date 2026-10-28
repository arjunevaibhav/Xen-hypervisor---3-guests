SUMMARY = "Rear-view camera on a dom0 screen while the car is in reverse"
DESCRIPTION = "rear-camera watches the gear frame on the vehicle CAN bus \
(receive only); in reverse it pauses the screen's viewer (e.g. the instrument \
cluster) and shows a USB (UVC) camera, mirrored, with parking guide lines, \
then gives the screen back. display-map runs it (rearcam:<source> in \
/etc/default/display-map)."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://rear-camera.c"
S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -Wall -Wextra -o rear-camera ${S}/rear-camera.c
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/rear-camera ${D}${bindir}/rear-camera
}

RDEPENDS:${PN} = "kernel-module-uvcvideo"
