#include "kadi_debug.h"
#include "linux_nvme_ioctl.h"

std::string print_key(const char *data, int length) {
    char key[512];
    int keylen = 0;
    for (int i =0 ; i < length; i++) {
      keylen += sprintf(key, "%02x", data[i]);
    }
    return std::string (key, keylen);
}

void dump_delete_cmd(struct nvme_passthru_kv_cmd *cmd) {
    char buf[2048];
    int offset = sprintf(buf, "[dump delete cmd (%02x)]\n", cmd->opcode);

    offset += sprintf(buf+offset, "\t opcode(%02x)\n", cmd->opcode);
    offset += sprintf(buf+offset, "\t nsid(%04x)\n", cmd->nsid);
    offset += sprintf(buf+offset, "\t cdw3(%04x)\n", cmd->cdw3);
    offset += sprintf(buf+offset, "\t cdw4(%04x)\n", cmd->cdw4);
    offset += sprintf(buf+offset, "\t cdw5(%04x)\n", cmd->cdw5);

    offset += sprintf(buf+offset, "\t cmd.key_length(%02x)\n", cmd->key_length);

    if (cmd->key_length <= 16) {
        offset += sprintf(buf+offset, "\t cmd.key (%s)\n", print_key((char*)cmd->key, cmd->key_length).c_str());
    }
    else {
        offset += sprintf(buf+offset, "\t cmd.key (%s)\n", print_key((char*)cmd->key_addr, cmd->key_length).c_str());
    }
    offset += sprintf(buf+offset, "\t reqid(%04llu)\n", cmd->reqid);
    offset += sprintf(buf+offset, "\t ctxid(%04d)\n", cmd->ctxid);
    std::cerr << buf <<std::endl;
}


void dump_retrieve_cmd(struct nvme_passthru_kv_cmd *cmd) {
    char buf[2048];
    int offset = sprintf(buf, "[dump retrieve cmd (%02x)]\n", cmd->opcode);

    offset += sprintf(buf+offset, "\t opcode(%02x)\n", cmd->opcode);
    offset += sprintf(buf+offset, "\t nsid(%04x)\n", cmd->nsid);
    offset += sprintf(buf+offset, "\t cdw3(%04x)\n", cmd->cdw3);
    offset += sprintf(buf+offset, "\t cdw4(%04x)\n", cmd->cdw4);
    offset += sprintf(buf+offset, "\t cdw5(%04x)\n", cmd->cdw5);

    offset += sprintf(buf+offset, "\t cmd.key_length(%02x)\n", cmd->key_length);

    if (cmd->key_length <= 16) {
        offset += sprintf(buf+offset, "\t cmd.key (%s)\n", print_key((char*)cmd->key, cmd->key_length).c_str());
    }
    else {
        offset += sprintf(buf+offset, "\t cmd.key (%s)\n", print_key((char*)cmd->key_addr, cmd->key_length).c_str());
    }

    offset += sprintf(buf+offset, "\t cmd.data_length(%02x)\n", cmd->data_length);
    offset += sprintf(buf+offset, "\t cmd.data(%p)\n", (void*)cmd->data_addr);
    offset += sprintf(buf+offset, "\t reqid(%04llu)\n", cmd->reqid);
    offset += sprintf(buf+offset, "\t ctxid(%04d)\n", cmd->ctxid);
    std::cerr << buf <<std::endl;
}


void dump_cmd(struct nvme_passthru_kv_cmd *cmd)
{
    char buf[2048];
    int offset = sprintf(buf, "[dump issued cmd opcode (%02x)]\n", cmd->opcode);
    offset += sprintf(buf+offset, "\t opcode(%02x)\n", cmd->opcode);
    offset += sprintf(buf+offset, "\t flags(%02x)\n", cmd->flags);
    offset += sprintf(buf+offset, "\t rsvd1(%04d)\n", cmd->rsvd1);
    offset += sprintf(buf+offset, "\t nsid(%08x)\n", cmd->nsid);
    offset += sprintf(buf+offset, "\t cdw2(%08x)\n", cmd->cdw2);
    offset += sprintf(buf+offset, "\t cdw3(%08x)\n", cmd->cdw3);
    offset += sprintf(buf+offset, "\t rsvd2(%08x)\n", cmd->cdw4);
    offset += sprintf(buf+offset, "\t cdw5(%08x)\n", cmd->cdw5);
    offset += sprintf(buf+offset, "\t data_addr(%p)\n",(void *)cmd->data_addr);
    offset += sprintf(buf+offset, "\t data_length(%08x)\n", cmd->data_length);
    offset += sprintf(buf+offset, "\t key_length(%08x)\n", cmd->key_length);
    offset += sprintf(buf+offset, "\t cdw10(%08x)\n", cmd->cdw10);
    offset += sprintf(buf+offset, "\t cdw11(%08x)\n", cmd->cdw11);
    offset += sprintf(buf+offset, "\t cdw12(%08x)\n", cmd->cdw12);
    offset += sprintf(buf+offset, "\t cdw13(%08x)\n", cmd->cdw13);
    offset += sprintf(buf+offset, "\t cdw14(%08x)\n", cmd->cdw14);
    offset += sprintf(buf+offset, "\t cdw15(%08x)\n", cmd->cdw15);
    offset += sprintf(buf+offset, "\t timeout_ms(%08x)\n", cmd->timeout_ms);
    offset += sprintf(buf+offset, "\t result(%08x)\n", cmd->result);
    std::cerr << buf <<std::endl;
}