/*
 * Copyright 2014 Marios Kogias <marioskogias@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 *   1. Redistributions of source code must retain the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer.
 *   2. Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials
 *      provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * In this example we have 2 processes communicating over a unix socket.
 * We are going to trace the communication with our library
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <zipkin_c.h>

#define SOCK_PATH "/tmp/socket"

struct message {
    char actual_message[20];
    struct blkin_trace_info trace_info;
};

void process_a() 
{
    int i, r;
    printf("I am process A: %d\n", getpid());

    r = blkin_init();
    if (r < 0) {
	    fprintf(stderr, "Could not initialize blkin\n");
	    exit(1);
    }

    /*initialize endpoint*/
    struct blkin_endpoint endp;
    blkin_init_endpoint(&endp, "10.0.0.1", 5000, "service a");

    struct blkin_trace trace;
    struct blkin_annotation ant;
    struct message msg = {.actual_message = "message"};
    char ack;

    /*create and bind socket*/
    int s, s2, len;
    socklen_t t;
    struct sockaddr_un local, remote;

    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, SOCK_PATH);
    unlink(local.sun_path);
    len = strlen(local.sun_path) + sizeof(local.sun_family);
    if (bind(s, (struct sockaddr *)&local, len) == -1) {
        perror("bind");
        exit(1);
    }

    if (listen(s, 5) == -1) {
        perror("listen");
        exit(1);
    }

    printf("Waiting for a connection...\n");
    t = sizeof(remote);
    if ((s2 = accept(s, (struct sockaddr *)&remote, &t)) == -1) {
        perror("accept");
        exit(1);
    }

    printf("Connected.\n");
    
    for (i=0;i<10;i++) {

        /*create trace*/
        blkin_init_new_trace(&trace, "process a", &endp);

        blkin_init_timestamp_annotation(&ant, "start", &endp);
        blkin_record(&trace, &ant);

        /*set trace fields to message*/
        blkin_get_trace_info(&trace, &msg.trace_info);

        /*send*/
        send(s2, &msg, sizeof(struct message), 0);

        /*wait for ack*/
        recv(s2, &ack, 1, 0);

        /*create annotation and log*/
        blkin_init_timestamp_annotation(&ant, "end", &endp);
        blkin_record(&trace, &ant);
    }
    close(s2);
}

void process_b() 
{
    int i, r;
    printf("I am process B: %d\n", getpid());

    r = blkin_init();
    if (r < 0) {
	    fprintf(stderr, "Could not initialize blkin\n");
	    exit(1);
    }
    /*initialize endpoint*/
    struct blkin_endpoint endp;
    blkin_init_endpoint(&endp, "10.0.0.2", 5001, "service b");

    struct blkin_trace trace;
    struct blkin_annotation ant;
    struct message msg;
    int s, len;
    struct sockaddr_un remote;

    /*Connect*/
    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    printf("Trying to connect...\n");

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, SOCK_PATH);
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if (connect(s, (struct sockaddr *)&remote, len) == -1) {
        perror("connect");
        exit(1);
    }

    printf("Connected.\n");

    for (i=0;i<10;i++) {
        recv(s, &msg, sizeof(struct message), 0);

        /*create child trace*/
        blkin_init_child_info(&trace, &msg.trace_info, &endp, "process b");  

        /*create annotation and log*/
        blkin_init_timestamp_annotation(&ant, "start", &endp);
        blkin_record(&trace, &ant);

        /*Process...*/
        usleep(10);
        printf("Message received %s\n", msg.actual_message);

        /*create annotation and log*/
        blkin_init_timestamp_annotation(&ant, "end", &endp);
        blkin_record(&trace, &ant);

        /*send ack*/
        send(s, "*", 1, 0);
    }
}


int main()
{
    if (fork()){
        process_a();
        exit(0);
    }
    else{
        process_b();
        exit(0);
    }
}
