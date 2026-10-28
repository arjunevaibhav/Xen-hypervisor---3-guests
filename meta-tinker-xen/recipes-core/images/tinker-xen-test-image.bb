SUMMARY = "Tinker Board RK3288 Xen bring-up test image (experimental)"
DESCRIPTION = "core-image-minimal plus the debug Xen hypervisor copied to the \
FAT boot partition as /xen. U-Boot runs /boot.scr at power-on, which starts \
Xen; /native/extlinux.conf boots Linux without Xen."

require recipes-core/images/core-image-minimal.bb

# No generated /extlinux/extlinux.conf, so U-Boot's distro boot uses /boot.scr
WKS_FILE = "tinker-xen.wks"

# Copy the deployed Xen binary (renamed to /xen), its U-Boot script (as
# /boot.scr for power-on and /xen-boot.scr for the prompt) and the
# Linux-without-Xen entry into the boot partition
IMAGE_BOOT_FILES:append = " xen-${MACHINE};xen xen-boot.scr xen-boot.scr;boot.scr \
    native-extlinux.conf;native/extlinux.conf"

# Xen is not a sysroot dependency of the image, so wait for its deploy step
do_image_wic[depends] += "xen:do_deploy xen-boot-script:do_deploy android-guest:do_deploy \
    media-guest-image:do_image_complete"

# Xen toolstack (xl, xenstored, xenconsoled, xencommons init script), the dom0
# backend modules it loads, guest networking with NAT (vif-bridge scripts +
# bridge xenbr0), a FreeRTOS guest and an Android guest (kernel +
# ramdisk here, disks on their own partitions) with adb in dom0; the guests
# start at boot (xen-guests) and a hung system is reset by the hardware
# watchdog (xen-guests-watchdog).
# media1 (media-guest): a Yocto Linux guest that plays videos and pictures
# (ffmpeg lives there, not in dom0); its root filesystem is a partition of
# its own (tinker-xen.wks), the kernel is dom0's /boot/zImage.
# Screens, touch and sound: QEMU (Xen's device model) serves the guests' PV
# framebuffers and touch over local VNC; display-map shows each OS on a
# screen of its own (HDMI with the Rockchip DRM modules, USB display
# adapters with udl): the guests through android-hdmi with dom0's
# touchscreen/mouse/keyboard, the instrument cluster and dom0's console;
# ALSA with the onboard USB audio codec for dom0.
# Rear camera: a USB webcam (uvcvideo) shown by rear-camera in place of the
# cluster while the car is in reverse (display-map). Audio pipeline:
# audio-mixer plays the vehicle chimes (highest priority), media1's video
# sound and android1's sound, sent over xenbr0, on the speaker output.
# Automotive bench (vehicle-bench): CAN (virtual bus, USB adapters,
# can-utils), a simulated vehicle, the read-only CAN -> FreeRTOS gateway and
# an instrument-cluster dashboard (cluster-hdmi); ignition-watch for a
# clean power-off when the car's ACC goes off (disabled until wired).
# xen-consoles: dom0 and guest consoles side by side (tmux) on the one
# serial line.
IMAGE_INSTALL:append = " \
    xen-tools-xencommons \
    kernel-module-xen-gntdev kernel-module-xen-gntalloc \
    kernel-module-xen-blkback kernel-module-xen-netback kernel-module-xen-wdt \
    xen-tools-scripts-network xen-tools-scripts-block xenbr0 \
    freertos-xen-guest \
    android-guest android-tools media-guest \
    xen-guests xen-guests-watchdog \
    qemu android-hdmi display-map \
    kernel-module-rockchipdrm kernel-module-dw-hdmi kernel-module-vfb kernel-module-udl \
    kernel-module-snd-usb-audio alsa-utils-aplay alsa-utils-amixer alsa-utils-speakertest \
    audio-mixer rear-camera kernel-module-uvcvideo \
    vehicle-bench can-utils cluster-hdmi ignition-watch \
    xen-consoles \
    kernel-module-vcan kernel-module-gs-usb kernel-module-peak-usb kernel-module-slcan \
    "

# SSH into dom0 (dropbear) over eth0, key logins only (TINKER_SSH_KEY)
inherit tinker-ssh

# Free space on dom0's root filesystem (32 MB): the guest kernels and
# ramdisk are already in the image, and /var/log is in RAM
IMAGE_ROOTFS_EXTRA_SPACE = "32768"
