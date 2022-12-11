#!/usr/bin/env python
#
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
# Script to find mock object files for a given guts System under Test
# 
# Outputs the mock object files ready for copying them to a Makefile
#
# The script uses the mock_info.json data, which is generated by the generate_mocks.py
# script.
from subprocess import Popen, PIPE
import sys
import os
from optparse import OptionParser
import logging
import subprocess
import shutil
import hashlib
import errno
import json
import re
from pprint import pprint

def load_mock_info(options):    
    mock_info = json.load(open(options.mock_info_file))
    
    if os.path.exists(options.manual_mock_info_file):
        manual_mock_info = json.load(open(options.manual_mock_info_file))
    mock_info["functions"].update(manual_mock_info["functions"])
    mock_info["globals"].update(manual_mock_info["globals"])
    return mock_info

def get_object_file(mock_source_filename):
    return "%s.o" % (os.path.splitext(mock_source_filename)[0])
    
def search_for_object(mock_info, object_name):
    if object_name in mock_info["functions"]:
        return mock_info["functions"][object_name]
    if object_name in mock_info["globals"]:
        return mock_info["globals"][object_name]
    return None

def get_mock_filename(source_filename):
    base = os.path.basename(source_filename)
    base = os.path.splitext(base)
    
    return "%s_mock.c" % (base[0])

if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    
    reference_line_pattern = re.compile(r'"_(.+)", referenced from')

    parser = OptionParser()
    parser.add_option("--mock_info_file",
                  dest="mock_info_file", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "mock/mock_info.json"))
    parser.add_option("--manual_mock_info_file",
                  dest="manual_mock_info_file", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "mock/manual_mock_info.json"))
    (options, args) = parser.parse_args()
    
    mock_info = load_mock_info(options)
    
    found_mock_sources = set()
    skipped_references = set()
    
    for line in sys.stdin:
        m = reference_line_pattern.search(line)
        if m:
            object_name = m.groups()[0]
            mock_info_data = search_for_object(mock_info, object_name)
            if mock_info_data:
                mock_source_filename = get_mock_filename(mock_info_data["filename"])
                found_mock_sources.add(mock_source_filename)
            else:
                skipped_references.add(object_name)
    
    print "Mock Source Files"
    for found_mock_source in sorted(found_mock_sources):
        print found_mock_source
    print
    print "Prepared for Makefile"
    formatted_mock_sources = ["$(MOCK_DIR)/%s" % get_object_file(found_mock_source) for found_mock_source in sorted(found_mock_sources)]
    print " \\\n\t".join(formatted_mock_sources)
    print
    
    if len(skipped_references) > 0:
        print "Skipped References"
        for skipped_ref in sorted(skipped_references):
            print skipped_ref