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
do_image_wic[depends] += "xen:do_deploy xen-boot-script:do_deploy android-guest:do_deploy"

# Xen toolstack (xl, xenstored, xenconsoled, xencommons init script), the dom0
# backend modules it loads, guest networking with NAT (vif-bridge scripts +
# bridge xenbr0), a FreeRTOS guest and an Android guest (kernel +
# ramdisk here, disks on their own partitions) with adb in dom0; the guests
# start at boot (xen-guests) and a hung system is reset by the hardware
# watchdog (xen-guests-watchdog).
# Screen, touch and sound: QEMU (Xen's device model) serves Android's PV
# framebuffer and touch over local VNC, android-hdmi shows it on the HDMI
# monitor (Rockchip DRM modules) with dom0's touchscreen/mouse/keyboard;
# ALSA with the onboard USB audio codec for dom0.
# Automotive bench (vehicle-bench): CAN (virtual bus, USB adapters,
# can-utils), a simulated vehicle, the read-only CAN -> FreeRTOS gateway and
# an instrument-cluster dashboard on HDMI (cluster-hdmi); ignition-watch for a
# clean power-off when the car's ACC goes off (disabled until wired).
IMAGE_INSTALL:append = " \
    xen-tools-xencommons \
    kernel-module-xen-gntdev kernel-module-xen-gntalloc \
    kernel-module-xen-blkback kernel-module-xen-netback kernel-module-xen-wdt \
    xen-tools-scripts-network xen-tools-scripts-block xenbr0 \
    freertos-xen-guest \
    android-guest android-tools \
    xen-guests xen-guests-watchdog \
    qemu android-hdmi \
    kernel-module-rockchipdrm kernel-module-dw-hdmi kernel-module-vfb \
    kernel-module-snd-usb-audio alsa-utils-aplay alsa-utils-amixer alsa-utils-speakertest \
    vehicle-bench can-utils cluster-hdmi ignition-watch \
    kernel-module-vcan kernel-module-gs-usb kernel-module-peak-usb kernel-module-slcan \
    "

# Room for the guest kernel and initramfs in /var/lib/xen
IMAGE_ROOTFS_EXTRA_SPACE = "65536"
