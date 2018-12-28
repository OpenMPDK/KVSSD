# reference for rocksdb build
git clone https://github.com/facebook/rocksdb.git
cd ~/src/rocksdb
export CPATH=/usr/local/include
export LIBRARY_PATH=/usr/local/lib
make -j 30 static_lib
make -j 30 release
