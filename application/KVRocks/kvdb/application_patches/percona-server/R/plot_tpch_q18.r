library(dplyr)
library(ggplot2)

library(plotly)
options(scipen=999)


dbbench <- read.csv("C:\\docs\\work\\kvdb\\results\\dbbench_0910.csv", header = TRUE)

dbbench$time <- as.POSIXct(dbbench$time, format = '%Y%m%d-%H-%M-%S');
dbbench$dbtype <- trimws(dbbench$dbtype, which = c("both", "left", "right"))
dbbench$run <- trimws(dbbench$run, which = c("both", "left", "right"))
dbbench$dbtype = as.factor(dbbench$dbtype)
dbbench$run = as.factor(dbbench$run)

dbbench1 <- filter(dbbench, dbbench$failure != 1)


attrs = c("fillsync",  "fillrandom", "overwrite",  "readtocache",
          "fillseq", "readrandom",   "readseq",  "readreverse",
          "deleteseq", "seekrandom", "deleterandom",  "readmissing",
          "readrandomwriterandom",  "randomreplacekeys", "readwhilewriting", "randomtransaction")

value_sizes <- unique(dbbench1$value_size)
cache_sizes <- unique(dbbench1$cache_size)
thread_count <- unique(dbbench1$threads)
dbtypes <- unique(dbbench1$dbtype)
types <- unique(dbbench1$run)


plot_db <- function(attrs, value, cache, threads, kvdb_df, rocks_df) {
  
}

get_latest_index <- function(rocksdf) {
  latest_index = which(rocksdf$time == max(rocksdf$time))[1]
  latest_index
}

compare_kvdb_rocksdb <- function(attrs, value, cache, threads, kvdb_df, rocks_df) {
  name <- sprintf("v%dB_c%dMB_%dthreads", value, cache/1024/1024, threads)
  #get latest sample
  
  for (attr in attrs) {
    index_kvdb <- get_latest_index(kvdb_df)
    index_rocks <- get_latest_index(rocks_df)
    v_kvdb = kvdb_df[[attr]][index_kvdb]
    v_rocks = rocks_df[[attr]][index_rocks]
    
    ratio = v_kvdb/v_rocks
    print(sprintf("%s: kvdb: %.2f us rocksdb %.2f us, kvdb/rocksdb=%.2f", attr, v_kvdb, v_rocks, ratio))
  }
  
}

# get max element index
# which(tt == max(tt))[1]

# latency or bandwidth
for (type in types) {
  
  for (value in value_sizes) {
    for (cache in cache_sizes) {
      for (threads in thread_count) {
        
          name <- sprintf("v%dB_c%dMB_%dthreads", value, cache/1024/1024, threads)
          print(name)
          kvdb_df <- dbbench1 %>% filter(cache_size == cache & value_size == value 
                                         & run == type
                                         & thread_count == threads & dbtype == "kvdb")
          rocks_df <- dbbench1 %>% filter(cache_size == cache & value_size == value 
                                          & run == type
                                          & thread_count == threads & dbtype == "rocksdb")
      
          #plot_kvdb_rocksdb(attrs, value, cache, threads, kvdb_df, rocks_df)
          #get latest attr to compare
        }
      }
  }
  
}




for (attr in attrs) {
  dbbench1[[attr]]
}


tpcc = read.csv("C:\\docs\\work\\myrocks\\kvdb_tpcc.csv", header = TRUE)
tpch = read.csv("C:\\docs\\work\\myrocks\\tpch_all.csv", header = TRUE)



#tpcc = read.csv("C:\\docs\\work\\myrocks\\kvdb_tpcc.csv", header = TRUE)
#tpch = read.csv("C:\\docs\\work\\myrocks\\tpch_all.csv", header = TRUE)


# tpcc
#tpcc_load <- filter(tpcc, tpcc$time <= 1534972242911070)
#tpcc_trx <- filter(tpcc, tpcc$time > 1534972242911070)

# tpch
#tpch_load <- filter(tpch, tpch$time <= 1534981657374511)
#tpch_trx <- filter(tpch, tpch$time > 1534981657374511)
#tpch cutoff
#1534981657374511

#tpcc cutoff
#1534972242911070


tpch18 <- read.csv("C:\\docs\\work\\myrocks\\q18_0828.csv", header = TRUE)
tpch18_trx <- filter(tpch18, tpch18$time > 1535495188180179)

newdf <- filter(tpch18_trx, tpch18_trx$operation == 'NEW ITERATOR')
keydf <- filter(tpch18_trx, tpch18_trx$operation == 'KEY IN ITERATOR')
valdf <- filter(tpch18_trx, tpch18_trx$operation == 'VALUE IN ITERATOR')

keydf$key.name = as.factor(keydf$key.name)
key_group <- keydf %>% 
  group_by(key.name) %>%
  summarise(total = n()) %>%
  arrange(desc(total))


p1 <- hist(newdf$time)
p2 <- hist(keydf$time)


title = sprintf("Iterator Creation Time Distribution: Mean %.1f, Sd %.1f", 
                mean(newdf$time), sd(newdf$time))
plot(p1,col="red",density=10,angle=45,
     main = title, xlab = "Time in us")


title = sprintf("Key Iterate Time Distribution: Mean %.1f, Sd %.1f", 
                mean(keydf$time), sd(keydf$time))
plot(p2,col="blue",density=10,angle=135, add=T,
     main = title, xlab = "Time in us")

dkey1 <- delta_key[ delta_key < 200]
hist(dkey1)



get_delta <- function(data) {

  rows = nrow(data)
  list <- rep(0, times = rows -1)
  for (i in 2:rows) {
    list[i-1] = data$time[i] - data$time[i-1]
  }
  list
}






delta_new <- get_delta(newdf)
delta_key  <- get_delta(keydf)
delta_val  <- get_delta(valdf)

p1 <- hist(delta_key)
p2 <- hist(delta_val)
p3 <- hist(delta_new)

plot(p1,col="green",density=10,angle=135,add=TRUE)
plot(p2,col="blue",density=10,angle=45,add=TRUE)




title = sprintf("Key Iteration Time Delta Distribution: Mean %.1f, Sd %.1f", 
                mean(delta_key), sd(delta_key))
plot(p1,col="green",density=10,angle=135, 
            main = title, xlab = "Delta Time in us")



title = sprintf("Value Iteration Time Delta Distribution: Mean %.1f, Sd %.1f", 
                mean(delta_val), sd(delta_val))
plot(p2,col="blue",density=10,angle=45,
     main = title, xlab = "Delta Time in us")



title = sprintf("Iterator Creation Delta Distribution: Mean %.1f, Sd %.1f", 
                mean(delta_new), sd(delta_new))
plot(p3,col="red",density=10,angle=45,
     main = title, xlab = "Delta Time in us")



op_group <- data %>% 
  group_by(operation) %>%
  summarise(total = n())


op_group <- data %>% 
  group_by(operation) %>%
  summarise(total = n())

# for PUT value 
fname = sprintf("%s%s_operation_type.png", run_type, phase)
png(filename=fname, height=8, width=10, units = 'in', res=300)
xlabel = sprintf("Operation Type Distribution for %s %s", run_type, phase)
h <-hist(op_group$operation, main = "Oper", xlab = xlabel)
text(h$mids,h$counts,labels=h$counts, adj=c(0.5, -0.5))
dev.off()

xlabel = sprintf("%s %s Phase Operation Types", run_type, phase)
barplot(op_group$total, beside = TRUE, col = 1:6, xlab = xlabel, space = c(0, 2))
legend("topleft", 
       legend = op_group$operation,
       fill = 1:6, ncol = 1,
       cex = 0.5)




procfunc <- function(data, run_type, phase) {
    setwd("C:\\docs\\work\\myrocks\\q18_plot")
    data$column.ID = as.factor(data$column.ID)
    data$operation = as.factor(data$operation)

    ## for GET
    get_key_size = filter(data, key.name.size > 0 & (operation == "GET"
                                                 |  operation == "GET FAIL" 
                                                 |  operation == "GET PINNABLE" 
                                                 |  operation == "GET PINNABLE FAIL" 
                                                 ))
    
    
    get_value_size = filter(data, value.size > 0 & (operation == "GET"
                                                 |  operation == "GET FAIL" 
                                                 |  operation == "GET PINNABLE" 
                                                 |  operation == "GET PINNABLE FAIL" 
                                                 ))
    
    ## for PUT
    put_key_size = filter(data, key.name.size > 0 & (operation == "PUT"
                                                 |  operation == "PUT FAIL" 
                                                 |  operation == "PUT IN BATCH" 
                                                 |  operation == "PUT PARTS IN BATCH" 
                                                 ))
    
    
    put_value_size = filter(data, value.size > 0 & (operation == "PUT"
                                                 |  operation == "PUT FAIL" 
                                                 |  operation == "PUT IN BATCH" 
                                                 |  operation == "PUT PARTS IN BATCH" 
                                                 ))
    
    
    put_key_title = sprintf("%s %s Phase PUT Histogram of Key Sizes", run_type, phase)
    put_val_title = sprintf("%s %s Phase PUT Histogram of Value Size", run_type, phase)

    get_key_title = sprintf("%s %s Phase GET Histogram of Key Size", run_type, phase)
    get_val_title = sprintf("%s %s Phase GET Histogram of Value Size Distribution", run_type, phase)

    # for GET key
    fname = sprintf("%s%s_get_key.png", run_type, phase)
    png(filename=fname, height=8, width=10, units = 'in', res=300)

    xlabel = sprintf("Key Sizes in Bytes -- mean %.1f, sd %.1f", mean(get_key_size$key.name.size), sd(get_key_size$key.name.size))
    h <- hist(get_key_size$key.name.size, main = get_key_title, xlab = xlabel)
    text(h$mids,h$counts,labels=h$counts, adj=c(0.5, -0.5))
    dev.off()

    
    # for GET value 
    fname = sprintf("%s%s_get_val.png", run_type, phase)
    png(filename=fname, height=8, width=10, units = 'in', res=300)
    xlabel = sprintf("Value Sizes in Bytes -- mean %.1f, sd %.1f", mean(get_value_size$value.size), sd(get_value_size$value.size))
    h <-hist(get_value_size$value.size, main = get_val_title, xlab = xlabel)
    text(h$mids,h$counts,labels=h$counts, adj=c(0.5, -0.5))
    fname = sprintf("%s%s_get", run_type, phase)
    dev.off()

    # for PUT key
    fname = sprintf("%s%s_put_key.png", run_type, phase)
    png(filename=fname, height=8, width=10, units = 'in', res=300)
    xlabel = sprintf("Key Sizes in Bytes -- mean %.1f, sd %.1f", mean(put_key_size$key.name.size), sd(put_key_size$key.name.size))
    h <- hist(put_key_size$key.name.size, main = put_key_title, xlab = xlabel)
    text(h$mids,h$counts,labels=h$counts, adj=c(0.5, -0.5))
    dev.off()
    
    # for PUT value 
    fname = sprintf("%s%s_put_val.png", run_type, phase)
    png(filename=fname, height=8, width=10, units = 'in', res=300)
    xlabel = sprintf("Value Sizes in Bytes -- mean %.1f, sd %.1f", mean(put_value_size$value.size), sd(put_value_size$value.size))
    h <-hist(put_value_size$value.size, main = put_val_title, xlab = xlabel)
    text(h$mids,h$counts,labels=h$counts, adj=c(0.5, -0.5))
    dev.off()
 
}




