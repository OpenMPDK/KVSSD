set -e
sudo make VERBOSE=1 ceph_test_kvsstore -j 64 > compile.txt && sudo nvme format /dev/nvme0n1 -s 0 -n 1 && sudo rm -rf kvsstore.test_temp_dir && sudo ./bin/ceph_test_kvsstore --gtest_break_on_failure  2>&1 | tee log.txt 
vi log.txt
