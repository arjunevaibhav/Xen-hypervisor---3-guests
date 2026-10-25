SUMMARY = "Automotive bench: simulated (or listen-only real) CAN bus into the FreeRTOS guest"
DESCRIPTION = "vehicle-sim sends a test drive cycle as CAN frames on vcan0; \
can2rtos reads the bus (receive only) and passes speed/rpm to the FreeRTOS \
guest's vehicle task, which checks them in real time (overspeed, redline, \
signal loss). With a real CAN adapter the bus is used in listen-only mode."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://vehicle-sim.c file://can2rtos.c file://vehicle-bench.init"
S = "${WORKDIR}"

inherit update-rc.d

INITSCRIPT_NAME = "vehicle-bench"
# after xen-guests (90)
INITSCRIPT_PARAMS = "start 92 2 3 4 5 . stop 08 0 1 6 ."

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -Wall -Wextra -o vehicle-sim ${S}/vehicle-sim.c
    ${CC} ${CFLAGS} ${LDFLAGS} -Wall -Wextra -o can2rtos ${S}/can2rtos.c
}

do_install() {
    install -d ${D}${bindir} ${D}${sysconfdir}/init.d
    install -m 0755 ${B}/vehicle-sim ${B}/can2rtos ${D}${bindir}/
    install -m 0755 ${S}/vehicle-bench.init ${D}${sysconfdir}/init.d/vehicle-bench
}

RDEPENDS:${PN} = "iproute2 kernel-module-vcan xen-tools-xenstore xen-tools-xl"
