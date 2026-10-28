#!/system/bin/sh
# Android sound to dom0's audio-mixer (TCP 192.168.100.1:5602).
#
# The emulator audio HAL (audio.primary.goldfish, also loaded as
# audio.primary.xen) writes 44.1 kHz stereo 16-bit PCM to /dev/eac, which
# here is a pipe instead of the emulator's sound device. netcat sends what
# comes out of it to dom0; the mixer reads only as fast as the speaker
# plays, so TCP flow control (through the pipe) paces the HAL.
#
#   xen-audio.sh setup   make /dev/eac (before audioserver starts)
#   xen-audio.sh         send the sound to dom0, reconnecting when needed

# mkfifo and netcat are in toybox, without links of their own in /system/bin
if [ "$1" = setup ]; then
    [ -p /dev/eac ] || { rm -f /dev/eac; toybox mkfifo -m 0660 /dev/eac; }
    toybox chown root:audio /dev/eac
    exit 0
fi

# small TCP send buffers: sound waiting here is delay
echo "4096 16384 32768" > /proc/sys/net/ipv4/tcp_wmem
while :; do
    toybox netcat 192.168.100.1 5602 < /dev/eac
    sleep 2
done
