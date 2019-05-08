#nvme format /dev/nvme2n1 -s1 -n1
#echo "formatted"
use_gdb=$1

sudo rm -rf out

./stoposd.sh
echo "formatting,..."
./prep.sh
echo "done"
rm -rf /tmp/dump
#sleep 10
#./kdd_insmod.sh
echo "## START OSD: kvsstore, gdbserver? $use_gdb  ----------------------------------------"
./startosd.sh kvsstore $use_gdb

#./test.sh
#./nvm_insmod.sh
