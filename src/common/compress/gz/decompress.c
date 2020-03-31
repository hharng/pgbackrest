/***********************************************************************************************************************************
Gz Decompress
***********************************************************************************************************************************/
#include "build.auto.h"

#include <stdio.h>
#include <zlib.h>

#include "common/compress/gz/common.h"
#include "common/compress/gz/decompress.h"
#include "common/debug.h"
#include "common/io/filter/filter.intern.h"
#include "common/log.h"
#include "common/memContext.h"
#include "common/type/object.h"

/***********************************************************************************************************************************
Filter type constant
***********************************************************************************************************************************/
STRING_EXTERN(GZ_DECOMPRESS_FILTER_TYPE_STR,                        GZ_DECOMPRESS_FILTER_TYPE);

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
#define GZ_DECOMPRESS_TYPE                                          GzDecompress
#define GZ_DECOMPRESS_PREFIX                                        gzDecompress

typedef struct GzDecompress
{
    MemContext *memContext;                                         // Context to store data
    z_stream stream;                                                // Decompression stream state

    int result;                                                     // Result of last operation
    bool inputSame;                                                 // Is the same input required on the next process call?
    bool done;                                                      // Is decompression done?
} GzDecompress;

/***********************************************************************************************************************************
Macros for function logging
***********************************************************************************************************************************/
static String *
gzDecompressToLog(const GzDecompress *this)
{
    return strNewFmt(
        "{inputSame: %s, done: %s, availIn: %u}", cvtBoolToConstZ(this->inputSame), cvtBoolToConstZ(this->done),
        this->stream.avail_in);
}

#define FUNCTION_LOG_GZ_DECOMPRESS_TYPE                                                                                            \
    GzDecompress *
#define FUNCTION_LOG_GZ_DECOMPRESS_FORMAT(value, buffer, bufferSize)                                                               \
    FUNCTION_LOG_STRING_OBJECT_FORMAT(value, gzDecompressToLog, buffer, bufferSize)

/***********************************************************************************************************************************
Free inflate stream
***********************************************************************************************************************************/
OBJECT_DEFINE_FREE_RESOURCE_BEGIN(GZ_DECOMPRESS, LOG, logLevelTrace)
{
    inflateEnd(&this->stream);
}
OBJECT_DEFINE_FREE_RESOURCE_END(LOG);

/***********************************************************************************************************************************
Decompress data
***********************************************************************************************************************************/
static void
gzDecompressProcess(THIS_VOID, const Buffer *compressed, Buffer *uncompressed)
{
    THIS(GzDecompress);

    FUNCTION_LOG_BEGIN(logLevelTrace);
        FUNCTION_LOG_PARAM(GZ_DECOMPRESS, this);
        FUNCTION_LOG_PARAM(BUFFER, compressed);
        FUNCTION_LOG_PARAM(BUFFER, uncompressed);
    FUNCTION_LOG_END();

    ASSERT(this != NULL);
    ASSERT(uncompressed != NULL);

    // There should never be a flush because in a valid compressed stream the end of data can be determined and done will be set.
    // If a flush is received it means the compressed stream terminated early, e.g. a zero-length or truncated file.
    if (compressed == NULL)
        THROW(FormatError, "unexpected eof in compressed data");

    if (!this->inputSame)
    {
        this->stream.avail_in = (unsigned int)bufUsed(compressed);
        this->stream.next_in = bufPtr(compressed);
    }

    this->stream.avail_out = (unsigned int)bufRemains(uncompressed);
    this->stream.next_out = bufPtr(uncompressed) + bufUsed(uncompressed);

    this->result = gzError(inflate(&this->stream, Z_NO_FLUSH));

    // Set buffer used space
    bufUsedSet(uncompressed, bufSize(uncompressed) - (size_t)this->stream.avail_out);

    // Is decompression done?
    this->done = this->result == Z_STREAM_END;

    // Is the same input expected on the next call?
    this->inputSame = this->done ? false : this->stream.avail_in != 0;

    FUNCTION_LOG_RETURN_VOID();
}

/***********************************************************************************************************************************
Is decompress done?
***********************************************************************************************************************************/
static bool
gzDecompressDone(const THIS_VOID)
{
    THIS(const GzDecompress);

    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(GZ_DECOMPRESS, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->done);
}

/***********************************************************************************************************************************
Is the same input required on the next process call?
***********************************************************************************************************************************/
static bool
gzDecompressInputSame(const THIS_VOID)
{
    THIS(const GzDecompress);

    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(GZ_DECOMPRESS, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->inputSame);
}

/***********************************************************************************************************************************
New object
***********************************************************************************************************************************/
IoFilter *
gzDecompressNew(void)
{
    FUNCTION_LOG_VOID(logLevelTrace);

    IoFilter *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("GzDecompress")
    {
        // Allocate state and set context
        GzDecompress *driver = memNew(sizeof(GzDecompress));

        *driver = (GzDecompress)
        {
            .memContext = MEM_CONTEXT_NEW(),
            .stream = {.zalloc = NULL},
        };

        // Create gz stream
        gzError(driver->result = inflateInit2(&driver->stream, WANT_GZ | WINDOW_BITS));

        // Set free callback to ensure gz context is freed
        memContextCallbackSet(driver->memContext, gzDecompressFreeResource, driver);

        // Create filter interface
        this = ioFilterNewP(
            GZ_DECOMPRESS_FILTER_TYPE_STR, driver, NULL, .done = gzDecompressDone, .inOut = gzDecompressProcess,
            .inputSame = gzDecompressInputSame);
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_LOG_RETURN(IO_FILTER, this);
}
