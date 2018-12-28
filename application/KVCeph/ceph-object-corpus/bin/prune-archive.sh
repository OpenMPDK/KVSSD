#!/bin/sh

[ -d archive ] || exit 1

for d in archive/*/objects/*; do bin/prune.sh $d ; done