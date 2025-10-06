EXIF data reader from binary `bytea` data for PostgreSQL
======================================================

This is a [PostgreSQL](https://www.postgresql.org/) extension which allows to determine MIME type of PostgreSQL `bytea` data and read EXIF data from images stored as `bytea` in PostgreSQL. This FDW works with all of PostgreSQL 8.4+ and confirmed with libexif 0.6.22.

<img src="https://upload.wikimedia.org/wikipedia/commons/2/29/Postgresql_elephant.svg" align="center" height="100" alt="PostgreSQL"/>	+	<img src="logo.gif" align="center" height="100" alt="EXIF"/>

Current CI status.

[![CI Status](https://github.com/mkgrgis/bytea_exif/actions/workflows/CI.yml/badge.svg)](https://github.com/mkgrgis/bytea_exif/actions/workflows/CI.yml)

Contents
--------

1. [Features](#features)
2. [Supported platforms](#supported-platforms)
3. [Installation](#installation)
4. [Usage](#usage)
5. [Functions](#functions)
6. [Examples](#examples)
10. [Limitations](#limitations)
11. [Tests](#tests)
12. [Contributing](#contributing)
13. [Useful links](#useful-links)

Features
--------

### Common features

This extension reads EXIF data from a `bytea` data in a PostgreSQL database.
Used output formats are determined by `libexif` functions with some Open
Geospatial Consorcium (OGC) additions including coordinates of photographer and
destination coordinates if assigned.

Also see [Limitations](#limitations)

Supported platforms
-------------------

`bytea_exif` was developed on Linux and should run on any
reasonably POSIX-compliant system.

Installation
------------

### Package installation

There are no Linux distributives contains internal package with `bytea_exif`.

Known special runtime dependency: `libexif12`
Use `apt-get install libexif12` to install this.

### Source installation

Prerequisites:
* `gcc`
* `make`
* `postgresql-server-dev`, especially `postgres.h`
* `libexif-dev`, especially `libexif/*.h` headers
* `libmagic-dev`, especially `magic.h` header
* `iconv.h` header for UTF-16 and JIS data converting to PostgreSQL value. This header usually belongs to `libc*-dev`.

#### 1. Install C EXIF library and Postgres development libraries

For Debian or Ubuntu:

`apt-get install libexif-dev libmagic-dev -y`

`apt-get install postgresql-server-dev-XX -y`, where XX matches your postgres version, i.e. `apt-get install postgresql-server-dev-18 -y`

#### 2. Build and install bytea_exif

`bytea_exif` does not require to be compiled with PostGIS. This is used only for full test which includes test for GIS support.

Before building please add a directory of `pg_config` to PATH or ensure `pg_config` program is accessible from command line only by the name.

Build and install without MIME detection function support
```sh
make USE_NO_MIME=1
make install USE_NO_MIME=1
```

Usual build, also can be combined with `USE_NO_MIME`.
```sh
make USE_PGXS=1
make install USE_PGXS=1
```

If you want to build `bytea_exif` in a source tree of PostgreSQL, use
```sh
make
make install
```

Usage
-----

### Datatypes of result
Spatial functions of this extension returns common OGC `ST_Point` data like `Point(lon lat)`;

Functions
---------

- **bytea_exif_version**();
Returns standard "version integer" as `major version * 10000 + minor version * 100 + bugfix`.
```
bytea_exif_version
--------------------
              10000
```

- bool **bytea_get_mime_type**(data bytea);

Returns mime type value of bytea data based on libmagic list of possible values.

- text **bytea_has_exif**(data bytea);

Returns `true` if there is EXIF data container. Warning: the container can be decalred without any useful data.

- bool  **bytea_has_exif_ifd**(data bytea, ifd text);

Returns true if there is any EXIF data of pointed EXIF directory in the bytea value. Ifd values: `0`, `1`, `EXIF`, `GPS`, `Interoperability`.

- text **bytea_get_exif_tag_value**(data bytea, tag text);

Returns value of a EXIF tag if there is such data

- json **bytea_get_exif_json**(data bytea);

Returns JSON data which contains full set of presented in bytea EXIF tags and it's values

- text **bytea_get_exif_point**(data bytea);

Returns text OGC `ST_Point` value of a photographer location.
Note: returns `SRID=4326` point for `WGS-84` EXIF data geodatum, no SRID otherwise.

- text **bytea_get_exif_dest_point**(data bytea);

Returns text OGC `ST_Point` value of a main photo object location.
Note: returns `SRID=4326` point for `WGS-84` EXIF data geodatum, no SRID otherwise.

- timestamptz **bytea_get_exif_gps_utc_timestamp**(data bytea);

Returns GPS timestamp of image. According EXIF standard the value is UTC time.

- timestamp **bytea_get_exif_gps_local_timestamp**(data bytea);

Returns GPS timestamp of image transformed to local time.

- text **bytea_get_exif_user_comment**(data bytea);

Returns UserComment EXIF tag text data as text encoded for current PostgreSQL database.

Examples
--------

### Install the extension:

Once for a database you need, as PostgreSQL superuser.

```sql
	CREATE EXTENSION bytea_exif;

-- only for testing or for example playground
	CREATE EXTENSION http;
	CREATE EXTENSION postgis;
```

### Parse EXIF data of a picture from internet:

```sql
-- Common flags and geo data
with a as ( -- download a picture from internet as `bytea` data
select text_to_bytea(content) img
  from http_get('http://moscowparks.narod.ru/_ph/58/61261130.jpg')
)
select bytea_has_exif(img) flag_exif,
       bytea_has_exif_ifd(img, 'GPS') flag_gps_ifd,
       bytea_get_exif_point(img)::geometry photographer,
       bytea_get_exif_dest_point(img)::geometry dest,
       ST_MakeLine(bytea_get_exif_point(img)::geometry, bytea_get_exif_dest_point(img)::geometry) line,
       img
from a;

-- Some other data
with a as ( -- download a picture from internet as `bytea` data
select text_to_bytea(content) img
  from http_get('http://moscowparks.narod.ru/_ph/58/61261130.jpg')
)
select bytea_get_exif_json(img) exif,
       to_timestamp(bytea_get_exif_json(img) ->> 'DateTimeOriginal',
                    'YYYY:MM:DD HH24:MI:SS'
                   )::timestamp without time zone at time zone 'Etc/UTC' ts,
       ((to_date(bytea_get_exif_json(img) ->> 'GPSDateStamp', 'YYYY:MM:DD')::timestamp +
       ((bytea_get_exif_json(img) ->> 'GPSTimeStamp')||' UTC')::time)) at time zone 'utc' "ts_GPS",
       bytea_get_exif_json(img) ->> 'Artist' "Artist",
       bytea_get_exif_gps_utc_timestamp(img) at time zone 'utc' "ts_GPS_fast",
       ST_MakeLine(bytea_get_exif_point(img)::geometry, bytea_get_exif_dest_point(img)::geometry) line,
       img
from a;

-- MIME downloaded data test
-- this is relative long operation implemented by libmagic
-- function, which is also used in "file" Unix command.
with a as ( -- download a picture from internet as `bytea` data
select text_to_bytea(content) img
  from http_get('http://moscowparks.narod.ru/_ph/58/61261130.jpg')
)
select bytea_get_mime_type(img) mime,
       img
from a;
```
Limitations
-----------

### Data size
- `bytea` objects larger than 40Mb is not supported for speed reasons

Tests
-----
Test directory have structure as following:

```
+---sql
|   +---13.15
|   |       filename1.sql
|   |       filename2.sql
|   |
|   +---14.12
|   |       filename1.sql
|   |       filename2.sql
|   |
.................
|   \---17.0
|          filename1.sql
|          filename2.sql
|
\---expected
|   +---13.15
|   |       filename1.out
|   |       filename2.out
|   |
|   +---14.12
|   |       filename1.out
|   |       filename2.out
|   |
.................
|   \---17.0
            filename1.out
            filename2.out
```
The test cases for each version are based on the test of corresponding version of PostgreSQL.
You can execute test by test.sh directly.
The version of PostgreSQL is detected automatically by $(VERSION) variable in Makefile.
The corresponding sql and expected directory will be used to compare the result. For example, for Postgres 15.0, you can execute "test.sh" directly, and the sql/15.0 and expected/15.0 will be used to compare automatically.

Test data directory is `/tmp/bytea_exif_test`. If you have `/tmp` mounted as `tmpfs` the tests will be up to 800% faster.

Contributing
------------

Opening issues and pull requests on GitHub are welcome.
For pull request, please make sure these items below for testing:
- Create test cases (if needed) for the latest version of PostgreSQL supported by `bytea_exif`. All error testcases should have a comment about test purpose.
- Execute test cases and update expectations for the latest version of PostgreSQL
- Test creation and execution for other PostgreSQL versions are welcome but not required.

Preferred code style see in PostgreSQL source codes. For example

```C
type
funct_name (type arg ...)
{
	t1 var1 = value1;
	t2 var2 = value2;

	for (;;)
	{
	}
	if ()
	{
	}
}
```
Useful links
------------

### Source

 - https://pgxn.org/dist/bytea_exif/

License
-------
* Copyright © 2025, mkgrgis

PostgreSQL license
Permission to use, copy, modify, and distribute this software and its documentation for any purpose, without fee, and without a written agreement is hereby granted, provided that the above copyright notice and this paragraph and the following two paragraphs appear in all copies.

See the [`License`](License) file for full details.
