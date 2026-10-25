FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Do not reset the RK3288 architected counter (timer7) under Xen, and enable
# the software bridge used for Xen guest networking; vfb module for testing
# the HDMI viewer without a monitor; CAN (vcan, USB adapters) for the
# automotive bench setup
SRC_URI:append:tinker-board = " \
    file://0001-ARM-rockchip-rk3288-keep-timer7-running-if-already-started.patch \
    file://xen-dom0-bridge.cfg \
    file://xen-dom0-display.cfg \
    file://xen-dom0-can.cfg \
    "
