# Commands run over ssh without a login shell ("ssh root@board xl console
# rtos1") get dropbear's built-in PATH, which has no sbin: xl, ip and ifup
# are not found. Use the same PATH as a login shell.
do_configure:append() {
    echo '#define DEFAULT_PATH "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"' > ${B}/localoptions.h
}
