library(plyr)
library(plotly)
options(scipen=999)

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

procfunc <- function(data, run_type, phase) {
    setwd("C:\\docs\\work\\myrocks\\plot1")
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
 
    
#    op_group <- data %>% 
#       group_by(operation) %>%
#        summarise(total = n())
    
#    xlabel = sprintf("%s %s Phase Operation Types", run_type, phase)
#    barplot(op_group$total, beside = TRUE, col = 1:6, xlab = xlabel, space = c(0, 2))
#    legend("topleft", 
#           legend = op_group$operation,
#           fill = 1:6, ncol = 1,
#           cex = 0.5)
}

