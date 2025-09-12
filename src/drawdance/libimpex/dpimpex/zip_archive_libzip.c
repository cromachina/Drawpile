// SPDX-License-Identifier: GPL-3.0-or-later
#include "zip_archive.h"
#include <dpcommon/common.h>
// On some macOS systems this header spews warnings about _Nullable and
// _Nonnull despite being a system header. Just shut it up I guess.
#ifdef __clang__
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wnullability-extension"
#endif
#include <zip.h>
#ifdef __clang__
#    pragma clang diagnostic pop
#endif


#define INITIAL_READ_CAPACITY 4098

typedef struct DP_ZipReaderFile {
    size_t size;
    unsigned char buffer[];
} DP_ZipReaderFile;


static const char *code_strerror(zip_error_t *ze)
{
    if (ze) {
        const char *str = zip_error_strerror(ze);
        if (str) {
            return str;
        }
    }
    return "null error";
}

static const char *archive_strerror(zip_t *archive)
{
    return code_strerror(zip_get_error(archive));
}

static const char *file_strerror(zip_file_t *file)
{
    return code_strerror(zip_file_get_error(file));
}

static void file_close(zip_file_t *file, const char *path)
{
    int errcode = zip_fclose(file);
    if (errcode != ZIP_ER_OK) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, errcode);
        DP_warn("Error closing '%s' in zip: %s", path, code_strerror(&ze));
    }
}


DP_ZipReader *DP_zip_reader_new(const char *path)
{
    int errcode;
    zip_t *archive = zip_open(path, ZIP_RDONLY, &errcode);
    if (archive) {
        return (DP_ZipReader *)archive;
    }
    else {
        zip_error_t ze;
        zip_error_init_with_code(&ze, errcode);
        DP_error_set("Error opening '%s': %s", path, code_strerror(&ze));
        zip_error_fini(&ze);
        return NULL;
    }
}

void DP_zip_reader_free(DP_ZipReader *zr)
{
    zip_t *archive = (zip_t *)zr;
    zip_discard(archive);
}

DP_ZipReaderFile *DP_zip_reader_read_file(DP_ZipReader *zr, const char *path)
{
    zip_t *archive = (zip_t *)zr;
    zip_file_t *file = zip_fopen(archive, path, 0);
    if (!file) {
        DP_error_set("Error opening file '%s' in zip: %s", path,
                     archive_strerror(archive));
        return NULL;
    }

    size_t capacity = INITIAL_READ_CAPACITY;
    size_t used = 0;
    DP_ZipReaderFile *zrf =
        DP_malloc(DP_FLEX_SIZEOF(DP_ZipReaderFile, buffer, capacity));
    while (true) {
        size_t expect = capacity - used;
        zip_int64_t result =
            zip_fread(file, zrf->buffer + used, (zip_uint64_t)expect);
        if (result < 0) {
            DP_error_set("Error reading from file '%s' in zip: %s", path,
                         file_strerror(file));
            DP_free(zrf);
            file_close(file, path);
            return NULL;
        }
        else {
            size_t read = (size_t)result;
            used += read;
            if (read < expect) {
                zrf->size = used;
                file_close(file, path);
                return zrf;
            }
            else {
                capacity *= 2;
                zrf = DP_realloc(
                    zrf, DP_FLEX_SIZEOF(DP_ZipReaderFile, buffer, capacity));
            }
        }
    }
}


size_t DP_zip_reader_file_size(DP_ZipReaderFile *zrf)
{
    return zrf->size;
}

void *DP_zip_reader_file_content(DP_ZipReaderFile *zrf)
{
    return zrf->buffer;
}

void DP_zip_reader_file_free(DP_ZipReaderFile *zrf)
{
    DP_free(zrf);
}


DP_ZipWriter *DP_zip_writer_new(const char *path)
{
    int errcode;
    zip_t *archive = zip_open(path, ZIP_CREATE | ZIP_TRUNCATE, &errcode);
    if (archive) {
        return (DP_ZipWriter *)archive;
    }
    else {
        zip_error_t ze;
        zip_error_init_with_code(&ze, errcode);
        DP_error_set("Error opening '%s': %s", path, code_strerror(&ze));
        zip_error_fini(&ze);
        return NULL;
    }
}

void DP_zip_writer_free_abort(DP_ZipWriter *zw)
{
    zip_t *archive = (zip_t *)zw;
    if (archive) {
        zip_discard(archive);
    }
}

bool DP_zip_writer_free_finish(DP_ZipWriter *zw)
{
    zip_t *archive = (zip_t *)zw;
    if (zip_close(archive) == 0) {
        return true;
    }
    else {
        DP_error_set("Error closing zip archive: %s",
                     archive_strerror(archive));
        zip_discard(archive);
        return false;
    }
}

bool DP_zip_writer_add_dir(DP_ZipWriter *zw, const char *path)
{
    zip_t *archive = (zip_t *)zw;
    if (zip_dir_add(archive, path, ZIP_FL_ENC_UTF_8) != -1) {
        return true;
    }
    else {
        DP_error_set("Error creating directory '%s': %s", path,
                     archive_strerror(archive));
        return false;
    }
}


bool DP_zip_writer_add_file(DP_ZipWriter *zw, const char *path,
                            const void *buffer, size_t size, bool deflate,
                            bool take_buffer)
{
    zip_t *archive = (zip_t *)zw;
    zip_source_t *source =
        zip_source_buffer(archive, buffer, size, take_buffer);
    if (!source) {
        DP_error_set("Error creating zip source for '%s': %s", path,
                     archive_strerror(archive));
        if (take_buffer) {
            DP_free((void *)buffer);
        }
        return false;
    }

    zip_int64_t index = zip_file_add(archive, path, source, ZIP_FL_ENC_UTF_8);
    if (index < 0) {
        DP_error_set("Error storing '%s' in zip: %s", path,
                     archive_strerror(archive));
        zip_source_free(source);
        return false;
    }

    zip_uint64_t uindex = (zip_uint64_t)index;
    zip_int32_t compression = deflate ? ZIP_CM_DEFLATE : ZIP_CM_STORE;
    if (zip_set_file_compression(archive, uindex, compression, 0) != 0) {
        DP_error_set("Error setting compression for '%s': %s", path,
                     archive_strerror(archive));
        return false;
    }

    return true;
}
