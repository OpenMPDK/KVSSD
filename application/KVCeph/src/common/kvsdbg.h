#ifndef CEPH_KVS_DEBUG_TERMINAL_H
#define CEPH_KVS_DEBUG_TERMINAL_H

#include <string>
#include <mutex>

class CephContext;

class KvsDebugTerminal {
private:
        int sock;
        bool connected;
        std::mutex mlock;
        CephContext *cct;
        std::string module;
        bool connect(const std::string &ip, int port, const std::string &name);
public:
        KvsDebugTerminal(int module_type, CephContext *c);

        bool ensureconnection();

        bool sendmsg_ex(const std::string fmt, ...);
        bool sendmsg_dp(const int depth, const std::string fmt, ...);
        bool sendmsg(const std::string &msg);
        bool sendmsg_nolock(const std::string &msg);
};

#define NODEBUG

#ifdef NODEBUG
#define DECLARE_KVSDBG
#define INIT_KVSDBG
#define KVS_DEBUG(ct)
#define KVS_MSG_EX(ct,depth,format, ...)
#define KVS_FTRACE(ct)
#define KVS_FTRACE_DONE(ct)
#define KVS_FTRACE2(ct, msg)
#define KVS_BT(ct)
#else
#define DECLARE_KVSDBG KvsDebugTerminal kvsdbg
#define INIT_KVSDBG kvsdbg(module_type_, this),
#define KVS_DEBUG(ct) (ct)->kvsdbg.sendmsg_ex("%s, %d\n", __func__, __line__);
#define KVS_MSG_EX(ct,depth,format, ...) do { (ct)->kvsdbg.sendmsg_dp(depth,"" format "", ##__VA_ARGS__); } while (0)
#define KVS_FTRACE(ct) do { (ct)->kvsdbg.sendmsg_ex("KvsStore::%s() - start\n", __func__); } while (0)
#define KVS_FTRACE_DONE(ct) do { (ct)->kvsdbg.sendmsg_ex("KvsStore::%s() - done\n", __func__);  } while (0)
#define KVS_FTRACE2(ct, msg) (ct)->kvsdbg.sendmsg_ex("%s - %s\n", __func__, (msg))
#define KVS_BT(ct) do { BackTrace bt(0); std::stringstream ss; bt.print(ss); (ct)->kvsdbg.sendmsg_ex("FUNC: KvsStore::%s, Stack Trace: \n %s\n", __func__, ss.str().c_str()); } while (0);
#endif

#endif

