
./stoposd.sh
echo "formatting,..."
./prep.sh
echo "done"
rm -rf /tmp/dump
#sleep 10
#./kdd_insmod.sh
./startosd.sh kvsstore 

