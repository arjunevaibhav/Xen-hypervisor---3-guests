SUMMARY = "Video and picture player for the media1 guest's screen"
DESCRIPTION = "media-play shows videos and pictures full screen on a \
framebuffer with ffmpeg (software decode); the media-player service loops \
/home/root/media on the guest's PV framebuffer from boot, or a river clip \
while that is empty."
# Clip: "Sixes River (33895113543).webm", Bureau of Land Management Oregon
# and Washington, public domain (Wikimedia Commons). 1080p source scaled to
# 854x480 (lanczos), centre-cropped to 800x480, H.264 main, -tune fastdecode,
# 2-pass 800 kbit/s: 2.5 Mbit/s decoded at only 1.1x real time on one vCPU.
LICENSE = "MIT & PD"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302 \
                    file://${COMMON_LICENSE_DIR}/PD;md5=b3597d12946881e13cb3b548d1173851"

SRC_URI = "file://media-play file://media-player.init file://sixes-river-800x480.mp4"
S = "${WORKDIR}"

inherit update-rc.d allarch

INITSCRIPT_NAME = "media-player"
INITSCRIPT_PARAMS = "start 94 2 3 4 5 . stop 06 0 1 6 ."

RDEPENDS:${PN} = "ffmpeg"

do_install() {
    install -d ${D}${bindir} ${D}${datadir}/media-player ${D}${sysconfdir}/init.d
    install -m 0755 ${S}/media-play ${D}${bindir}/media-play
    install -m 0644 ${S}/sixes-river-800x480.mp4 ${D}${datadir}/media-player/
    install -m 0755 ${S}/media-player.init ${D}${sysconfdir}/init.d/media-player
}
