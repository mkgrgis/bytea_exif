/* contrib/bytea_exif/bytea_exif--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION bytea_exif" to load this file. \quit

CREATE OR REPLACE FUNCTION bytea_exif_version()
  RETURNS int STRICT
  AS 'MODULE_PATHNAME' LANGUAGE C;

COMMENT ON FUNCTION bytea_exif_version
IS 'Returns version of bytea EXIF data processing extension';

CREATE OR REPLACE FUNCTION bytea_exif_libexif_version()
  RETURNS int STRICT
  AS 'MODULE_PATHNAME' LANGUAGE C;

COMMENT ON FUNCTION bytea_exif_libexif_version
IS 'Returns version of libexif used by PostgreSQL extension';

CREATE OR REPLACE FUNCTION bytea_get_mime_type(data bytea)
  RETURNS text STRICT
  AS 'MODULE_PATHNAME' LANGUAGE C;

COMMENT ON FUNCTION bytea_get_mime_type
IS 'Returns mime type of bytea data';

CREATE OR REPLACE FUNCTION bytea_has_exif(data bytea)
  RETURNS bool STRICT
  AS 'MODULE_PATHNAME' LANGUAGE C;

COMMENT ON FUNCTION bytea_has_exif
IS 'Returns true if there is EXIF data container. Warning: the container can be decalred without any useful data';


CREATE OR REPLACE FUNCTION bytea_has_exif_ifd(data bytea, ifd text)
  RETURNS bool STRICT
  AS 'MODULE_PATHNAME' LANGUAGE C;

COMMENT ON FUNCTION bytea_has_exif_ifd
IS 'Returns true if there is any EXIF data of pointed EXIF directory in the bytea value. Ifd values: ''0'', ''1'', ''EXIF'', ''GPS'', ''Interoperability''';

CREATE OR REPLACE FUNCTION bytea_get_exif_tag_value(data bytea, tag text)
  RETURNS text STRICT
  AS 'MODULE_PATHNAME' LANGUAGE C;

COMMENT ON FUNCTION bytea_get_exif_tag_value
IS 'Returns value of a EXIF tag if there is such data';

CREATE OR REPLACE FUNCTION bytea_get_exif_json(data bytea)
  RETURNS json STRICT
  AS 'MODULE_PATHNAME' LANGUAGE C;

COMMENT ON FUNCTION bytea_get_exif_json
IS 'Returns JSON data which contains full set of presented in bytea EXIF tags and it''s values';

CREATE OR REPLACE FUNCTION bytea_get_exif_point(data bytea)
  RETURNS text STRICT
  AS 'MODULE_PATHNAME' LANGUAGE C;

COMMENT ON FUNCTION bytea_get_exif_point
IS 'Returns text OGC ST_Point value of a photographer location';

CREATE OR REPLACE FUNCTION bytea_get_dest_exif_point(data bytea)
  RETURNS text STRICT
  AS 'MODULE_PATHNAME' LANGUAGE C;

COMMENT ON FUNCTION bytea_get_dest_exif_point
IS 'Returns text OGC ST_Point value of a main photo object location';
