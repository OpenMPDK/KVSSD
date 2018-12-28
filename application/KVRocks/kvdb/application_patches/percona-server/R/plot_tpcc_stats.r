#install.packages("reshape")

library(dplyr)
library(ggplot2)
library(reshape)

library(plotly)
options(scipen=999)


get_vector_delta <- function(data) {
  rows = length(data)
  list <- rep(0, rows -1)
  for (i in 2:rows) {
    list[i-1] = data[i] - data[i-1]
  }
  list
}


## set up a new device for plot
dev.new()
#======================================================
## for ramdisk tc.log
############## trx

# plot_tpcc <- function(datapath, iskvdb)

# log.perf29_to_dev36.kvrocks_1hr1k_fwk_20181112_19_51_06 
# log.perf31_to_dev35.kvrocks_1hr1k_fw49_20181112_17_08_12 
# log.perf29_to_dev36.kvrocks_1hr4k_fwk_20181113_12_13_02 
# log.perf28_to_dev35.kvrocks_1hr4k_fw49_20181113_12_14_21 

## kv
#datapath_kv =  "c:/docs/work/kvdb/tpcc/1mysql/log.perf29_to_dev36.kvrocks_1hr1k_fwk_20181112_19_51_06/"
#datapath_kv =  "c:/docs/work/kvdb/tpcc/1mysql/log.perf31_to_dev35.kvrocks_1hr1k_fw49_20181112_17_08_12/"
#datapath_kv =  "c:/docs/work/kvdb/tpcc/1mysql/log.perf29_to_dev36.kvrocks_1hr4k_fwk_20181113_12_13_02/"
#datapath_kv =  "c:/docs/work/kvdb/tpcc/1mysql/log.perf28_to_dev35.kvrocks_1hr4k_fw49_20181113_12_14_21/"
datapath_kv =  "c:/docs/work/kvdb/tpcc/1mysql/log.perf33_to_dev36.kvrocks_1hr4k_a64_kc400k_fw53_20181114_13_05_46/"
#datapath_kv =  "c:/docs/work/kvdb/tpcc/1mysql/log.perf33_to_dev35.kvrocks_1hr4k_a64_kc4m_fw53_20181114_13_27_11/"

## 4G
datapath_my =  "c:/docs/work/kvdb/tpcc/1mysql/log.perf29_to_dev36.myrocks_1hr20181112_17_14_49/"


#######################
## tpcc
trx_kv <- read.csv(paste0(datapath_kv, "trx.csv"), header = TRUE, sep = ",", strip.white = TRUE, stringsAsFactors=FALSE)
trx_my <- read.csv(paste0(datapath_my, "trx.csv"), header = TRUE, sep = ",", strip.white = TRUE, stringsAsFactors=FALSE)
rows = min(nrow(trx_kv), nrow(trx_my))

neworder_kv <- trx_kv$neworder_trx[1:rows]
neworder_my <- trx_my$neworder_trx[1:rows]

trxdf <- data.frame(trx_kv$time[1:rows], neworder_kv, neworder_my)
colnames(trxdf) = c("time", "KvRocks ", "MyRocks")

meltdf <- melt(trxdf,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('Transactions / 10s') +
  labs(title="TPCC W100 TRX Rate/10s KvRocks vs. MyRocks") +
  labs(colour="Type")
p


#######################
## tpcc

trxdf <- data.frame(trx_kv$time[1:rows], neworder_kv)
colnames(trxdf) = c("time", "KvRocks ")

meltdf <- melt(trxdf,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('Transactions / 10s') +
  labs(title="TPCC W100 TRX Rate/10s KvRocks") +
  labs(colour="Type")
p


##################################################
## transaction
## compare 2 kv and 1my
datapath_kv1 =  "c:/docs/work/kvdb/tpcc/1mysql/log.perf33_to_dev36.kvrocks_1hr4k_a64_kc400k_fw53_20181114_13_05_46/"
datapath_kv2 =  "c:/docs/work/kvdb/tpcc/1mysql/log.perf33_to_dev35.kvrocks_1hr4k_a64_kc4m_fw53_20181114_13_27_11/"
datapath_my =  "c:/docs/work/kvdb/tpcc/1mysql/log.perf29_to_dev36.myrocks_1hr20181112_17_14_49/"

trx_kv1 <- read.csv(paste0(datapath_kv1, "trx.csv"), header = TRUE, sep = ",", strip.white = TRUE, stringsAsFactors=FALSE)
trx_kv2 <- read.csv(paste0(datapath_kv2, "trx.csv"), header = TRUE, sep = ",", strip.white = TRUE, stringsAsFactors=FALSE)
trx_my <- read.csv(paste0(datapath_my, "trx.csv"), header = TRUE, sep = ",", strip.white = TRUE, stringsAsFactors=FALSE)
rows = min(nrow(trx_kv1), nrow(trx_kv2))

neworder_kv1 <- trx_kv1$neworder_trx[1:rows]
neworder_kv2 <- trx_kv2$neworder_trx[1:rows]
neworder_my <- trx_my$neworder_trx[1:rows]

trxdf <- data.frame(trx_kv1$time[1:rows], neworder_kv1, neworder_kv2, neworder_my)
colnames(trxdf) = c("time", "KvRocks sktable low: 400K ", "KvRocks sktable low: 4M", "MyRocks")

meltdf <- melt(trxdf,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('Transactions / 10s') +
  labs(title="TPCC W100 TRX Rate/10s using different sktable count\nrequest size: 4K, alignment 64, FW 53") +
  labs(colour="Type")
p

##########################################################
###### cpu and mem
memdf_kv1 <- read.csv(paste0(datapath_kv1, "mem.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)
memdf_kv2 <- read.csv(paste0(datapath_kv2, "mem.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)
memdf_my <- read.csv(paste0(datapath_my, "mem.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)

cpudf_kv1 <- read.csv(paste0(datapath_kv1, "cpu.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)
cpudf_kv2 <- read.csv(paste0(datapath_kv2, "cpu.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)
cpudf_my <- read.csv(paste0(datapath_my, "cpu.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)

# get common len
lens = c(nrow(memdf_kv1), nrow(memdf_my), nrow(cpudf_kv1), nrow(cpudf_my))
len1 = min(lens) - 3
mem_kv1 <- memdf_kv1$X.memused[1:len1]
mem_kv2 <- memdf_kv2$X.memused[1:len1]
mem_my <- memdf_my$X.memused[1:len1]
cpu_kv1 <- 100 - cpudf_kv1$X.idle[1:len1]
cpu_kv2 <- 100 - cpudf_kv2$X.idle[1:len1]
cpu_my <- 100 - cpudf_my$X.idle[1:len1]


timeline1 = seq(1, len1, by=1)
timeline1 = 2 * timeline1

cpumem_df <- data.frame(timeline1, cpu_kv1, cpu_kv2, cpu_my, mem_kv1, mem_kv2, mem_my)
colnames(cpumem_df) = c("time", "KvRocks CPU sktable low: 400K",  "KvRocks CPU sktable low: 4M",  "MyRocks CPU",
                        "KvRocks mem sktable low: 400K", "KvRocks mem sktable low: 4M", "MyRocks mem")

meltdf <- melt(cpumem_df,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('Percentage') +
  labs(title="CPU and Memory Consumption KvRocks vs. MyRocks\nusing different sktable count\nrequest size: 4K, alignment 64, FW 53") +
  labs(colour="Type")
p

###=========================================================







#=====================
#### kv operations
syncput <- read.csv(paste0(datapath_kv, "sync_put.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)
syncget <- read.csv(paste0(datapath_kv, "sync_get.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)
sync_completed <- read.csv(paste0(datapath_kv, "sync_completed.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)
async_completed <- read.csv(paste0(datapath_kv, "async_completed.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)
sync_submitted <- read.csv(paste0(datapath_kv, "sync_submitted.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

asyncput <- read.csv(paste0(datapath_kv, "async_put.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)
asyncget <- read.csv(paste0(datapath_kv, "async_get.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)
asyncdel <- read.csv(paste0(datapath_kv, "async_del.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)
canceled <- read.csv(paste0(datapath_kv, "canceled.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

driver_pending <- read.csv(paste0(datapath_kv, "driver_pending.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)
device_pending <- read.csv(paste0(datapath_kv, "device_pending.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

total_async_recv <- read.csv(paste0(datapath_kv, "total_async_req_recv.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)
total_async_submit <- read.csv(paste0(datapath_kv, "total_async_req_submitted.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

cpudf_kv <- read.csv(paste0(datapath_kv, "cpu.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)
len1 = min(lens) - 3
cpu_kv <- 100 - cpudf_kv$X.idle[1:len1]

lens = c(nrow(asyncput), nrow(asyncget), nrow(asyncdel), 
         nrow(total_async_recv), nrow(total_async_submit), 
         nrow(total_async_recv), nrow(canceled), nrow(iops),
         nrow(syncput),
         nrow(syncget),
         nrow(sync_completed),
         nrow(async_completed),
         nrow(sync_submitted),
         nrow(driver_pending),
         nrow(device_pending),
         nrow(cpu_kv)
         )

min_len = min(lens)
#min_len = 1600 


## delta assuming sampling interval is 2 seconds
## async
put_delta = get_vector_delta(asyncput$V2)/2
get_delta = get_vector_delta(asyncget$V2)/2
del_delta = get_vector_delta(asyncdel$V2)/2

## sync
sync_get_delta = get_vector_delta(syncget$V2)/2
sync_put_delta = get_vector_delta(syncput$V2)/2
sync_submitted_delta = get_vector_delta(sync_submited$V2)/2
sync_completed_delta = get_vector_delta(sync_completed$V2)/2

driver_queue = driver_pending$V2
device_queue = device_pending$V2

total_iops <- sync_completed$V2[1:min_len] + async_completed$V2[1:min_len]
iops = get_vector_delta(total_iops)/2

## total async request pending
total_async_diff = (total_async_recv$V2 - total_async_submit$V2 - canceled$V2)/2


## plot out various kv operations
timeline = seq(1, min_len, by=1)
timeline = 2 * timeline
kvops_df <- data.frame(timeline, 
                       #put_delta[1:min_len], get_delta[1:min_len], del_delta[1:min_len], total_async_diff[1:min_len], 
                       iops[1:min_len],
                       #sync_get_delta[1:min_len], sync_put_delta[1:min_len], sync_submitted_delta[1:min_len], sync_completed_delta[1:min_len],
                       cpu_kv[1:min_len]*1000)
colnames(kvops_df) = c("time", 
                       # "async put", "async get", "async delete", "async pending submit", 
                       "IOPS",
                       #"sync get", "sync put", "sync submitted", "sync completed",
                        "CPU Busy"
                       )

meltdf <- melt(kvops_df,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('KV operations/s') +
  labs(title="KvRocks KV Operations") +
  labs(colour="Type")
p


#############################################
## pending
min_len = 300
timeline = seq(1, min_len, by=1)
timeline = 2 * timeline
kvops_df <- data.frame(timeline, 
                       #put_delta[1:min_len], get_delta[1:min_len], del_delta[1:min_len], total_async_diff[1:min_len], 
                       driver_queue[1:min_len],
                       device_queue[1:min_len],
                       #sync_get_delta[1:min_len], sync_put_delta[1:min_len], sync_submitted_delta[1:min_len], sync_completed_delta[1:min_len],
                       cpu_kv[1:min_len] * 50)
colnames(kvops_df) = c("time", 
                       # "async put", "async get", "async delete", "async pending submit", 
                       "Driver Queue pending",
                       "Device Queue pending",
                       #"sync get", "sync put", "sync submitted", "sync completed",
                        "CPU Busy"
                       )

meltdf <- melt(kvops_df,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('KV operations/s') +
  labs(title="KvRocks KV Operations") +
  ylim(0,3000) +
  labs(colour="Type")
p


## save data
# write.table(kvops_df, file = "c:/docs/work/kvdb/results/kvops.csv", sep = ",", row.names = TRUE)
 

###### cpu and mem
memdf_kv <- read.csv(paste0(datapath_kv, "mem.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)
memdf_my <- read.csv(paste0(datapath_my, "mem.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)

cpudf_kv <- read.csv(paste0(datapath_kv, "cpu.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)
cpudf_my <- read.csv(paste0(datapath_my, "cpu.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)

# get common len
lens = c(nrow(memdf_kv), nrow(memdf_my), nrow(cpudf_kv), nrow(cpudf_my))
len1 = min(lens) - 3
mem_kv <- memdf_kv$X.memused[1:len1]
mem_my <- memdf_my$X.memused[1:len1]
cpu_kv <- 100 - cpudf_kv$X.idle[1:len1]
cpu_my <- 100 - cpudf_my$X.idle[1:len1]


timeline1 = seq(1, len1, by=1)
timeline1 = 2 * timeline1

cpumem_df <- data.frame(timeline1, cpu_kv, cpu_my, mem_kv, mem_my)
colnames(cpumem_df) = c("time", "KvRocks CPU", "MyRocks CPU", "KvRocks mem", "MyRocks mem")

meltdf <- melt(cpumem_df,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('Percentage') +
  labs(title="CPU and Memory Consumption KvRocks vs. MyRocks") +
  labs(colour="Type")
p


###############################################
########## disk (root drive)
disk_kv <- read.csv(paste0(datapath_kv, "disk.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)
disk_my <- read.csv(paste0(datapath_my, "disk.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)

root_drive_kv <- disk_kv %>% filter(DEV == "sde")
root_drive_my <- disk_my %>% filter(DEV == "sde")

lens = c(nrow(disk_kv), nrow(disk_my))
#len = min(lens) - 3
len = 2421

rootr_kv = as.numeric(root_drive_kv$rd_sec.s[1:len])
rootw_kv = as.numeric(root_drive_kv$wr_sec.s[1:len])

rootr_my = as.numeric(root_drive_my$rd_sec.s[1:len])
rootw_my = as.numeric(root_drive_my$wr_sec.s[1:len])


########### for kv rocks
timeline1 = seq(1, len, by=1)
timeline1 = timeline1 * 2
diskstats <- data.frame(timeline1, rootr_kv, rootw_kv)
colnames(diskstats) = c("time", "root drive read", "root drive write")


meltdf <- melt(diskstats,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('Sectors/s') +
  labs(title="Root Drive Activity KvRocks") +
  labs(colour="Type")
p

########### for kv rocks
timeline1 = seq(1, len, by=1)
timeline1 = timeline1 * 2
diskstats <- data.frame(timeline1, rootr_my, rootw_my)
colnames(diskstats) = c("time", "root drive read", "root drive write")


########### for my rocks
meltdf <- melt(diskstats,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('Sectors/s') +
  labs(title="Root Drive Activity MyRocks") +
  labs(colour="Type")
p


###############################################
########## disk (data dir drive)
disk_kv <- read.csv(paste0(datapath_kv, "disk.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)
disk_my <- read.csv(paste0(datapath_my, "disk.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)

data_drive_kv <- disk_kv %>% filter(DEV == "nvme1n1")
data_drive_my <- disk_my %>% filter(DEV == "nvme1n1")

lens = c(nrow(disk_kv), nrow(disk_my))
#len = min(lens) - 3
len = 2421

datar_kv = as.numeric(data_drive_kv$rd_sec.s[1:len])
dataw_kv = as.numeric(data_drive_kv$wr_sec.s[1:len])

datar_my = as.numeric(data_drive_my$rd_sec.s[1:len])
dataw_my = as.numeric(data_drive_my$wr_sec.s[1:len])


########### for kv rocks
timeline1 = seq(1, len, by=1)
timeline1 = timeline1 * 2
diskstats <- data.frame(timeline1, datar_kv, dataw_kv)
colnames(diskstats) = c("time", "data drive read", "data drive write")


meltdf <- melt(diskstats,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('Sectors/s') +
  labs(title="Data Drive Activity KvRocks") +
  labs(colour="Type")
p

########### for my rocks
timeline1 = seq(1, len, by=1)
timeline1 = timeline1 * 2
diskstats <- data.frame(timeline1, datar_my, dataw_my)
colnames(diskstats) = c("time", "data drive read", "data drive write")


########### for my rocks
meltdf <- melt(diskstats,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('Sectors/s') +
  labs(title="Root Drive Activity MyRocks") +
  labs(colour="Type")
p

