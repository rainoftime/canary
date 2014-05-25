/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id: inout.c 7350 2008-12-11 00:39:45Z charles $
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h> /* realloc */
#include <string.h> /* memcmp */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/sha.h>

#include "transmission.h"
#include "crypto.h"
#include "fdlimit.h"
#include "inout.h"
#include "platform.h"
#include "stats.h"
#include "torrent.h"
#include "utils.h"

/****
*****  Low-level IO functions
****/

#ifdef WIN32
 #if defined(read)
  #undef read
 #endif
 #define read  _read
 
 #if defined(write)
  #undef write
 #endif
 #define write _write
#endif

enum { TR_IO_READ, TR_IO_WRITE };

static int64_t
tr_lseek( int fd, int64_t offset, int whence )
{
#if defined(_LARGEFILE_SOURCE)
    return lseek64( fd, (off64_t)offset, whence );
#elif defined(WIN32)
    return _lseeki64( fd, offset, whence );
#else
    return lseek( fd, (off_t)offset, whence );
#endif
}

/* returns 0 on success, or an errno on failure */
static int
readOrWriteBytes( const tr_torrent * tor,
                  int                ioMode,
                  tr_file_index_t    fileIndex,
                  uint64_t           fileOffset,
                  void *             buf,
                  size_t             buflen )
{
    const tr_info * info = &tor->info;
    const tr_file * file = &info->files[fileIndex];

    typedef size_t ( *iofunc )( int, void *, size_t );
    iofunc          func = ioMode ==
                           TR_IO_READ ? (iofunc)read : (iofunc)write;
    char          * path;
    struct stat     sb;
    int             fd = -1;
    int             err;
    int             fileExists;

    assert( fileIndex < info->fileCount );
    assert( !file->length || ( fileOffset < file->length ) );
    assert( fileOffset + buflen <= file->length );

    path = tr_buildPath( tor->downloadDir, file->name, NULL );
    fileExists = !stat( path, &sb );
    tr_free( path );

    if( !file->length )
        return 0;

    if( ( ioMode == TR_IO_READ ) && !fileExists ) /* does file exist? */
        err = errno;
    else if( ( fd = tr_fdFileCheckout ( tor->downloadDir, file->name, ioMode == TR_IO_WRITE, !file->dnd, file->length ) ) < 0 )
        err = errno;
    else if( tr_lseek( fd, (int64_t)fileOffset, SEEK_SET ) == -1 )
        err = errno;
    else if( func( fd, buf, buflen ) != buflen )
        err = errno;
    else
        err = 0;

    if( ( !err ) && ( !fileExists ) && ( ioMode == TR_IO_WRITE ) )
        tr_statsFileCreated( tor->session );

    if( fd >= 0 )
        tr_fdFileReturn( fd );

    return err;
}

static int
compareOffsetToFile( const void * a,
                     const void * b )
{
    const uint64_t  offset = *(const uint64_t*)a;
    const tr_file * file = b;

    if( offset < file->offset ) return -1;
    if( offset >= file->offset + file->length ) return 1;
    return 0;
}

void
tr_ioFindFileLocation( const tr_torrent * tor,
                       tr_piece_index_t   pieceIndex,
                       uint32_t           pieceOffset,
                       tr_file_index_t  * fileIndex,
                       uint64_t         * fileOffset )
{
    const uint64_t  offset = tr_pieceOffset( tor, pieceIndex, pieceOffset, 0 );
    const tr_file * file;

    file = bsearch( &offset,
                    tor->info.files, tor->info.fileCount, sizeof( tr_file ),
                    compareOffsetToFile );

    *fileIndex = file - tor->info.files;
    *fileOffset = offset - file->offset;

    assert( *fileIndex < tor->info.fileCount );
    assert( *fileOffset < file->length );
    assert( tor->info.files[*fileIndex].offset + *fileOffset == offset );
}

/* returns 0 on success, or an errno on failure */
static int
readOrWritePiece( const tr_torrent * tor,
                  int                ioMode,
                  tr_piece_index_t   pieceIndex,
                  uint32_t           pieceOffset,
                  uint8_t *          buf,
                  size_t             buflen )
{
    int             err = 0;
    tr_file_index_t fileIndex;
    uint64_t        fileOffset;
    const tr_info * info = &tor->info;

    if( pieceIndex >= tor->info.pieceCount )
        return EINVAL;
    if( pieceOffset + buflen > tr_torPieceCountBytes( tor, pieceIndex ) )
        return EINVAL;

    tr_ioFindFileLocation( tor, pieceIndex, pieceOffset,
                           &fileIndex, &fileOffset );

    while( buflen && !err )
    {
        const tr_file * file = &info->files[fileIndex];
        const uint64_t  bytesThisPass = MIN( buflen,
                                             file->length - fileOffset );

        err = readOrWriteBytes( tor, ioMode,
                                fileIndex, fileOffset, buf, bytesThisPass );
        buf += bytesThisPass;
        buflen -= bytesThisPass;
        ++fileIndex;
        fileOffset = 0;
    }

    return err;
}

int
tr_ioRead( const tr_torrent * tor,
           tr_piece_index_t   pieceIndex,
           uint32_t           begin,
           uint32_t           len,
           uint8_t *          buf )
{
    return readOrWritePiece( tor, TR_IO_READ, pieceIndex, begin, buf, len );
}

int
tr_ioWrite( const tr_torrent * tor,
            tr_piece_index_t   pieceIndex,
            uint32_t           begin,
            uint32_t           len,
            const uint8_t *    buf )
{
    return readOrWritePiece( tor, TR_IO_WRITE, pieceIndex, begin,
                             (uint8_t*)buf,
                             len );
}

/****
*****
****/

static int
recalculateHash( const tr_torrent * tor,
                 tr_piece_index_t   pieceIndex,
                 uint8_t *          setme )
{
    size_t   bytesLeft;
    uint32_t offset = 0;
    int      success = TRUE;
    SHA_CTX  sha;

    assert( tor );
    assert( setme );
    assert( pieceIndex < tor->info.pieceCount );

    SHA1_Init( &sha );
    bytesLeft = tr_torPieceCountBytes( tor, pieceIndex );

    while( bytesLeft )
    {
        uint8_t   buf[8192];
        const int len = MIN( bytesLeft, sizeof( buf ) );
        success = !tr_ioRead( tor, pieceIndex, offset, len, buf );
        if( !success )
            break;
        SHA1_Update( &sha, buf, len );
        offset += len;
        bytesLeft -= len;
    }

    if( success )
        SHA1_Final( setme, &sha );

    return success;
}

int
tr_ioTestPiece( const tr_torrent * tor,
                int                pieceIndex )
{
    uint8_t hash[SHA_DIGEST_LENGTH];
    const int recalculated = recalculateHash( tor, pieceIndex, hash );
    return recalculated && !memcmp( hash, tor->info.pieces[pieceIndex].hash, SHA_DIGEST_LENGTH );
}

