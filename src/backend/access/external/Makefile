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
# Makefile--
#    Makefile for access/external
#
#-------------------------------------------------------------------------

subdir = src/backend/access/external
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global

OBJS = fileam.o plugstorage.o url.o url_curl.o libchurl.o hd_work_mgr.o pxfuriparser.o pxfheaders.o pxfmasterapi.o ha_config.o pxfcomutils.o pxfutils.o pxffilters.o pxfanalyze.o read_cache.o

include $(top_srcdir)/src/backend/common.mk

