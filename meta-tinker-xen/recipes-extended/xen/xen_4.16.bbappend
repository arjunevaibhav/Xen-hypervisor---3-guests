FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Board-specific: Cortex-A12/A17 CPU support and L2CTLR emulation for dom0,
# an RK3288 platform with reset support, and a config fragment (merged by
# xen-hypervisor.inc do_configure). 0004/0005 are upstream RTDS fixes (a
# paused unit stayed on the replenishment queue: list corruption, Xen panic).
# By default the config is the release build;
# XEN_TINKER_DEBUG = "1" in local.conf builds the debug Xen instead
# (asserts, early printk on UART2, guest CONSOLEIO output on the UART).
XEN_TINKER_DEBUG ??= "0"
SRC_URI:append:tinker-board = " \
    ${@'file://tinker-rk3288-early-printk.cfg' if d.getVar('XEN_TINKER_DEBUG') == '1' else 'file://tinker-rk3288-release.cfg'} \
    file://0001-xen-arm32-add-Cortex-A12-and-Cortex-A17-processor-info.patch \
    file://0002-xen-arm32-emulate-L2CTLR-for-guests.patch \
    file://0003-xen-arm-add-Rockchip-RK3288-platform-with-reset.patch \
    file://0004-xen-sched-fix-race-in-RTDS-scheduler.patch \
    file://0005-xen-sched-try-harder-to-find-a-runnable-unit-in-rt_schedule.patch \
    "
