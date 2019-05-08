// Added for scheduler
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdarg.h>
#include "common/stack_trace.h"
#include "acconfig.h"

#include <fstream>
#include <iostream>
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>
#include <ctype.h>
#include <boost/scoped_ptr.hpp>

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

#include "osd/PG.h"

#include "include/types.h"
#include "include/compat.h"

#include "OSD.h"
#include "OSDMap.h"
#include "Watch.h"
#include "osdc/Objecter.h"
#include "PrimaryLogPG.h"

#include "common/perf_counters.h"
#include "common/Timer.h"
#include "common/LogClient.h"
#include "common/ceph_context.h"

#include "global/signal_handler.h"
#include "global/pidfile.h"

#include "include/color.h"
#include "perfglue/cpu_profiler.h"
#include "perfglue/heap_profiler.h"

#include "osd/OpRequest.h"

#include "auth/AuthAuthorizeHandler.h"
#include "auth/RotatingKeyRing.h"
#include "common/errno.h"

#include "objclass/objclass.h"

#include "common/cmdparse.h"
#include "include/str_list.h"
#include "include/util.h"

#include "include/assert.h"
#include "common/config.h"
#include "common/EventTrace.h"
#include "rr_spinlock.h"

#define dout_context cct
#define dout_subsys ceph_subsys_osd
#undef dout_prefix
#define dout_prefix _prefix(_dout, whoami, get_osdmap_epoch())


// Added for epoll mechanism
#define MAX_PG_LIMIT 307200
static int SignalFD_Queue[MAX_PG_LIMIT];
static int Event_EpollFD;
struct epoll_event main_event;
struct epoll_event *sched_events;

// =============================================================

#undef dout_context
#define dout_context osd->cct
#undef dout_prefix
#define dout_prefix *_dout << "osd." << osd->whoami << " op_wq "


int OSD::EpollOpWQ::init() {
    // Start the epoll and eventfd mechanism here
    Event_EpollFD = epoll_create(1024); // this value does not matter, just a hint to the kernel
    if (Event_EpollFD<0){
        derr << __func__ << " Epoll:## Unable to do epoll_create: " << cpp_strerror(errno)
             << dendl;
        return -errno;
    }
    dout(2) << __func__ << " Epoll:## Event_EpollFD "<< Event_EpollFD
            << " , pid:"<< ::getpid() << " " << dendl;
    for (int i =0; i<MAX_PG_LIMIT;i++){
        SignalFD_Queue[i] = eventfd(0,EFD_NONBLOCK); // Create signalfd for every PG queue
        if (SignalFD_Queue[i] < 0){
            derr << __func__ << " Epoll:## Failed to create SignalFD: "<< i << dendl;
        }

        main_event.events = EPOLLIN;// | EPOLLONESHOT;
        main_event.data.fd = SignalFD_Queue[i];
        int ret;
        // register interest for fd SignalFD_Queue[i]
        ret = epoll_ctl(Event_EpollFD, EPOLL_CTL_ADD, SignalFD_Queue[i], &main_event);
        if (ret != 0){
            derr << __func__ << " Epoll:## Failed to perform ctl ADD for FD: " << SignalFD_Queue[i]
                 << " EventPoll FD: "<< Event_EpollFD << " ..Error " << cpp_strerror(errno)<< dendl;
            return ret;
        }
    }
    return 0;
}


void OSD::EpollOpWQ::wake_pg_waiters(spg_t pgid)
{
    PGDataRef sdata = get_pgshard(pgid);
    if (sdata == nullptr) {
        return;
    }

    std::unique_lock<std::mutex> lock(sdata->sdata_op_ordering_lock);
    if (sdata->pg) {
        sdata->waiting_for_pg = false;
    }
    lock.unlock();

    /// when queue is not empty
    if (sdata->pending_reqs.load()) {
        std::unique_lock<std::mutex> lock(sdata->sdata_lock);
        sdata->sdata_cond.notify_one();
    }
}


void OSD::EpollOpWQ::prune_pg_waiters(OSDMapRef osdmap, int whoami)
{
    unsigned pushes_to_free = 0;

    // go through all pools
    uint32_t pgarray_index = 0;
    for (uint32_t i = 0; i < pool_pgindex_array_size; i++) {
        pool_pgindex_t *poolpgs = pool_pgindex_array + i;
        uint32_t pgcount = poolpgs->pgcount.load();

        // no pgs, skip this pool
        if (pgcount == 0) {
            continue;
        }
        for (uint32_t j = 0; j < pgcount; j++) {
            pgarray_index = poolpgs->pgqueue_index[j];
            PGDataRef sdata = pgqueue_array[pgarray_index];
            spg_t pgid = sdata->pgid;

            // if no data, just skip
            if (!sdata->valid.load() || sdata->pending_reqs.load() == 0) {
                continue;
            }
            std::unique_lock<std::mutex> lock(sdata->sdata_op_ordering_lock);
            sdata->waiting_for_pg_osdmap = osdmap;

            if (!sdata->pqueue->empty() && sdata->num_running == 0) {
                if (osdmap->is_up_acting_osd_shard(pgid, whoami)) {
                    dout(20) << __func__ << "  " << pgid << " maps to us, keeping"
                             << dendl;
                    continue;
                }

                while (!sdata->pqueue->empty()) {

                    // dequeue an item to look
                    // if we can make pqueue like a regular queue, that would be great
                    pair<spg_t, PGQueueable> item = sdata->pqueue->dequeue();
                    PGQueueable qi = item.second;
                    if (qi.get_map_epoch() <= osdmap->get_epoch()) {
                        dout(20) << __func__ << "  " << pgid
                                 << " item " << qi
                                 << " epoch " << qi.get_map_epoch()
                                 << " <= " << osdmap->get_epoch()
                                 << ", stale, dropping" << dendl;
                        pushes_to_free += qi.get_reserved_pushes();
                        // assume here qi is reference counted, will go out of scope by itself
                        sdata->pending_reqs--;
                    } else {
                        // put the item back to pqueue, we are done for this PG
                        sdata->_enqueue_front(item, osd->op_prio_cutoff);
                        continue;
                    }
                }
            }
        }
    }

    if (pushes_to_free > 0) {
        osd->service.release_reserved_pushes(pushes_to_free);
    }
}


void OSD::EpollOpWQ::clear_pg_pointer(spg_t pgid)
{
    PGDataRef sdata = get_pgshard(pgid);
    std::unique_lock<std::mutex> lock(sdata->sdata_op_ordering_lock);
    sdata->pg = nullptr;
}

void OSD::EpollOpWQ::clear_pg_slots()
{
    // go through all pools
    uint32_t pgarray_index = 0;
    for (uint32_t i = 0; i < pool_pgindex_array_size; i++) {
        pool_pgindex_t *poolpgs = pool_pgindex_array + i;
        uint32_t pgcount = poolpgs->pgcount.load();

        // no pgs, skip this pool
        if (pgcount == 0) {
            continue;
        }
        for (uint32_t j = 0; j < pgcount; j++) {
            pgarray_index = poolpgs->pgqueue_index[j];
            PGDataRef sdata = pgqueue_array[pgarray_index];
            // if no data, just skip
            if (!sdata->valid.load() || sdata->pending_reqs.load() == 0) {
                continue;
            }
            std::unique_lock<std::mutex> lock(sdata->sdata_op_ordering_lock);
            sdata->waiting_for_pg_osdmap.reset();
            // don't bother with reserved pushes; we are shutting down
        }
    }
}


#undef dout_prefix
// #define dout_prefix *_dout << "osd." << osd->whoami << " op_wq(" << shard_index << ") "
#define dout_prefix *_dout << "osd." << osd->whoami


void OSD::EpollOpWQ::_process(uint32_t thread_index, heartbeat_handle_d *hb)
{
    // can add a global atomic variable to check if there are any queues pending
    /*uint32_t num_pgs = pg_count.load();
    // no PG yet, just return
    if (num_pgs == 0) {
        return;
      }  	  */

    // Added for epoll scheduler
    // We want each thread to process just one ready event
    uint32_t max_events_per_thread = 1;
    // buffer for adding fds of ready events
    struct epoll_event eventlist[max_events_per_thread];
    //dout(10) << __func__ << " Epoll:## Entering Epoll, Event_EpollFD: "<< Event_EpollFD
    //				   << ", thread_index: "<< thread_index << " " << dendl;
    uint32_t pg_index;
    int timeout = -1;
    timeout = 0;

    int waiting_epoll = epoll_wait(Event_EpollFD, eventlist, 1, timeout);

    if (waiting_epoll < 0){
        //derr << __func__ << " Epoll:## Epoll wait failed " << dendl;
        return;
    } else if (waiting_epoll == 0){
        //dout(2) << __func__ << " Epoll:## No events till timeout:"<< timeout
        //					  << " for thread_index: " << thread_index << " "
        //					  << dendl;
        return;
    }


    //dout(10) << __func__ << " Epoll:## epoll_wait result number_events:"
    //					   << waiting_epoll << ", thread_index "<< thread_index
    //					   << " "<< dendl;

    uint32_t ready_index = eventlist[0].data.fd;
    pg_index = ready_index - SignalFD_Queue[0];

    //dout(20) << __func__ << " Epoll:## Ready fd eventlist[0].data.fd:"
    //					   << ready_index << " pg_index: "<< pg_index << ", thread_index: "
    //				       << thread_index << dendl;

    PGDataRef sdata = get_pgqueue(pg_index);
    // now we have an access to a queue
    std::unique_lock<std::mutex> lock(sdata->sdata_op_ordering_lock);

    // change to ceph abort
    if (sdata == nullptr) {
        ceph_abort_msg(osd->cct, "sdata is null");
    }
    if (sdata->pqueue->empty()) {
        char buffer[256];
        sprintf(buffer, "signal was fired but the queue was empty, pgIndex = %d\n", pg_index);
        //dout (2) << buffer << dendl;
        return;
        ceph_abort_msg(osd->cct,buffer);
    }

    // if there are other threads working on this queue
    if (sdata->num_running.load() > 0) {
        uint64_t counter = 1;
        int fd_write = write(SignalFD_Queue[pg_index], &counter, sizeof(uint64_t));
        if (fd_write < 0){
            //derr << __func__ << " Epoll:## Failed to re-enable "<< fd_write << "on FD "
            //				   << SignalFD_Queue[pg_index] << " , counter "<< counter << dendl;
        }

        // dout(2) << __func__ << " Epoll:## Success to re-enable "<< fd_write << "on FD "
        //				   << SignalFD_Queue[pg_index] << " , counter "<< counter << dendl;

        return;
    }

// dout(2) << __func__ << "Epoll:## 11" << dendl;
#if 0
    if (sdata->num_running.load() > 0) {
	  //dout(2) << __func__ << "Epoll:## 12 ERR" << dendl;
    /*
	// Added for epoll scheduler
    // Re-enable the queue
	main_event.events = EPOLLIN | EPOLLONESHOT;
    main_event.data.fd = SignalFD_Queue[pg_index];
    int ret;
	// register interest for fd SignalFD_Queue[i]
	ret = epoll_ctl(Event_EpollFD, EPOLL_CTL_MOD, SignalFD_Queue[pg_index], &main_event);
    if (ret < 0){
		derr << __func__ << "Epoll:## Failed to perform ctl MOD for FD:" << SignalFD_Queue[pg_index]
				<< " " << cpp_strerror(errno)<< dendl;
		return;
	}
	uint64_t counter = 1;
    int fd_write = write(SignalFD_Queue[pg_index], &counter, sizeof(uint64_t));
    if (fd_write < 0){
	  derr << __func__ << " Epoll:## Failed to re-enable "<< fd_write << "on FD "
					   << SignalFD_Queue[pg_index] << " , counter "<< counter << dendl;
    }

    dout(2) << __func__ << " Epoll:## Success to re-enable "<< fd_write << "on FD "
					   << SignalFD_Queue[pg_index] << " , counter "<< counter << dendl;
	*/
	return;
  }




  if (sdata->pqueue->empty()) {
	  // dout(2) << __func__ << "Epoll:## 13" << dendl;
	  return;

     // derr << __func__ << " empty pg queue q," << pg_index
	 //    <<", thread_index "<< thread_index
	 //	 <<", skipping " << dendl;
    lock.unlock();
	//pg->unlock();
	return;
  }
#endif


    // dout(2) << __func__ << "Epoll:## 14 sdata->pqueue->length()=" << sdata->pqueue->length() << dendl;
    // take one op out of pqueue
    pair<spg_t, PGQueueable> item = sdata->pqueue->dequeue();
    // dout(2) << __func__ << "Epoll:## 15 sdata->pqueue->length()=" << sdata->pqueue->length() << dendl;
    boost::optional<PGQueueable> qi;
    boost::optional<OpRequestRef> _op;
    utime_t latency;
    utime_t now = ceph_clock_now();
    // process the item
    qi = item.second;
    _op = qi->maybe_get_op();

    // record actual dequeue time
    if (_op) {
        (*_op)->set_dequeued_time(now);
        latency = now - (*_op)->get_req()->get_recv_stamp();
        osd->logger->tinc(l_osd_op_before_dequeue_op_lat, latency);
    }


    sdata->pending_reqs--;
    sdata->num_running++;

    // dout(2) << __func__ << "Epoll:## 16 reqs =" << sdata->pending_reqs << ", running" << sdata->num_running << dendl;
    // check if PG is good
    PGRef pg = sdata->pg;

    // dout(2) << __func__ << " Epoll:## 1. Dequeue pg_index:" << pg_index
    //			          << ", thread_index:"<< thread_index << " " << dendl;
    // got one OP, allow enqueue
    lock.unlock();
    osd->service.maybe_inject_dispatch_delay();

    if (osd->is_stopping()) {
        // sdata->one_pg_op_lock.Unlock();
        return;    // OSD shutdown, discard.
    }


    // dout(20) << __func__ << " " << item.first << " item " << *qi
    //	   << " pg " << pg << dendl;

    // [lookup +] lock pg (if we have it)
    if (!pg) {
        pg = osd->_lookup_lock_pg(item.first);
    } else {
        pg->lock();
    }

    lock.lock();
    if (pg && !sdata->pg && !pg->deleting) {
        sdata->pg = pg;
    }

    osd->service.maybe_inject_dispatch_delay();

    // make sure we're not already waiting for this pg
    if (sdata->waiting_for_pg) {
        if (pg) {
            pg->unlock();
        }
        sdata->num_running.fetch_sub(1);
        return;
    }

    // if PG is still not there yet
    if (!pg) {
        while (!pg) {

            sched_yield();
        }
        // should this pg shard exist on this osd in this (or a later) epoch?
        OSDMapRef osdmap = sdata->waiting_for_pg_osdmap;
        if (osdmap->is_up_acting_osd_shard(item.first, osd->whoami)) {
            // dout(20) << __func__ << " " << item.first
            //     << " no pg, should exist, will wait" << " on " << *qi << dendl;
            sdata->waiting_for_pg = true;
            // put it back
            sdata->_enqueue_front(item, osd->op_prio_cutoff);
            sdata->pending_reqs++;
        } else if (qi->get_map_epoch() > osdmap->get_epoch()) {
            // dout(20) << __func__ << " " << item.first << " no pg, item epoch is "
            //     << qi->get_map_epoch() << " > " << osdmap->get_epoch()
            //     << ", will wait on " << *qi << dendl;
            sdata->waiting_for_pg = true;
            // put it back
            //sdata->_enqueue_front(item, osd->op_prio_cutoff);
            //sdata->pending_reqs++;
        } else {
            // dout(20) << __func__ << " " << item.first << " no pg, shouldn't exist,"
            //     << " dropping " << *qi << dendl;
            // invalidated request
            sdata->pending_reqs--;
            // share map with client?
            if (boost::optional<OpRequestRef> _op = qi->maybe_get_op()) {
                Session *session = static_cast<Session *>(
                        (*_op)->get_req()->get_connection()->get_priv());
                if (session) {
                    osd->maybe_share_map(session, *_op, sdata->waiting_for_pg_osdmap);
                    session->put();
                }
            }
            unsigned pushes_to_free = qi->get_reserved_pushes();
            if (pushes_to_free > 0) {
                sdata->num_running--;
                osd->service.release_reserved_pushes(pushes_to_free);
                return;
            }
        }
        sdata->num_running--;
        return;
    }
    lock.unlock();

    // now we assume there is a active worker, update total worker count average
    uint64_t total_active = threads_active.fetch_add(1);
    now = ceph_clock_now();
    utime_t time_since_lastcheck = now - sdata->last_time_active_threads_counted;
    // sample the active worker every 100 milliseconds
    if (time_since_lastcheck.to_msec() > 500) {
        osd->logger->inc(l_osd_active_opworker_count, total_active);
        sdata->last_time_active_threads_counted = now;
    }

    // PG locked already, record time
    utime_t pglock_start = ceph_clock_now();
    if (_op) {
        (*_op)->set_pglock_start_time(pglock_start);
    }

    // dout(2) << __func__ << "Epoll:## 27 "  << dendl;
    // with a good PG and one op to process
    // note the requeue seq now...
    // osd_opwq _process marks the point at which an operation has been dequeued
    // and will begin to be handled by a worker thread.
    {
#ifdef WITH_LTTNG
        osd_reqid_t reqid;
    if (boost::optional<OpRequestRef> _op = qi->maybe_get_op()) {
      reqid = (*_op)->get_reqid();
    }
#endif

        // tracepoint(osd, opwq_process_start, reqid.name._type,
        //           reqid.name._num, reqid.tid, reqid.inc);
    }

    lgeneric_subdout(osd->cct, osd, 30) << "dequeue status: ";
    Formatter *f = Formatter::create("json");
    f->open_object_section("q");
    dump(f);
    f->close_section();
    f->flush(*_dout);
    delete f;
    *_dout << dendl;

    ThreadPool::TPHandle tp_handle(osd->cct, hb, timeout_interval,
                                   suicide_interval, thread_index);
    qi->run(osd, pg, tp_handle);

    {
#ifdef WITH_LTTNG
        osd_reqid_t reqid;
    if (boost::optional<OpRequestRef> _op = qi->maybe_get_op()) {
      reqid = (*_op)->get_reqid();
    }
#endif
        // tracepoint(osd, opwq_process_finish, reqid.name._type,
        //           reqid.name._num, reqid.tid, reqid.inc);
    }

    // dout(2) << __func__ << "Epoll:## , MOD, " << pg_index << ", queue size " << sdata->pqueue->length() << dendl;
    // finished request
    sdata->pending_reqs--;
    pg->unlock();


    sdata->num_running--;
    sdata->waiting_for_pg.store(false);
    // decrease active worker count#
    threads_active.fetch_sub(1);

    // PG unlocked already, record time
    utime_t pglock_end = ceph_clock_now();
    utime_t pglock_time = pglock_end - pglock_start;

    if (_op) {
        OpRequestRef op = *_op;
        op->set_pglock_end_time(pglock_end);

        // avoid crash at log layer
        if (op->rmw_flags != 0) {
            if (op->may_read() && op->may_write()) {
                // read-modify-write
                osd->logger->tinc(l_osd_pg_lock_latency_rw, pglock_time);
            } else if (op->may_read()) {
                // read pglock time
                osd->logger->tinc(l_osd_pg_lock_latency_r, pglock_time);
            } else if (op->may_write() || op->may_cache()) {
                // write pglock time
                osd->logger->tinc(l_osd_pg_lock_latency_w, pglock_time);
            } else {
                ceph_abort();
            }
        }
    }

}


// this is called by disptacher to enqueue
void OSD::EpollOpWQ::_enqueue(pair<spg_t, PGQueueable> item) {

    // get pg info
    spg_t pgid = item.first;
    PGDataRef sdata = get_pgshard(pgid);
    assert (nullptr != sdata);
    unsigned priority = item.second.get_priority();
    unsigned cost = item.second.get_cost();

    std::unique_lock<std::mutex> lock(sdata->sdata_op_ordering_lock);

    //dout(20) << __func__ << " " << item.first << " "
    //					   << item.second << dendl;
    if (priority >= osd->op_prio_cutoff)
        sdata->pqueue->enqueue_strict(
                item.second.get_owner(), priority, item);
    else
        sdata->pqueue->enqueue(
                item.second.get_owner(),
                priority, cost, item);
    // Added for epoll scheduler

    //void (*prev_handler) (int);
    // signal corresponding signalfd
    uint32_t fdindex = get_pgindex(pgid);
    uint64_t counter = 1;
    int fd_write = write(SignalFD_Queue[fdindex], &counter, sizeof(uint64_t));
    if (fd_write < 0){
        derr << __func__ << " Epoll:## Failed to write "<< fd_write << " bytes on FD "
             << SignalFD_Queue[fdindex] << ", pg_index " << fd_write
             << " , counter "<< counter << dendl;
    }

    //dout(2) << __func__ << " Epoll:## Success to write "<< fd_write << " bytes on FD "
    //				   << SignalFD_Queue[fdindex] << ", pg_index " << fdindex
    //				   << " , counter "<< counter << dendl;

    //
    sdata->pending_reqs++;

    //////////////////////
    // request XXX debug
    /*
    {
      osd_reqid_t reqid;
      if (boost::optional<OpRequestRef> _op = item.second.maybe_get_op()) {
        reqid = (*_op)->get_reqid();
      }
      // dout(0) << "XXX enqueue reqid: " << reqid.name << reqid.tid << reqid.inc << " PG: " << item.first << dendl;
    }
    */

    /*
    std::unique_lock<std::mutex> lock(sdata->sdata_lock);
    sdata->sdata_cond.SignalOne();
    */
}

// this is called by disptacher to enqueue
void OSD::EpollOpWQ::_enqueue_front(pair<spg_t, PGQueueable> item)
{

    // get pg info
    spg_t pgid = item.first;
    PGDataRef sdata = get_pgshard(pgid);

    assert (NULL != sdata);

    std::unique_lock<std::mutex> lock(sdata->sdata_op_ordering_lock);
    sdata->_enqueue_front(item, osd->op_prio_cutoff);

    // Added for epoll scheduler
    uint32_t fdindex = get_pgindex(pgid);
    uint64_t counter = 1;
    int fd_write = write(SignalFD_Queue[fdindex], &counter, sizeof(uint64_t));
    if (fd_write < 0){
        derr << __func__ << " Epoll:## Failed to write "<< fd_write << "on FD "
             << SignalFD_Queue[fdindex] << " , counter "<< counter << dendl;
    }

    // dout(2) << __func__ << " Epoll:## Success to write "<< fd_write << "on FD "
    //					   << SignalFD_Queue[fdindex] << " , counter "<< counter << dendl;
    sdata->pending_reqs++;

    /*
    std::unique_lock<std::mutex> lock(sdata->sdata_lock);
    sdata->sdata_cond.SignalOne();
    */
}


/// ==============================================
/// Round Robin Scheduler
/// ==============================================


/// wake any pg waiters after a PG is created/instantiated
void OSD::RoundRobinOpWQ::wake_pg_waiters(spg_t pgid)
{
    PGDataRef sdata = get_pgshard(pgid);
    if (sdata == nullptr) {
        return;
    }
    std::unique_lock<std::mutex> lock(sdata->sdata_op_ordering_lock);
    if (sdata->pg) {
        sdata->waiting_for_pg = false;
    }
    lock.unlock();
    /// when queue is not empty
    if (sdata->pending_reqs.load()) {
        std::unique_lock<std::mutex> lock(sdata->sdata_lock);
        sdata->sdata_cond.notify_one();
    }
}

/// prune ops (and possiblye pg_slots) for pgs that shouldn't be here
void OSD::RoundRobinOpWQ::prune_pg_waiters(OSDMapRef osdmap, int whoami)
{
    unsigned pushes_to_free = 0;

    // go through all pools
    uint32_t pgarray_index = 0;
    for (uint32_t i = 0; i < pool_pgindex_array_size; i++) {
        pool_pgindex_t *poolpgs = pool_pgindex_array + i;
        uint32_t pgcount = poolpgs->pgcount.load();

        // no pgs, skip this pool
        if (pgcount == 0) {
            continue;
        }

        for (uint32_t j = 0; j < pgcount; j++) {
            pgarray_index = poolpgs->pgqueue_index[j];
            PGDataRef sdata = pgqueue_array[pgarray_index];
            spg_t pgid = sdata->pgid;

            std::unique_lock<std::mutex> lock(sdata->sdata_op_ordering_lock);
            sdata->waiting_for_pg_osdmap = osdmap;
            // if no data, just skip
            if (!sdata->valid.load() || sdata->pending_reqs.load() == 0) {
                continue;
            }

            if (!sdata->pqueue->empty() && sdata->num_running == 0) {
                if (osdmap->is_up_acting_osd_shard(pgid, whoami)) {
                    dout(20) << __func__ << "  " << pgid << " maps to us, keeping"
                             << dendl;
                    continue;
                }

                while (!sdata->pqueue->empty()) {

                    // dequeue an item to look
                    // if we can make pqueue like a regular queue, that would be great
                    pair<spg_t, PGQueueable> item = sdata->pqueue->dequeue();
                    PGQueueable qi = item.second;
                    if (qi.get_map_epoch() <= osdmap->get_epoch()) {
                        dout(20) << __func__ << "  " << pgid
                                 << " item " << qi
                                 << " epoch " << qi.get_map_epoch()
                                 << " <= " << osdmap->get_epoch()
                                 << ", stale, dropping" << dendl;
                        pushes_to_free += qi.get_reserved_pushes();
                        // assume here qi is reference counted, will go out of scope by itself
                        sdata->pending_reqs--;
                    } else {
                        // put the item back to pqueue, we are done for this PG
                        sdata->_enqueue_front(item, osd->op_prio_cutoff);
                        continue;
                    }
                }
            }
        }
    }

    if (pushes_to_free > 0) {
        osd->service.release_reserved_pushes(pushes_to_free);
    }

}

/// clear cached PGRef on pg deletion
void OSD::RoundRobinOpWQ::clear_pg_pointer(spg_t pgid){
    PGDataRef sdata = get_pgshard(pgid);
    std::unique_lock<std::mutex> lock(sdata->sdata_op_ordering_lock);
    sdata->pg = nullptr;

}

/// clear pg_slots on shutdown
void OSD::RoundRobinOpWQ::clear_pg_slots(){
    // go through all pools
    uint32_t pgarray_index = 0;
    for (uint32_t i = 0; i < pool_pgindex_array_size; i++) {
        pool_pgindex_t *poolpgs = pool_pgindex_array + i;
        uint32_t pgcount = poolpgs->pgcount.load();

        // no pgs, skip this pool
        if (pgcount == 0) {
            continue;
        }
        for (uint32_t j = 0; j < pgcount; j++) {
            pgarray_index = poolpgs->pgqueue_index[j];
            PGDataRef sdata = pgqueue_array[pgarray_index];
            // if no data, just skip
            if (!sdata->valid.load() || sdata->pending_reqs.load() == 0) {
                continue;
            }
            std::unique_lock<std::mutex> lock(sdata->sdata_op_ordering_lock);
            sdata->waiting_for_pg_osdmap.reset();
            // don't bother with reserved pushes; we are shutting down
        }
    }

}

void OSD::RoundRobinOpWQ::_process(uint32_t thread_index, heartbeat_handle_d *hb)
{
    if (osd->is_stopping()) {
        return;    // OSD shutdown, discard.
    }

    // can add a global atomic variable to check if there are any queues pending
    uint32_t num_pgs = pg_count.load();

    // no PG yet, just return
    if (num_pgs == 0) {
        return;
    }

    // uint32_t current_pgindex = thread_current_pgdindex[thread_index];
    uint32_t current_pgindex = 0;
    PGDataRef sdata = get_next_pgqueue(current_pgindex);
    if (sdata == nullptr) {
        return;
    }

    std::unique_lock<std::mutex> lock(sdata->sdata_op_ordering_lock);

    // if no item in queue, then reset opcount and move to next pg
    // and return
    if (sdata->pqueue->empty() && !sdata->front_request_valid.load()) {
        dout(20) << __func__ << " empty q, skipping " << dendl;
        thread_current_opcount[thread_index] = 0;
        // thread_current_pgdindex[thread_index] = (pgindex + 1) % num_pgs;
        return;
    }

    // keep working on PG queue for some requests before moving on
    uint32_t threshold = std::min(sdata->pending_reqs.load(), 10);
    if (thread_current_opcount[thread_index] > threshold) {
        thread_current_opcount[thread_index] = 0;
        // thread_current_pgdindex[thread_index] = (pgindex + 1) % num_pgs;
        // thread_current_pgdindex[thread_index] = (pgindex + num_threads) % num_pgs;
    } else {
        thread_current_opcount[thread_index]++;
    }

    /*
    // XXX debug
    std::this_thread::sleep_for(std::chrono::seconds(2));

    dout(0) << "\nXXX pgindex: " << pgindex << ", pgid " << sdata->pgid << ", thread index:" << thread_index
        << "\n pg request# " << sdata->pending_reqs
        << "\n pg valid: " << sdata->valid
        << "\n pg waiting_for_pg: " << sdata->waiting_for_pg
        << "\n pg num_running: " << sdata->num_running
        << "\n queue empty: " << sdata->pqueue->empty()
        << "\n front_request_valid: " << sdata->front_request_valid.load()
        << dendl;
      */

    // only work on one request on a PG queue
    // thread_current_pgdindex[thread_index] = pgindex + 1;

    // if PG is not yet created or
    // if another thread is still working on it
    // then skip it
    //
    // if (!sdata->valid.load() || sdata->num_running.load() > 0) {
    //
    // get_next_pgindex already set num_running to be 1
    if (!sdata->valid.load()) {
        return;
    }

    if (sdata->waiting_for_pg.load()) {
        return;
    }

    // sdata->num_running.fetch_add(1);

    // take one op out of pqueue
    pair<spg_t, boost::optional<PGQueueable>> item;
    boost::optional<PGQueueable> qi;
    boost::optional<OpRequestRef> _op;
    utime_t latency;
    utime_t now = ceph_clock_now();

    // either get op from saved or from queue
    if (sdata->front_request_valid.load()) {
        // consume last saved item first
        item = sdata->front_request;
        // process the item
        qi = item.second;
        _op = qi->maybe_get_op();
    } else {
        if (!sdata->pqueue->empty()) {
            pair<spg_t, PGQueueable> item1 = sdata->pqueue->dequeue();

            item = item1;
            // process the item
            qi = item.second;
            _op = qi->maybe_get_op();

            // record actual dequeue time
            if (_op) {
                (*_op)->set_dequeued_time(now);
                latency = now - (*_op)->get_req()->get_recv_stamp();
                osd->logger->tinc(l_osd_op_before_dequeue_op_lat, latency);
            }

            // save new one first
            sdata->front_request = item1;
            sdata->front_request_valid.store(true);
        } else {
            sdata->num_running.fetch_sub(1);
            return;
        }
    }

    // check if PG is good
    PGRef pg = sdata->pg;

    // got one OP, allow enqueue
    lock.unlock();


    dout(20) << __func__ << " " << item.first << " item " << *qi
             << " pg " << pg << dendl;

    osd->service.maybe_inject_dispatch_delay();

    // [lookup +] lock pg (if we have it)
    if (!pg) {
        pg = osd->_lookup_lock_pg(item.first);
    } else {
        pg->lock();
    }

    // lock on sdata_op_ordering_lock
    lock.lock();
    if (pg && !sdata->pg && !pg->deleting) {
        sdata->pg = pg;
    }

    osd->service.maybe_inject_dispatch_delay();

    // make sure we're not already waiting for this pg
    if (sdata->waiting_for_pg) {
        if (pg) {
            pg->unlock();
        }
        sdata->num_running.fetch_sub(1);
        return;
    }

    // if PG is still not there yet
    if (!pg) {
        // should this pg shard exist on this osd in this (or a later) epoch?
        OSDMapRef osdmap = sdata->waiting_for_pg_osdmap;
        if (osdmap->is_up_acting_osd_shard(item.first, osd->whoami)) {
            dout(20) << __func__ << " " << item.first
                     << " no pg, should exist, will wait" << " on " << *qi << dendl;
            sdata->waiting_for_pg = true;
        } else if (qi->get_map_epoch() > osdmap->get_epoch()) {
            dout(20) << __func__ << " " << item.first << " no pg, item epoch is "
                     << qi->get_map_epoch() << " > " << osdmap->get_epoch()
                     << ", will wait on " << *qi << dendl;
            sdata->waiting_for_pg = true;
        } else {
            dout(20) << __func__ << " " << item.first << " no pg, shouldn't exist,"
                     << " dropping " << *qi << dendl;

            // invalidated request
            sdata->pending_reqs--;
            sdata->front_request_valid.store(false);

            // share map with client?
            if (boost::optional<OpRequestRef> _op = qi->maybe_get_op()) {
                Session *session = static_cast<Session *>(
                        (*_op)->get_req()->get_connection()->get_priv());
                if (session) {
                    osd->maybe_share_map(session, *_op, sdata->waiting_for_pg_osdmap);
                    session->put();
                }
            }
            unsigned pushes_to_free = qi->get_reserved_pushes();
            if (pushes_to_free > 0) {
                sdata->num_running.fetch_sub(1);
                osd->service.release_reserved_pushes(pushes_to_free);
                return;
            }
        }
        sdata->num_running.fetch_sub(1);
        return;
    }
    lock.unlock();


    // now we assume there is a active worker, update total worker count average
    uint64_t total_active = threads_active.fetch_add(1);
    // dout(0) << "XXX active threads " << total_active << dendl;
    now = ceph_clock_now();
    utime_t time_since_lastcheck = now - sdata->last_time_active_threads_counted;
    // sample the active worker every 100 milliseconds
    if (time_since_lastcheck.to_msec() > 500) {
        osd->logger->inc(l_osd_active_opworker_count, total_active);
        sdata->last_time_active_threads_counted = now;
    }

    // PG locked already, record time
    utime_t pglock_start = ceph_clock_now();
    if (_op) {
        (*_op)->set_pglock_start_time(pglock_start);
    }

    // with a good PG and one op to process
    // note the requeue seq now...
    // osd_opwq _process marks the point at which an operation has been dequeued
    // and will begin to be handled by a worker thread.
    {
#ifdef WITH_LTTNG
        osd_reqid_t reqid;
        if (boost::optional<OpRequestRef> _op = qi->maybe_get_op()) {
            reqid = (*_op)->get_reqid();
        }
#endif
        //tracepoint(osd, opwq_process_start, reqid.name._type,
                   //reqid.name._num, reqid.tid, reqid.inc);
    }

    lgeneric_subdout(osd->cct, osd, 30) << "dequeue status: ";
            Formatter *f = Formatter::create("json");
            f->open_object_section("q");
            dump(f);
            f->close_section();
            f->flush(*_dout);
            delete f;
            *_dout << dendl;

    ThreadPool::TPHandle tp_handle(osd->cct, hb, timeout_interval,
                                   suicide_interval, thread_index);

    qi->run(osd, pg, tp_handle);

    {
#ifdef WITH_LTTNG
        osd_reqid_t reqid;
        if (boost::optional<OpRequestRef> _op = qi->maybe_get_op()) {
            reqid = (*_op)->get_reqid();
        }
#endif
        //tracepoint(osd, opwq_process_finish, reqid.name._type,
        //           reqid.name._num, reqid.tid, reqid.inc);
    }

    // finished request
    sdata->pending_reqs--;
    sdata->front_request_valid.store(false);

    pg->unlock();
    sdata->num_running.fetch_sub(1);

    // decrease active worker count#
    threads_active.fetch_sub(1);

    // PG unlocked already, record time
    utime_t pglock_end = ceph_clock_now();
    utime_t pglock_time = pglock_end - pglock_start;

    if (_op) {
        OpRequestRef op = *_op;
        op->set_pglock_end_time(pglock_end);

        // avoid crash at log layer
        if (op->rmw_flags != 0) {
            if (op->may_read() && op->may_write()) {
                // read-modify-write
                osd->logger->tinc(l_osd_pg_lock_latency_rw, pglock_time);
            } else if (op->may_read()) {
                // read pglock time
                osd->logger->tinc(l_osd_pg_lock_latency_r, pglock_time);
            } else if (op->may_write() || op->may_cache()) {
                // write pglock time
                osd->logger->tinc(l_osd_pg_lock_latency_w, pglock_time);
            } else {
                ceph_abort();
            }
        }
    }

    // dout(0) << " XXX pglock time " << (pglock_end - pglock_start) << dendl;
}



/// enqueue a new item
void OSD::RoundRobinOpWQ::_enqueue(pair <spg_t, PGQueueable> item){

    // get pg info
    spg_t pgid = item.first;
    PGDataRef sdata = get_pgshard(pgid);
    assert (nullptr != sdata);
    unsigned priority = item.second.get_priority();
    unsigned cost = item.second.get_cost();

    std::unique_lock<std::mutex> lock(sdata->sdata_op_ordering_lock);

    dout(20) << __func__ << " " << item.first << " " << item.second << dendl;
    if (priority >= osd->op_prio_cutoff)
        sdata->pqueue->enqueue_strict(
                item.second.get_owner(), priority, item);
    else
        sdata->pqueue->enqueue(
                item.second.get_owner(),
                priority, cost, item);

    sdata->pending_reqs.fetch_add(1);

    //////////////////////
    // request XXX debug
    /*
    {
      osd_reqid_t reqid;
      if (boost::optional<OpRequestRef> _op = item.second.maybe_get_op()) {
        reqid = (*_op)->get_reqid();
      }
      // dout(0) << "XXX enqueue reqid: " << reqid.name << reqid.tid << reqid.inc << " PG: " << item.first << dendl;
    }
    */

    /*
    std::unique_lock<std::mutex> lock(sdata->sdata_lock);
    sdata->sdata_cond.SignalOne();
    */
}

/// requeue an old item (at the front of the line)
void OSD::RoundRobinOpWQ::_enqueue_front(pair <spg_t, PGQueueable> item) {

    // get pg info
    spg_t pgid = item.first;
    PGDataRef sdata = get_pgshard(pgid);

    assert (NULL != sdata);

    std::unique_lock<std::mutex> lock(sdata->sdata_op_ordering_lock);
    sdata->_enqueue_front(item, osd->op_prio_cutoff);

    sdata->pending_reqs.fetch_add(1);

    /*
    std::unique_lock<std::mutex> lock(sdata->sdata_lock);
    sdata->sdata_cond.SignalOne();
    */
}

// get next pgindex that's not busy, called by each worker thread
OSD::RoundRobinOpWQ::PGDataRef OSD::RoundRobinOpWQ::get_next_pgqueue(uint32_t& current_pgindex) {
    if (pg_count.load() == 0) {
        return nullptr;
    }

    std::unique_lock<ceph::spinlock> lock(pgindex_lock);
    uint32_t current_index = g_pgindex;
    uint32_t next_index = g_pgindex++;
    g_pgindex = next_index % pg_count.load();
    PGDataRef sdata = nullptr;

    while (1) {
        sdata = get_pgqueue(g_pgindex);

        // there is a match, return
        if (sdata->valid.load() && (sdata->num_running.load() == 0)
            && (!sdata->pqueue->empty() || sdata->front_request_valid.load())) {
            sdata->num_running.fetch_add(1);
            break;
        }

        // no match, move to next
        next_index++;
        g_pgindex = next_index % pg_count.load();

        // max one round, if it comes back to start position, just move to next
        if (current_index == g_pgindex) {
            sdata = nullptr;
            break;
        }
    }

    current_pgindex = g_pgindex;
    return sdata;

}


