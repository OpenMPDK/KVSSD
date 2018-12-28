#ifndef CEPH_KVS_DEBUG_TERMINAL_H
#define CEPH_KVS_DEBUG_TERMINAL_H

#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <string>
#include <stdarg.h>

class KvsDebugTerminal {
private:
        int sock;
        bool connected;
public:
        KvsDebugTerminal();
        bool connect(const std::string &ip, int port);
        bool sendmsg_ex(const std::string fmt, ...);
        bool sendmsg(const std::string &msg);
};

#endif

