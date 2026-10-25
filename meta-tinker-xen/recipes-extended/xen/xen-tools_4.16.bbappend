# Let xenconsoled also log each guest's console to
# /var/log/xen/console/guest-<name>.log (xl console keeps working): the
# HDMI cluster dashboard reads FreeRTOS's vehicle alerts from there.
# /var/log is recreated at boot (volatile), and xenconsoled does not create
# the console/ directory itself.
do_install:append:tinker-board() {
    sed -i 's|^#XENCONSOLED_TRACE=.*|XENCONSOLED_TRACE=guest|' ${D}${sysconfdir}/default/xencommons
    echo "d root root 0755 /var/log/xen/console none" >> ${D}${sysconfdir}/default/volatiles/99_xen
}
