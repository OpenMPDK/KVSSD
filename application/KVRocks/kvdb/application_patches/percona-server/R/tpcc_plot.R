#!/usr/bin/Rscript
## this program assumes cpu/mem sampling frequence is 1s
args <- commandArgs(trailingOnly = TRUE)

# print first two command line arguments
print(args[1])
if (length(args) != 5) {
    print("need 4 argument");
    print("<1. max_request_size>")
    print("<2. alignment_size>")
    print("<3. key_count>")
    print("<4. sktable_cache_count(df)>")
    print("<5. logpath")
    stop();
}

max_request_size = args[1]
alignment = args[2]
keycount = args[3]
sktablecache_count = args[4]
logpath=paste0(args[5],  "/")

runtype = sprintf("mr%s_a%s_kc%s_df%s", max_request_size, alignment, keycount, sktablecache_count)

print(paste0("set current working directory: ", logpath))
setwd(logpath)

#print(args[2]) 
# Simple addition #print(as.double(args[1]) + as.double(args[2]))

#install.packages("reshape")

library(dplyr)
library(ggplot2)
library(reshape)

#library(plotly)
options(scipen=999)


get_vector_delta <- function(data) {
  rows = length(data)
  list <- rep(0, rows -1)
  for (i in 2:rows) {
    list[i-1] = data[i] - data[i-1]
  }
  list
}


#======================================================
############## trx

# plot_tpcc <- function(datapath, iskvdb)

datapath_kv1 = logpath
## tpcc
trx_kv <- read.csv(paste0(datapath_kv1, "trx.csv"), header = TRUE, sep = ",", strip.white = TRUE, stringsAsFactors=FALSE)
rows = nrow(trx_kv)
neworder_kv <- trx_kv$neworder_trx[1:rows]

#######################
## tpcc

trxdf <- data.frame(trx_kv$time[1:rows], neworder_kv)
colnames(trxdf) = c("time", "KvRocks ")

## set up a new device for plot
#dev.new()

meltdf <- melt(trxdf,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('Transactions / 10s') +
  labs(title=paste0("TPCC W100 TRX Rate/10s\nType: ", runtype)) +
  labs(colour="Type")
plotfname = paste0("tpcc_trx_", runtype, ".png")
ggsave(filename=plotfname, plot=p, device=png(height=8, width=10, units = 'in', res=300))

##################################################
# adjust max request size for CPU and MEM
#################################################
###### cpu and mem, sampled every 2 seconds
memdf_kv1 <- read.csv(paste0(datapath_kv1, "mem.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)
cpudf_kv1 <- read.csv(paste0(datapath_kv1, "cpu.log"), skip = 2, header = TRUE, sep = "", strip.white = TRUE, stringsAsFactors=FALSE)

# get common len
lens = c(nrow(memdf_kv1), nrow(cpudf_kv1))
len1 = min(lens) - 3
mem_kv1 <- memdf_kv1$X.memused[1:len1]
cpu_kv1 <- 100 - cpudf_kv1$X.idle[1:len1]

timeline1 = seq(1, len1, by=1)

cpumem_df <- data.frame(timeline1, cpu_kv1, mem_kv1)
colnames(cpumem_df) = c("time", "CPU", "Mem")

meltdf <- melt(cpumem_df,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('Percentage') +
  labs(title=paste0("System CPU and Memory Consumption: ", runtype)) +
  labs(colour="Type")
plotfname = paste0("tpcc_syscpu_mem_", runtype, ".png")
ggsave(filename=plotfname, plot=p, device=png(height=8, width=10, units = 'in', res=300))


#############################
# for comparing 2 kv tpcc runs
#### kv operations
syncput1 <- read.csv(paste0(datapath_kv1, "sync_put.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

syncget1 <- read.csv(paste0(datapath_kv1, "sync_get.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

sync_completed1 <- read.csv(paste0(datapath_kv1, "sync_completed.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

async_completed1 <- read.csv(paste0(datapath_kv1, "async_completed.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

sync_submitted1 <- read.csv(paste0(datapath_kv1, "sync_submitted.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

asyncput1 <- read.csv(paste0(datapath_kv1, "async_put.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

asyncget1 <- read.csv(paste0(datapath_kv1, "async_get.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

asyncdel1 <- read.csv(paste0(datapath_kv1, "async_del.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

canceled1 <- read.csv(paste0(datapath_kv1, "canceled.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

driver_pending1 <- read.csv(paste0(datapath_kv1, "driver_pending.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

device_pending1 <- read.csv(paste0(datapath_kv1, "device_pending.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

rahit1 <- read.csv(paste0(datapath_kv1, "rahit.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

radiscard1 <- read.csv(paste0(datapath_kv1, "radiscard.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

total_async_recv1 <- read.csv(paste0(datapath_kv1, "total_async_req_recv.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)

total_async_submit1 <- read.csv(paste0(datapath_kv1, "total_async_req_submitted.txt"), header = FALSE, sep = ":", strip.white = TRUE, stringsAsFactors=FALSE)


lens = c(
         nrow(asyncput1), nrow(asyncget1), nrow(asyncdel1), 
         nrow(total_async_recv1), nrow(total_async_submit1), 
         nrow(total_async_recv1), nrow(canceled1), 
         nrow(syncput1),
         nrow(syncget1),
         nrow(sync_completed1),
         nrow(async_completed1),
         nrow(sync_submitted1),
         nrow(driver_pending1),
         nrow(device_pending1),
         nrow(rahit1),
         nrow(radiscard1)
         )

min_len = min(lens)
#min_len = 1600 


## delta assuming sampling interval is 1 seconds
## async
put_delta1 = get_vector_delta(asyncput1$V2)
get_delta1 = get_vector_delta(asyncget1$V2)
del_delta1 = get_vector_delta(asyncdel1$V2)

## sync
sync_get_delta1 = get_vector_delta(syncget1$V2)
sync_put_delta1= get_vector_delta(syncput1$V2)
sync_submitted_delta1 = get_vector_delta(sync_submitted1$V2)
sync_completed_delta1 = get_vector_delta(sync_completed1$V2)

driver_queue1= driver_pending1$V2
device_queue1 = device_pending1$V2

rahit_delta1 <- get_vector_delta(rahit1$V2)

radiscard_delta1 <- get_vector_delta(radiscard1$V2)

total_iops1 <- sync_completed1$V2[1:min_len] + async_completed1$V2[1:min_len]
iops1 = get_vector_delta(total_iops1)

## total async request pending
total_async_diff1 = (total_async_recv1$V2 - total_async_submit1$V2 - canceled1$V2)


########
# for IOPS
########
## plot out various kv operations
timeline = seq(1, min_len, by=1)
#timeline = timeline
kvops_df <- data.frame(timeline, 
                       iops1[1:min_len],
                       put_delta1[1:min_len], get_delta1[1:min_len], del_delta1[1:min_len]
                       #sync_get_delta[1:min_len], sync_put_delta[1:min_len], sync_submitted_delta[1:min_len], sync_completed_delta[1:min_len],
                       #cpu_kv[1:min_len]*1000
                       )
colnames(kvops_df) = c("time", 
                       "IOPS",
                       "async put", "async get", "async delete"
                       #"sync get", "sync put", "sync submitted", "sync completed",
                       # "CPU Busy"
                       )

meltdf <- melt(kvops_df,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('KV operations/s') +
  labs(title=paste0("KvRocks Async OP: ", runtype)) +
#  ylim(0,1000) +
  labs(colour="Type")
plotfname = paste0("tpcc_asyncop_", runtype, ".png")
ggsave(filename=plotfname, plot=p, device=png(height=8, width=10, units = 'in', res=300))


########
# for prefetch overlaying for one TPCC run
########
## plot out various kv operations
timeline = seq(1, min_len, by=1)
#timeline = timeline
kvops_df <- data.frame(timeline, 
                       iops1[1:min_len],
                       rahit_delta1[1:min_len],
                       radiscard_delta1[1:min_len]
                       #cpu_kv[1:min_len]*1000
                       )
colnames(kvops_df) = c("time", 
                       # "async put", "async get", "async delete", "async pending submit", 
                       "IOPS",
                       
                       "RAHit",
                       
                       "RADiscard"
                       #"sync get", "sync put", "sync submitted", "sync completed",
                       # "CPU Busy"
                       )

meltdf <- melt(kvops_df,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('KV operations/s') +
  labs(title=paste0("KvRocks IOPS and Sync OP: ", runtype)) +
#  ylim(0,1000) +
  labs(colour="Type")
plotfname = paste0("tpcc_prefetch_", runtype, ".png")
ggsave(filename=plotfname, plot=p, device=png(height=8, width=10, units = 'in', res=300))

########
# different KV ops
########
## plot out various kv operations
timeline = seq(1, min_len, by=1)
#timeline = timeline
kvops_df <- data.frame(timeline, 
                       iops1[1:min_len],
                       sync_get_delta1[1:min_len], sync_put_delta1[1:min_len]
                       )
colnames(kvops_df) = c("time", 
                       "IOPS",
                       "sync get", "sync put"
                       )

meltdf <- melt(kvops_df,id="time")
p <- ggplot(meltdf,aes(x=time,y=value,colour=variable, group=variable)) + geom_line(size = 1) +
  xlab('Time in Second') +
  ylab('KV operations/s') +
  labs(title=paste0("KvRocks OPs and Sync OP: ", runtype)) +
#  ylim(0,1000) +
  labs(colour="Type")
plotfname = filename=paste0("tpcc_syncop_", runtype, ".png")
ggsave(filename=plotfname, plot=p, device=png(height=8, width=10, units = 'in', res=300))
