<pre>
KV SSD host software package
=============================
KV SSD host software package includes the host software that operates with KV SSD.
The package includes API library, emulator, kernel device driver and performance 
evaluation suite called kvbench.
With the package users can evaluate multiple application(e.g., RocksDB, Aerospike, etc.) 
performance on block device in addition to direct key-value stack performance on KV SSD.

KV SSD host software architecture 
==================================
Here is the KV SSD host software architecture. 

    -------------------------------------------------------------------------
    |                      Benchmark suite (kvbench)                        |
    -------------------------------------------------------------------------    --
    |  Software KV Store  |  |                SNIA KV API                   |     |
    |   (e.g.,RocksDB)    |  |                                              |     |
    -----------------------  ------------------------------------------------     |- PDK
    |     File system     |  |     KV SSD        |  |      Device Driver    |     | (Platform Development Kit)
    -----------------------  |    Emulator       |  |  (Kernel/user driver) |     | 
    |     Block layer     |  |                   |  |                       |     |
    -----------------------  ---------------------  -------------------------    -- 
              | Storage               |                       |
              | protocol              |                       | Storage protocol   
              |(NVMe block)           |                       |   (NVMe KV)
              |                       |                       |
    ---------------------   ---------------------  -------------------------                   
    |    Block device   |   |       Memory      |  |         KV SSD        |
    ---------------------   ---------------------  -------------------------


KV SSD host software package
=============================
This is the directory structure and package description.

KVSSD 
   |- application
   |    |- kvbench : KV benchmark suite
   |       
   |- PDK (Platform Development Kit)
   |    |- core
   |    |    |- src
   |    |        |- api : SNIA KV Storage API (Application Programming Interface)
   |    |        |- device abstract layer
   |    |             |- emulator : KV SSD emulator
   |    |             |- kernel_driver_adapater : Kernel driver adapter
   |    |   
   |    |- driver : device driver
   |         |- PCIe
   |             |- kernel driver : Kernel device driver
   |             |- user driver   : user space device driver
   |- spec
        |- SNIA API library spec
</pre>

