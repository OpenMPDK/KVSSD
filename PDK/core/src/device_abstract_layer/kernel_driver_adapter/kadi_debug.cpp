#include "kadi_debug.h"
#include "linux_nvme_ioctl.h"
#include <string.h>
std::string print_key(const char *data, int length) {
    char key[512];
    int keylen = 0;
    for (int i =0 ; i < length; i++) {
      keylen += snprintf(key, sizeof(key), "%02x", data[i]);
    }
    return std::string (key, keylen);
}

void dump_delete_cmd(struct nvme_passthru_kv_cmd *cmd) {
    char buf[2048];
    memset(buf, '\0', sizeof(buf));
    int offset = snprintf(buf, sizeof(cmd->opcode), "[dump delete cmd (%02x)]\n", cmd->opcode);

    offset += snprintf(buf+offset, sizeof(cmd->opcode), "\t opcode(%02x)\n", cmd->opcode);
    offset += snprintf(buf+offset, sizeof(cmd->nsid), "\t nsid(%04x)\n", cmd->nsid);
    offset += snprintf(buf+offset, sizeof(cmd->cdw3), "\t cdw3(%04x)\n", cmd->cdw3);
    offset += snprintf(buf+offset, sizeof(cmd->cdw4), "\t cdw4(%04x)\n", cmd->cdw4);
    offset += snprintf(buf+offset, sizeof(cmd->cdw5), "\t cdw5(%04x)\n", cmd->cdw5);

    offset += snprintf(buf+offset, sizeof(cmd->key_length), "\t cmd.key_length(%02x)\n", cmd->key_length);

    if (cmd->key_length <= 16) {
        offset += snprintf(buf+offset, sizeof(print_key((char*)cmd->key, cmd->key_length).c_str()), 
                            "\t cmd.key (%s)\n", print_key((char*)cmd->key, cmd->key_length).c_str());
    }
    else {
        offset += snprintf(buf+offset, sizeof(print_key((char*)cmd->key_addr, cmd->key_length).c_str()), 
                            "\t cmd.key (%s)\n", print_key((char*)cmd->key_addr, cmd->key_length).c_str());
    }
    offset += snprintf(buf+offset, sizeof(cmd->reqid), "\t reqid(%04llu)\n", cmd->reqid);
    offset += snprintf(buf+offset, sizeof(cmd->ctxid), "\t ctxid(%04d)\n", cmd->ctxid);
    std::cerr << buf <<std::endl;
}


void dump_retrieve_cmd(struct nvme_passthru_kv_cmd *cmd) {
    char buf[2048];
    memset(buf, '\0', sizeof(buf));
    int offset = snprintf(buf, sizeof(cmd->opcode), "[dump retrieve cmd (%02x)]\n", cmd->opcode);

    offset += snprintf(buf+offset, sizeof(cmd->opcode), "\t opcode(%02x)\n", cmd->opcode);
    offset += snprintf(buf+offset, sizeof(cmd->nsid), "\t nsid(%04x)\n", cmd->nsid);
    offset += snprintf(buf+offset, sizeof(cmd->cdw3), "\t cdw3(%04x)\n", cmd->cdw3);
    offset += snprintf(buf+offset, sizeof(cmd->cdw4), "\t cdw4(%04x)\n", cmd->cdw4);
    offset += snprintf(buf+offset, sizeof(cmd->cdw5), "\t cdw5(%04x)\n", cmd->cdw5);

    offset += snprintf(buf+offset, sizeof(cmd->key_length), "\t cmd.key_length(%02x)\n", cmd->key_length);

    if (cmd->key_length <= 16) {
        offset += snprintf(buf+offset, sizeof(print_key((char*)cmd->key, cmd->key_length).c_str()),
                            "\t cmd.key (%s)\n", print_key((char*)cmd->key, cmd->key_length).c_str());
    }
    else {
        offset += snprintf(buf+offset, sizeof(print_key((char*)cmd->key_addr, cmd->key_length).c_str()),
                            "\t cmd.key (%s)\n", print_key((char*)cmd->key_addr, cmd->key_length).c_str());
    }

    offset += snprintf(buf+offset, sizeof(cmd->data_length), "\t cmd.data_length(%02x)\n", cmd->data_length);
    offset += snprintf(buf+offset, sizeof((void*)cmd->data_addr), "\t cmd.data(%p)\n", (void*)cmd->data_addr);
    offset += snprintf(buf+offset, sizeof(cmd->reqid), "\t reqid(%04llu)\n", cmd->reqid);
    offset += snprintf(buf+offset, sizeof(cmd->ctxid), "\t ctxid(%04d)\n", cmd->ctxid);
    std::cerr << buf <<std::endl;
}


void dump_cmd(struct nvme_passthru_kv_cmd *cmd)
{
    char buf[2048];
    memset(buf, '\0', sizeof(buf));
    int offset = snprintf(buf, sizeof(cmd->opcode), "[dump issued cmd opcode (%02x)]\n", cmd->opcode);
    offset += snprintf(buf+offset, sizeof(cmd->opcode), "\t opcode(%02x)\n", cmd->opcode);
    offset += snprintf(buf+offset, sizeof(cmd->flags), "\t flags(%02x)\n", cmd->flags);
    offset += snprintf(buf+offset, sizeof(cmd->rsvd1), "\t rsvd1(%04d)\n", cmd->rsvd1);
    offset += snprintf(buf+offset, sizeof(cmd->nsid), "\t nsid(%08x)\n", cmd->nsid);
    offset += snprintf(buf+offset, sizeof(cmd->cdw2), "\t cdw2(%08x)\n", cmd->cdw2);
    offset += snprintf(buf+offset, sizeof(cmd->cdw3), "\t cdw3(%08x)\n", cmd->cdw3);
    offset += snprintf(buf+offset, sizeof(cmd->cdw4), "\t rsvd2(%08x)\n", cmd->cdw4);
    offset += snprintf(buf+offset, sizeof(cmd->cdw5), "\t cdw5(%08x)\n", cmd->cdw5);
    offset += snprintf(buf+offset, sizeof((void *)cmd->data_addr), "\t data_addr(%p)\n",(void *)cmd->data_addr);
    offset += snprintf(buf+offset, sizeof(cmd->data_length), "\t data_length(%08x)\n", cmd->data_length);
    offset += snprintf(buf+offset, sizeof(cmd->key_length), "\t key_length(%08x)\n", cmd->key_length);
    offset += snprintf(buf+offset, sizeof(cmd->cdw10), "\t cdw10(%08x)\n", cmd->cdw10);
    offset += snprintf(buf+offset, sizeof(cmd->cdw11), "\t cdw11(%08x)\n", cmd->cdw11);
    offset += snprintf(buf+offset, sizeof(cmd->cdw12), "\t cdw12(%08x)\n", cmd->cdw12);
    offset += snprintf(buf+offset, sizeof(cmd->cdw13), "\t cdw13(%08x)\n", cmd->cdw13);
    offset += snprintf(buf+offset, sizeof(cmd->cdw14), "\t cdw14(%08x)\n", cmd->cdw14);
    offset += snprintf(buf+offset, sizeof(cmd->cdw15), "\t cdw15(%08x)\n", cmd->cdw15);
    offset += snprintf(buf+offset, sizeof(cmd->timeout_ms), "\t timeout_ms(%08x)\n", cmd->timeout_ms);
    offset += snprintf(buf+offset, sizeof(cmd->result), "\t result(%08x)\n", cmd->result);
    std::cerr << buf <<std::endl;
}