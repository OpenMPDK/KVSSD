/*-
 *   BSD LICENSE
 *
 *   Copyright (c) Intel Corporation.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "rocksdb/env.h"
#include <set>
#include <sstream>

extern "C" {
#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/blob.h"
#include "spdk/blobfs.h"
#include "spdk/blob_bdev.h"
#include "spdk/log.h"
#include "spdk/io_channel.h"
#include "spdk/bdev.h"
}

namespace rocksdb
{

struct init_args {
	struct spdk_app_opts *opts;
	std::vector<std::string> *bdev_names;
};

struct spdk_filesystem **g_fs = NULL;
uint32_t *g_lcore;
uint32_t *g_index;
uint32_t g_num = 0;
uint32_t g_loaded_fs = 0;
uint32_t g_unloaded_fs = 0;
volatile bool g_spdk_ready = false;
struct sync_args {
	struct spdk_io_channel *channel[50];
};

__thread struct sync_args g_sync_args;

static void
__call_fn(void *arg1, void *arg2)
{
	fs_request_fn fn;

	fn = (fs_request_fn)arg1;
	fn(arg2);
}

static void
__send_request(void *send_arg, fs_request_fn fn, void *arg)
{
	struct spdk_event *event;
	uint32_t *i = (uint32_t *)send_arg;
	event = spdk_event_allocate(g_lcore[*i], __call_fn, (void *)fn, arg);
	spdk_event_call(event);
}

static bool
find_directory_index(const std::string& fname, std::vector<std::string>& directory, uint32_t *index)
{
	auto pos = fname.find_last_of("/");
	for(uint32_t i = 0; i < directory.size(); i++){
		//if (fname.compare(0, directory[i].length(), directory[i]) == 0) {
		if (fname.compare(0, pos, directory[i]) == 0) {
			*index = i;
			return true;
		}
	}
	return false;
}

static bool
find_directory_index_dir(const std::string& fname, std::vector<std::string>& directory, uint32_t *index)
{
	for(uint32_t i = 0; i < directory.size(); i++){
		if (fname.compare(0, directory[i].length(), directory[i]) == 0) {
			*index = i;
			return true;
		}
	}
	return false;
}

static std::string
sanitize_path(const std::string &input, const std::string &mount_directory)
{
	int index = 0;
	std::string name;
	std::string input_tmp;

	input_tmp = input.substr(mount_directory.length(), input.length());
	for (const char &c : input_tmp) {
		if (index == 0) {
			if (c != '/') {
				name = name.insert(index, 1, '/');
				index++;
			}
			name = name.insert(index, 1, c);
			index++;
		} else {
			if (name[index - 1] == '/' && c == '/') {
				continue;
			} else {
				name = name.insert(index, 1, c);
				index++;
			}
		}
	}

	if (name[name.size() - 1] == '/') {
		name = name.erase(name.size() - 1, 1);
	}
	return name;
}

class SpdkSequentialFile : public SequentialFile
{
	struct spdk_file *mFile;
	uint32_t mI;
	uint64_t mOffset;
public:
	SpdkSequentialFile(struct spdk_file *file, uint32_t mi) : mFile(file), mI(mi), mOffset(0) {}
	virtual ~SpdkSequentialFile();

	virtual Status Read(size_t n, Slice *result, char *scratch) override;
	virtual Status Skip(uint64_t n) override;
	virtual Status InvalidateCache(size_t offset, size_t length) override;
};

SpdkSequentialFile::~SpdkSequentialFile(void)
{
	spdk_file_close(mFile, g_sync_args.channel[mI]);
}

Status
SpdkSequentialFile::Read(size_t n, Slice *result, char *scratch)
{
	uint64_t ret;

	ret = spdk_file_read(mFile, g_sync_args.channel[mI], scratch, mOffset, n);
	mOffset += ret;
	*result = Slice(scratch, ret);
	return Status::OK();
}

Status
SpdkSequentialFile::Skip(uint64_t n)
{
	mOffset += n;
	return Status::OK();
}

Status
SpdkSequentialFile::InvalidateCache(size_t offset, size_t length)
{
	return Status::OK();
}

class SpdkRandomAccessFile : public RandomAccessFile
{
	struct spdk_file *mFile;
	uint32_t mI;
public:
	SpdkRandomAccessFile(const std::string &fname, const EnvOptions &options, uint32_t mi);
	virtual ~SpdkRandomAccessFile();

	virtual Status Read(uint64_t offset, size_t n, Slice *result, char *scratch) const override;
	virtual Status InvalidateCache(size_t offset, size_t length) override;
};

SpdkRandomAccessFile::SpdkRandomAccessFile(const std::string &fname, const EnvOptions &options, uint32_t mi)
{
	mI = mi;
	spdk_fs_open_file(g_fs[mI], g_sync_args.channel[mI], fname.c_str(), SPDK_BLOBFS_OPEN_CREATE, &mFile);
}

SpdkRandomAccessFile::~SpdkRandomAccessFile(void)
{
	spdk_file_close(mFile, g_sync_args.channel[mI]);
}

Status
SpdkRandomAccessFile::Read(uint64_t offset, size_t n, Slice *result, char *scratch) const
{
	spdk_file_read(mFile, g_sync_args.channel[mI], scratch, offset, n);
	*result = Slice(scratch, n);
	return Status::OK();
}

Status
SpdkRandomAccessFile::InvalidateCache(size_t offset, size_t length)
{
	return Status::OK();
}

class SpdkWritableFile : public WritableFile
{
	struct spdk_file *mFile;
	uint32_t mSize;
	uint32_t mI;

public:
	SpdkWritableFile(const std::string &fname, const EnvOptions &options, uint32_t mi);
	~SpdkWritableFile()
	{
		if (mFile != NULL) {
			Close();
		}
	}

	virtual void SetIOPriority(Env::IOPriority pri)
	{
		if (pri == Env::IO_HIGH) {
			spdk_file_set_priority(mFile, SPDK_FILE_PRIORITY_HIGH);
		}
	}

	virtual Status Truncate(uint64_t size) override
	{
		spdk_file_truncate(mFile, g_sync_args.channel[mI], size);
		mSize = size;
		return Status::OK();
	}
	virtual Status Close() override
	{
		spdk_file_close(mFile, g_sync_args.channel[mI]);
		mFile = NULL;
		return Status::OK();
	}
	virtual Status Append(const Slice &data) override;
	virtual Status Flush() override
	{
		return Status::OK();
	}
	virtual Status Sync() override
	{
		spdk_file_sync(mFile, g_sync_args.channel[mI]);
		return Status::OK();
	}
	virtual Status Fsync() override
	{
		spdk_file_sync(mFile, g_sync_args.channel[mI]);
		return Status::OK();
	}
	virtual bool IsSyncThreadSafe() const override
	{
		return true;
	}
	virtual uint64_t GetFileSize() override
	{
		return mSize;
	}
	virtual Status InvalidateCache(size_t offset, size_t length) override
	{
		return Status::OK();
	}
#ifdef ROCKSDB_FALLOCATE_PRESENT
	virtual Status Allocate(uint64_t offset, uint64_t len) override
	{
		spdk_file_truncate(mFile, g_sync_args.channel[mI], offset + len);
		return Status::OK();
	}
	virtual Status RangeSync(uint64_t offset, uint64_t nbytes) override
	{
		/*
		 * SPDK BlobFS does not have a range sync operation yet, so just sync
		 *  the whole file.
		 */
		spdk_file_sync(mFile, g_sync_args.channel[mI]);
		return Status::OK();
	}
	virtual size_t GetUniqueId(char *id, size_t max_size) const override
	{
		return 0;
	}
#endif
};

SpdkWritableFile::SpdkWritableFile(const std::string &fname, const EnvOptions &options, uint32_t mi) : mSize(0)
{
	mI = mi;
	spdk_fs_open_file(g_fs[mI], g_sync_args.channel[mI], fname.c_str(), SPDK_BLOBFS_OPEN_CREATE, &mFile);
}

Status
SpdkWritableFile::Append(const Slice &data)
{
	spdk_file_write(mFile, g_sync_args.channel[mI], (void *)data.data(), mSize, data.size());
	mSize += data.size();

	return Status::OK();
}

class SpdkDirectory : public Directory
{
public:
	SpdkDirectory() {}
	~SpdkDirectory() {}
	Status Fsync() override
	{
		return Status::OK();
	}
};

class SpdkEnv : public EnvWrapper
{
private:
	pthread_t mSpdkTid;
	std::vector<std::string> mDirectory;

public:
	SpdkEnv(Env *base_env, const std::string &dir, const std::string &conf,
		const std::string &bdev, uint64_t cache_size_in_mb);

	virtual ~SpdkEnv();

	virtual Status NewSequentialFile(const std::string &fname,
					 unique_ptr<SequentialFile> *result,
					 const EnvOptions &options) override
	{
		uint32_t mi = 0;
		if (find_directory_index(fname, mDirectory, &mi)) {
			struct spdk_file *file;
			int rc;
			
			std::string name = sanitize_path(fname, mDirectory[mi]);
			rc = spdk_fs_open_file(g_fs[mi], g_sync_args.channel[mi],
					       name.c_str(), 0, &file);
			if (rc == 0) {
				result->reset(new SpdkSequentialFile(file, mi));
				return Status::OK();
			} else {
				/* Myrocks engine uses errno(ENOENT) as one
				 * special condition, for the purpose to
				 * support MySQL, set the errno to right value.
				 */
				errno = -rc;
				return Status::IOError(name, strerror(errno));
			}
		} else {
			return EnvWrapper::NewSequentialFile(fname, result, options);
		}
	}

	virtual Status NewRandomAccessFile(const std::string &fname,
					   unique_ptr<RandomAccessFile> *result,
					   const EnvOptions &options) override
	{
		uint32_t mi = 0;
		if (find_directory_index(fname, mDirectory, &mi)) {
			std::string name = sanitize_path(fname, mDirectory[mi]);
			result->reset(new SpdkRandomAccessFile(name, options, mi));
			return Status::OK();
		} else {
			return EnvWrapper::NewRandomAccessFile(fname, result, options);
		}
	}

	virtual Status NewWritableFile(const std::string &fname,
				       unique_ptr<WritableFile> *result,
				       const EnvOptions &options) override
	{
	//	if (fname.compare(0, mDirectory[0].length(), mDirectory[0]) == 0) {
		uint32_t mi = 0;
		if (find_directory_index(fname, mDirectory, &mi)) {
			std::string name = sanitize_path(fname, mDirectory[mi]);
			result->reset(new SpdkWritableFile(name, options, mi));
			return Status::OK();
		} else {
			return EnvWrapper::NewWritableFile(fname, result, options);
		}
	}

	virtual Status ReuseWritableFile(const std::string &fname,
					 const std::string &old_fname,
					 unique_ptr<WritableFile> *result,
					 const EnvOptions &options) override
	{
		return EnvWrapper::ReuseWritableFile(fname, old_fname, result, options);
	}

	virtual Status NewDirectory(const std::string &name,
				    unique_ptr<Directory> *result) override
	{
		result->reset(new SpdkDirectory());
		return Status::OK();
	}
	virtual Status FileExists(const std::string &fname) override
	{
		struct spdk_file_stat stat;
		int rc;
		uint32_t mi = 0;
		find_directory_index(fname, mDirectory, &mi);
		std::string name = sanitize_path(fname, mDirectory[mi]);

		rc = spdk_fs_file_stat(g_fs[mi], g_sync_args.channel[mi], name.c_str(), &stat);
		if (rc == 0) {
			return Status::OK();
		}
		return EnvWrapper::FileExists(fname);
	}
	virtual Status RenameFile(const std::string &src, const std::string &target) override
	{
		int rc;
		uint32_t mi = 0;
		find_directory_index(src, mDirectory, &mi);
		std::string src_name = sanitize_path(src, mDirectory[mi]);
		std::string target_name = sanitize_path(target, mDirectory[mi]);

		rc = spdk_fs_rename_file(g_fs[mi], g_sync_args.channel[mi],
					 src_name.c_str(), target_name.c_str());
		if (rc == -ENOENT) {
			return EnvWrapper::RenameFile(src, target);
		}
		return Status::OK();
	}
	virtual Status LinkFile(const std::string &src, const std::string &target) override
	{
		return Status::NotSupported("SpdkEnv does not support LinkFile");
	}
	virtual Status GetFileSize(const std::string &fname, uint64_t *size) override
	{
		struct spdk_file_stat stat;
		int rc;
		uint32_t mi = 0;
		find_directory_index(fname, mDirectory, &mi);
		std::string name = sanitize_path(fname, mDirectory[mi]);

		rc = spdk_fs_file_stat(g_fs[mi], g_sync_args.channel[mi], name.c_str(), &stat);
		if (rc == -ENOENT) {
			return EnvWrapper::GetFileSize(fname, size);
		}
		*size = stat.size;
		return Status::OK();
	}
	virtual Status DeleteFile(const std::string &fname) override
	{
		int rc;
		uint32_t mi = 0;
		find_directory_index(fname, mDirectory, &mi);
		std::string name = sanitize_path(fname, mDirectory[mi]);

		rc = spdk_fs_delete_file(g_fs[mi], g_sync_args.channel[mi], name.c_str());
		if (rc == -ENOENT) {
			return EnvWrapper::DeleteFile(fname);
		}
		return Status::OK();
	}
	virtual void StartThread(void (*function)(void *arg), void *arg) override;
	virtual Status LockFile(const std::string &fname, FileLock **lock) override
	{
		uint32_t mi = 0;
		find_directory_index(fname, mDirectory, &mi);
		std::string name = sanitize_path(fname, mDirectory[mi]);

		spdk_fs_open_file(g_fs[mi], g_sync_args.channel[mi], name.c_str(),
				  SPDK_BLOBFS_OPEN_CREATE, (struct spdk_file **)lock);
		return Status::OK();
	}
	virtual Status UnlockFile(FileLock *lock) override
	{
		spdk_file_close((struct spdk_file *)lock, g_sync_args.channel[0]);
		return Status::OK();
	}
	virtual Status GetChildren(const std::string &dir,
				   std::vector<std::string> *result) override
	{
		std::string::size_type pos;
		std::set<std::string> dir_and_file_set;
		std::string full_path;
		std::string filename;
		std::string dir_name;

		if (dir.find("archive") != std::string::npos) {
			return Status::OK();
		}
		//if (dir.compare(0, mDirectory[0].length(), mDirectory[0]) == 0) {
		uint32_t mi = 0;
		if (find_directory_index_dir(dir, mDirectory, &mi)) {
			spdk_fs_iter iter;
			struct spdk_file *file;
			dir_name = sanitize_path(dir, mDirectory[mi]);

			iter = spdk_fs_iter_first(g_fs[mi]);
			while (iter != NULL) {
				file = spdk_fs_iter_get_file(iter);
				full_path = spdk_file_get_name(file);
				if (strncmp(dir_name.c_str(), full_path.c_str(), dir_name.length())) {
					iter = spdk_fs_iter_next(iter);
					continue;
				}
				pos = full_path.find("/", dir_name.length() + 1);

				if (pos != std::string::npos) {
					filename = full_path.substr(dir_name.length() + 1, pos - dir_name.length() - 1);
				} else {
					filename = full_path.substr(dir_name.length() + 1);
				}
				dir_and_file_set.insert(filename);
				iter = spdk_fs_iter_next(iter);
			}

			for (auto &s : dir_and_file_set) {
				result->push_back(s);
			}

			result->push_back(".");
			result->push_back("..");

			return Status::OK();
		}
		return EnvWrapper::GetChildren(dir, result);
	}
};

static void
_spdk_send_msg(spdk_thread_fn fn, void *ctx, void *thread_ctx)
{
	/* Not supported */
	assert(false);
}

void SpdkInitializeThread(void)
{
	if (g_fs != NULL) {
		spdk_allocate_thread(_spdk_send_msg, NULL, NULL);
		//spdk_allocate_thread(_spdk_send_msg, NULL, "spdk_rocksdb");
		
		for(uint32_t i=0; i < g_num; i++) {
			g_sync_args.channel[i] = spdk_fs_alloc_io_channel_sync(g_fs[i]);
		}
	}
}

struct SpdkThreadState {
	void (*user_function)(void *);
	void *arg;
};

static void SpdkStartThreadWrapper(void *arg)
{
	SpdkThreadState *state = reinterpret_cast<SpdkThreadState *>(arg);

	SpdkInitializeThread();
	state->user_function(state->arg);
	delete state;
}

void SpdkEnv::StartThread(void (*function)(void *arg), void *arg)
{
	SpdkThreadState *state = new SpdkThreadState;
	state->user_function = function;
	state->arg = arg;
	EnvWrapper::StartThread(SpdkStartThreadWrapper, state);
}

static void
fs_load_cb(void *ctx, struct spdk_filesystem *fs, int fserrno)
{
	if (fserrno == 0) {
		uint32_t *i = (uint32_t *)ctx;
		g_fs[*i] = fs;
		g_loaded_fs++;
	}
	if(g_loaded_fs == g_num) {
		g_spdk_ready = true;
	}
}

static void 
__fs_load_fn(void *arg1, void *arg2)
{
	struct spdk_bs_dev *bs_dev = (struct spdk_bs_dev *)arg1;

	spdk_fs_load(bs_dev, __send_request, arg2, fs_load_cb, arg2);
}

static void
spdk_rocksdb_run(void *arg1, void *arg2)
{
	struct spdk_bdev *bdev;
	struct spdk_bs_dev *bs_dev;
	std::vector<std::string>* bdev_names = (std::vector<std::string>*)arg1;

	g_num = spdk_env_get_core_count();

	if (g_num != (*bdev_names).size()) {
	  printf("Error: Num cores and num devices are not equal.\n");
		spdk_app_stop(0);
		exit(1);
	}

	g_lcore = new uint32_t[g_num];
	g_index = new uint32_t[g_num];
	g_fs = new struct spdk_filesystem*[g_num];

	g_lcore[0] = spdk_env_get_first_core();
	g_index[0] = 0;
	for(uint32_t i=1; i < g_num; i++) {
		g_lcore[i] = spdk_env_get_next_core(g_lcore[i-1]);
		g_index[i] = i;
	}

	for (uint32_t i=0; i<(*bdev_names).size(); i++) {
		bdev = spdk_bdev_get_by_name((*bdev_names)[i].c_str());

		if (bdev == NULL) {
			SPDK_ERRLOG("bdev %s not found\n", (*bdev_names)[i].c_str());
			exit(1);
		}

		bs_dev = spdk_bdev_create_bs_dev(bdev);
		spdk_event_call(spdk_event_allocate(g_lcore[*(g_index+i)], __fs_load_fn, (void *)bs_dev, g_index+i));
		printf("using bdev %s\n", (*bdev_names)[i].c_str());
	}

}

static void
fs_unload_cb(void *ctx, int fserrno)
{
	assert(fserrno == 0);

	g_unloaded_fs++;
	if(g_unloaded_fs == g_num){
		spdk_app_stop(0);
	}
}

static void
spdk_rocksdb_shutdown(void)
{
	if (g_fs != NULL) {
		for(uint32_t i=0; i < g_num; i++) {
			spdk_fs_unload(g_fs[i], fs_unload_cb, NULL);
		}
	} else {
		fs_unload_cb(NULL, 0);
	}
}

static void *
initialize_spdk(void *arg)
{
	struct init_args *iopts = (struct init_args *)arg;

	spdk_app_start(iopts->opts, spdk_rocksdb_run, iopts->bdev_names, NULL);
	spdk_app_fini();

	delete iopts->opts;
	delete iopts->bdev_names;
	delete iopts;
	delete g_lcore;
	delete g_fs;
	pthread_exit(NULL);
}

SpdkEnv::SpdkEnv(Env *base_env, const std::string &dir, const std::string &conf,
		 const std::string &bdev, uint64_t cache_size_in_mb)
	: EnvWrapper(base_env)
{
	struct spdk_app_opts *opts = new struct spdk_app_opts;

	std::vector<std::string> *bdevnames = new std::vector<std::string>;

	std::stringstream snames, sdir;
	snames.str(bdev);
	std::string n;
	while(std::getline(snames, n, ',')) {
		(*bdevnames).push_back(n);
	}

	sdir.str(dir);
	while(std::getline(sdir, n, ',')) {
		mDirectory.push_back(n);
	}

	struct init_args *iopts = new struct init_args;
	iopts->opts = opts;
	iopts->bdev_names = bdevnames;

	spdk_app_opts_init(opts);
	opts->name = "rocksdb";
	opts->config_file = conf.c_str();
	opts->mem_size = 1024 + cache_size_in_mb;
	opts->shutdown_cb = spdk_rocksdb_shutdown;

	spdk_fs_set_cache_size(cache_size_in_mb);

	pthread_create(&mSpdkTid, NULL, &initialize_spdk, iopts);
	while (!g_spdk_ready)
		;

	SpdkInitializeThread();
}

SpdkEnv::~SpdkEnv()
{
	/* This is a workaround for rocksdb test, we close the files if the rocksdb not
	 * do the work before the test quit.
	 */
	if (g_fs != NULL) {
		spdk_fs_iter iter;
		struct spdk_file *file;

		if(!g_sync_args.channel) {
			SpdkInitializeThread();
		}
		for(uint32_t i=0; i < g_num; i++) {
			iter = spdk_fs_iter_first(g_fs[i]);
			while (iter != NULL) {
				file = spdk_fs_iter_get_file(iter);
				spdk_file_close(file, g_sync_args.channel[i]);
				iter = spdk_fs_iter_next(iter);
			}
		}
	}

	spdk_app_start_shutdown();
	pthread_join(mSpdkTid, NULL);
}

Env *NewSpdkEnv(Env *base_env, const std::string &dir, const std::string &conf,
		const std::string &bdev, uint64_t cache_size_in_mb)
{
	SpdkEnv *spdk_env = new SpdkEnv(base_env, dir, conf, bdev, cache_size_in_mb);

	if (g_fs != NULL) {
		return spdk_env;
	} else {
		delete spdk_env;
		return NULL;
	}
}

} // namespace rocksdb
