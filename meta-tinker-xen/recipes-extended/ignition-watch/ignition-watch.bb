SUMMARY = "Clean power-off when the car's ignition (ACC) signal goes off"
DESCRIPTION = "ignition-watch reads the ACC signal on a GPIO line and powers \
the board off (guests stopped first) when it has been off for a few \
seconds. Disabled by default until the signal is wired."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://ignition-watch file://ignition-watch.init file://ignition-watch.default"
S = "${WORKDIR}"

inherit update-rc.d allarch

INITSCRIPT_NAME = "ignition-watch"
# after xen-guests (90); stopped before it
INITSCRIPT_PARAMS = "start 94 2 3 4 5 . stop 05 0 1 6 ."

do_install() {
    install -d ${D}${sbindir} ${D}${sysconfdir}/init.d ${D}${sysconfdir}/default
    install -m 0755 ${S}/ignition-watch ${D}${sbindir}/ignition-watch
    install -m 0755 ${S}/ignition-watch.init ${D}${sysconfdir}/init.d/ignition-watch
    install -m 0644 ${S}/ignition-watch.default ${D}${sysconfdir}/default/ignition-watch
}

CONFFILES:${PN} = "${sysconfdir}/default/ignition-watch"
RDEPENDS:${PN} = "libgpiod-tools"
