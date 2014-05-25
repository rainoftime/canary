/******************************************************************************
 * $Id: torrent.h 7365 2008-12-13 22:52:13Z charles $
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

#ifndef TR_TORRENT_H
#define TR_TORRENT_H 1

struct tr_bandwidth;
struct tr_ratecontrol;

/**
***  Package-visible ctor API
**/

void        tr_ctorSetSave( tr_ctor * ctor,
                            int       saveMetadataInOurTorrentsDir );

int         tr_ctorGetSave( const tr_ctor * ctor );

/**
***
**/

/* just like tr_torrentSetFileDLs but doesn't trigger a fastresume save */
void        tr_torrentInitFileDLs( tr_torrent *      tor,
                                   tr_file_index_t * files,
                                   tr_file_index_t   fileCount,
                                   int               do_download );

int         tr_torrentIsPrivate( const tr_torrent * );

void        tr_torrentRecheckCompleteness( tr_torrent * );

void        tr_torrentResetTransferStats( tr_torrent * );

void        tr_torrentSetHasPiece( tr_torrent *     tor,
                                   tr_piece_index_t pieceIndex,
                                   int              has );

void        tr_torrentLock( const tr_torrent * session );

void        tr_torrentUnlock( const tr_torrent * session );

int         tr_torrentIsSeed( const tr_torrent * session );

void        tr_torrentChangeMyPort( tr_torrent * session );

int         tr_torrentExists( const tr_session * session,
                              const uint8_t    * hash );

tr_torrent* tr_torrentFindFromId( tr_session * session,
                                  int          id );

tr_torrent* tr_torrentFindFromHash( tr_session *    session,
                                    const uint8_t * hash );

tr_torrent* tr_torrentFindFromHashString( tr_session * session,
                                          const char * hashString );

tr_torrent* tr_torrentFindFromObfuscatedHash( tr_session    * session,
                                              const uint8_t * hash );

int         tr_torrentAllowsPex( const tr_torrent * );

tr_bool     tr_torrentIsPieceTransferAllowed( const tr_torrent * torrent,
                                              tr_direction       direction );

/* get the index of this piece's first block */
#define tr_torPieceFirstBlock( tor, piece ) ( ( piece ) *\
                                             ( tor )->blockCountInPiece )

/* what piece index is this block in? */
#define tr_torBlockPiece( tor, block ) ( ( block ) /\
                                        ( tor )->blockCountInPiece )

/* how many blocks are in this piece? */
#define tr_torPieceCountBlocks( tor, piece ) \
    ( ( ( piece ) ==\
       ( ( tor )->info.pieceCount - \
        1 ) ) ? ( tor )->blockCountInLastPiece : ( tor )->blockCountInPiece )

/* how many bytes are in this piece? */
#define tr_torPieceCountBytes( tor, piece ) \
    ( ( ( piece ) ==\
       ( ( tor )->info.pieceCount - \
        1 ) ) ? ( tor )->lastPieceSize : ( tor )->info.pieceSize )

/* how many bytes are in this block? */
#define tr_torBlockCountBytes( tor, block ) \
    ( ( ( block ) ==\
       ( ( tor )->blockCount - \
        1 ) ) ? ( tor )->lastBlockSize : ( tor )->blockSize )

#define tr_block( a, b ) _tr_block( tor, a, b )
tr_block_index_t _tr_block( const tr_torrent * tor,
                            tr_piece_index_t   index,
                            uint32_t           offset );

int              tr_torrentReqIsValid( const tr_torrent * tor,
                                       tr_piece_index_t   index,
                                       uint32_t           offset,
                                       uint32_t           length );

uint64_t         tr_pieceOffset( const tr_torrent * tor,
                                 tr_piece_index_t   index,
                                 uint32_t           offset,
                                 uint32_t           length );

void             tr_torrentInitFilePriority( tr_torrent       * tor,
                                             tr_file_index_t    fileIndex,
                                             tr_priority_t      priority );


int              tr_torrentCountUncheckedPieces( const tr_torrent * );

int              tr_torrentIsPieceChecked( const tr_torrent  * tor,
                                           tr_piece_index_t    piece );

int              tr_torrentIsFileChecked( const tr_torrent  * tor,
                                          tr_file_index_t     file );

void             tr_torrentSetPieceChecked( tr_torrent       * tor,
                                            tr_piece_index_t   piece,
                                            int                isChecked );

void             tr_torrentSetFileChecked( tr_torrent       * tor,
                                           tr_file_index_t    file,
                                           int                isChecked );

void             tr_torrentUncheck( tr_torrent * tor );

int              tr_torrentPromoteTracker( tr_torrent   * tor,
                                           int            trackerIndex );

time_t*          tr_torrentGetMTimes( const tr_torrent  * tor,
                                      size_t            * setmeCount );

typedef enum
{
    TR_VERIFY_NONE,
    TR_VERIFY_WAIT,
    TR_VERIFY_NOW
}
tr_verify_state;

struct tr_torrent
{
    tr_session *             session;
    tr_info                  info;

    tr_speedlimit            speedLimitMode[2];

    struct tr_ratecontrol *  swarmSpeed;

    int                      error;
    char                     errorString[128];

    uint8_t                  obfuscatedHash[SHA_DIGEST_LENGTH];

    /* If the initiator of the connection receives a handshake in which the
     * peer_id does not match the expected peerid, then the initiator is
     * expected to drop the connection. Note that the initiator presumably
     * received the peer information from the tracker, which includes the
     * peer_id that was registered by the peer. The peer_id from the tracker
     * and in the handshake are expected to match.
     */
    uint8_t * peer_id;

    /* Where to download */
    char * downloadDir;

    /* How many bytes we ask for per request */
    uint32_t                   blockSize;
    tr_block_index_t           blockCount;

    uint32_t                   lastBlockSize;
    uint32_t                   lastPieceSize;

    uint32_t                   blockCountInPiece;
    uint32_t                   blockCountInLastPiece;

    struct tr_completion *     completion;

    struct tr_bitfield *       checkedPieces;
    tr_completeness            completeness;

    struct tr_tracker *        tracker;
    struct tr_publisher_tag *  trackerSubscription;

    uint64_t                   downloadedCur;
    uint64_t                   downloadedPrev;
    uint64_t                   uploadedCur;
    uint64_t                   uploadedPrev;
    uint64_t                   corruptCur;
    uint64_t                   corruptPrev;

    time_t                     addedDate;
    time_t                     activityDate;
    time_t                     doneDate;
    time_t                     startDate;

    tr_torrent_completeness_func *   completeness_func;
    void *                     completeness_func_user_data;

    tr_bool                    isRunning;
    tr_bool                    isDeleting;

    uint16_t                   maxConnectedPeers;

    tr_verify_state            verifyState;

    time_t                     lastStatTime;
    tr_stat                    stats;

    tr_torrent *               next;

    int                        uniqueId;

    struct tr_bandwidth      * bandwidth;
};

#endif
