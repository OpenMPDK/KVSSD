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
#include <thread>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ztracer.hpp>
#include <iostream>

#define SOCK_PATH "socket"

struct message {
	int  actual_message;
	struct blkin_trace_info trace_info;
	message(int s) : actual_message(s) {};
	message() {}
};

class Parent {
	private:
		int s, s2;
		ZTracer::Endpoint e;
	public:
		Parent() : e("0.0.0.0", 1, "parent")
		{
			connect();
		}
		void operator()() 
		{
			struct sockaddr_un remote;
			int t;
			std::cout << "I am parent process : " << getpid() << std::endl;
			
			/* Wait for connection */
			t = sizeof(remote);
			if ((s2 = accept(s, (struct sockaddr *)&remote, (socklen_t *)&t)) == -1) {
				std::cerr << "accept" << std::endl;
				exit(1);
			}

			std::cerr << "Connected" << std::endl;
			
			for (int i=0;i<10;i++) {
				/*Init trace*/
        ZTracer::Trace tr("parent process", &e);

				process(tr);
				
				wait_response();

				/*Log received*/
				tr.event("parent end");
			}	
		}

		void process(ZTracer::Trace &tr)
		{
			struct message msg(rand());
			/*Annotate*/
			tr.event("parent start");
			/*Set trace info to the message*/
			msg.trace_info = *tr.get_info();
			
			/*send*/
			send(s2, &msg, sizeof(struct message), 0);
		}
		
		void wait_response() 
		{
			char ack;
			recv(s2, &ack, 1, 0);
		}

		void connect()
		{
			/*create and bind socket*/
			int len;
			struct sockaddr_un local;

			if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
				std::cerr << "socket" << std::endl;
				exit(1);
			}

			local.sun_family = AF_UNIX;
			strcpy(local.sun_path, SOCK_PATH);
			unlink(local.sun_path);
			len = strlen(local.sun_path) + sizeof(local.sun_family);
			if (bind(s, (struct sockaddr *)&local, len) == -1) {
				std::cerr << "bind" << std::endl;
				exit(1);
			}

			if (listen(s, 5) == -1) {
				std::cerr << "listen" << std::endl;
				exit(1);
			}

			std::cout << "Waiting for a connection..." << std::endl;
		}

};

class Child {
	private:
		int s;
		ZTracer::Endpoint e;
	public:
		Child() : e("0.0.0.1", 2, "child")
		{
		}
		void operator()() 
		{
			/*Connect to the socket*/
			soc_connect();	

			for (int i=0;i<10;i++) 
				process();
		}

		void process()
		{
			struct message msg;
			recv(s, &msg, sizeof(struct message), 0);
			
			ZTracer::Trace tr("Child process", &e, &msg.trace_info, true);
			tr.event("child start");
			
			usleep(10);
			std::cout << "Message received : " << msg.actual_message << ::std::endl;
			tr.event("child end");
			
			send(s, "*", 1, 0);
		}
		

		void soc_connect()
		{
			int len;
			struct sockaddr_un remote;

			if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
				std::cerr << "socket" << std::endl;
				exit(1);
			}

			std::cout << "Trying to connect...\n" << std::endl;

			remote.sun_family = AF_UNIX;
			strcpy(remote.sun_path, SOCK_PATH);
			len = strlen(remote.sun_path) + sizeof(remote.sun_family);
			if (connect(s, (struct sockaddr *)&remote, len) == -1) {
				std::cerr << "connect" << std::endl;;
				exit(1);
			}

			std::cout << "Connected" << std::endl;
		}

};
int main(int argc, const char *argv[])
{
	int r = ZTracer::ztrace_init();
	if (r < 0) {
		std::cout << "Error initializing blkin" << std::endl;
		return -1;
	}
	Parent p;
	Child c;
	std::thread workerThread1(p);
	std::thread workerThread2(c);
	workerThread1.join();
	workerThread2.join();

	return 0;
}
