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

MAJOR=0
MINOR=1
LIBS= -ldl -llttng-ust
DLIB=libblkin

LIB_DIR=$(shell pwd)
TEST_DIR=$(shell pwd)/tests
prefix= /usr/local
libdir= $(prefix)/lib
incdir= $(prefix)/include

H_FILES= zipkin_c.h zipkin_trace.h ztracer.hpp

default: $(DLIB).so test testpp testppp

$(DLIB).so: $(DLIB).$(MAJOR).so
	ln -sf $< $@

$(DLIB).$(MAJOR).so: $(DLIB).$(MAJOR).$(MINOR).so
	ln -sf $< $@

$(DLIB).$(MAJOR).$(MINOR).so: zipkin_c.o tp.o
	g++ -shared -o $@ $^ $(LIBS)

zipkin_c.o: zipkin_c.c zipkin_c.h zipkin_trace.h
	gcc -I. -Wall -fpic -g -c -o $@ $<

tp.o: tp.c zipkin_trace.h
	gcc -I. -fpic -g -c -o $@ $<

test: $(TEST_DIR)/test.c $(DLIB).so
	make -C tests test

testpp: $(TEST_DIR)/test.cc $(DLIB).so
	make -C tests testpp

testppp: $(TEST_DIR)/test_p.cc $(DLIB).so
	make -C tests testppp

run_c:
	make -C tests run_c

run_pp:
	make -C tests run_pp

run_ppp:
	make -C tests run_ppp

run: run_c run_pp

install:
	install -m 644  $(DLIB).$(MAJOR).$(MINOR).so $(DESTDIR)/$(libdir)
	cp -P $(DLIB).$(MAJOR).so $(DESTDIR)/$(libdir)
	cp -P $(DLIB).so $(DESTDIR)/$(libdir)
	install -m 644 $(H_FILES) $(DESTDIR)/$(incdir)

clean:
	rm -f *.o *.so
	make -C tests clean

distclean: clean
	rm -f socket
