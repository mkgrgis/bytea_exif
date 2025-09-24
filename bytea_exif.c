/*-------------------------------------------------------------------------
 *
 * EXIF data extractor for PotgreSQL bytea data
 *
 * (c) 2025, mkgrgis
 *
 * IDENTIFICATION
 *		bytea_exif.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "bytea_exif.h"

#include <iconv.h>

#include "fmgr.h"
#include "mb/pg_wchar.h"
#include "storage/ipc.h"
#include "utils/builtins.h"
#include "utils/timestamp.h"
#if PG_VERSION_NUM >= 160000
	#include "varatt.h"
#else
	#include "lib/stringinfo.h"
#endif
#ifdef BYTEA_MIME
#include <magic.h>
#endif

#ifdef BYTEA_MIME
static	magic_t magic_cookie;
#endif

Datum bytea_exif_version(PG_FUNCTION_ARGS);
Datum bytea_exif_libexif_version(PG_FUNCTION_ARGS);
Datum bytea_get_mime_type(PG_FUNCTION_ARGS);
Datum bytea_has_exif(PG_FUNCTION_ARGS);
Datum bytea_has_exif_ifd(PG_FUNCTION_ARGS);
Datum bytea_get_exif_tag_value(PG_FUNCTION_ARGS);
Datum bytea_get_exif_json(PG_FUNCTION_ARGS);
Datum bytea_get_exif_point(PG_FUNCTION_ARGS);
Datum bytea_get_exif_dest_point(PG_FUNCTION_ARGS);
Datum bytea_get_exif_gps_utc_timestamp(PG_FUNCTION_ARGS);
Datum bytea_get_exif_user_comment(PG_FUNCTION_ARGS);
static void bytea_exif_exit(int code, Datum arg);

extern PGDLLEXPORT void _PG_init(void);
static void bytea_exif_exit(int code, Datum arg);

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(bytea_exif_version);
PG_FUNCTION_INFO_V1(bytea_exif_libexif_version);
PG_FUNCTION_INFO_V1(bytea_get_mime_type);
PG_FUNCTION_INFO_V1(bytea_has_exif);
PG_FUNCTION_INFO_V1(bytea_has_exif_ifd);
PG_FUNCTION_INFO_V1(bytea_get_exif_tag_value);
PG_FUNCTION_INFO_V1(bytea_get_exif_json);
PG_FUNCTION_INFO_V1(bytea_get_exif_point);
PG_FUNCTION_INFO_V1(bytea_get_exif_dest_point);
PG_FUNCTION_INFO_V1(bytea_get_exif_gps_utc_timestamp);
PG_FUNCTION_INFO_V1(bytea_get_exif_user_comment);

static double
exif_gps_extract_double (ExifByteOrder o, ExifEntry * e);
static void
bytea_exif_exit(int code, Datum arg);
static int
Ifd_from_name (char* ifdname);
char*
escapeJson(const char* json);

/*
 * Library load-time initialization, sets on_proc_exit() callback for
 * backend shutdown.
 */
void
_PG_init(void)
{
#ifdef BYTEA_MIME
	/*
	 * MAGIC_MIME tells magic to return a mime of the file,
	 * but you can specify different things
	 */
	magic_cookie = magic_open(MAGIC_MIME_TYPE);
	if (magic_cookie == NULL) {
		ereport(ERROR,
			(errcode(ERRCODE_WARNING),
			 errmsg("Unable to initialize mime type library"),
			 errhint("Libmagic constant magic_open(%d)", MAGIC_MIME_TYPE)));
	}
	if (magic_load(magic_cookie, NULL) != 0)
	{
		ereport(ERROR,
		(errcode(ERRCODE_WARNING),
		 errmsg("Unable to load mime type library"),
		 errhint("Libmagic error: %s", magic_error(magic_cookie))));
		magic_close(magic_cookie);
		magic_cookie = NULL;
	}
#endif
	on_proc_exit(&bytea_exif_exit, PointerGetDatum(NULL));
}

/*
 * bytea_exif_exit: Exit callback function.
 */
static void
bytea_exif_exit(int code, Datum arg)
{
#ifdef BYTEA_MIME
	if (magic_cookie != NULL)
		magic_close(magic_cookie);
#endif
}

Datum
bytea_exif_version(PG_FUNCTION_ARGS)
{
	PG_RETURN_INT32(CODE_VERSION);
}

Datum
bytea_exif_libexif_version(PG_FUNCTION_ARGS)
{
	/* No libexif version number is availlable now */
	PG_RETURN_NULL();
	/* PG_RETURN_INT32(LIBEXIF_VERSION); */
}

Datum
bytea_has_exif(PG_FUNCTION_ARGS)
{
	bytea		   *arg = PG_GETARG_BYTEA_PP(0);
	unsigned		len = VARSIZE_ANY_EXHDR(arg);
	unsigned char  *dat = (unsigned char*)VARDATA_ANY(arg);
	ExifLoader	   *loader = NULL;
	ExifData	   *edata = NULL;

	if (len == 0) /* no data */
		PG_RETURN_BOOL(false);

	loader = exif_loader_new ();
	exif_loader_write (loader, dat, len);
	edata = exif_loader_get_data (loader);
	exif_loader_unref (loader);
	if (!edata) /* no EXIF data structure */
	{
		PG_RETURN_BOOL(false);
	}

	/* Free the EXIF data */
	exif_data_unref(edata);

	PG_RETURN_BOOL(true);
}

static const struct {
	ExifIfd ifd;
	const char *name;
} ExifIfdTable[] = {
	{EXIF_IFD_0, "0"},
	{EXIF_IFD_1, "1"},
	{EXIF_IFD_EXIF, "EXIF"},
	{EXIF_IFD_GPS, "GPS"},
	{EXIF_IFD_INTEROPERABILITY, "Interoperability"},
	{0, NULL}
};

static int
Ifd_from_name (char* ifdname)
{
	for (int i = 0; ExifIfdTable[i].name; i++)
	{
		if (!strcmp(ExifIfdTable[i].name, ifdname))
		{
			return i;
			break;
		}
	}
	return -1;
}

Datum
bytea_has_exif_ifd(PG_FUNCTION_ARGS)
{
	bytea		   *arg = PG_GETARG_BYTEA_PP(0);
	char		   *ifdname = text_to_cstring(PG_GETARG_TEXT_PP(1));
	int				ifd = -1;
	unsigned		len = VARSIZE_ANY_EXHDR(arg);
	unsigned char  *dat = (unsigned char*)VARDATA_ANY(arg);
	bool			res = false;
	ExifLoader	   *loader = NULL;
	ExifData	   *edata = NULL;
	ExifContent	   *content = NULL;

	if (len == 0) /* no data */
		PG_RETURN_BOOL(false);

	ifd = Ifd_from_name(ifdname);
	if (ifd == -1) /* invalid text of EXIF directory name */
		PG_RETURN_NULL();

	loader = exif_loader_new ();
	exif_loader_write (loader, dat, len);
	edata = exif_loader_get_data (loader);
	exif_loader_unref (loader);
	if (!edata) /* no EXIF data structure */
	{
		PG_RETURN_BOOL(false);
	}

	content = edata->ifd[ifd];
	if (!content) /* no EXIF data */
	{
		/* Free the EXIF data */
		exif_data_unref(edata);
		PG_RETURN_BOOL(false);
	}

	res = content->count > 0;

	/* Free the EXIF data */
	exif_data_unref(edata);

	PG_RETURN_BOOL(res);
}

Datum
bytea_get_exif_tag_value(PG_FUNCTION_ARGS)
{
	bytea		   *arg = PG_GETARG_BYTEA_PP(0);
	char		   *tagname = text_to_cstring(PG_GETARG_TEXT_PP(1));
	unsigned		len = VARSIZE_ANY_EXHDR(arg);
	unsigned char  *dat = (unsigned char*)VARDATA_ANY(arg);
	ExifLoader	   *loader = NULL;
	ExifData	   *edata = NULL;
	ExifContent	*content = NULL;
	ExifTag			tag = exif_tag_from_name(tagname);
	ExifEntry	   *ee = NULL;
	char			buf0[4096];

	if (len == 0)
	{
		PG_RETURN_NULL();
	}

	if (!tag) /* no data, incorrect tag */
	{
		ereport(WARNING,
			(errcode(ERRCODE_WARNING),
			 errmsg("Tag name is not correct"),
			 errhint("Please read EXIF specification and search for \"%s\"", tagname)));
		PG_RETURN_NULL();
	}

	loader = exif_loader_new ();
	exif_loader_write (loader, dat, len);
	edata = exif_loader_get_data (loader);
	exif_loader_unref (loader);
	if (edata == NULL) /* no EXIF data structure */
	{
		PG_RETURN_NULL();
	}

	for (unsigned j = 0; j < EXIF_IFD_COUNT; j++)
	{
		content = edata->ifd[j];
		if (!content) /* no EXIF data */
			continue;

		for (unsigned int i = 0; i < content->count; i++)
		{
			ExifEntry	   *ee_loop = content->entries[i];
			const char	   *tname = exif_tag_get_name_in_ifd(ee_loop->tag, j);

			if (strcmp(tname, tagname) == 0)
			{
				ee = ee_loop;
				break;
			}
		}
		if (ee != NULL)
			break;
	} /* ifd */

	if (ee == NULL) /* no such tage name */
	{
		PG_RETURN_NULL();
	}

	exif_entry_get_value(ee, buf0, sizeof(buf0));
	exif_data_free (edata);
	PG_RETURN_TEXT_P(cstring_to_text(buf0));
}

/**
 * Escapes special characters in a JSON string.
 *
 * @param json: The JSON string to be escaped.
 * @return: The escaped JSON string.
 */
char*
escapeJson(const char* json)
{
	// Calculate the length of the input JSON string.
	size_t inputLength = strlen(json);
	size_t j = 0;  // Index for the escaped JSON string.
		// Allocate memory for the escaped JSON string.
	char* escapedJson = (char*) palloc((2 * inputLength + 1) * sizeof(char));
	if (escapedJson == NULL)
	{	// Checking for unsuccessful memory allocation.
		ereport(ERROR,
		(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("out of memory"),
				 errdetail("String of %ld bytes is too long.", inputLength)));
	}

	// Iterate over each character in the input JSON string.
	for (size_t i = 0; i < inputLength; i++) {
		// Check for special characters that need to be escaped.
		switch (json[i]) {
			case '\"':
				escapedJson[j++] = '\\';
				escapedJson[j++] = '\"';
				break;
			case '\\':
				escapedJson[j++] = '\\';
				escapedJson[j++] = '\\';
				break;
			case '/':
				escapedJson[j++] = '\\';
				escapedJson[j++] = '/';
				break;
			case '\b':
				escapedJson[j++] = '\\';
				escapedJson[j++] = 'b';
				break;
			case '\f':
				escapedJson[j++] = '\\';
				escapedJson[j++] = 'f';
				break;
			case '\n':
				escapedJson[j++] = '\\';
				escapedJson[j++] = 'n';
				break;
			case '\r':
				escapedJson[j++] = '\\';
				escapedJson[j++] = 'r';
				break;
			case '\t':
				escapedJson[j++] = '\\';
				escapedJson[j++] = 't';
				break;
			default:
				escapedJson[j++] = json[i];
				break;
		}
	}

	escapedJson[j] = '\0';  // Null-terminate the escaped JSON string.

	return escapedJson;  // Returning the escaped JSON string.
}

/* Get all availlable EXIF data from all of EXIF directories of the bytea image data */
Datum
bytea_get_exif_json(PG_FUNCTION_ARGS)
{
	bytea		   *arg = PG_GETARG_BYTEA_PP(0);
	unsigned		len = VARSIZE_ANY_EXHDR(arg);
	unsigned char  *dat = (unsigned char*)VARDATA_ANY(arg);
	ExifLoader	   *loader = NULL;
	ExifData	   *edata = NULL;
	ExifContent	   *content = NULL;
	StringInfo		buf = makeStringInfo();
	bool			first_tag = true;
	char		   *json_val = NULL;

	if (len == 0) /* no data */
		PG_RETURN_NULL();

	loader = exif_loader_new ();
	exif_loader_write (loader, dat, len);
	edata = exif_loader_get_data (loader);
	exif_loader_unref (loader);
	if (!edata) /* no EXIF data structure */
	{
		PG_RETURN_NULL();
	}

	appendStringInfo(buf, "{\n");
	for (unsigned j = 0; j < EXIF_IFD_COUNT; j++)
	{
		content = edata->ifd[j];
		if (!content) /* no EXIF data */
			continue;

		for (unsigned int i = 0; i < content->count; i++)
		{
			ExifEntry * ee = content->entries[i];
			char v[2001];
			for (unsigned int k = 0; k < 2001; k++)
				v[k] = 0;

			if (first_tag)
				first_tag = false;
			else
				appendStringInfo(buf, ",\n");

			appendStringInfo(buf, " \"");
			/* no need to JSON escape tag name */
			appendStringInfo(buf, "%s", exif_tag_get_name_in_ifd(ee->tag, j));
			appendStringInfo(buf, "\" : \"");
			exif_entry_get_value (ee, v, sizeof (v));
			json_val = escapeJson(v);
			appendStringInfo(buf, "%s", json_val);
			appendStringInfo(buf, "\"");
		}
	} /* ifd */
	appendStringInfo(buf, "\n}");
	exif_data_free (edata);
	PG_RETURN_TEXT_P(cstring_to_text(buf->data));
}

static double
exif_gps_extract_double (ExifByteOrder o, ExifEntry * e)
{
	ExifRational v_rat;
	double res = 0.0;

	if (e == NULL || e->format != EXIF_FORMAT_RATIONAL)
		return NAN;

	if (e->components > 0)
	{
		v_rat = exif_get_rational (e->data + 8 * 0, o);
		res += (double) v_rat.numerator / (double) v_rat.denominator;
	}

	if (e->components > 1)
	{
		v_rat = exif_get_rational (e->data + 8 * 1, o);
		res += (double) v_rat.numerator / (double) v_rat.denominator / 60.0;
	}

	if (e->components > 2)
	{
		v_rat = exif_get_rational (e->data + 8 * 2, o);
		res += (double) v_rat.numerator / (double) v_rat.denominator / 3600.0;
	}

	return res;
}

Datum
bytea_get_exif_point(PG_FUNCTION_ARGS)
{
	bytea		   *arg = PG_GETARG_BYTEA_PP(0);
	unsigned		len = VARSIZE_ANY_EXHDR(arg);
	unsigned char  *dat = (unsigned char*)VARDATA_ANY(arg);
	ExifLoader	   *loader = NULL;
	ExifData	   *edata = NULL;
	ExifContent	   *content = NULL;
	StringInfo		buf = makeStringInfo();

	if (len == 0) /* no data */
		PG_RETURN_NULL();

	loader = exif_loader_new ();
	exif_loader_write (loader, dat, len);
	edata = exif_loader_get_data (loader);
	exif_loader_unref (loader);
	if (!edata) /* no EXIF data structure */
	{
		PG_RETURN_NULL();
	}
	content = edata->ifd[EXIF_IFD_GPS];
	if (!content) /* no EXIF data */
	{
		exif_data_free (edata);
		PG_RETURN_NULL();
	}

	{
		ExifByteOrder		o = exif_data_get_byte_order (edata);

		ExifEntry		   *e_lat = exif_content_get_entry (content, EXIF_TAG_GPS_LATITUDE);
		ExifEntry		   *e_lat_ref = exif_content_get_entry (content, EXIF_TAG_GPS_LATITUDE_REF);
		ExifEntry		   *e_lon = exif_content_get_entry (content, EXIF_TAG_GPS_LONGITUDE);
		ExifEntry		   *e_lon_ref = exif_content_get_entry (content, EXIF_TAG_GPS_LONGITUDE_REF);
		ExifEntry		   *e_geo_datum = exif_content_get_entry (content, EXIF_TAG_GPS_MAP_DATUM);

		if (e_lon == NULL || e_lat == NULL || e_lon_ref == NULL || e_lat_ref == NULL) /* no necessary geo data */
		{
			exif_data_free (edata);
			PG_RETURN_NULL();
		}
		else
		{
			double		lat = exif_gps_extract_double(o, e_lat);
			double		lon = exif_gps_extract_double(o, e_lon);
			char		lat_ref = e_lat_ref->size ? e_lat_ref->data[0] : 0;
			char		lon_ref = e_lon_ref->size ? e_lon_ref->data[0] : 0;
			const char *srid = (e_geo_datum == NULL) ? "" : (strncmp("WGS-84", (const char *)(e_geo_datum->data), e_geo_datum->size) == 0) ? "SRID=4326;" : "";

			if ( lat_ref == 'S')
				lat = lat * -1.0;
			if ( lon_ref == 'W')
				lon = lon * -1.0;
			appendStringInfo(buf, "%sPoint(%2.*f %2.*f)", srid, 14, lon, 14, lat);
		}
	}

	exif_data_free (edata);
	PG_RETURN_TEXT_P(cstring_to_text(buf->data));
}

Datum
bytea_get_exif_dest_point(PG_FUNCTION_ARGS)
{
	bytea		   *arg = PG_GETARG_BYTEA_PP(0);
	unsigned		len = VARSIZE_ANY_EXHDR(arg);
	unsigned char  *dat = (unsigned char*)VARDATA_ANY(arg);
	ExifLoader	   *loader = NULL;
	ExifData	   *edata = NULL;
	ExifContent	   *content = NULL;
	StringInfo		buf = makeStringInfo();

	if (len == 0) /* no data */
		PG_RETURN_NULL();

	loader = exif_loader_new ();
	exif_loader_write (loader, dat, len);
	edata = exif_loader_get_data (loader);
	exif_loader_unref (loader);
	if (!edata) /* no EXIF data structure */
	{
		PG_RETURN_NULL();
	}
	content = edata->ifd[EXIF_IFD_GPS];
	if (!content) /* no EXIF data */
	{
		exif_data_free (edata);
		PG_RETURN_NULL();
	}

	{
		ExifByteOrder		o = exif_data_get_byte_order (edata);

		ExifEntry		   *e_lat = exif_content_get_entry (content, EXIF_TAG_GPS_DEST_LATITUDE);
		ExifEntry		   *e_lat_ref = exif_content_get_entry (content, EXIF_TAG_GPS_DEST_LATITUDE_REF);
		ExifEntry		   *e_lon = exif_content_get_entry (content, EXIF_TAG_GPS_DEST_LONGITUDE);
		ExifEntry		   *e_lon_ref = exif_content_get_entry (content, EXIF_TAG_GPS_DEST_LONGITUDE_REF);
		ExifEntry		   *e_geo_datum = exif_content_get_entry (content, EXIF_TAG_GPS_MAP_DATUM);

		if (e_lon == NULL || e_lat == NULL || e_lon_ref == NULL || e_lat_ref == NULL) /* no necessary geo data */
		{
			exif_data_free (edata);
			PG_RETURN_NULL();
		}
		else
		{
			double		lat = exif_gps_extract_double(o, e_lat);
			double		lon = exif_gps_extract_double(o, e_lon);
			char		lat_ref = e_lat_ref->size ? e_lat_ref->data[0] : 0;
			char		lon_ref = e_lon_ref->size ? e_lon_ref->data[0] : 0;
			const char *srid = (e_geo_datum == NULL) ? "" : (strncmp("WGS-84", (const char *)(e_geo_datum->data), e_geo_datum->size) == 0) ? "SRID=4326;" : "";

			if ( lat_ref == 'S')
				lat = lat * -1.0;
			if ( lon_ref == 'W')
				lon = lon * -1.0;
			appendStringInfo(buf, "%sPoint(%2.*f %2.*f)", srid, 14, lon, 14, lat);
		}
	}

	exif_data_free (edata);
	PG_RETURN_TEXT_P(cstring_to_text(buf->data));
}

Datum
bytea_get_exif_gps_utc_timestamp(PG_FUNCTION_ARGS)
{
	bytea		   *arg = PG_GETARG_BYTEA_PP(0);
	unsigned		len = VARSIZE_ANY_EXHDR(arg);
	unsigned char  *dat = (unsigned char*)VARDATA_ANY(arg);
	ExifLoader	   *loader = NULL;
	ExifData	   *edata = NULL;
	ExifContent	   *content = NULL;
	Datum			res_tstz;

	if (len == 0) /* no data */
		PG_RETURN_NULL();

	loader = exif_loader_new ();
	exif_loader_write (loader, dat, len);
	edata = exif_loader_get_data (loader);
	exif_loader_unref (loader);
	if (!edata) /* no EXIF data structure */
	{
		PG_RETURN_NULL();
	}
	content = edata->ifd[EXIF_IFD_GPS];
	if (!content) /* no EXIF data */
	{
		exif_data_free (edata);
		PG_RETURN_NULL();
	}

	{
		ExifByteOrder		o = exif_data_get_byte_order (edata);
		ExifEntry		   *e_ts = exif_content_get_entry (content, EXIF_TAG_GPS_TIME_STAMP);
		ExifEntry		   *e_ds = exif_content_get_entry (content, EXIF_TAG_GPS_DATE_STAMP);

		if (e_ts == NULL || e_ds == NULL) /* no necessary timestamp data */
		{
			exif_data_free (edata);
			PG_RETURN_NULL();
		}
		else if (e_ds->format != EXIF_FORMAT_ASCII || e_ds->size != 11)
		{
			ereport(WARNING,
				(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
				 errmsg("Invalid GPS date EXIF format"),
				 errhint("EXIF code: %d, EXIF length: %d", e_ds->format, e_ds->size)));
			exif_data_free (edata);
			PG_RETURN_NULL();
		}
		else if (e_ts->format != EXIF_FORMAT_RATIONAL)
		{
			ereport(WARNING,
				(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
				 errmsg("Invalid GPS time EXIF format"),
				 errhint("EXIF code: %d", e_ts->format)));
			exif_data_free (edata);
			PG_RETURN_NULL();
		}
		else
		{
			int				y, m = 0, d = 0;
			char		   *date = palloc(e_ds->size);
			char		  **dptr = NULL;
			ExifRational	rh = exif_get_rational (e_ts->data, o);
			ExifRational	rm = exif_get_rational (e_ts->data + exif_format_get_size (e_ts->format),  o);
			ExifRational	rs = exif_get_rational (e_ts->data + 2 * exif_format_get_size (e_ts->format), o);
			bool			valid_time = rh.denominator & rm.denominator & rs.denominator;

			strncpy (date, (char *) e_ds->data, e_ds->size);
			y = (int) strtol((char*)(date + 0 * sizeof(char)), dptr, 10);
			m = (int) strtol((char*)(date + 5 * sizeof(char)), dptr, 10);
			d = (int) strtol((char*)(date + 8 * sizeof(char)), dptr, 10);
			pfree(date);

			if ( !valid_time )
			{
				exif_data_free (edata);
				ereport(WARNING,
					(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
					 errmsg("Invalid GPS data or GPS time EXIF format")));
				PG_RETURN_NULL();
			}
			else
			{
				double sec = (double) rs.numerator / (double) rs.denominator;
				res_tstz = DirectFunctionCall6(make_timestamp,
					Int32GetDatum(y),
					Int32GetDatum(m),
					Int32GetDatum(d),
					Int32GetDatum(rh.numerator / rh.denominator),
					Int32GetDatum(rm.numerator / rm.denominator),
					Float8GetDatum(sec));
			}
		}
	}
	exif_data_free (edata);
	PG_RETURN_DATUM(res_tstz);
}

Datum
bytea_get_exif_user_comment(PG_FUNCTION_ARGS)
{
	bytea		   *arg = PG_GETARG_BYTEA_PP(0);
	unsigned		len = VARSIZE_ANY_EXHDR(arg);
	unsigned char  *dat = (unsigned char*)VARDATA_ANY(arg);
	ExifLoader	   *loader = NULL;
	ExifData	   *edata = NULL;
	ExifContent	   *content = NULL;
	ExifEntry	   *e = NULL;
	bool			uc_ascii = false;
	bool			uc_unicode = false;
	bool			uc_jis = false;
	bool			uc_undef = false;
	bool			uc_encoding_ok = false;
	char		   *src_enc = "UTF-16"; /* very often */
	char		   *user_comment_utf8 = "";
	char		   *user_comment_pg = "";
	char		   *exif_uc = NULL;
	int				len_uc = 0;
#define UC_ENCODING_FIELD_SIZE 8

	if (len == 0) /* no data */
		PG_RETURN_NULL();

	loader = exif_loader_new ();
	exif_loader_write (loader, dat, len);
	edata = exif_loader_get_data (loader);
	exif_loader_unref (loader);
	if (!edata) /* no EXIF data structure */
	{
		PG_RETURN_NULL();
	}
	content = edata->ifd[EXIF_IFD_EXIF];
	if (!content) /* no EXIF data */
	{
		exif_data_free (edata);
		PG_RETURN_NULL();
	}

	e = exif_content_get_entry (content, EXIF_TAG_USER_COMMENT);

	if (e == NULL) /* no necessary data */
	{
		exif_data_free (edata);
		PG_RETURN_NULL();
	}

	/*
	 * The EXIF specification says UNDEFINED, but some
	 * manufacturers don't care and use ASCII.
	 */
	if (e->format != EXIF_FORMAT_UNDEFINED && e->format != EXIF_FORMAT_ASCII )
	{
		ereport(WARNING,
			(errcode(ERRCODE_WRONG_OBJECT_TYPE),
			 errmsg("Invalid user comment EXIF format"),
			 errdetail("Exif code: %d, normal is %d; bytea data length is %d bytes", e->format, EXIF_FORMAT_UNDEFINED, len)));
		exif_data_free (edata);
		PG_RETURN_NULL();
	}

	if (e->format == EXIF_FORMAT_ASCII )
	{
		ereport(WARNING,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("EXIF user comment have EXIF_ACSII format"),
				 errhint("Should be marked as EXIF undefined data; bytea data length is %d bytes", len)));
	}

	if (e->size <= UC_ENCODING_FIELD_SIZE)
	{
		ereport(WARNING,
			(errcode(ERRCODE_WRONG_OBJECT_TYPE),
			 errmsg("No EXIF user comment text data"),
			 errhint("Actual field length: %d, minimal is %d; bytea data length is %d bytes", e->size, UC_ENCODING_FIELD_SIZE, len)));
		exif_data_free (edata);
		PG_RETURN_NULL();
	}

	uc_ascii = 0 == memcmp (e->data, "ASCII\0\0\0"  , UC_ENCODING_FIELD_SIZE);
	uc_unicode = 0 == memcmp (e->data, "UNICODE\0"	, UC_ENCODING_FIELD_SIZE);
	uc_jis = 0 == memcmp (e->data, "JIS\0\0\0\0\0", UC_ENCODING_FIELD_SIZE);
	uc_undef = 0 == memcmp (e->data, "\0\0\0\0\0\0\0\0", UC_ENCODING_FIELD_SIZE);
	uc_encoding_ok = uc_ascii || uc_unicode || uc_jis || uc_undef;

	if (uc_encoding_ok == false)
	{
		ereport(WARNING,
			(errcode(ERRCODE_WRONG_OBJECT_TYPE),
			 errmsg("Invalid encoding for user comment EXIF data"),
			 errdetail("Encoding mark %.*s; bytea data length is %d bytes", UC_ENCODING_FIELD_SIZE, e->data, len)));
		exif_data_free (edata);
		PG_RETURN_NULL();
	}

	len_uc = e->size - UC_ENCODING_FIELD_SIZE;
	/*
	 * Note that, according to the specification (V2.1, p 40),
	 * the user comment field does not have to be
	 * NULL terminated.
	 */
	exif_uc = palloc(len_uc + 1);
	memcpy(exif_uc, (char *) (e->data + UC_ENCODING_FIELD_SIZE), len_uc);
	exif_uc[len_uc] = '\0';

	if (uc_ascii || uc_undef)
		user_comment_utf8 = exif_uc;
	else if (uc_unicode || uc_jis)
	{
		char *input_ptr = exif_uc;
		size_t input_bytes = len_uc;
		size_t output_bytes = input_bytes * 2; /* maximum to UTF-8*/
		char *output = palloc(output_bytes);
		char *output_ptr = output;
		size_t result = 0;
		iconv_t conv_desc = NULL;

		if (output == NULL)
		{
			ereport(WARNING,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("Iconv memory fail"),
				 errhint("bytea data length is %d bytes", len)));
			exif_data_free (edata);
			PG_RETURN_NULL();
		}

		src_enc = uc_unicode ? "UTF-16" : "JIS";

		conv_desc = iconv_open ("UTF-8", src_enc);
		if ( conv_desc == NULL )
		{
			if (errno == EINVAL)
				ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_FUNCTION),
					 errmsg("default conversion function for encoding \"%s\" to \"%s\" does not exist in iconv",
							src_enc,
							"UTF-8")));
			else
				ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_FUNCTION),
					 errmsg("Iconv initialization error")));
		}

		result = iconv(conv_desc, &input_ptr, &input_bytes, &output_ptr, &output_bytes);
		if (result == (size_t)-1)
		{
			ereport(WARNING,
				(errcode(ERRCODE_UNDEFINED_FUNCTION),
				 errmsg("Iconv fail"),
				 errhint("Encoding mark %.*s; bytea data length is %d bytes", UC_ENCODING_FIELD_SIZE, e->data, len)));
			exif_data_free (edata);
			PG_RETURN_NULL();
		}
		*output_ptr = '\0';
		user_comment_utf8 = output;
	}

	user_comment_pg = (char *) pg_do_encoding_conversion(
		(unsigned char *) user_comment_utf8,
		strlen(user_comment_utf8),
		PG_UTF8,
		GetDatabaseEncoding());
	exif_data_free (edata);
	PG_RETURN_TEXT_P(cstring_to_text(user_comment_pg));
}

Datum
bytea_get_mime_type(PG_FUNCTION_ARGS)
{
	bytea		   *arg = PG_GETARG_BYTEA_PP(0);
	unsigned		len = VARSIZE_ANY_EXHDR(arg);
	unsigned char  *dat = (unsigned char*)VARDATA_ANY(arg);
	const char	   *mime;

	if (len == 0) /* no data */
		PG_RETURN_NULL();
#ifdef BYTEA_MIME
	mime = magic_buffer(magic_cookie, dat, len);
	PG_RETURN_TEXT_P(cstring_to_text(mime));
#else
		ereport(ERROR,
			(errcode(ERRCODE_WARNING),
			 errmsg("PostgreSQL extension 'bytea_exif' was compiled without libmagick mime type library support")));
#endif
}

