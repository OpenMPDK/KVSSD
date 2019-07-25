cmake_minimum_required(VERSION 2.8)

include_directories(BEFORE ${CMAKE_SOURCE_DIR}/utils)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/bench)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/include)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/wrappers)
include_directories(BEFORE ${CMAKE_INCLUDE_PATH})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GNU_SOURCE -D__DEBUG")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -L${CMAKE_LIBRARY_PATH} -std=c++11 -fpermissive -march=native ")
set(COMMON_FLAGS -std=c++11 -g  -MMD -MP -D_FILE_OFFSET_BITS=64 -fPIC  -march=native -fpermissive -fomit-frame-pointer)
set(COMMON_LIB -fPIC -Wall -Wl,--no-as-needed -lev -lssl -lcrypto -lnuma -lbz2 -lm -lsnappy -llz4 -lzstd -lgflags -ljemalloc -lz -ldl -lrt -pthread -march=native)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif(NOT CMAKE_BUILD_TYPE)

if (NOT WIN32)
    set(PTHREAD_LIB pthread)
    set(LIBM m)
    set(LIBZ z)
    set(LIBAIO aio)
    set(LIBDL dl)
    set(LIBJEMALLOC jemalloc)
    set(LIBSNAPPY snappy)
    set(LIBBZ2 bz2)
    set(LIBFDB forestdb)
    set(LIBLDB leveldb)
    set(LIBRDB rocksdb)
    set(LIBKVS kvapi)
    set(LIBCOUCH couchstore)
    set(LIBWT wiredtiger)
    set(LIBSSL ssl)
    set(LIBNUMA numa)
endif(NOT WIN32)

if ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Qunused-arguments -g -fomit-frame-pointer -pthread ")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fomit-frame-pointer -pthread")
    set(LIBRT rt)
endif()

add_executable(fdb_bench
               bench/couch_bench.cc
	       wrappers/couch_fdb.cc
	       utils/avltree.cc
	       utils/stopwatch.cc
	       utils/iniparser.cc
	       utils/crc32.cc
	       utils/memleak.cc
	       utils/zipfian_random.cc
	       utils/keyloader.cc
	       utils/keygen.cc
	       utils/memory.cc)
target_link_libraries(fdb_bench ${PTHREAD_LIB} ${LIBM} ${LIBSNAPPY} ${LIBNUMA} ${LIBFDB})
set_target_properties(fdb_bench PROPERTIES COMPILE_FLAGS "-D__FDB_BENCH")
file(COPY ${CMAKE_SOURCE_DIR}/bench_config.ini DESTINATION ./)

add_executable(couch_bench
               bench/couch_bench.cc
               utils/avltree.cc
               utils/stopwatch.cc
               utils/iniparser.cc
               utils/crc32.cc
               utils/memleak.cc
               utils/zipfian_random.cc
               utils/keyloader.cc
	       utils/memory.cc
               utils/keygen.cc)
target_link_libraries(couch_bench ${PTHREAD_LIB} ${LIBM} ${LIBSNAPPY} ${LIBNUMA} ${LIBCOUCH})
set_target_properties(couch_bench PROPERTIES COMPILE_FLAGS "-D__COUCH_BENCH")
file(COPY ${CMAKE_SOURCE_DIR}/bench_config.ini DESTINATION ./)

add_executable(leveldb_bench
               bench/couch_bench.cc
	       wrappers/couch_leveldb.cc
	       utils/avltree.cc
	       utils/stopwatch.cc
	       utils/iniparser.cc
	       utils/crc32.cc
	       utils/memleak.cc
	       utils/zipfian_random.cc
	       utils/keyloader.cc
	       utils/memory.cc
	       utils/keygen.cc)
target_link_libraries(leveldb_bench ${PTHREAD_LIB} ${LIBM} ${LIBSNAPPY} ${LIBNUMA} ${LIBLDB})
set_target_properties(leveldb_bench PROPERTIES COMPILE_FLAGS "-D__LEVEL_BENCH")
file(COPY ${CMAKE_SOURCE_DIR}/bench_config.ini DESTINATION ./)

add_executable(wt_bench
               bench/couch_bench.cc
	       wrappers/couch_wt.cc
	       utils/avltree.cc
	       utils/stopwatch.cc
	       utils/iniparser.cc
	       utils/crc32.cc
	       utils/memleak.cc
	       utils/zipfian_random.cc
	       utils/keyloader.cc
	       utils/memory.cc
	       utils/keygen.cc)
target_link_libraries(wt_bench ${PTHREAD_LIB} ${LIBM} ${LIBSNAPPY} ${LIBNUMA} ${LIBWT})
set_target_properties(wt_bench PROPERTIES COMPILE_FLAGS "-D__WT_BENCH")
file(COPY ${CMAKE_SOURCE_DIR}/bench_config.ini DESTINATION ./)

add_executable(rocksdb_bench
               bench/couch_bench.cc
               wrappers/couch_rocksdb.cc
	       utils/thpool.cc
               utils/avltree.cc
               utils/stopwatch.cc
               utils/iniparser.cc
               utils/crc32.cc
               utils/memleak.cc
               utils/zipfian_random.cc
               utils/keyloader.cc
	       utils/memory.cc
               utils/keygen.cc)
target_include_directories(rocksdb_bench PRIVATE ${CMAKE_SOURCE_DIR}/rocksdb/include)
set(RDB_LIB -L${CMAKE_SOURCE_DIR}/rocksdb -lrocksdb)
target_link_libraries(rocksdb_bench ${COMMON_LIB} ${RDB_LIB})
set_target_properties(rocksdb_bench PROPERTIES COMPILE_FLAGS "-D__ROCKS_BENCH")
target_compile_options(rocksdb_bench PRIVATE ${COMMON_FLAGS})
file(COPY ${CMAKE_SOURCE_DIR}/bench_config.ini DESTINATION ./)

add_executable(kvdb_bench
               bench/couch_bench.cc
               wrappers/couch_kvdb.cc
	       utils/thpool.cc
               utils/avltree.cc
               utils/stopwatch.cc
               utils/iniparser.cc
               utils/crc32.cc
               utils/memleak.cc
               utils/zipfian_random.cc
               utils/keyloader.cc
               utils/memory.cc
               utils/keygen.cc)
target_include_directories(kvdb_bench PRIVATE ${CMAKE_INCLUDE_DIR})
set(KVDB_LIB -ltcmalloc ${CMAKE_LIBRARY_PATH} -lkvdb -linsdb -lfolly -lglog -lgflags -ldouble-conversion)
target_link_libraries(kvdb_bench ${COMMON_LIB} ${KVDB_LIB})
set_target_properties(kvdb_bench PROPERTIES COMPILE_FLAGS "-D__KVDB_BENCH")
target_compile_options(kvdb_bench PRIVATE ${COMMON_FLAGS})
#file(COPY ${CMAKE_SOURCE_DIR}/bench_config-kvdb.ini DESTINATION ./)

add_executable(kv_bench
               bench/couch_bench.cc
	       wrappers/couch_kv.cc
	       utils/thpool.cc
	       utils/avltree.cc
	       utils/stopwatch.cc
	       utils/iniparser.cc
	       utils/crc32.cc
	       utils/memleak.cc
	       utils/zipfian_random.cc
	       utils/keyloader.cc
	       utils/memory.cc
	       utils/keygen.cc)
#set(KVS_LIB -L${CMAKE_LIBRARY_PATH} -lkvapi)
target_link_libraries(kv_bench ${COMMON_LIB} ${CMAKE_LIBRARY_PATH})
set_target_properties(kv_bench PROPERTIES COMPILE_FLAGS "-D__KV_BENCH")
target_compile_options(kv_bench PRIVATE ${COMMON_FLAGS})

add_executable(as_bench
               bench/couch_bench.cc
	       wrappers/couch_aerospike.cc
	       utils/thpool.cc
	       utils/avltree.cc
	       utils/stopwatch.cc
	       utils/iniparser.cc
	       utils/crc32.cc
	       utils/memleak.cc
	       utils/zipfian_random.cc
	       utils/keyloader.cc
	       utils/keygen.cc
	       utils/memory.cc)
set(AS_LIB -L${CMAKE_SOURCE_DIR}/lib -laerospike -laerospike-common)
target_link_libraries(as_bench ${COMMON_LIB} ${AS_LIB})
set_target_properties(as_bench PROPERTIES COMPILE_FLAGS "-D__AS_BENCH")
target_compile_options(as_bench PRIVATE ${COMMON_FLAGS})
#file(COPY ${CMAKE_SOURCE_DIR}/bench_config.ini DESTINATION ./)

include_directories(${CMAKE_SOURCE_DIR}/spdk/spdk/include)
include_directories(${CMAKE_SOURCE_DIR}/spdk/rocksdb/include)
add_executable(blobfs_rocksdb_bench
               bench/couch_bench.cc
	       wrappers/couch_blobfs_rocksdb.cc
	       utils/avltree.cc
	       utils/stopwatch.cc
	       utils/iniparser.cc
	       utils/crc32.cc
	       utils/memleak.cc
	       utils/zipfian_random.cc
	       utils/keyloader.cc
	       utils/memory.cc
	       utils/keygen.cc)
set(SPDK_DIR ${CMAKE_SOURCE_DIR}/spdk)
set(RDB_SPDK_LIB -L${SPDK_DIR}/rocksdb -lrocksdb)
set(ROCKS_SPDK_LIB -L${SPDK_DIR}/spdk/dpdk/build/lib -L${SPDK_DIR}/rocksdb rocksdb.a -Wl,--start-group -Wl,--whole-archive librte_eal.a librte_mempool.a librte_ring.a librte_mempool_ring.a -Wl,--end-group -Wl,--no-whole-archive -L${SPDK_DIR}/spdk/build/lib libspdk_event.a libspdk_blobfs.a libspdk_blob.a libspdk_blob_bdev.a libspdk_conf.a libspdk_trace.a libspdk_log.a -Wl,--start-group -Wl,--whole-archive libspdk_event_bdev.a libspdk_event_copy.a libspdk_event_rpc.a libspdk_copy_ioat.a libspdk_ioat.a libspdk_bdev.a libspdk_bdev_malloc.a libspdk_bdev_null.a libspdk_bdev_nvme.a libspdk_nvme.a libspdk_vbdev_error.a libspdk_vbdev_gpt.a libspdk_vbdev_split.a libspdk_rpc.a -Wl,--end-group -Wl,--no-whole-archive libspdk_copy.a libspdk_util.a libspdk_env_dpdk.a libspdk_jsonrpc.a libspdk_json.a)
target_link_libraries(blobfs_rocksdb_bench ${COMMON_LIB} ${ROCKS_SPDK_LIB})
set(ROCKS_SPDK_FLAGS -DROCKSDB_JEMALLOC -D__BLOBFS_ROCKS_BENCH)
set_target_properties(blobfs_rocksdb_bench PROPERTIES COMPILE_FLAGS "-D__BLOBFS_ROCKS_BENCH")
target_compile_options(blobfs_rocksdb_bench PRIVATE ${COMMON_FLAGS} ${ROCKS_SPDK_FLAGS})
