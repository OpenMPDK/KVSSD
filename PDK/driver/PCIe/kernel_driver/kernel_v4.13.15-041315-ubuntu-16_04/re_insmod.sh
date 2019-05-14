#!/bin/bash

sudo rmmod nvme
sudo rmmod nvme_core

sudo insmod nvme-core.ko
sudo insmod nvme.ko
