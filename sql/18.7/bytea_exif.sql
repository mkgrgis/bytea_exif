--SET log_min_messages TO DEBUG1;
--SET client_min_messages TO DEBUG1;
--Testcase 001:
CREATE EXTENSION bytea_exif;
--Testcase 002:
SELECT bytea_exif_version() v;
--Testcase 003:
SELECT bytea_exif_libexif_version() v;

--Testcase 004:
CREATE TABLE img (id int2 not null, img bytea);
--Testcase 005:
\copy img from './sql/test_images.data';
--Testcase 006:
set timezone to 'UTC';

--Testcase 010:
SELECT id, bytea_has_exif(img) exif, bytea_has_exif(img) IS NULL n, length(img) l FROM img;
--Testcase 011:
SELECT id, bytea_has_exif_ifd(img, '0') exif, bytea_has_exif_ifd(img, '0') IS NULL n, length(img) l FROM img;
--Testcase 012:
SELECT id, bytea_has_exif_ifd(img, '1') exif, bytea_has_exif_ifd(img, '1') IS NULL n, length(img) l FROM img;
--Testcase 013:
SELECT id, bytea_has_exif_ifd(img, 'EXIF') exif, bytea_has_exif_ifd(img, 'EXIF') IS NULL n, length(img) l FROM img;
--Testcase 014:
SELECT id, bytea_has_exif_ifd(img, 'GPS') exif, bytea_has_exif_ifd(img, 'GPS') IS NULL n, length(img) l FROM img;
--Testcase 015:
SELECT id, bytea_has_exif_ifd(img, 'Interoperability') exif, bytea_has_exif_ifd(img, 'Interoperability') IS NULL n, length(img) l FROM img;
--Testcase 016:
SELECT id, bytea_has_exif_ifd(img, 'foo') exif, bytea_has_exif_ifd(img, 'foo') IS NULL n, length(img) l FROM img;

--Testcase 017:
SELECT id,
       bytea_get_exif_json(img) IS NULL "null",
       bytea_get_exif_json(img) exif
FROM img;

--Testcase 018:
SELECT id,
       bytea_get_exif_tag_value(img, 'GPSLatitude') lat,
       bytea_get_exif_tag_value(img, 'GPSLongitude') lon
FROM img;
--Testcase 019:
SELECT id,
       bytea_get_exif_json(img) ->> 'GPSLatitude' lat,
       bytea_get_exif_json(img) ->> 'GPSLongitude' lon
FROM img;

--Testcase 020:
SELECT id,
       bytea_get_exif_tag_value(img, 'Make') make,
       bytea_get_exif_tag_value(img, 'ResolutionUnit') ru,
       bytea_get_exif_tag_value(img, 'Saturation') st,
       bytea_get_exif_tag_value(img, 'ExposureProgram') ep
FROM img;

--Testcase 021:
SELECT id, bytea_get_exif_point(img) exif, bytea_get_exif_point(img) IS NULL n FROM img;
--Testcase 022:
SELECT id, bytea_get_exif_dest_point(img) exif, bytea_get_exif_dest_point(img) IS NULL n FROM img;

--Testcase 023:
SELECT id, bytea_get_mime_type(img) mime, bytea_get_mime_type(img) IS NULL n FROM img;

--Testcase 024:
SELECT id,
		to_char (bytea_get_exif_gps_utc_timestamp(img), 'YYYY-MM-DD HH24:MI:SS TZ') ts,
		bytea_get_exif_gps_utc_timestamp(img) IS NULL n
FROM img;

--Testcase 025:
SELECT id,
       to_timestamp((bytea_get_exif_json(img) ->> 'DateTimeOriginal') || '+00', 'YYYY:MM:DD HH24:MI:SS')::timestamp without time zone at time zone 'Etc/UTC' ts
FROM img;

--Testcase 026:
set timezone to 'Australia/Canberra';
--Testcase 027:
SELECT id,
       to_timestamp((bytea_get_exif_json(img) ->> 'DateTimeOriginal') || '+00', 'YYYY:MM:DD HH24:MI:SS')::timestamp without time zone at time zone 'Etc/UTC' ts
FROM img;

--Testcase 028:
SELECT id,
		to_char (bytea_get_exif_gps_local_timestamp(img), 'YYYY-MM-DD HH24:MI:SS TZ') ts,
		bytea_get_exif_gps_local_timestamp(img) IS NULL n
FROM img;
--Testcase 029:
set timezone to 'UTC';
--Testcase 030:
SELECT id,
		to_char (bytea_get_exif_gps_local_timestamp(img), 'YYYY-MM-DD HH24:MI:SS TZ') ts,
		bytea_get_exif_gps_local_timestamp(img) IS NULL n
FROM img;


--Testcase 031:
SELECT id,
		replace(bytea_get_exif_user_comment(img), '- ', '
') uc,
		bytea_get_exif_user_comment(img) IS NULL n
FROM img;

--Testcase 040:
DROP EXTENSION bytea_exif CASCADE;
