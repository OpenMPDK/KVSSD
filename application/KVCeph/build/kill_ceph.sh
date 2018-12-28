#!/bin/bash


        echo "terminate ceph in Node_${i}"
        sudo killall -e ceph-mon
        sudo killall -e ceph-osd
        sudo killall -e ceph-mgr

