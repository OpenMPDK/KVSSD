#ifndef KADI_DEBUG_H
#define KADI_DEBUG_H

#include <string>
#include <iostream>

// #define FTRACE

#ifndef FTRACE
#define FTRACE FtraceObject fobj(__FUNCTION__);
#endif

struct FtraceObject {
    std::string func;
    FtraceObject(const char *f) : func(f) {
      std::cerr << "ENTER: " << func << std::endl;
    }

    ~FtraceObject() {
      std::cerr << "EXIT : " << func << std::endl;
    }
};

struct nvme_passthru_kv_cmd;
void dump_cmd(struct nvme_passthru_kv_cmd *cmd);
void dump_retrieve_cmd(struct nvme_passthru_kv_cmd *cmd);
void dump_delete_cmd(struct nvme_passthru_kv_cmd *cmd);
std::string print_key(const char *data, int length);

#endif 