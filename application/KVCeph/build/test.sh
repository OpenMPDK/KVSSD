sudo CEPH_DEV=1 ./bin/ceph osd pool create rbd 100
sudo CEPH_DEV=1 ./bin/ceph osd pool application enable rbd cephfs
# size: # of replicas
sudo CEPH_DEV=1 ./bin/ceph osd pool set rbd size 1
# min_size: # of replicas to be written before sending an ACK to a client 
sudo CEPH_DEV=1 ./bin/ceph osd pool set rbd min_size 1 

#sudo ./bin/rados bench -p rbd -b 4096 --max-objects 64 --run-name m -t 64 100 write --no-cleanup
sudo ./bin/rados bench -p rbd -b 4096 --max-objects 10000000 --run-name m -t 64 40 write --no-cleanup
