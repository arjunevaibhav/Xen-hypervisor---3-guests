SUMMARY = "Bridge xenbr0 (192.168.100.1/24) with NAT for Xen guest networking"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://xenbr0"
S = "${WORKDIR}"

inherit update-rc.d

# depends on the machine kernel's netfilter modules
PACKAGE_ARCH = "${MACHINE_ARCH}"

INITSCRIPT_NAME = "xenbr0"
INITSCRIPT_PARAMS = "defaults 79"

do_install() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${S}/xenbr0 ${D}${sysconfdir}/init.d/xenbr0
}

RDEPENDS:${PN} = " \
    bridge-utils iptables \
    kernel-module-iptable-nat kernel-module-xt-masquerade kernel-module-nf-nat \
    "
