#!/bin/bash

rmmod nvme
rmmod nvme_core

insmod nvme-core.ko
insmod nvme.ko
