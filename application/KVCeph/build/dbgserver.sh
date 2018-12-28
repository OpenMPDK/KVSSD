cd ../src/kvsdbg 
rm *.txt
sudo rm -rf /tmp/debug.txt
g++ debugserver.cpp -o debugserver -pthread
./debugserver 1234
read -n1 kbd 
