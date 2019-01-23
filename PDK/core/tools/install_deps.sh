#!/bin/bash
# Please run this script as root.

if [ -s /etc/redhat-release ]; then
    # Includes Fedora, CentOS
    sudo yum install CUnit-devel
    sudo yum install libaio-devel
    sudo yum install tbb tbb-devel
    sudo yum install openssl
    sudo yum install numactl-devel

    sudo yum install libev-devel
    sudo yum install boost boost-thread boost-devel

elif [ -f /etc/debian_version ]; then
    # Includes Ubuntu, Debian
    sudo apt-get install g++
    sudo apt-get install cmake
    sudo apt-get install libcunit1-dev
    sudo apt-get install libaio-dev
    sudo apt-get install libtbb-dev
    sudo apt-get install openssl
    sudo apt-get install libnuma-dev

    #boost-program-options
    sudo apt-get install libboost-dev

    #aerospike
    sudo apt-get install libev-dev

else
    echo "unsupported system type."
    exit 1
fi
