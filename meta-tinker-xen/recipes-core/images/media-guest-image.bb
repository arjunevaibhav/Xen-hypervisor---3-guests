SUMMARY = "media1: Yocto Linux Xen guest that plays videos and pictures"
DESCRIPTION = "core-image-minimal with ffmpeg and the media player for the \
media1 guest: shows the files in /home/root/media on its PV framebuffer, which \
dom0's display-map puts on a screen of its own. Runs dom0's kernel; this \
root filesystem goes to its own SD card partition (tinker-xen.wks)."

require recipes-core/images/core-image-minimal.bb

# needs LICENSE_FLAGS_ACCEPTED += "commercial" in local.conf (ffmpeg)
IMAGE_INSTALL:append = " media-player ffmpeg"

# Only the ext4 root filesystem, written to the SD card by dom0's image
IMAGE_FSTYPES = "ext4"
# Room for videos and pictures copied in later (64 MB; raise it for more)
IMAGE_ROOTFS_EXTRA_SPACE = "65536"

# SSH (scp files to /home/root/media), key logins only (TINKER_SSH_KEY)
inherit tinker-ssh

# On xenbr0 next to dom0 (192.168.100.1, gateway with NAT) and android1 (.3)
MEDIA_GUEST_IP ?= "192.168.100.4"
ROOTFS_POSTPROCESS_COMMAND += "media_guest_setup;"
media_guest_setup[vardeps] += "MEDIA_GUEST_IP"
media_guest_setup() {
    echo media1 > ${IMAGE_ROOTFS}${sysconfdir}/hostname
    cat > ${IMAGE_ROOTFS}${sysconfdir}/network/interfaces <<EOF
auto lo
iface lo inet loopback

auto eth0
iface eth0 inet static
    address ${MEDIA_GUEST_IP}
    netmask 255.255.255.0
    gateway 192.168.100.1
EOF
    # a plain file (no DHCP client to fill it), not the usual symlink to /run
    rm -f ${IMAGE_ROOTFS}${sysconfdir}/resolv.conf
    echo "nameserver 8.8.8.8" > ${IMAGE_ROOTFS}${sysconfdir}/resolv.conf
}
