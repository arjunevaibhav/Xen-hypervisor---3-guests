SUMMARY = "Tinker Board Xen system services: guest autostart and hardware watchdog"
DESCRIPTION = "xen-guests starts the guests in /etc/default/xen-guests at boot \
(rtos1 through rtos1-start, android1 and media1 with xl create) and keeps the onboard \
USB hub out of runtime suspend (adb in dom0 resets it otherwise); \
hw-watchdog keeps the RK3288 hardware watchdog alive from dom0 so a hung \
system resets itself."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://xen-guests file://xen-guests.default file://hw-watchdog file://50-tinker-usb-hub.rules"
S = "${WORKDIR}"

inherit update-rc.d allarch

PACKAGES =+ "${PN}-watchdog"

INITSCRIPT_PACKAGES = "${PN} ${PN}-watchdog"
INITSCRIPT_NAME:${PN} = "xen-guests"
# after xenbr0 (79) and xencommons (80)
INITSCRIPT_PARAMS:${PN} = "start 90 2 3 4 5 . stop 10 0 1 6 ."
INITSCRIPT_NAME:${PN}-watchdog = "hw-watchdog"
INITSCRIPT_PARAMS:${PN}-watchdog = "start 05 2 3 4 5 ."

do_install() {
    install -d ${D}${sysconfdir}/init.d ${D}${sysconfdir}/default
    install -m 0755 ${S}/xen-guests ${D}${sysconfdir}/init.d/xen-guests
    install -m 0644 ${S}/xen-guests.default ${D}${sysconfdir}/default/xen-guests
    install -m 0755 ${S}/hw-watchdog ${D}${sysconfdir}/init.d/hw-watchdog
    install -d ${D}${sysconfdir}/udev/rules.d
    install -m 0644 ${S}/50-tinker-usb-hub.rules ${D}${sysconfdir}/udev/rules.d/
}

FILES:${PN} = "${sysconfdir}/init.d/xen-guests ${sysconfdir}/default/xen-guests \
    ${sysconfdir}/udev/rules.d/50-tinker-usb-hub.rules"
FILES:${PN}-watchdog = "${sysconfdir}/init.d/hw-watchdog"
CONFFILES:${PN} = "${sysconfdir}/default/xen-guests"
RDEPENDS:${PN} = "xen-tools-xl freertos-xen-guest android-guest media-guest"
