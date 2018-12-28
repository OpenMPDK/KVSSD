

#format nvme device; change <nvme path> to a kernel device path.
nvme format <nvme path> -s0 -n1

echo "formatted"

rm -rf /tmp/dump

# install kernel drivers
./kdd_insmod.sh
