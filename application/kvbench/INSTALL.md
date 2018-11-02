
## Compilation and Build

We use [CMake](http://www.cmake.org/cmake/) to provide the build support for a wide range of platforms. Please follow the instructions below to install CMake in your target platform.

* **Ubuntu**

    `sudo apt-get install cmake`

* **CentoOS**
    `sudo yum install cmake`

Once CMake is installed, please follow the instructions below to compile and build the benchmark program on Ubuntu:

`cd ForestDB-Benchmark`

`mkdir build`

`cd build`

`cmake ../`

Note that DB libraries that you want to evaluate with the benchmark program should be installed before executing `cmake` command.

If you have libraries or include header files in non-standard locations, you can assign custom paths as follows:

`cmake -DCMAKE_INCLUDE_PATH=[your_include_path] -DCMAKE_LIBRARY_PATH=[your_library_path] ../`

(We recommend that all custom paths should be absolute paths to avoid potential problems.)

After that, you can build each benchmark program using commands below:

`make fdb_bench`: ForestDB benchmark

`make couch_bench`: Couchstore benchmark

`make leveldb_bench`: LevelDB benchmark

`make rocksdb_bench`: RocksDB benchmark

`make wt_bench`: WiredTiger benchmark

`make kv_bench`: KV SSD benchmark

If the following error occurs due to the custom library path,

`error while loading shared libraries: [library_filename]: cannot open shared object file: No such file or directory`

you need to manually set the environmental variable `LD_LIBRARY_PATH` as follows:

`export LD_LIBRARY_PATH=[your_library_path]`
