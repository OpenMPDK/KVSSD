#!/bin/bash

rmmod nvme.ko
rmmod nvme-core.ko

cd ..
modprobe nvme
modprobe nvme-core
