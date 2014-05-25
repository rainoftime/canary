/******************************************************************************
 * $Id: inout.h 7175 2008-11-29 16:32:10Z charles $
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_IO_H
#define TR_IO_H 1

struct tr_torrent;

/**
 * Reads the block specified by the piece index, offset, and length.
 * @return 0 on success, or an errno value on failure.
 */
int tr_ioRead( const struct tr_torrent * tor,
               tr_piece_index_t          pieceIndex,
               uint32_t                  offset,
               uint32_t                  len,
               uint8_t *                 setme );

/**
 * Writes the block specified by the piece index, offset, and length.
 * @return 0 on success, or an errno value on failure.
 */
int tr_ioWrite( const struct tr_torrent * tor,
                tr_piece_index_t          pieceIndex,
                uint32_t                  offset,
                uint32_t                  len,
                const uint8_t *           writeme );

/**
 * returns nonzero if the piece matches its metainfo's SHA1 checksum.
 */
int tr_ioTestPiece( const tr_torrent*,
                    int   piece );


/**
 * Converts a piece index + offset into a file index + offset.
 */
void     tr_ioFindFileLocation( const tr_torrent * tor,
                                tr_piece_index_t   pieceIndex,
                                uint32_t           pieceOffset,
                                tr_file_index_t *  fileIndex,
                                uint64_t *         fileOffset );


#endif
