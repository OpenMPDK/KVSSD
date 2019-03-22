sudo nvme format /dev/nvme0n1 -s0 -n1
ulimit -n 1000000
#echo "formatted"
rm -rf /tmp/dump
#sudo ./kdd_insmod.sh
rm -rf ./build/dev/osd0
rm -rf ./out
