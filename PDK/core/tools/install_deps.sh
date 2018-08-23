#!/bin/bash
# Please run this script as root.

if [ -s /etc/redhat-release ]; then
    # Includes Fedora, CentOS
    sudo yum install CUnit-devel
    sudo yum install libaio-devel
    sudo yum install tbb
    sudo yum install openssl

    sudo yum install libev-devel

elif [ -f /etc/debian_version ]; then
    # Includes Ubuntu, Debian
    sudo apt-get install libcunit1-dev
    sudo apt-get install libaio-dev
    sudo apt-get install libtbb-dev
    sudo apt-get install openssl

    #boost-program-options

    #aerospike
    sudo apt-get install libev-dev

else
    echo "unsupported system type."
    exit 1
fi
