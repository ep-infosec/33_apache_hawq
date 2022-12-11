DROP TABLE IF EXISTS test_parquet_gzip_3;
CREATE TABLE test_parquet_gzip_3 (c0 int4, c1 text) with (appendonly=true, orientation=parquet, compresstype=gzip, rowgroupsize=8388608, pagesize=1048576, compresslevel=3);
INSERT INTO test_parquet_gzip_3 values (128, 'hello world'),
(null, 'hello world'),
(-32768, repeat('b',1000)),
(128, repeat('b',1000)),
(-128, null),
(2147483647, null),
(null, 'hello world'),
(-32768, 'hello world'),
(null, 'hello world'),
(128, repeat('b',1000)),
(-128, ''),
(128, null),
(32767, ''),
(128, null),
(-2147483648, null),
(0, repeat('b',1000)),
(0, repeat('b',1000)),
(0, 'hello world'),
(-32768, 'hello world'),
(128, 'z'),
(128, repeat('b',1000)),
(2147483647, 'hello world'),
(-128, repeat('b',1000)),
(-2147483648, 'z'),
(-2147483648, null),
(null, ''),
(0, ''),
(-32768, 'hello world'),
(128, null),
(-2147483648, 'z'),
(null, 'hello world'),
(-128, null),
(-128, 'hello world'),
(32767, null),
(0, ''),
(-2147483648, repeat('b',1000)),
(-128, repeat('b',1000)),
(0, ''),
(128, 'z'),
(128, ''),
(-128, 'z'),
(32767, repeat('b',1000)),
(2147483647, 'hello world'),
(-2147483648, repeat('b',1000)),
(32767, 'hello world'),
(-128, ''),
(-32768, 'z'),
(0, repeat('b',1000)),
(-2147483648, repeat('b',1000)),
(0, 'hello world'),
(-128, null),
(32767, 'z'),
(-32768, null),
(2147483647, null),
(-32768, null),
(2147483647, ''),
(-32768, 'z'),
(-2147483648, 'z'),
(0, null),
(0, 'z'),
(128, repeat('b',1000)),
(-2147483648, ''),
(0, repeat('b',1000)),
(null, null),
(null, repeat('b',1000)),
(null, repeat('b',1000)),
(null, ''),
(2147483647, repeat('b',1000)),
(-2147483648, 'hello world'),
(32767, 'hello world'),
(2147483647, null),
(2147483647, ''),
(-32768, ''),
(32767, 'hello world'),
(-2147483648, 'z'),
(-128, ''),
(32767, 'z'),
(-32768, null),
(null, repeat('b',1000)),
(0, 'z'),
(null, null),
(-128, 'hello world'),
(-32768, repeat('b',1000)),
(0, ''),
(128, repeat('b',1000)),
(2147483647, ''),
(-2147483648, repeat('b',1000)),
(-2147483648, null),
(0, 'z'),
(2147483647, 'z'),
(-2147483648, repeat('b',1000)),
(0, 'z'),
(-32768, ''),
(-128, 'z'),
(128, 'hello world'),
(128, 'z'),
(-2147483648, ''),
(-32768, null),
(32767, null),
(2147483647, null),
(null, repeat('b',1000)),
(128, repeat('b',1000)),
(-128, repeat('b',1000)),
(32767, 'z'),
(null, null),
(-128, 'z'),
(null, ''),
(2147483647, repeat('b',1000)),
(-32768, 'hello world'),
(null, ''),
(-32768, repeat('b',1000)),
(-128, 'hello world'),
(-32768, 'z'),
(128, repeat('b',1000)),
(0, ''),
(-128, 'hello world'),
(2147483647, 'z'),
(-128, null),
(128, repeat('b',1000)),
(2147483647, ''),
(-128, repeat('b',1000)),
(-2147483648, ''),
(-32768, null),
(2147483647, ''),
(128, null),
(128, 'hello world'),
(null, 'hello world'),
(128, ''),
(32767, null),
(128, null),
(-2147483648, repeat('b',1000)),
(0, ''),
(-32768, ''),
(-32768, null),
(null, 'z'),
(null, null),
(null, null),
(32767, 'z'),
(128, repeat('b',1000)),
(2147483647, repeat('b',1000)),
(32767, ''),
(-2147483648, ''),
(128, ''),
(-2147483648, null),
(2147483647, 'hello world'),
(-128, 'z'),
(null, repeat('b',1000)),
(-32768, 'z'),
(2147483647, null),
(-32768, 'hello world'),
(0, repeat('b',1000)),
(32767, null),
(128, 'z'),
(-2147483648, null),
(128, 'hello world'),
(0, 'z'),
(-2147483648, 'hello world'),
(32767, ''),
(-128, repeat('b',1000)),
(128, 'hello world'),
(-32768, null),
(32767, repeat('b',1000)),
(32767, null),
(-32768, ''),
(32767, ''),
(null, ''),
(-128, ''),
(2147483647, 'hello world'),
(-32768, 'z'),
(32767, 'z'),
(-2147483648, ''),
(-2147483648, 'hello world'),
(-32768, 'z'),
(-32768, 'hello world'),
(null, ''),
(-128, null),
(32767, null),
(128, 'hello world'),
(-128, ''),
(32767, null),
(2147483647, 'hello world'),
(-32768, repeat('b',1000)),
(0, repeat('b',1000)),
(-128, ''),
(2147483647, 'hello world'),
(-128, null),
(128, null),
(-32768, 'hello world'),
(0, 'z'),
(-2147483648, 'z'),
(null, 'z'),
(2147483647, ''),
(32767, 'z'),
(null, 'z'),
(null, 'z'),
(-32768, ''),
(-128, null),
(0, null),
(32767, 'hello world'),
(128, repeat('b',1000)),
(128, repeat('b',1000)),
(2147483647, ''),
(-32768, repeat('b',1000)),
(32767, ''),
(0, ''),
(-2147483648, 'z'),
(128, null),
(128, 'hello world'),
(-32768, repeat('b',1000)),
(null, repeat('b',1000)),
(-32768, 'z'),
(-2147483648, ''),
(2147483647, null),
(-128, null),
(128, null),
(32767, 'z'),
(32767, repeat('b',1000)),
(32767, 'z'),
(128, ''),
(2147483647, 'hello world'),
(-2147483648, ''),
(128, repeat('b',1000)),
(-128, repeat('b',1000)),
(null, 'z'),
(-2147483648, ''),
(2147483647, 'z'),
(-32768, 'z'),
(-2147483648, 'z'),
(32767, 'hello world'),
(-32768, 'z'),
(-32768, null),
(-2147483648, 'z'),
(null, ''),
(128, null),
(0, null),
(128, 'hello world'),
(-128, null),
(-32768, repeat('b',1000)),
(null, ''),
(-32768, null),
(32767, 'z'),
(-2147483648, 'z'),
(-32768, null),
(null, 'hello world'),
(32767, ''),
(-2147483648, 'hello world'),
(128, 'hello world'),
(-2147483648, ''),
(128, ''),
(128, repeat('b',1000)),
(32767, 'hello world'),
(2147483647, null),
(2147483647, repeat('b',1000)),
(-128, repeat('b',1000)),
(-128, repeat('b',1000)),
(32767, 'hello world'),
(32767, null),
(-32768, null),
(32767, 'hello world'),
(-2147483648, 'z'),
(2147483647, ''),
(-128, ''),
(0, ''),
(0, 'hello world'),
(32767, 'z'),
(-2147483648, repeat('b',1000)),
(0, 'z'),
(-2147483648, 'z'),
(2147483647, repeat('b',1000)),
(-128, 'hello world'),
(-32768, ''),
(-128, 'hello world'),
(null, null),
(-128, 'hello world'),
(-32768, ''),
(2147483647, 'z'),
(-2147483648, 'hello world'),
(-32768, 'z'),
(32767, repeat('b',1000)),
(-2147483648, null),
(null, 'z'),
(2147483647, 'z'),
(0, ''),
(0, ''),
(0, null),
(-128, ''),
(null, 'hello world'),
(128, 'hello world'),
(-128, 'hello world'),
(0, 'hello world'),
(2147483647, 'z'),
(32767, ''),
(-32768, 'hello world'),
(-128, null),
(-32768, null),
(-2147483648, ''),
(128, ''),
(0, ''),
(32767, repeat('b',1000)),
(128, ''),
(-128, null),
(128, repeat('b',1000)),
(-2147483648, repeat('b',1000)),
(null, 'z'),
(-2147483648, 'hello world'),
(-2147483648, 'z'),
(-32768, 'hello world'),
(-32768, 'hello world'),
(2147483647, ''),
(128, 'hello world'),
(32767, null),
(-32768, ''),
(null, null),
(0, 'z'),
(128, ''),
(128, ''),
(-2147483648, ''),
(128, ''),
(32767, 'z'),
(-128, 'hello world'),
(-128, repeat('b',1000)),
(128, 'hello world'),
(32767, ''),
(-32768, repeat('b',1000)),
(-2147483648, repeat('b',1000)),
(0, null),
(2147483647, repeat('b',1000)),
(null, ''),
(32767, ''),
(-32768, null),
(2147483647, 'z'),
(null, 'z'),
(-128, ''),
(-32768, null),
(-32768, ''),
(2147483647, 'hello world'),
(128, ''),
(0, 'hello world'),
(2147483647, 'hello world'),
(null, 'z'),
(32767, null),
(-32768, 'hello world'),
(128, 'hello world'),
(-2147483648, 'z'),
(-32768, null),
(32767, ''),
(0, 'hello world'),
(2147483647, 'z'),
(-128, ''),
(null, ''),
(null, repeat('b',1000)),
(128, null),
(32767, 'z'),
(-32768, repeat('b',1000)),
(128, 'z'),
(32767, ''),
(-128, 'z'),
(2147483647, 'hello world'),
(32767, repeat('b',1000)),
(0, null),
(0, ''),
(32767, 'hello world'),
(-32768, 'hello world'),
(0, 'z'),
(2147483647, repeat('b',1000)),
(null, repeat('b',1000)),
(-128, 'hello world'),
(null, repeat('b',1000)),
(2147483647, 'hello world'),
(2147483647, 'z'),
(2147483647, ''),
(-2147483648, 'hello world'),
(-2147483648, 'z'),
(null, 'z'),
(2147483647, null),
(0, 'z'),
(-32768, repeat('b',1000)),
(null, repeat('b',1000)),
(null, 'z'),
(2147483647, ''),
(-32768, repeat('b',1000)),
(-2147483648, 'hello world'),
(2147483647, null),
(2147483647, 'z'),
(-32768, null),
(-128, 'z'),
(128, null),
(null, repeat('b',1000)),
(32767, 'hello world'),
(0, 'hello world'),
(-128, repeat('b',1000)),
(32767, 'z'),
(128, 'z'),
(2147483647, ''),
(-128, 'hello world'),
(0, repeat('b',1000)),
(32767, null),
(null, 'z'),
(-128, repeat('b',1000)),
(0, 'hello world'),
(-128, null),
(-2147483648, null),
(128, 'z'),
(128, ''),
(null, ''),
(128, ''),
(0, repeat('b',1000)),
(-128, 'z'),
(32767, 'z'),
(0, null),
(128, 'z'),
(32767, null),
(-2147483648, ''),
(-128, ''),
(null, 'hello world'),
(-32768, 'z'),
(32767, repeat('b',1000)),
(-128, repeat('b',1000)),
(128, repeat('b',1000)),
(0, 'hello world'),
(0, 'z'),
(-128, null),
(0, null),
(null, 'hello world'),
(2147483647, repeat('b',1000)),
(-128, 'hello world'),
(2147483647, null),
(-128, 'z'),
(32767, 'z'),
(-32768, ''),
(128, 'z'),
(0, 'z'),
(2147483647, null),
(2147483647, 'hello world'),
(-2147483648, repeat('b',1000)),
(2147483647, ''),
(-32768, 'z'),
(128, ''),
(128, 'hello world'),
(-2147483648, 'hello world'),
(128, 'z'),
(2147483647, null),
(-2147483648, 'hello world'),
(null, repeat('b',1000)),
(-32768, 'hello world'),
(32767, 'hello world'),
(null, ''),
(2147483647, 'z'),
(128, 'hello world'),
(32767, 'hello world'),
(-32768, ''),
(-32768, repeat('b',1000)),
(0, repeat('b',1000)),
(32767, repeat('b',1000)),
(2147483647, ''),
(128, 'z'),
(0, repeat('b',1000)),
(-2147483648, null),
(2147483647, repeat('b',1000)),
(32767, null),
(32767, 'z'),
(2147483647, ''),
(-32768, null),
(null, ''),
(-2147483648, 'z'),
(32767, null),
(-128, 'hello world'),
(-2147483648, repeat('b',1000)),
(-32768, ''),
(32767, repeat('b',1000)),
(-32768, 'z'),
(-128, 'z'),
(-32768, null),
(-128, 'hello world'),
(128, 'hello world'),
(-128, 'z'),
(0, ''),
(128, repeat('b',1000)),
(128, 'z'),
(-2147483648, 'z'),
(-128, ''),
(0, 'hello world'),
(-2147483648, null),
(128, ''),
(0, 'z'),
(null, 'z'),
(0, repeat('b',1000)),
(2147483647, repeat('b',1000)),
(null, ''),
(-2147483648, repeat('b',1000)),
(32767, ''),
(null, repeat('b',1000)),
(128, ''),
(128, ''),
(-2147483648, null),
(2147483647, ''),
(128, 'z'),
(null, ''),
(32767, repeat('b',1000)),
(32767, 'hello world');