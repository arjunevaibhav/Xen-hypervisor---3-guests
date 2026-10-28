SUMMARY = "dom0 audio pipeline: vehicle chimes, media1 and android1 sound to the speaker"
DESCRIPTION = "audio-mixer mixes the vehicle chimes (made in dom0, highest \
priority: the guests are turned down while one plays), media1's video sound \
(UDP) and android1's sound (TCP, paced by flow control) from xenbr0 into the \
board's speaker output with ALSA. vehicle-chimes turns the FreeRTOS guest's \
real-time warnings into chimes; rear-camera asks for the reverse chime."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://audio-mixer.c file://vehicle-chimes \
    file://audio-mixer.init file://audio-mixer.default"
S = "${WORKDIR}"

DEPENDS = "alsa-lib"

inherit update-rc.d

INITSCRIPT_NAME = "audio-mixer"
# after xen-guests (90), before vehicle-bench (92)
INITSCRIPT_PARAMS = "start 91 2 3 4 5 . stop 09 0 1 6 ."

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -Wall -Wextra -o audio-mixer ${S}/audio-mixer.c -lasound -lm
}

do_install() {
    install -d ${D}${bindir} ${D}${sysconfdir}/init.d ${D}${sysconfdir}/default
    install -m 0755 ${B}/audio-mixer ${S}/vehicle-chimes ${D}${bindir}/
    install -m 0755 ${S}/audio-mixer.init ${D}${sysconfdir}/init.d/audio-mixer
    install -m 0644 ${S}/audio-mixer.default ${D}${sysconfdir}/default/audio-mixer
}

CONFFILES:${PN} = "${sysconfdir}/default/audio-mixer"
RDEPENDS:${PN} = "kernel-module-snd-usb-audio"
