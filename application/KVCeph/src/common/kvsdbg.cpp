
#include "kvsdbg.h"
#include "include/Context.h"
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <string>
#include <stdarg.h>

#define dout_context cct
#define dout_subsys ceph_subsys_ms
#undef dout_prefix
#define dout_prefix *_dout << "DebugTerminal "

std::string get_module_name(int type) {
    switch(type) {
        case CEPH_ENTITY_TYPE_MON:
            return "mon";
        case CEPH_ENTITY_TYPE_MDS:
            return "mds";
        case CEPH_ENTITY_TYPE_OSD:
            return "osd";
        case CEPH_ENTITY_TYPE_CLIENT:
            return "client";
        case CEPH_ENTITY_TYPE_MGR:
            return "mgr";
        case CEPH_ENTITY_TYPE_AUTH:
            return "auth";
        case CEPH_ENTITY_TYPE_ANY:
            return "any";
        default:
            return "unknown";
    };

}

KvsDebugTerminal::KvsDebugTerminal(int module_type_, CephContext *c):
    sock(-1), connected(false),cct(c), module(get_module_name(module_type_)){

}

// true if connected
bool KvsDebugTerminal::ensureconnection() {
#ifdef NODEBUG
    return true;
#else
    return connect("127.0.0.1", 1234, module);
#endif
}

bool KvsDebugTerminal::connect(const std::string &ip, int port, const std::string &name) {
    #ifdef NODEBUG
        return true;
    #endif
    if (port == -1)  { return false; }

    if (sock != -1 || connected) {
        // already connected
        return true;
    }

    struct sockaddr_in serv_addr;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
            lderr(cct) << "cannot create a socket" << dendl;
            return false;
    }

    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if(::inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr)<=0)
    {
            lderr(cct) << "cannot create configure the Ip address: "<< ip.c_str() << dendl;
            return false;
    }

    if (::connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
            lderr(cct) << "cannot connect to the server: " << ip.c_str() << ", port = " << port << dendl;
            return false;
    }

    connected = true;

    if (!sendmsg_nolock(name)) {
        lderr(cct) << "cannot send the name to the server" << dendl;
        return false;
    }

    lderr(cct) << "[" << name.c_str() << "] connected to the debug terminal" << dendl;

    return true;

}

bool KvsDebugTerminal::sendmsg_dp(const int depth, const std::string fmt, ...) {
    #ifdef NODEBUG
        return true;
    #endif
    //ensureconnection();
    std::unique_lock<std::mutex> lock(mlock);

    int size = ((int)fmt.size()) * 2 + 250;   // Use a rubric appropriate for your code
    std::string str;
    va_list ap;

    if (depth > 0) str.insert(str.begin(), depth, ' ');

    while (1) {     // Maximum two passes on a POSIX system...
            str.resize(size);
            va_start(ap, fmt);
            int n = vsnprintf((char *)str.data() + depth, size - depth, fmt.c_str(), ap);
            va_end(ap);
            if (n > -1 && n < (size-depth)) {  // Everything worked
                    str.resize(n + depth);
                    return sendmsg_nolock(str);
            }
            if (n > -1)  // Needed size returned
                    size = n + depth + 1;   // For null char
            else
                    size *= 2;      // Guess at a larger size (OS specific)
    }
    return sendmsg_nolock(str);
}


bool KvsDebugTerminal::sendmsg_ex(const std::string fmt, ...) {
    #ifdef NODEBUG
        return true;
    #endif
    //ensureconnection();
    std::unique_lock<std::mutex> lock(mlock);

    int size = ((int)fmt.size()) * 2 + 50;   // Use a rubric appropriate for your code
    std::string str;
    va_list ap;
    while (1) {     // Maximum two passes on a POSIX system...
            str.resize(size);
            va_start(ap, fmt);
            int n = vsnprintf((char *)str.data(), size, fmt.c_str(), ap);
            va_end(ap);
            if (n > -1 && n < size) {  // Everything worked
                    str.resize(n);
                    return sendmsg_nolock(str);
            }
            if (n > -1)  // Needed size returned
                    size = n + 1;   // For null char
            else
                    size *= 2;      // Guess at a larger size (OS specific)
    }
    return sendmsg_nolock(str);
}

bool KvsDebugTerminal::sendmsg_nolock(const std::string &msg) {
    #ifdef NODEBUG
        return true;
    #endif
    if (!ensureconnection()) {
        if (cct) lderr(cct) << "can't send the message: not connected yet. MSG = " << msg << "connected = " << connected << ", sock = " << sock << dendl;
        return false;
    }

    long size= 0;

    bool retry = false;
    do {
        retry = false;
        size = send(sock , msg.c_str() , msg.length() , 0 );

        if (size == -1) {
            sock = -1;
            connected = false;
            if (!ensureconnection()) break;
            retry = true;
        }
    } while (errno == EINTR || retry == true);

    if (size != (long)msg.length()) {
        if (cct) lderr(cct) << "can't send the message: size " << size << ", msg length " << msg.length() << ", errno " << errno << ", module " << module << dendl;
        return false;
    }

    do { if (size > 0 && msg[msg.length()-1] != '\n') size = send(sock , "\n", 1 , 0 ); } while (errno == EINTR);

    if (size < 0) {
        connected = false;
        sock = -1;
    }

    return true;
}

bool KvsDebugTerminal::sendmsg(const std::string &msg) {
    #ifdef NODEBUG
        return true;
    #endif
    std::unique_lock<std::mutex> lock(mlock);
    return sendmsg_nolock(msg);
}

