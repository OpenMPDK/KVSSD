#!/bin/sh -e

dir=$1
max=$2

usage()
{
    echo "usage: $0 <dir> [max items]"
    exit 1
}

[ -z "$dir" ] && usage
[ -d $dir ] || usage
[ -z "$max" ] && max=10


num=`ls $dir | wc -l`
echo num $num

if [ $num -gt $max ]; then
    kill=$(($num - $max))
    echo will remove $kill

    # keep biggest and smallest 2
    ( cd $dir && ls -S | tail -n +2 | head -n -2 | sort | head -n $kill | xargs rm )
fi




