# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#-------------------------------------------------------------------------
#
# Makefile for the managerment utilities
#
#-------------------------------------------------------------------------

top_builddir = ../..
include $(top_builddir)/src/Makefile.global

override CXXFLAGS += -Wall -O0 -g -std=c++11

all:
	${MAKE} -C tools $@
	$(CXX) $(CXXFLAGS) -o parallel_dsdgen parallel_dsdgen.cpp

install: all
	${MAKE} -C tools $@
	$(INSTALL_PROGRAM) parallel_dsdgen $(bindir)
	
clean distclean:
	${MAKE} -C tools $@
	-rm -f parallel_dsdgen
