CFLAGS= -g -Wall -D_GNU_SOURCE -m64 -fPIC
LDFLAGS= -lm
MYLIB=libkvnvme.a
MYLIB_SOURCE=kv_nvme.c
MYLIB_OBJ=$(MYLIB_SOURCE:.c=.o)
TEST_PROG=kv_store kv_store_unalign kv_store_16 kv_store_aio kv_store_aio_unalign kv_retrieve kv_retrieve_unalign  kv_retrieve_16 kv_retrieve_aio kv_retrieve_aio_unalign kv_delete kv_delete_aio kv_async_perf  kv_sync_perf kv_write_read kv_iterate_req kv_iterate_req_aio kv_iterate_read kv_iterate_read_aio kv_iterate_read_one kv_iterate_read_one_aio kv_exist kv_exist_aio kv_iter_get_log
#kv_exist kv_exist_aio kv_iterate  kv_iterate_aio


AR=ar
CC=gcc




all : $(MYLIB) $(TEST_PROG) 

$(MYLIB): $(MYLIB_SOURCE)
	$(CC) $(CFLAGS) -c $(MYLIB_SOURCE)
	$(AR) -cvq $(MYLIB) $(MYLIB_OBJ)

kv_write_read: kv_write_read.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_store: kv_store.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_store_unalign: kv_store_unalign.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_store_16: kv_store_16.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_store_aio: kv_store_aio.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_store_aio_unalign: kv_store_aio_unalign.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_retrieve: kv_retrieve.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_retrieve_unalign: kv_retrieve_unalign.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_retrieve_16: kv_retrieve_16.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_retrieve_aio: kv_retrieve_aio.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_retrieve_aio_unalign: kv_retrieve_aio_unalign.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_delete: kv_delete.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_delete_aio: kv_delete_aio.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_iterate_req: kv_iterate_req.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_iterate_req_aio: kv_iterate_req_aio.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_iterate_read: kv_iterate_read.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_iterate_read_aio: kv_iterate_read_aio.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_iterate_read_one: kv_iterate_read_one.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_iterate_read_one_aio: kv_iterate_read_one_aio.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_exist: kv_exist.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_exist_aio: kv_exist_aio.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)

kv_async_perf: kv_async_perf.c
	$(CC) $(CFLAGS) $@.c -o $@ $(LDFLAGS) -lpthread

kv_sync_perf: kv_sync_perf.c
	$(CC) $(CFLAGS) $@.c -o $@ $(LDFLAGS) -lpthread

kv_iter_get_log: kv_iter_get_log.c
	$(CC) $(CFLAGS) $@.c -o $@ $(MYLIB_OBJ) $(LDFLAGS)


clean:
	-rm -f $(MYLIB) $(TEST_PROG) *.o
