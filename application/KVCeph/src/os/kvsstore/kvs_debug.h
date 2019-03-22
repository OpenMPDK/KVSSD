//
// Created by root on 8/21/18.
//

#ifndef CEPH_KVS_DEBUG_H
#define CEPH_KVS_DEBUG_H

#include "os/ObjectStore.h"
#include <sstream>
#include <pthread.h>


#define dout_context cct
#define dout_subsys ceph_subsys_kvs

#undef dout_prefix
#define dout_prefix *_dout << "[kvs] "

#define FTRACE
#define FTRACE2
//#define PRINTRKEY(k) 
#define PRINTRKEY_CCT(ct, k)

#ifndef FTRACE
#define FTRACE FtraceObject fobj(__FUNCTION__, cct);
#endif

#ifndef FTRACE2
#define FTRACE2 //FtraceObject obj(__FUNCTION__, store->cct);
#endif

#define PRINTWKEY(k) derr << __func__ << ": write key = " << print_key((const char*)(k)->key, (k)->length) << dendl;
#define PRINTRKEY(k) derr << __func__ << ": read key = " << print_key((const char*)(k)->key, (k)->length) << dendl;
//#define PRINTRKEY_CCT(ct, k) lderr(ct) << __func__ << ": read key = " << print_key((const char*)(k)->key, (k)->length) << dendl;

struct FtraceObject {
    std::string func;
    CephContext *cct;
    FtraceObject(const char *f, CephContext *c) : func(f), cct(c) {
      derr << "ENTER: " << func << dendl;
    }

    ~FtraceObject() {
      derr << "EXIT : " << func << dendl;
    }

};


static const std::map<int, std::string> opstr_map = {
        {ObjectStore::Transaction::OP_NOP              ,"OP_NOP               "},
        {ObjectStore::Transaction::OP_TOUCH            ,"OP_TOUCH             "},
        {ObjectStore::Transaction::OP_WRITE            ,"OP_WRITE             "},
        {ObjectStore::Transaction::OP_ZERO             ,"OP_ZERO              "},
        {ObjectStore::Transaction::OP_TRUNCATE         ,"OP_TRUNCATE          "},
        {ObjectStore::Transaction::OP_REMOVE           ,"OP_REMOVE            "},
        {ObjectStore::Transaction::OP_SETATTR          ,"OP_SETATTR           "},
        {ObjectStore::Transaction::OP_SETATTRS         ,"OP_SETATTRS          "},
        {ObjectStore::Transaction::OP_RMATTR           ,"OP_RMATTR            "},
        {ObjectStore::Transaction::OP_CLONE            ,"OP_CLONE             "},
        {ObjectStore::Transaction::OP_CLONERANGE       ,"OP_CLONERANGE        "},
        {ObjectStore::Transaction::OP_CLONERANGE2      ,"OP_CLONERANGE2       "},
        {ObjectStore::Transaction::OP_TRIMCACHE        ,"OP_TRIMCACHE         "},
        {ObjectStore::Transaction::OP_MKCOLL           ,"OP_MKCOLL            "},
        {ObjectStore::Transaction::OP_RMCOLL           ,"OP_RMCOLL            "},
        {ObjectStore::Transaction::OP_COLL_ADD         ,"OP_COLL_ADD          "},
        {ObjectStore::Transaction::OP_COLL_REMOVE      ,"OP_COLL_REMOVE       "},
        {ObjectStore::Transaction::OP_COLL_SETATTR     ,"OP_COLL_SETATTR      "},
        {ObjectStore::Transaction::OP_COLL_RMATTR      ,"OP_COLL_RMATTR       "},
        {ObjectStore::Transaction::OP_COLL_SETATTRS    ,"OP_COLL_SETATTRS     "},
        {ObjectStore::Transaction::OP_COLL_MOVE        ,"OP_COLL_MOVE         "},
        {ObjectStore::Transaction::OP_STARTSYNC        ,"OP_STARTSYNC         "},
        {ObjectStore::Transaction::OP_RMATTRS          ,"OP_RMATTRS           "},
        {ObjectStore::Transaction::OP_COLL_RENAME      ,"OP_COLL_RENAME       "},
        {ObjectStore::Transaction::OP_OMAP_CLEAR       ,"OP_OMAP_CLEAR        "},
        {ObjectStore::Transaction::OP_OMAP_SETKEYS     ,"OP_OMAP_SETKEYS      "},
        {ObjectStore::Transaction::OP_OMAP_RMKEYS      ,"OP_OMAP_RMKEYS       "},
        {ObjectStore::Transaction::OP_OMAP_SETHEADER   ,"OP_OMAP_SETHEADER    "},
        {ObjectStore::Transaction::OP_SPLIT_COLLECTION ,"OP_SPLIT_COLLECTION  "},
        {ObjectStore::Transaction::OP_SPLIT_COLLECTION2,"OP_SPLIT_COLLECTION2 "},
        {ObjectStore::Transaction::OP_OMAP_RMKEYRANGE  ,"OP_OMAP_RMKEYRANGE   "},
        {ObjectStore::Transaction::OP_COLL_MOVE_RENAME ,"OP_COLL_MOVE_RENAME  "},
        {ObjectStore::Transaction::OP_SETALLOCHINT     ,"OP_SETALLOCHINT      "},
        {ObjectStore::Transaction::OP_COLL_HINT        ,"OP_COLL_HINT         "},
        {ObjectStore::Transaction::OP_TRY_RENAME       ,"OP_TRY_RENAME        "},
        {ObjectStore::Transaction::OP_COLL_SET_BITS    ,"OP_COLL_SET_BITS     "}
};

inline std::string opstr(int op) {
    auto it  = opstr_map.find(op);
    if (it == opstr_map.end()) {
        return "unknown op";
    } else {
        return it->second;
    }
}


inline string print_key(const char* in, unsigned length)
{
    char buf[10];
    string out;
    out.reserve(length * 3);
    enum { NONE, HEX, STRING } mode = NONE;
    unsigned from = 0, i;
    for (i=0; i < length; ++i) {
        if ((in[i] < 32 || (unsigned char)in[i] > 126) ||
            (mode == HEX && length - i >= 4 &&
             ((in[i] < 32 || (unsigned char)in[i] > 126) ||
              (in[i+1] < 32 || (unsigned char)in[i+1] > 126) ||
              (in[i+2] < 32 || (unsigned char)in[i+2] > 126) ||
              (in[i+3] < 32 || (unsigned char)in[i+3] > 126)))) {
            if (mode == STRING) {
                out.append(in + from, i - from);
                out.push_back('\'');
            }
            if (mode != HEX) {
                out.append("0x");
                mode = HEX;
            }
            if (length - i >= 4) {
                // print a whole u32 at once
                snprintf(buf, sizeof(buf), "%08x",
                         (uint32_t)(((unsigned char)in[i] << 24) |
                                    ((unsigned char)in[i+1] << 16) |
                                    ((unsigned char)in[i+2] << 8) |
                                    ((unsigned char)in[i+3] << 0)));
                i += 3;
            } else {
                snprintf(buf, sizeof(buf), "%02x", (int)(unsigned char)in[i]);
            }
            out.append(buf);
        } else {
            if (mode != STRING) {
                out.push_back('\'');
                mode = STRING;
                from = i;
            }
        }
    }
    if (mode == STRING) {
        out.append(in + from, i - from);
        out.push_back('\'');
    }
    return out;
}

template<typename S>
static string pretty_binary_string(const S& in)
{
    char buf[10];
    string out;
    out.reserve(in.length() * 3);
    enum { NONE, HEX, STRING } mode = NONE;
    unsigned from = 0, i;
    for (i=0; i < in.length(); ++i) {
        if ((in[i] < 32 || (unsigned char)in[i] > 126) ||
            (mode == HEX && in.length() - i >= 4 &&
             ((in[i] < 32 || (unsigned char)in[i] > 126) ||
              (in[i+1] < 32 || (unsigned char)in[i+1] > 126) ||
              (in[i+2] < 32 || (unsigned char)in[i+2] > 126) ||
              (in[i+3] < 32 || (unsigned char)in[i+3] > 126)))) {
            if (mode == STRING) {
                out.append(in.c_str() + from, i - from);
                out.push_back('\'');
            }
            if (mode != HEX) {
                out.append("0x");
                mode = HEX;
            }
            if (in.length() - i >= 4) {
                // print a whole u32 at once
                snprintf(buf, sizeof(buf), "%08x",
                         (uint32_t)(((unsigned char)in[i] << 24) |
                                    ((unsigned char)in[i+1] << 16) |
                                    ((unsigned char)in[i+2] << 8) |
                                    ((unsigned char)in[i+3] << 0)));
                i += 3;
            } else {
                snprintf(buf, sizeof(buf), "%02x", (int)(unsigned char)in[i]);
            }
            out.append(buf);
        } else {
            if (mode != STRING) {
                out.push_back('\'');
                mode = STRING;
                from = i;
            }
        }
    }
    if (mode == STRING) {
        out.append(in.c_str() + from, i - from);
        out.push_back('\'');
    }
    return out;
}


#endif //CEPH_KVS_DEBUG_H
