osdsocket=$(cat .asok)/osd.0.asok
sudo ./bin/ceph --admin-daemon $osdsocket perf dump
