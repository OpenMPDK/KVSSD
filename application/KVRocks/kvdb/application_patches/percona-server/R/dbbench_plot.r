library(dplyr)
library(ggplot2)

options(scipen=999)


#<- read.csv("C:\\docs\\work\\kvdb\\results\\tmp.csv", header = TRUE)

# use PM683 for rocksdb
# dbbench <- read.csv("C:\\docs\\work\\kvdb\\results\\dbbench_1004.1.csv", header = TRUE, sep = ",", strip.white = TRUE)

# use PM983 for rocksdb
# dbbench <- read.csv("C:\\docs\\work\\kvdb\\results\\dbbench_20181023.csv", header = TRUE, sep = ",", strip.white = TRUE)
dbbench <- read.csv("C:/docs/work/kvdb/results/dbbench_20181116.csv", header = TRUE, sep = ",", strip.white = TRUE)

dbbench$time <- as.POSIXct(dbbench$time, format = '%Y%m%d-%H-%M-%S');
#dbbench$dbtype <- trimws(dbbench$dbtype, which = c("both", "left", "right"))
#dbbench$stats_type <- trimws(dbbench$stats_type, which = c("both", "left", "right"))
#dbbench$runtype <- trimws(dbbench$runtype, which = c("both", "left", "right"))
dbbench$dbtype = as.factor(dbbench$dbtype)
dbbench$stats_type = as.factor(dbbench$stats_type)
dbbench$runtype = as.factor(dbbench$runtype)

# for rocksdb, these will be 0, only applicable for kvdb
## vector of sktable key count low and high
sktable_keycount = sprintf("L%dK_H%dK", as.integer(dbbench$sktable_keycount_low/1000), as.integer(dbbench$sktable_keycount_high/1000))
dbbench[["sktable_keycount"]] <- sktable_keycount
dbbench$sktable_keycount <-  as.factor(dbbench$sktable_keycount)

dbbench <- dbbench %>% 
  mutate(kvdb_rowlabel = sprintf("k%dK#v%dB_c%dMB_%dthreads#watermark_%s", numkeys/1000, 
                            value_size, as.integer(cache_size/1024/1024), 
                            threads, sktable_keycount))

dbbench <- dbbench %>% 
  mutate(common_rowlabel = sprintf("k%dK#v%dB_c%dMB_%dthreads", numkeys/1000, 
                            value_size, as.integer(cache_size/1024/1024), 
                            threads))

convert_kvdb_rowlabel_to_rocksdb_rowlabel <- function(kvdb_rowlabel) {
  # split with #
  a <- unlist(strsplit(kvdb_rowlabel, '[#]'))
  return (paste(a[1], a[2], sep = "#"))
}


## kvdb_rowlabel can uniquely identify a run type
dbbench$kvdb_rowlabel = as.factor(dbbench$kvdb_rowlabel)
## kvdb and rocksdb are the same
dbbench$common_rowlabel = as.factor(dbbench$common_rowlabel)

# sort data according to time in  ascending order
dbbench <- dbbench %>% arrange(time)


dbbench <- dbbench %>% filter(failure != 1)


#### complete list
#attrs = c("fillsync", "fillbatch", "readtocache", "fillrandom", "overwrite", "newiterator",
#          "fillseq", "readrandom", "readseq", "readreverse", "updaterandom", "deleteseq",
#         "seekrandom", "appendrandom", "deleterandom", "readmissing",  "readrandomwriterandom",
#          "randomreplacekeys",  "readwhilemerging",   "mergerandom",  "readwhilewriting", 
#          "randomtransaction",  "seekrandomwhilewriting",  "seekrandomwhilemerging", 
#          "fillseekseq", "readrandommergerandom")

## daily run list
#attrs = c("fillbatch", "fillrandom", "overwrite", "newiterator", "fillseq", "readrandom", "readseq",
#          "updaterandom", "deleteseq", "seekrandom", "appendrandom", "readrandomwriterandom", 
#          "randomreplacekeys", "readwhilewriting", "randomtransaction")

attrs = c("fillseq", "readrandom", "readseq", "fillrandom", "updaterandom")


value_sizes <- unique(dbbench$value_size)
cache_sizes <- unique(dbbench$cache_size)
threads <- unique(dbbench$threads)
dbtypes <- unique(dbbench$dbtype)
stats_types <- unique(dbbench$stats_type)


# get last good value based on time for an attribute
get_latest_value <- function(df, attr) {
  maxrow = length(df[[attr]])
  while (maxrow >= 1) {
    if ( !is.na(df[[attr]][maxrow]) & (df[[attr]][maxrow] > 1e-6)  )  {
      return (df[[attr]][maxrow])
    } else {
      maxrow = maxrow - 1;
    }
  }
  return (0)
}


## just use latest to generate the ratio
## for one runtype: key#4000K_v100B_vcache6050MB_1threads_keycount_LOW_HIGH
## get ratio for just one row for all attributes
## ratio = kvdb/rocksdb
generate_kvdb_rocksdb_ratio <- function(ratio_df, attrs, rowlabel, kvdb_df, rocks_df) {
  new_row = nrow(ratio_df) + 1
  ## set up a new empty row
  ratio_df[new_row, ] <- NA
  
  ## fill in new row content by columns
  first_column_name = names(ratio_df)[1]
  ratio_df[[first_column_name]][new_row] = rowlabel
  
  for (attr in attrs) {
    
    v_kvdb <- get_latest_value(kvdb_df, attr)
    v_rocks <- get_latest_value(rocks_df, attr)

    if (v_rocks > 0 & v_kvdb > 0) {
      
      # calculate ratio
      ratio = round(v_kvdb/v_rocks, digits = 2)
      
      # run type
      # print(sprintf("%s %s: rocksdb: %.2f us kvdb %.2f us, rocksdb/kvdb=%.2f", name, attr, v_rocks, v_kvdb, ratio))
      ratio_df[[attr]][new_row] = ratio
    } else {
      ratio_df[[attr]][new_row] = NA
    }
  }
    
  return (ratio_df)
}

#sample output of ratio = kvdb/rocksdb
#runtype fillsync fillbatch readtocache fillrandom overwrite newiterator fillseq readrandom readseq
#1   v100B_c6050MB_1threads      3.5       0.1         0.6        0.4       0.5         2.7     0.4   
#2  v100B_c12100MB_1threads      4.7       0.1         0.6        0.4       0.5         2.7     0.4    
#3   v100B_c1210MB_1threads      4.6       0.1         0.6        0.4       0.5         2.8     0.4      
#4   v100B_c2420MB_1threads      4.5       0.1         0.5        0.4       0.5         3.1     0.4
#                                 data,   attrs, stats type, 
get_kvdb_rocks_ratio <- function(dbbench, attrs, stats, omit_na) {
  ratio_df = data.frame(matrix(ncol = length(attrs) + 1, nrow = 0))
  colnames(ratio_df) <- c("runtype", attrs)
  
  # get max element index
  # which(tt == max(tt))[1]
  # latency or bandwidth
  
  type = stats
  kvdb_rowlabels <- dbbench %>% filter(dbtype == "kvdb" & stats_type == type) %>% select(kvdb_rowlabel)
  for (rlabel in levels(as.factor(kvdb_rowlabels$kvdb_rowlabel))) { 
    
    # print(sprintf("checking type %s and rowlabel %s", type, rlabel))
    
    kvdb_df <- dbbench %>% filter(kvdb_rowlabel == rlabel & stats_type == type & dbtype == "kvdb")
    
    rdb_label = convert_kvdb_rowlabel_to_rocksdb_rowlabel(rlabel)
    rocks_df <- dbbench %>% filter(common_rowlabel == rdb_label & stats_type == type & dbtype == "rocksdb")

    
    if (dim(kvdb_df)[1] > 0 & dim(rocks_df)[1] > 0) {
      # print(name)

      ratio_df <- generate_kvdb_rocksdb_ratio(ratio_df, attrs, rlabel, kvdb_df, rocks_df)
    }

    #plot_kvdb_rocksdb(attrs, value, cache, threads, kvdb_df, rocks_df)
    #get latest attr to compare
  }

  if (omit_na) {
    return (na.omit(ratio_df))
  } else {
    return (ratio_df)
  }
  
}


## sort ratio and output in descending order
get_sorted_ratiodf <- function(ratio_df, attrs) {

  sorted_ratiodf = data.frame(matrix(ncol = 3, nrow = 0))
  colnames(sorted_ratiodf) <- c("ratio", "runtype", "attr")
  
  
  for (attr in attrs) {
    if (attr %in% names(ratio_df)) {
      for (i in 1:nrow(ratio_df)) {
        ratio = ratio_df[[attr]][i]
        runtype = ratio_df[["runtype"]][i]
        
        # set a new row
        new_row = nrow(sorted_ratiodf) + 1
        ## set up a new empty row
        sorted_ratiodf[new_row, ] <- NA
        
        sorted_ratiodf$ratio[new_row] = ratio
        sorted_ratiodf$runtype[new_row] = runtype
        sorted_ratiodf$attr[new_row] = attr
      }
    }
  }

  
  finaldf <- sorted_ratiodf %>% arrange(desc(ratio))
  
  return (finaldf)
}

#
# return a series of data frame for all attibutes
# get performance time series for each attribute 
# names(perf_history) = data frame of all all attributes
# runtype: v100KB_c1506MB_8threads
# date, fillseq, fillsync, ...
# 
get_perf_history <- function(dbbench, attrs, stats) {

  perf_history = list()
  
  type = stats
  for (rlabel in levels(dbbench$rowlabel)) {
        
        kvdb_df <- dbbench %>% filter(stats_type == type
                                       & rowlabel == rlabel & dbtype == "kvdb")
        perf_history[[rlabel]] = kvdb_df
  }
  
  return (perf_history)
}


plot_perf_history <- function(perfhistory, attrs) {
  
}



## get performance history for each runtype
# perfhist = get_perf_history(dbbench1, attrs, stats_types, value_sizes, cache_sizes, threads)

## get ratio of kvdb/rocksdb
ratio_df = get_kvdb_rocks_ratio(dbbench, attrs, "latency", TRUE)
ratio_df_na = get_kvdb_rocks_ratio(dbbench, attrs, "latency", FALSE)

sorted_ratiodf = get_sorted_ratiodf(ratio_df, attrs)

## get ratios with a threshold
sorted_ratiodf %>% filter(ratio>3)

## get sorted ratio (the best performance first)
sorted_ratiodf %>%arrange(ratio) %>% head

## get sorted ratio (the best performance first)
sorted_ratiodf %>%arrange(desc(ratio)) %>% head

## sorted according to runtype(variation of watermark)
ratio_df_runtype <- ratio_df %>% arrange(runtype)



get_all_ratios <- function(ratio_df) {
  list1 = c()
  for (attr in attrs) {
    onedf <- ratio_df %>% select(attr) %>% filter(!is.na(attr))
    list1[[attr]] = onedf %>% filter(!is.na(attr))
  }
  return (list1)
}




