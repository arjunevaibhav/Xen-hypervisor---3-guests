FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

# start_getty: do not run a getty on the board UART when booted under Xen
# (the UART belongs to the hypervisor; dom0 uses hvc0)
