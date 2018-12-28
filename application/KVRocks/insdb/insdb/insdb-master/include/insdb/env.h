// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// An Env is an interface used by the insdb implementation to access
// operating system functionality like the filesystem etc.  Callers
// may wish to provide a custom Env object when opening a database to
// get fine gain control; e.g., to rate limit file system operations.
//
// All Env implementations are safe for concurrent access from
// multiple threads without any external synchronization.

#ifndef STORAGE_INSDB_INCLUDE_ENV_H_
#define STORAGE_INSDB_INCLUDE_ENV_H_

#include <stdarg.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "insdb/export.h"
#include "insdb/status.h"
//#include "util/linux_nvme_ioctl.h"

namespace insdb {

    class FileLock;
    class Logger;
    class RandomAccessFile;
    class SequentialFile;
    class Slice;
    class WritableFile;

    enum FlushType{
        kWriteFlush = 0x81, // nvme_cmd_kv_store,
        kReadFlush = 0x90, // nvme_cmd_kv_retrieve,
    };
    enum GetType{
        kSyncGet = 0x1,
        kReadahead = 0x2,
        kNonblokcingRead = 0x3,
    };
    union INSDB_EXPORT InSDBKey {
        struct {
            uint64_t    first;
            uint64_t    second;
        };
        struct {
            uint32_t    db_id;
            uint32_t    type:3; // set to 0 for internal key-value pair
            uint32_t    split_seq:29;
            uint64_t    seq;
        };

        struct {
            uint32_t    dbname;
            uint32_t    name_type:3; // 1 -->sktable and super sktable, 2 -->delete list, 3 -->uncomplete list, 4 -->manifest
            uint32_t    skt_id:29;
            uint64_t    skt_split_seq; // used by sktable meta except sktable and supper sktable, 
        };
        struct {
            uint32_t w0;
            uint32_t w1;
            uint64_t dw0;
        };
    } __attribute__((packed)) ;

    static_assert (sizeof(InSDBKey) == 16, "Invalid InSDBKey size");

    inline bool operator==(const InSDBKey& x, const InSDBKey& y) {
        return ((x.first == y.first) && (x.second == y.second));
    }

    inline bool operator!=(const InSDBKey& x, const InSDBKey& y) {
        return !(x == y);
    }

    InSDBKey EncodeInSDBKey(InSDBKey);

    class INSDB_EXPORT Env {
        public:
            Env() { }
            virtual ~Env();

            // Return a default environment suitable for the current operating
            // system.  Sophisticated users may wish to provide their own Env
            // implementation instead of relying on this default environment.
            //
            // The result of Default() belongs to insdb and must never be deleted.
            static Env* Default();

            /**
              @brief Open KV SSD device.
              @param  in  kv_ssd_name std::string&
              @return Status::OK() if success, else error messages.

              Open a KV SSD device. Because single device open enough during DB
              life cycle, It hide to expose to upper.
              For the operation withoug opeinning device, will report 
              - We may need a mechanism, which maniging multiple KV SSD.
              Current implementation just assume a single KV SSD.

              [Note]
kv_dev : KV SSD device name.
*/
            virtual Status Open(const std::string& kv_ssd_name) = 0;

            /**
              Close a KV SSD device
              */
            virtual void Close() = 0;

            /**
              @brief acquire database lock.
              @param  in  dbname std::string&
              @return true if success, false.

              AcauireDBLock(). Used to protect DB from multiple-acess.
              Kernel must support this feature and also cover the case, which
              application leave without ReleaseDBLock() call.
              Use internel DB handler which is Opened by Open() call.
              */
            virtual bool AcquireDBLock(uint16_t db_name) = 0;

            /**
              @brief release database lock.
              @param  in  dbname std::string&
              @return true if success, false.

              ReleaseDBLock(), Kernel must free internal data structure for DB lock(),
              Use internel DB handler which is Opened by Open() call.
              */
            virtual void ReleaseDBLock(uint16_t db_name) = 0;

            /**
              @brief save key-value pair synchronously.
              @param  in  key Slice&
              @param  in  value Sliece&
              @param  in  async bool
              @return Status::OK() if success, else error messages.

              (A)Synchronous Put operation with Key-value pair.
              Use internel DB handler which is Opened by Open() call.
              */
            virtual Status Put(InSDBKey key, const Slice& value, bool async = false) = 0;

            /**
              @brief flush all keys.
              @param  in  key Slice&
              @return Status::OK() if success, else error messages.

              Flush single or all Key. it guarantees of writng keyes to KV SSD, which maybe exist on
              kernel write buffer.
              Use internel DB handler which is Opened by Open() call.
              */
            virtual Status Flush(FlushType type, InSDBKey key = {0, 0}) = 0;

            /**
              @brief  retrieve key-value pair synchronously.
              @param  in  key Slice&
              @param  out  buf char*
              @param  in/out  size  int
              @param  in  readahead
              @return Status::OK() if success, else error messages.

              Synchronous Get operation with Key, contents of key will be saved on value.
              Value defined as Slice, it must provide pre-allocated buffer.
              Use internel DB handler which is Opened by Open() call.
              */
            virtual Status Get(InSDBKey key, char *buf, int *size, GetType type = kSyncGet, bool *from_readahead = NULL) = 0;

            virtual Status MultiGet(std::vector<InSDBKey> &key, std::vector<char*> &buf, std::vector<int> &size, uint32_t count, InSDBKey blocking_key, bool readahead) = 0;
            /**
              @brief ask discard a single key in kernel buffer
              @param  in  key Slice&
              @return Status::OK() if success, else error messages.

              It removes a key or all keys from kernel read-ahread buffer.
              Use internel DB handler which is Opened by Open() call.
              */
            virtual Status DiscardPrefetch(InSDBKey key = {0, 0}) = 0;

            /**
              @brief Delete existing key.
              @param  in  key Slice&
              @param  in  async bool
              @return Status::OK() if success, else error messages.

              It delete existing key.
              */
            virtual Status Delete(InSDBKey key, bool async = true) = 0;



            /**
              @brief check whether key is in database or not.
              @param  in  key Slice&
              @return true if success, else false.

              It check whetehr key exist or not.
              */
            virtual bool KeyExist(InSDBKey key) = 0;

            virtual Status IterReq(uint32_t mask, uint32_t iter_val, unsigned char *iter_handle, bool open = false, bool rm = false) = 0;
            virtual Status IterRead(char* buf, int *size, unsigned char iter_handle) = 0;
            virtual Status GetLog(char pagecode, char* buf, int bufflen) = 0;
            // Arrange to run "(*function)(arg)" once in a background thread.
            //
            // "function" may run in an unspecified thread.  Multiple functions
            // added to the same Env may run concurrently in different threads.
            // I.e., the caller may not assume that background work items are
            // serialized.
            virtual void Schedule(
                    void (*function)(void* arg),
                    void* arg) = 0;

            // Start a new thread, invoking "function(arg)" within the new thread.
            // When "function(arg)" returns, the thread will be destroyed.
            virtual void StartThread(void (*function)(void* arg), void* arg) = 0;

            // Wait for all threads started by StartThread to terminate.
            virtual void WaitForJoin() {}

            // Create and return a log file for storing informational messages.
            virtual Status NewLogger(const std::string& fname, Logger** result) = 0;

            // Returns the number of micro-seconds since some fixed point in time. Only
            // useful for computing deltas of time.
            virtual uint64_t NowMicros() = 0;

            virtual uint64_t NowSecond() = 0;

            // Sleep/delay the thread for the prescribed number of micro-seconds.
            virtual void SleepForMicroseconds(int micros) = 0;

        private:
            // No copying allowed
            Env(const Env&);
            void operator=(const Env&);
    };

    // An interface for writing log messages.
    class INSDB_EXPORT Logger {
        public:
            Logger() { }
            virtual ~Logger();

            // Write an entry to the log file with the specified format.
            virtual void Logv(const char* format, va_list ap) = 0;

        private:
            // No copying allowed
            Logger(const Logger&);
            void operator=(const Logger&);
    };

    // Log the specified data to *info_log if info_log is non-NULL.
    extern void Log(Logger* info_log, const char* format, ...)
#   if defined(__GNUC__) || defined(__clang__)
        __attribute__((__format__ (__printf__, 2, 3)))
#   endif
        ;

    // An implementation of Env that forwards all calls to another Env.
    // May be useful to clients who wish to override just part of the
    // functionality of another Env.
    class INSDB_EXPORT EnvWrapper : public Env {
        public:
            // Initialize an EnvWrapper that delegates all calls to *t
            explicit EnvWrapper(Env* t) : target_(t) { }
            virtual ~EnvWrapper();

            // Return the target to which this Env forwards all calls
            Env* target() const { return target_; }

            // The following text is boilerplate that forwards all methods to target()
            Status Open(const std::string& kv_ssd_name) { return target_->Open(kv_ssd_name); }
            void Close() { return target_->Close(); }
            bool AcquireDBLock(const uint16_t db_name) { return target_->AcquireDBLock(db_name); }
            void ReleaseDBLock(const uint16_t db_name) { return target_->ReleaseDBLock(db_name); }
            Status Put(InSDBKey key, const Slice& value, bool async = false) { return target_->Put(key, value, async); }
            Status Flush(FlushType type, InSDBKey key = {0,0}) { return target_->Flush(type, key); }
            Status Get(InSDBKey key, char *buf, int *size, GetType type = kSyncGet, bool *from_readahead = NULL) { return target_->Get(key, buf, size, type, from_readahead); }
            Status MultiGet(std::vector<InSDBKey> &key, std::vector<char*> &buf, std::vector<int> &size, uint32_t count, InSDBKey blocking_key, bool readahead) { return target_->MultiGet(key, buf, size, count, blocking_key, readahead); }
            Status DiscardPrefetch(InSDBKey key = {0,0}) {return target_->DiscardPrefetch(key); }
            Status Delete(InSDBKey key, bool async = true) { return target_->Delete(key, async); }
            bool KeyExist(InSDBKey key) { return target_->KeyExist(key); }


            virtual Status IterReq(uint32_t mask, uint32_t iter_val, unsigned char *iter_handle, bool open = false, bool rm = false) {
                return target_->IterReq(mask, iter_val, iter_handle, open, rm);
            }
            virtual Status IterRead(char* buf, int *size, unsigned char iter_handle) {
                return target_->IterRead(buf, size, iter_handle);
            }
            virtual Status GetLog(char pagecode, char* buf, int bufflen) {
                return target_->GetLog(pagecode, buf, bufflen);
            }
            void StartThread(void (*f)(void*), void* a) {
                return target_->StartThread(f, a);
            }
            virtual Status NewLogger(const std::string& fname, Logger** result) {
                return target_->NewLogger(fname, result);
            }
            uint64_t NowMicros() {
                return target_->NowMicros();
            }

            uint64_t NowSecond() {
                return target_->NowSecond();
            }
            void SleepForMicroseconds(int micros) {
                target_->SleepForMicroseconds(micros);
            }
        private:
            Env* target_;
    };

}  // namespace insdb

#endif  // STORAGE_INSDB_INCLUDE_ENV_H_
