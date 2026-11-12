platform_check_image() {
 echo 'RN07 custom slot-1 install: generic sysupgrade is disabled. Use the RN07 installer.' >&2
 return 1
}
platform_do_upgrade() { return 1; }
