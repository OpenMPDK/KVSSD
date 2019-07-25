for i in $(seq 1 256)
do
    echo KKKKHONG${i}
    ./kv_store_16 -d /dev/nvme0n1 -k KKKKHONG${i} 
done
