# Copyright 2014 Marios Kogias <marioskogias@gmail.com>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or
# without modification, are permitted provided that the following
# conditions are met:
#
#   1. Redistributions of source code must retain the above
#      copyright notice, this list of conditions and the following
#      disclaimer.
#   2. Redistributions in binary form must reproduce the above
#      copyright notice, this list of conditions and the following
#      disclaimer in the documentation and/or other materials
#      provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS ``AS IS'' AND ANY EXPRESS
# OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
# USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

.PHONY: default clean distclean run run_c run_pp

LIB_DIR=$(shell pwd)/..
DLIB=$(LIB_DIR)/libblkin

test: test.c $(DLIB).so
	gcc test.c -o test -g -I$(LIB_DIR) -L$(LIB_DIR) -lblkin

testpp: test.cc $(DLIB).so
	LD_LIBRARY_PATH=$(LIB_DIR) g++ $< -o testpp -std=c++11 -g -I$(LIB_DIR) -L$(LIB_DIR) -lblkin -lpthread

testppp: test_p.cc $(DLIB).so
	LD_LIBRARY_PATH=$(LIB_DIR) g++ $< -o testppp -g -I$(LIB_DIR) -L$(LIB_DIR) -lblkin

run_c:
	LD_LIBRARY_PATH=$(LIB_DIR) ./test

run_pp:
	LD_LIBRARY_PATH=$(LIB_DIR) ./testpp

run_ppp:
	LD_LIBRARY_PATH=$(LIB_DIR) ./testppp

run: run_c run_pp

clean:
	rm -f *.o *.so test testpp testppp socket
