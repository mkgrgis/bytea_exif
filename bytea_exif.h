/*-------------------------------------------------------------------------
 *
 * EXIF data extractor for PotgreSQL bytea data
 *
 * Copyright (c) 2025, mkgrgis
 *
 * IDENTIFICATION
 *        bytea_exif.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef BYTEA_EXIF_H
#define BYTEA_EXIF_H

#define CODE_VERSION 100000

#include <libexif/exif-loader.h>
#include <libexif/exif-utils.h>
#include <libexif/exif-ifd.h>
#include <libexif/exif-data-type.h>
#include <libexif/exif-tag.h>
#include <libexif/exif-format.h>

#include <math.h>
#include <string.h>

#if PG_VERSION_NUM < 120000
/* NullableDatum is introduced from PG12, we define it here in case of PG11 or earlier. */
typedef struct NullableDatum
{
#define FIELDNO_NULLABLE_DATUM_DATUM 0
    Datum        value;
#define FIELDNO_NULLABLE_DATUM_ISNULL 1
    bool        isnull;
    /* due to alignment padding this could be used for flags for free */
} NullableDatum;
#endif

#endif	/* BYTEA_EXIF_H */
