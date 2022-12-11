/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef STORAGE_SRC_STORAGE_CWRAPPER_HIVE_FILE_SYSTEM_C_H_
#define STORAGE_SRC_STORAGE_CWRAPPER_HIVE_FILE_SYSTEM_C_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int errorCode;
  const char* errorMessage;
} statusHiveC;

__attribute__((weak)) void getHiveDataDirectoryC(const char* host, int port, const char* dbname,
                           const char* tblname, const char** hiveUrl,
                           statusHiveC* status);

#ifdef __cplusplus
}
#endif

#endif  // STORAGE_SRC_STORAGE_CWRAPPER_HIVE_FILE_SYSTEM_C_H_
