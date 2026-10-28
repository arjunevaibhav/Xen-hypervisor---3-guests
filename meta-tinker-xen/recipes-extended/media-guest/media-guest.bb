SUMMARY = "xl config of the media1 guest (Yocto Linux video and picture player)"
DESCRIPTION = "dom0 side of the media1 guest: its xl config. The guest's root \
filesystem is media-guest-image on its own SD card partition (media-root), \
its kernel is dom0's."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://media1.cfg"
S = "${WORKDIR}"

inherit allarch

do_install() {
    install -d ${D}${sysconfdir}/xen
    install -m 0644 ${S}/media1.cfg ${D}${sysconfdir}/xen/media1.cfg
}

# the guest boots /boot/zImage; the PV framebuffer needs QEMU
RDEPENDS:${PN} = "kernel-image-zimage qemu"
