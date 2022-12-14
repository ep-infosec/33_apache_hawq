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
/* 
 * Legal Notice 
 * 
 * This document and associated source code (the "Work") is a part of a 
 * benchmark specification maintained by the TPC. 
 * 
 * The TPC reserves all right, title, and interest to the Work as provided 
 * under U.S. and international laws, including without limitation all patent 
 * and trademark rights therein. 
 * 
 * No Warranty 
 * 
 * 1.1 TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THE INFORMATION 
 *     CONTAINED HEREIN IS PROVIDED "AS IS" AND WITH ALL FAULTS, AND THE 
 *     AUTHORS AND DEVELOPERS OF THE WORK HEREBY DISCLAIM ALL OTHER 
 *     WARRANTIES AND CONDITIONS, EITHER EXPRESS, IMPLIED OR STATUTORY, 
 *     INCLUDING, BUT NOT LIMITED TO, ANY (IF ANY) IMPLIED WARRANTIES, 
 *     DUTIES OR CONDITIONS OF MERCHANTABILITY, OF FITNESS FOR A PARTICULAR 
 *     PURPOSE, OF ACCURACY OR COMPLETENESS OF RESPONSES, OF RESULTS, OF 
 *     WORKMANLIKE EFFORT, OF LACK OF VIRUSES, AND OF LACK OF NEGLIGENCE. 
 *     ALSO, THERE IS NO WARRANTY OR CONDITION OF TITLE, QUIET ENJOYMENT, 
 *     QUIET POSSESSION, CORRESPONDENCE TO DESCRIPTION OR NON-INFRINGEMENT 
 *     WITH REGARD TO THE WORK. 
 * 1.2 IN NO EVENT WILL ANY AUTHOR OR DEVELOPER OF THE WORK BE LIABLE TO 
 *     ANY OTHER PARTY FOR ANY DAMAGES, INCLUDING BUT NOT LIMITED TO THE 
 *     COST OF PROCURING SUBSTITUTE GOODS OR SERVICES, LOST PROFITS, LOSS 
 *     OF USE, LOSS OF DATA, OR ANY INCIDENTAL, CONSEQUENTIAL, DIRECT, 
 *     INDIRECT, OR SPECIAL DAMAGES WHETHER UNDER CONTRACT, TORT, WARRANTY,
 *     OR OTHERWISE, ARISING IN ANY WAY OUT OF THIS OR ANY OTHER AGREEMENT 
 *     RELATING TO THE WORK, WHETHER OR NOT SUCH AUTHOR OR DEVELOPER HAD 
 *     ADVANCE NOTICE OF THE POSSIBILITY OF SUCH DAMAGES. 
 * 
 * Contributors:
 * Gradient Systems
 */ 
#ifndef W_TDEFS_H
#define W_TDEFS_H
tdef w_tdefs[] = { 
{"call_center", "cc", FL_TYPE_2|FL_SMALL, CALL_CENTER_START, CALL_CENTER_END, CALL_CENTER, -1, NULL, 0, 0, 100, 0, 0x0B, NULL}, 
{"catalog_page", "cp", 0, CATALOG_PAGE_START, CATALOG_PAGE_END, CATALOG_PAGE, -1, NULL, 0, 0, 200, 0, 0x03, NULL}, 
{"catalog_returns", "cr", FL_CHILD, CATALOG_RETURNS_START, CATALOG_RETURNS_END, CATALOG_RETURNS, -1, NULL, 0, 0, 400, 0, 0x10007, NULL}, 
{"catalog_sales", "cs", FL_PARENT|FL_DATE_BASED|FL_VPRINT, CATALOG_SALES_START, CATALOG_SALES_END, CATALOG_SALES, CATALOG_RETURNS, NULL, 0, 0, 100, 0, 0x28000, NULL}, 
{"customer", "cu", 0, CUSTOMER_START, CUSTOMER_END, CUSTOMER, -1, NULL, 0, 0, 700, 0, 0x13, NULL}, 
{"customer_address", "ca", 0, CUSTOMER_ADDRESS_START, CUSTOMER_ADDRESS_END, CUSTOMER_ADDRESS, -1, NULL, 0, 0, 600, 0, 0x03, NULL}, 
{"customer_demographics", "cd", 0, CUSTOMER_DEMOGRAPHICS_START, CUSTOMER_DEMOGRAPHICS_END, CUSTOMER_DEMOGRAPHICS, 823200, NULL, 0, 0, 0, 0, 0x1, NULL}, 
{"date_dim", "da", 0, DATE_START, DATE_END, DATE, -1, NULL, 0, 0, 0, 0, 0x03, NULL}, 
{"household_demographics", "hd", 0, HOUSEHOLD_DEMOGRAPHICS_START, HOUSEHOLD_DEMOGRAPHICS_END, HOUSEHOLD_DEMOGRAPHICS, 7200, NULL, 0, 0, 0, 0, 0x01, NULL}, 
{"income_band", "ib", 0, INCOME_BAND_START, INCOME_BAND_END, INCOME_BAND, -1, NULL, 0, 0, 0, 0, 0x1, NULL}, 
{"inventory", "inv", FL_DATE_BASED, INVENTORY_START, INVENTORY_END, INVENTORY, -1, NULL, 0, 0, 1000, 0, 0x07, NULL}, 
{"item", "it", FL_TYPE_2, ITEM_START, ITEM_END, ITEM, -1, NULL, 0, 0, 50, 0, 0x0B, NULL}, 
{"promotion", "pr", 0, PROMOTION_START, PROMOTION_END, PROMOTION, -1, NULL, 0, 0, 200, 0, 0x03, NULL}, 
{"reason", "re", 0, REASON_START, REASON_END, REASON, -1, NULL, 0, 0, 0, 0, 0x03, NULL}, 
{"ship_mode", "sm", 0, SHIP_MODE_START, SHIP_MODE_END, SHIP_MODE, -1, NULL, 0, 0, 0, 0, 0x03, NULL}, 
{"store", "st", FL_TYPE_2|FL_SMALL, STORE_START, STORE_END, STORE, -1, NULL, 0, 0, 100, 0, 0xB, NULL}, 
{"store_returns", "sr", FL_CHILD, STORE_RETURNS_START, STORE_RETURNS_END, STORE_RETURNS, -1, NULL, 0, 0, 700, 0, 0x204, NULL}, 
{"store_sales", "ss", FL_PARENT|FL_DATE_BASED|FL_VPRINT, STORE_SALES_START, STORE_SALES_END, STORE_SALES, STORE_RETURNS, NULL, 0, 0, 900, 0, 0x204, NULL}, 
{"time_dim", "ti", 0, TIME_START, TIME_END, TIME, -1, NULL, 0, 0, 0, 0, 0x03, NULL}, 
{"warehouse", "wa", FL_SMALL, WAREHOUSE_START, WAREHOUSE_END, WAREHOUSE, -1, NULL, 0, 0, 200, 0, 0x03, NULL}, 
{"web_page", "wp", FL_TYPE_2, WEB_PAGE_START, WEB_PAGE_END, WEB_PAGE, -1, NULL, 0, 0, 250, 0, 0x0B, NULL}, 
{"web_returns", "wr", FL_CHILD, WEB_RETURNS_START, WEB_RETURNS_END, WEB_RETURNS, -1, NULL, 0, 0, 900, 0, 0x2004, NULL}, 
{"web_sales", "ws", FL_VPRINT|FL_PARENT|FL_DATE_BASED, WEB_SALES_START, WEB_SALES_END, WEB_SALES, WEB_RETURNS, NULL, 0, 0, 5, 1100, 0x20008, NULL}, 
{"web_site", "web", FL_TYPE_2|FL_SMALL, WEB_SITE_START, WEB_SITE_END, WEB_SITE, -1, NULL, 0, 0, 100, 0, 0x0B, NULL}, 
{"dbgen_version", "dv", 0, DBGEN_VERSION_START, DBGEN_VERSION_END, DBGEN_VERSION, -1, NULL, 0, 0, 0, 0, 0, NULL}, 
{NULL}
};
#endif
