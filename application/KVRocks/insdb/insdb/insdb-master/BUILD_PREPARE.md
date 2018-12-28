InSDB build Prepare...

#Dependency
 -c++14
 -folly facebook libary (also need googletest library for test).
 -gcc 4.9+ and boost library compiled with c++14 support
 -libdouble-conversion-dev
 -libgoogle-glog-dev
 -libdl

##googletest library install
googletest is required to build and run folly's test code.  You can download
it from https://github.com/google/googletest/archive/release-1.8.0.tar.gz
The following commands can be used to download and install it:

```
wget https://github.com/google/googletest/archive/release-1.8.0.tar.gz && \
tar zxf release-1.8.0.tar.gz && \
rm -f release-1.8.0.tar.gz && \
cd googletest-release-1.8.0 && \
cmake configure . && \
make && \
make install
```


## Install required library

The following packages are required (feel free to cut and paste the apt-get
command below):

```
sudo apt-get install \
    g++ \
    cmake \
    libboost-all-dev \
    libevent-dev \
    libdouble-conversion-dev \
    libgoogle-glog-dev \
    libgflags-dev \
    libiberty-dev \
    liblz4-dev \
    liblzma-dev \
    libsnappy-dev \
    make \
    zlib1g-dev \
    binutils-dev \
    libjemalloc-dev \
    libssl-dev \
    libc6-dev \
    pkg-config
```

If advanced debugging functionality is required, use:

```
sudo apt-get install \
    libunwind8-dev \
    libelf-dev \
    libdwarf-dev
```

Install folly, folly need to be compiled with -fPIC, so after download you need to chagne Cmake files.

Download

```
  git clone https://github.com/facebook/folly.git
```
Update Cmake file
```
  vim folly/CMake/FollyCompilerUnix.cmake
  (add line -fPIC in target_compile_options)
```
Build
```
  cd folly
  mkdir _build && cd _build
  cmake configure ..
  make -j $(nproc)
  make install

