SUMMARY = "dom0 and guest consoles side by side on the serial line"
DESCRIPTION = "consoles opens one tmux session over the board's serial \
console with four panes: a dom0 shell, xl console rtos1, xl console \
android1 and xl console media1."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://consoles file://tmux.conf"
S = "${WORKDIR}"

inherit allarch

do_install() {
    install -d ${D}${sbindir} ${D}${sysconfdir}
    install -m 0755 ${S}/consoles ${D}${sbindir}/consoles
    install -m 0644 ${S}/tmux.conf ${D}${sysconfdir}/tmux.conf
}

CONFFILES:${PN} = "${sysconfdir}/tmux.conf"
# tmux 3 refuses to start without a UTF-8 locale (the image has none)
RDEPENDS:${PN} = "tmux ncurses-terminfo-base glibc-binary-localedata-en-us xen-tools-xl"
