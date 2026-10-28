#!/system/bin/sh
# Android guest network: eth0 is on dom0's xenbr0. Dom0 (192.168.100.1) is
# the gateway and NATs the traffic to the LAN through its own eth0.
ifconfig eth0 192.168.100.3 netmask 255.255.255.0 up
ip route add default via 192.168.100.1 dev eth0

# Android 7 ignores net.dns1: DNS goes through netd's resolver, which needs
# a default network. This kernel has no policy routing, so the network is
# only a resolver context; traffic uses the main table route above.
# Wait for the end of boot: system_server resets netd when it starts, which
# drops a network created before that (google.com did not resolve).
n=0
while [ "$(getprop sys.boot_completed)" != 1 ] && [ $n -lt 300 ]; do
    sleep 1
    n=$((n + 1))
done
ndc network create 60000
ndc resolver setnetdns 60000 "" 8.8.8.8 1.1.1.1
ndc network default set 60000
