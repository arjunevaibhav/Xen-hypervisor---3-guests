FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Non-secure HYP boot and PSCI (SMP) for Xen (only used when bootm_boot_mode=nonsec)
SRC_URI:append:tinker-board = " \
    file://0001-rockchip-rk3288-allow-booting-in-non-secure-HYP-mode.patch \
    file://0002-rockchip-rk3288-add-PSCI-to-start-the-secondary-CPUs.patch \
    "
