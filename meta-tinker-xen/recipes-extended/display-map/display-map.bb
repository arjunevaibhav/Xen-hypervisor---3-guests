SUMMARY = "One screen per OS: Xen guests, instrument cluster and dom0 console on HDMI + USB displays"
DESCRIPTION = "Maps each screen (the HDMI output and USB display adapters, \
udl driver) to a source from /etc/default/display-map: a guest's PV \
framebuffer (android-hdmi), the instrument cluster with the FreeRTOS status \
(cluster-hdmi) or dom0's text console (con2fb), optionally replaced by the \
rear camera in reverse (rear-camera). Replaces the single-screen \
android-hdmi and cluster-hdmi services."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://con2fb.c file://display-map.init file://display-map.default"
S = "${WORKDIR}"

inherit update-rc.d

INITSCRIPT_NAME = "display-map"
# after xen-guests (90) and vehicle-bench (92)
INITSCRIPT_PARAMS = "start 93 2 3 4 5 . stop 07 0 1 6 ."

RDEPENDS:${PN} = "android-hdmi cluster-hdmi rear-camera kernel-module-udl"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -Wall -Wextra -o con2fb ${S}/con2fb.c
}

do_install() {
    install -d ${D}${bindir} ${D}${sysconfdir}/init.d ${D}${sysconfdir}/default
    install -m 0755 ${B}/con2fb ${D}${bindir}/con2fb
    install -m 0755 ${S}/display-map.init ${D}${sysconfdir}/init.d/display-map
    install -m 0644 ${S}/display-map.default ${D}${sysconfdir}/default/display-map
    # display-map runs the viewers itself: keep their single-screen
    # services (HDMI only) from taking the HDMI screen too
    echo "ENABLED=0" > ${D}${sysconfdir}/default/android-hdmi
    echo "ENABLED=0" > ${D}${sysconfdir}/default/cluster-hdmi
}

CONFFILES:${PN} = "${sysconfdir}/default/display-map \
    ${sysconfdir}/default/android-hdmi ${sysconfdir}/default/cluster-hdmi"
