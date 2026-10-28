FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Do not reset the RK3288 architected counter (timer7) under Xen, and enable
# the software bridge used for Xen guest networking; vfb module for testing
# the HDMI viewer without a monitor; CAN (vcan, USB adapters) for the
# automotive bench setup; HDMI modes outside the fixed RK3288 pixel clock
# table (e.g. 7" 1024x600 panels); DMA buffers from lowmem when dom0's CMA
# area is in highmem (USB webcams oopsed under Xen)
SRC_URI:append:tinker-board = " \
    file://0001-ARM-rockchip-rk3288-keep-timer7-running-if-already-started.patch \
    file://0002-drm-rockchip-rk3288-hdmi-allow-any-pixel-clock.patch \
    file://0003-dma-mapping-no-highmem-cma-pages-in-dma_common_alloc_pages.patch \
    file://xen-dom0-bridge.cfg \
    file://xen-dom0-display.cfg \
    file://xen-dom0-can.cfg \
    "
