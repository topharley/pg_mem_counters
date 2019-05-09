
pg_mem_counters
==============

This is a smalI extension for keeping total and rpm counters in shared memory of PostgreSQL server.

# Features

* Saves total hit for named counters in shared memory
* Saves RPM (requests per minute) for named counters in shared memory
 
PostgreSQL PostgreSQL 10, 11 are supported.

# Functions

Add counter 'counter1' incremented with 1 hit and get total hits:

    postgres=# select inc_mem_counter('counter1', 1);
Only get total hits of counter 'counter1':

    postgres=# select inc_mem_counter('counter1');    
Get counter 'counter1' RPM:    

    postgres=# select get_mem_counter_rpm('counter1');
Get table of all counters with total hits and RPM:

    postgres=# select * from metrics();

## Configuration

    shared_preload_libraries = 'pg_mem_counters' # (change requires restart)

# Compilation

You need a development environment for PostgreSQL extensions:

    export PATH="$PATH:/usr/pgsql-10/bin"
    make USE_PGXS=1 clean    
    make USE_PGXS=1 all
    make USE_PGXS=1 install

result:

	[topper@osboxes pg_mem_counters]# make USE_PGXS=1 clean
	rm -f pg_mem_counters.so pg_mem_counters.o
	rm -f pg_mem_counters.o
	rm -rf results/ regression.diffs regression.out tmp_check/ tmp_check_iso/ log/ output_iso/
	[topper@osboxes pg_mem_counters]# make USE_PGXS=1 all
	gcc -Wall -Wmissing-prototypes -Wpointer-arith -Wdeclaration-after-statement -Wendif-labels -Wmissing-format-attribute -Wformat-security -fno-strict-aliasing -fwrapv -fexcess-precision=standard -O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector-strong --param=ssp-buffer-size=4 -grecord-gcc-switches -m64 -mtune=generic -fPIC -I. -I./ -I/usr/pgsql-10/include/server -I/usr/pgsql-10/include/internal  -D_GNU_SOURCE -I/usr/include/libxml2  -I/usr/include  -c -o pg_mem_counters.o pg_mem_counters.c
	gcc -Wall -Wmissing-prototypes -Wpointer-arith -Wdeclaration-after-statement -Wendif-labels -Wmissing-format-attribute -Wformat-security -fno-strict-aliasing -fwrapv -fexcess-precision=standard -O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector-strong --param=ssp-buffer-size=4 -grecord-gcc-switches -m64 -mtune=generic -fPIC -L/usr/pgsql-10/lib -Wl,--as-needed  -L/usr/lib64 -Wl,--as-needed -Wl,-rpath,'/usr/pgsql-10/lib',--enable-new-dtags  -shared -o pg_mem_counters.so pg_mem_counters.o
	[topper@osboxes pg_mem_counters]# make USE_PGXS=1 install
	gcc -Wall -Wmissing-prototypes -Wpointer-arith -Wdeclaration-after-statement -Wendif-labels -Wmissing-format-attribute -Wformat-security -fno-strict-aliasing -fwrapv -fexcess-precision=standard -O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector-strong --param=ssp-buffer-size=4 -grecord-gcc-switches -m64 -mtune=generic -fPIC -I. -I./ -I/usr/pgsql-10/include/server -I/usr/pgsql-10/include/internal  -D_GNU_SOURCE -I/usr/include/libxml2  -I/usr/include  -c -o pg_mem_counters.o pg_mem_counters.c
	gcc -Wall -Wmissing-prototypes -Wpointer-arith -Wdeclaration-after-statement -Wendif-labels -Wmissing-format-attribute -Wformat-security -fno-strict-aliasing -fwrapv -fexcess-precision=standard -O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector-strong --param=ssp-buffer-size=4 -grecord-gcc-switches -m64 -mtune=generic -fPIC -L/usr/pgsql-10/lib -Wl,--as-needed  -L/usr/lib64 -Wl,--as-needed -Wl,-rpath,'/usr/pgsql-10/lib',--enable-new-dtags  -shared -o pg_mem_counters.so pg_mem_counters.o
	/usr/bin/mkdir -p '/usr/pgsql-10/share/extension'
	/usr/bin/mkdir -p '/usr/pgsql-10/share/extension'
	/usr/bin/mkdir -p '/usr/pgsql-10/lib'
	/usr/bin/install -c -m 644 .//pg_mem_counters.control '/usr/pgsql-10/share/extension/'
	/usr/bin/install -c -m 644 .//pg_mem_counters--1.0.sql  '/usr/pgsql-10/share/extension/'
	/usr/bin/install -c -m 755  pg_mem_counters.so '/usr/pgsql-10/lib/'

# Licence

Copyright (c) Victor Kosonogov (m1@topperharley.ru)

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.