#!/bin/bash

################################################################################
#
# This script installs some locales and language packs used by sqlite_fdw
# tests in Ubuntu.
#
# Usage: ./install_locales.sh
#
# Requirements:
# - having superuser privileges
#
################################################################################

sudo apt-get update
sudo apt-get install libexif12 libexif-dev
