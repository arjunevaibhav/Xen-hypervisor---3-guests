# Start Xen with Linux as dom0 on the Tinker Board (RK3288). EXPERIMENTAL.
# Installed twice: as /boot.scr, which U-Boot's distro boot runs at power-on,
# and as /xen-boot.scr for the U-Boot prompt (nothing is saved):
#   load mmc 1:6 ${scriptaddr} /xen-boot.scr; source ${scriptaddr}
# Linux without Xen, from the U-Boot prompt:
#   sysboot mmc 1:6 any ${scriptaddr} /native/extlinux.conf

# Enter the kernel (here: Xen) in non-secure HYP mode for this boot only
setenv bootm_boot_mode nonsec

setenv xen_addr 0x02000000
setenv dom0_addr 0x04000000
load mmc 1:6 ${fdt_addr_r} /rk3288-tinker.dtb
load mmc 1:6 ${xen_addr} /xen
load mmc 1:6 ${dom0_addr} /zImage
setenv dom0_size ${filesize}

fdt addr ${fdt_addr_r}
fdt resize 1024
# Dom0 runs non-secure: use the bus DMAC's non-secure interface (ff600000);
# its secure interface (ffb20000, "dmac_bus_s") faults from non-secure.
fdt set /dma-controller@ffb20000 reg <0x0 0xff600000 0x0 0x4000>
# Keep U-Boot's area (0x79f80000-0x7fffffff, see bdinfo) away from Xen: U-Boot
# leaves a USB controller running ("usb@ff540000 failed to remove"), and Xen
# would otherwise put its heap there. Without this dom0 hangs ~2 s into boot.
fdt rsvmem add 0x79e00000 0x06200000
# dom0 sees no physical CPU nodes, so the CPU thermal zone cannot bind its
# cpufreq cooling devices and the TSADC driver defers its probe forever: no
# temperature readings and no hardware over-temperature shutdown. Without the
# cooling maps the zone registers (trip points and the critical shutdown stay).
fdt rm /thermal-zones/cpu-thermal/cooling-maps
fdt set /chosen \#address-cells <1>
fdt set /chosen \#size-cells <1>
# sched=credit2 (Xen's default) for dom0 and Android. The RTOS gets CPU 3 in
# its own cpupool with the real-time RTDS scheduler (see rtos1-start): RTDS is
# "experimental" in Xen 4.16 and hit an assertion (rt.c:587) when it scheduled
# all 4 CPUs while a 2-vCPU guest was created.
# dom0_mem=320M: dom0 is the base image plus the Xen backends, QEMU for the
# guests' screens and the display, vehicle and console services (video
# playback is in the media1 guest).
fdt set /chosen xen,xen-bootargs "console=dtuart dtuart=serial2 dom0_mem=320M dom0_max_vcpus=1 sched=credit2 loglvl=all guest_loglvl=all"
fdt mknod /chosen module@4000000
fdt set /chosen/module@4000000 compatible "xen,linux-zimage" "xen,multiboot-module"
fdt set /chosen/module@4000000 reg <${dom0_addr} 0x${dom0_size}>
fdt set /chosen/module@4000000 bootargs "console=hvc0 earlycon=xenboot root=PARTLABEL=root rootwait rw rootfstype=ext4 clk_ignore_unused"

bootz ${xen_addr} - ${fdt_addr_r}

# Only reached if Xen could not be loaded or started
echo "Xen did not start: booting Linux without Xen"
setenv bootm_boot_mode sec
sysboot mmc 1:6 any ${scriptaddr} /native/extlinux.conf
