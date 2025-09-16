#!/bin/bash

################################################################################
#
# This script builds bytea_exif in PostgreSQL source tree.
#
# Usage: ./build_bytea_exif.sh pg_version mode sqlite_for_testing_dir
#     pg_version is a PostgreSQL version like 17.0 to be built in.
#     mode is flag for bytea_exif compiler.
#     sqlite_for_testing_dir: path to install directory of SQLite version for testing
#
# Requirements
# - the source code of bytea_exif is available by git clone.
# - the source code of PostgreSQL is located in ~/workdir/postgresql-{pg_version}.
# - EXIF C library development package is installed in a system.
################################################################################

VERSION="$1"
MODE="$2"

mkdir -p ./workdir/postgresql-${VERSION}/contrib/bytea_exif
tar zxf ./bytea_exif.tar.gz -C ./workdir/postgresql-${VERSION}/contrib/bytea_exif/
cd ./workdir/postgresql-${VERSION}/contrib/bytea_exif

# show locally compiled sqlite library
ls -la /usr/local/lib

make

sudo make install
