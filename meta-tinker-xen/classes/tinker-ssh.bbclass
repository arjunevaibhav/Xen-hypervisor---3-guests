# SSH (dropbear) for the board's Linux images (dom0, media1), key logins only:
# the images have an empty root password (debug-tweaks), fine on a console
# but not on a network. Set TINKER_SSH_KEY in local.conf to the public key
# file(s) allowed to log in as root, e.g.
# TINKER_SSH_KEY = "/home/<user>/.ssh/id_ed25519.pub" (several files
# separated by spaces for more PCs).
IMAGE_FEATURES += "ssh-server-dropbear"
TINKER_SSH_KEY ??= ""
ROOTFS_POSTPROCESS_COMMAND += "tinker_ssh_setup;"
tinker_ssh_setup[vardeps] += "TINKER_SSH_KEY"
tinker_ssh_setup() {
    sed -i '/^DROPBEAR_EXTRA_ARGS=/ s/"$/ -s"/' ${IMAGE_ROOTFS}${sysconfdir}/default/dropbear
    if [ -n "${TINKER_SSH_KEY}" ]; then
        install -d -m 0700 ${IMAGE_ROOTFS}${ROOT_HOME}/.ssh
        cat ${TINKER_SSH_KEY} > ${IMAGE_ROOTFS}${ROOT_HOME}/.ssh/authorized_keys
        chmod 0600 ${IMAGE_ROOTFS}${ROOT_HOME}/.ssh/authorized_keys
    else
        bbwarn "TINKER_SSH_KEY not set: nobody can log in over SSH"
    fi
}
