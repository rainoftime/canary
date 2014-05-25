/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id: peer-mgr.c 7487 2008-12-24 02:50:09Z charles $
 */

#include <assert.h>
#include <errno.h>
#include <string.h> /* memcpy, memcmp, strstr */
#include <stdlib.h> /* qsort */
#include <limits.h> /* INT_MAX */

#include "libevent/event.h"

#include "transmission.h"
#include "bandwidth.h"
#include "bencode.h"
#include "blocklist.h"
#include "clients.h"
#include "completion.h"
#include "crypto.h"
#include "handshake.h"
#include "inout.h" /* tr_ioTestPiece */
#include "net.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "peer-mgr-private.h"
#include "peer-msgs.h"
#include "ptrarray.h"
#include "stats.h" /* tr_statsAddUploaded, tr_statsAddDownloaded */
#include "torrent.h"
#include "trevent.h"
#include "utils.h"
#include "webseed.h"

enum
{
    /* how frequently to change which peers are choked */
    RECHOKE_PERIOD_MSEC = ( 10 * 1000 ),

    /* minimum interval for refilling peers' request lists */
    REFILL_PERIOD_MSEC = 333,

    /* when many peers are available, keep idle ones this long */
    MIN_UPLOAD_IDLE_SECS = ( 30 ),

    /* when few peers are available, keep idle ones this long */
    MAX_UPLOAD_IDLE_SECS = ( 60 * 5 ),

    /* how frequently to decide which peers live and die */
    RECONNECT_PERIOD_MSEC = ( 2 * 1000 ),
   
    /* how frequently to reallocate bandwidth */
    BANDWIDTH_PERIOD_MSEC = 500,

    /* max # of peers to ask fer per torrent per reconnect pulse */
    MAX_RECONNECTIONS_PER_PULSE = 4,

    /* max number of peers to ask for per second overall.
    * this throttle is to avoid overloading the router */
    MAX_CONNECTIONS_PER_SECOND = 8,

    /* number of unchoked peers per torrent.
     * FIXME: this probably ought to be configurable */
    MAX_UNCHOKED_PEERS = 14,

    /* number of bad pieces a peer is allowed to send before we ban them */
    MAX_BAD_PIECES_PER_PEER = 5,

    /* use for bitwise operations w/peer_atom.myflags */
    MYFLAG_BANNED = 1,

    /* unreachable for now... but not banned.
     * if they try to connect to us it's okay */
    MYFLAG_UNREACHABLE = 2,

    /* the minimum we'll wait before attempting to reconnect to a peer */
    MINIMUM_RECONNECT_INTERVAL_SECS = 5
};


/**
***
**/

enum
{
    UPLOAD_ONLY_UKNOWN,
    UPLOAD_ONLY_YES,
    UPLOAD_ONLY_NO
};

/**
 * Peer information that should be kept even before we've connected and
 * after we've disconnected.  These are kept in a pool of peer_atoms to decide
 * which ones would make good candidates for connecting to, and to watch out
 * for banned peers.
 *
 * @see tr_peer
 * @see tr_peermsgs
 */
struct peer_atom
{
    uint8_t     from;
    uint8_t     flags;       /* these match the added_f flags */
    uint8_t     myflags;     /* flags that aren't defined in added_f */
    uint8_t     uploadOnly;  /* UPLOAD_ONLY_ */
    tr_port     port;
    uint16_t    numFails;
    tr_address  addr;
    time_t      time;        /* when the peer's connection status last changed */
    time_t      piece_data_time;
};

typedef struct
{
    tr_bool         isRunning;

    uint8_t         hash[SHA_DIGEST_LENGTH];
    int         *   pendingRequestCount;
    tr_ptrArray *   outgoingHandshakes; /* tr_handshake */
    tr_ptrArray *   pool; /* struct peer_atom */
    tr_ptrArray *   peers; /* tr_peer */
    tr_ptrArray *   webseeds; /* tr_webseed */
    tr_timer *      reconnectTimer;
    tr_timer *      rechokeTimer;
    tr_timer *      refillTimer;
    tr_torrent *    tor;
    tr_peer *       optimistic; /* the optimistic peer, or NULL if none */

    struct tr_peerMgr * manager;
}
Torrent;

struct tr_peerMgr
{
    tr_session      * session;
    tr_ptrArray     * torrents; /* Torrent */
    tr_ptrArray     * incomingHandshakes; /* tr_handshake */
    tr_ptrArray     * finishedHandshakes; /* tr_handshake */
    tr_timer        * bandwidthTimer;
};

#define tordbg( t, ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, t->tor->info.name, __VA_ARGS__ ); \
    } while( 0 )

#define dbgmsg( ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, NULL, __VA_ARGS__ ); \
    } while( 0 )

/**
***
**/

static void
managerLock( const struct tr_peerMgr * manager )
{
    tr_globalLock( manager->session );
}

static void
managerUnlock( const struct tr_peerMgr * manager )
{
    tr_globalUnlock( manager->session );
}

static void
torrentLock( Torrent * torrent )
{
    managerLock( torrent->manager );
}

static void
torrentUnlock( Torrent * torrent )
{
    managerUnlock( torrent->manager );
}

static int
torrentIsLocked( const Torrent * t )
{
    return tr_globalIsLocked( t->manager->session );
}

/**
***
**/

static int
handshakeCompareToAddr( const void * va, const void * vb )
{
    const tr_handshake * a = va;

    return tr_compareAddresses( tr_handshakeGetAddr( a, NULL ), vb );
}

static int
handshakeCompare( const void * a, const void * b )
{
    return handshakeCompareToAddr( a, tr_handshakeGetAddr( b, NULL ) );
}

static tr_handshake*
getExistingHandshake( tr_ptrArray      * handshakes,
                      const tr_address * addr )
{
    return tr_ptrArrayFindSorted( handshakes, addr, handshakeCompareToAddr );
}

static int
comparePeerAtomToAddress( const void * va, const void * vb )
{
    const struct peer_atom * a = va;

    return tr_compareAddresses( &a->addr, vb );
}

static int
comparePeerAtoms( const void * va, const void * vb )
{
    const struct peer_atom * b = vb;

    return comparePeerAtomToAddress( va, &b->addr );
}

/**
***
**/

static int
torrentCompare( const void * va,
                const void * vb )
{
    const Torrent * a = va;
    const Torrent * b = vb;

    return memcmp( a->hash, b->hash, SHA_DIGEST_LENGTH );
}

static int
torrentCompareToHash( const void * va,
                      const void * vb )
{
    const Torrent * a = va;
    const uint8_t * b_hash = vb;

    return memcmp( a->hash, b_hash, SHA_DIGEST_LENGTH );
}

static Torrent*
getExistingTorrent( tr_peerMgr *    manager,
                    const uint8_t * hash )
{
    return (Torrent*) tr_ptrArrayFindSorted( manager->torrents,
                                             hash,
                                             torrentCompareToHash );
}

static int
peerCompare( const void * va, const void * vb )
{
    const tr_peer * a = va;
    const tr_peer * b = vb;

    return tr_compareAddresses( &a->addr, &b->addr );
}

static int
peerCompareToAddr( const void * va, const void * vb )
{
    const tr_peer * a = va;

    return tr_compareAddresses( &a->addr, vb );
}

static tr_peer*
getExistingPeer( Torrent          * torrent,
                 const tr_address * addr )
{
    assert( torrentIsLocked( torrent ) );
    assert( addr );

    return tr_ptrArrayFindSorted( torrent->peers, addr, peerCompareToAddr );
}

static struct peer_atom*
getExistingAtom( const Torrent    * t,
                 const tr_address * addr )
{
    assert( torrentIsLocked( t ) );
    return tr_ptrArrayFindSorted( t->pool, addr, comparePeerAtomToAddress );
}

static tr_bool
peerIsInUse( const Torrent    * ct,
             const tr_address * addr )
{
    Torrent * t = (Torrent*) ct;

    assert( torrentIsLocked ( t ) );

    return getExistingPeer( t, addr )
        || getExistingHandshake( t->outgoingHandshakes, addr )
        || getExistingHandshake( t->manager->incomingHandshakes, addr );
}

static tr_peer*
peerConstructor( tr_torrent * tor, const tr_address * addr )
{
    tr_peer * p;
    p = tr_new0( tr_peer, 1 );
    p->addr = *addr;
    p->bandwidth = tr_bandwidthNew( tor->session, tor->bandwidth );
    return p;
}

static tr_peer*
getPeer( Torrent          * torrent,
         const tr_address * addr )
{
    tr_peer * peer;

    assert( torrentIsLocked( torrent ) );

    peer = getExistingPeer( torrent, addr );

    if( peer == NULL )
    {
        peer = peerConstructor( torrent->tor, addr );
        tr_ptrArrayInsertSorted( torrent->peers, peer, peerCompare );
    }

    return peer;
}

static void
peerDestructor( tr_peer * peer )
{
    assert( peer );

    if( peer->msgs != NULL )
    {
        tr_peerMsgsUnsubscribe( peer->msgs, peer->msgsTag );
        tr_peerMsgsFree( peer->msgs );
    }

    tr_peerIoFree( peer->io );

    tr_bitfieldFree( peer->have );
    tr_bitfieldFree( peer->blame );
    tr_free( peer->client );

    tr_bandwidthFree( peer->bandwidth );

    tr_free( peer );
}

static void
removePeer( Torrent * t,
            tr_peer * peer )
{
    tr_peer *          removed;
    struct peer_atom * atom;

    assert( torrentIsLocked( t ) );

    atom = getExistingAtom( t, &peer->addr );
    assert( atom );
    atom->time = time( NULL );

    removed = tr_ptrArrayRemoveSorted( t->peers, peer, peerCompare );
    assert( removed == peer );
    peerDestructor( removed );
}

static void
removeAllPeers( Torrent * t )
{
    while( !tr_ptrArrayEmpty( t->peers ) )
        removePeer( t, tr_ptrArrayNth( t->peers, 0 ) );
}

static void
torrentDestructor( void * vt )
{
    Torrent * t = vt;
    uint8_t   hash[SHA_DIGEST_LENGTH];

    assert( t );
    assert( !t->isRunning );
    assert( t->peers );
    assert( torrentIsLocked( t ) );
    assert( tr_ptrArrayEmpty( t->outgoingHandshakes ) );
    assert( tr_ptrArrayEmpty( t->peers ) );

    memcpy( hash, t->hash, SHA_DIGEST_LENGTH );

    tr_timerFree( &t->reconnectTimer );
    tr_timerFree( &t->rechokeTimer );
    tr_timerFree( &t->refillTimer );

    tr_ptrArrayFree( t->webseeds, (PtrArrayForeachFunc)tr_webseedFree );
    tr_ptrArrayFree( t->pool, (PtrArrayForeachFunc)tr_free );
    tr_ptrArrayFree( t->outgoingHandshakes, NULL );
    tr_ptrArrayFree( t->peers, NULL );

    tr_free( t->pendingRequestCount );
    tr_free( t );
}

static void peerCallbackFunc( void * vpeer,
                              void * vevent,
                              void * vt );

static Torrent*
torrentConstructor( tr_peerMgr * manager,
                    tr_torrent * tor )
{
    int       i;
    Torrent * t;

    t = tr_new0( Torrent, 1 );
    t->manager = manager;
    t->tor = tor;
    t->pool = tr_ptrArrayNew( );
    t->peers = tr_ptrArrayNew( );
    t->webseeds = tr_ptrArrayNew( );
    t->outgoingHandshakes = tr_ptrArrayNew( );
    memcpy( t->hash, tor->info.hash, SHA_DIGEST_LENGTH );

    for( i = 0; i < tor->info.webseedCount; ++i )
    {
        tr_webseed * w =
            tr_webseedNew( tor, tor->info.webseeds[i], peerCallbackFunc,
                           t );
        tr_ptrArrayAppend( t->webseeds, w );
    }

    return t;
}


static int bandwidthPulse( void * vmgr );


tr_peerMgr*
tr_peerMgrNew( tr_session * session )
{
    tr_peerMgr * m = tr_new0( tr_peerMgr, 1 );

    m->session = session;
    m->torrents = tr_ptrArrayNew( );
    m->incomingHandshakes = tr_ptrArrayNew( );
    m->finishedHandshakes = tr_ptrArrayNew( );
    m->bandwidthTimer = tr_timerNew( session, bandwidthPulse, m, BANDWIDTH_PERIOD_MSEC );
    return m;
}

void
tr_peerMgrFree( tr_peerMgr * manager )
{
    tr_handshake * handshake;

    managerLock( manager );

    tr_timerFree( &manager->bandwidthTimer );

    /* free the handshakes.  Abort invokes handshakeDoneCB(), which removes
     * the item from manager->handshakes, so this is a little roundabout... */
    while( !tr_ptrArrayEmpty( manager->incomingHandshakes ) )
        tr_handshakeAbort( tr_ptrArrayNth( manager->incomingHandshakes, 0 ) );

    tr_ptrArrayFree( manager->incomingHandshakes, NULL );

    while(( handshake = tr_ptrArrayPop( manager->finishedHandshakes )))
        tr_handshakeFree( handshake );

    tr_ptrArrayFree( manager->finishedHandshakes, NULL );

    /* free the torrents. */
    tr_ptrArrayFree( manager->torrents, torrentDestructor );

    managerUnlock( manager );
    tr_free( manager );
}

static tr_peer**
getConnectedPeers( Torrent * t, int * setmeCount )
{
    int i, peerCount, connectionCount;
    tr_peer **peers;
    tr_peer **ret;

    assert( torrentIsLocked( t ) );

    peers = (tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );
    ret = tr_new( tr_peer *, peerCount );

    for( i = connectionCount = 0; i < peerCount; ++i )
        if( peers[i]->msgs )
            ret[connectionCount++] = peers[i];

    *setmeCount = connectionCount;
    return ret;
}

static int
clientIsDownloadingFrom( const tr_peer * peer )
{
    return peer->clientIsInterested && !peer->clientIsChoked;
}

static int
clientIsUploadingTo( const tr_peer * peer )
{
    return peer->peerIsInterested && !peer->peerIsChoked;
}

/***
****
***/

tr_bool
tr_peerMgrPeerIsSeed( const tr_peerMgr  * mgr,
                      const uint8_t     * torrentHash,
                      const tr_address  * addr )
{
    tr_bool isSeed = FALSE;
    const Torrent * t = NULL;
    const struct peer_atom * atom = NULL;

    t = getExistingTorrent( (tr_peerMgr*)mgr, torrentHash );
    if( t )
        atom = getExistingAtom( t, addr );
    if( atom )
        isSeed = ( atom->flags & ADDED_F_SEED_FLAG ) != 0;

    return isSeed;
}

/****
*****
*****  REFILL
*****
****/

static void
assertValidPiece( Torrent * t, tr_piece_index_t piece )
{
    assert( t );
    assert( t->tor );
    assert( piece < t->tor->info.pieceCount );
}

static int
getPieceRequests( Torrent * t, tr_piece_index_t piece )
{
    assertValidPiece( t, piece );

    return t->pendingRequestCount ? t->pendingRequestCount[piece] : 0;
}

static void
incrementPieceRequests( Torrent * t, tr_piece_index_t piece )
{
    assertValidPiece( t, piece );

    if( t->pendingRequestCount == NULL )
        t->pendingRequestCount = tr_new0( int, t->tor->info.pieceCount );
    t->pendingRequestCount[piece]++;
}

static void
decrementPieceRequests( Torrent * t, tr_piece_index_t piece )
{
    assertValidPiece( t, piece );

    if( t->pendingRequestCount )
        t->pendingRequestCount[piece]--;
}

struct tr_refill_piece
{
    tr_priority_t    priority;
    uint32_t         piece;
    uint32_t         peerCount;
    int              random;
    int              pendingRequestCount;
    int              missingBlockCount;
};

static int
compareRefillPiece( const void * aIn, const void * bIn )
{
    const struct tr_refill_piece * a = aIn;
    const struct tr_refill_piece * b = bIn;

    /* if one piece has a higher priority, it goes first */
    if( a->priority != b->priority )
        return a->priority > b->priority ? -1 : 1;

    /* have a per-priority endgame */
    if( a->pendingRequestCount != b->pendingRequestCount )
        return a->pendingRequestCount < b->pendingRequestCount ? -1 : 1;

    /* fewer missing pieces goes first */
    if( a->missingBlockCount != b->missingBlockCount )
        return a->missingBlockCount < b->missingBlockCount ? -1 : 1;

    /* otherwise if one has fewer peers, it goes first */
    if( a->peerCount != b->peerCount )
        return a->peerCount < b->peerCount ? -1 : 1;

    /* otherwise go with our random seed */
    if( a->random != b->random )
        return a->random < b->random ? -1 : 1;

    return 0;
}

static tr_piece_index_t *
getPreferredPieces( Torrent * t, tr_piece_index_t * pieceCount )
{
    const tr_torrent  * tor = t->tor;
    const tr_info     * inf = &tor->info;
    tr_piece_index_t    i;
    tr_piece_index_t    poolSize = 0;
    tr_piece_index_t  * pool = tr_new( tr_piece_index_t , inf->pieceCount );
    int                 peerCount;
    tr_peer**           peers;

    assert( torrentIsLocked( t ) );

    peers = getConnectedPeers( t, &peerCount );

    /* make a list of the pieces that we want but don't have */
    for( i = 0; i < inf->pieceCount; ++i )
        if( !tor->info.pieces[i].dnd
                && !tr_cpPieceIsComplete( tor->completion, i ) )
            pool[poolSize++] = i;

    /* sort the pool by which to request next */
    if( poolSize > 1 )
    {
        tr_piece_index_t j;
        struct tr_refill_piece * p = tr_new( struct tr_refill_piece, poolSize );

        for( j = 0; j < poolSize; ++j )
        {
            int k;
            const tr_piece_index_t piece = pool[j];
            struct tr_refill_piece * setme = p + j;

            setme->piece = piece;
            setme->priority = inf->pieces[piece].priority;
            setme->peerCount = 0;
            setme->random = tr_cryptoWeakRandInt( INT_MAX );
            setme->pendingRequestCount = getPieceRequests( t, piece );
            setme->missingBlockCount
                         = tr_cpMissingBlocksInPiece( tor->completion, piece );

            for( k = 0; k < peerCount; ++k )
            {
                const tr_peer * peer = peers[k];
                if( peer->peerIsInterested
                        && !peer->clientIsChoked
                        && tr_bitfieldHas( peer->have, piece ) )
                    ++setme->peerCount;
            }
        }

        qsort( p, poolSize, sizeof( struct tr_refill_piece ),
               compareRefillPiece );

        for( j = 0; j < poolSize; ++j )
            pool[j] = p[j].piece;

        tr_free( p );
    }

    tr_free( peers );

    *pieceCount = poolSize;
    return pool;
}

struct tr_blockIterator
{
    Torrent * t;
    tr_block_index_t blockIndex, blockCount, *blocks;
    tr_piece_index_t pieceIndex, pieceCount, *pieces;
};

static struct tr_blockIterator*
blockIteratorNew( Torrent * t )
{
    struct tr_blockIterator * i = tr_new0( struct tr_blockIterator, 1 );
    i->t = t;
    i->pieces = getPreferredPieces( t, &i->pieceCount );
    i->blocks = tr_new0( tr_block_index_t, t->tor->blockCount );
    return i;
}

static int
blockIteratorNext( struct tr_blockIterator * i, tr_block_index_t * setme )
{
    int found;
    Torrent * t = i->t;
    tr_torrent * tor = t->tor;

    while( ( i->blockIndex == i->blockCount )
        && ( i->pieceIndex < i->pieceCount ) )
    {
        const tr_piece_index_t index = i->pieces[i->pieceIndex++];
        const tr_block_index_t b = tr_torPieceFirstBlock( tor, index );
        const tr_block_index_t e = b + tr_torPieceCountBlocks( tor, index );
        tr_block_index_t block;

        assert( index < tor->info.pieceCount );

        i->blockCount = 0;
        i->blockIndex = 0;
        for( block=b; block!=e; ++block )
            if( !tr_cpBlockIsComplete( tor->completion, block ) )
                i->blocks[i->blockCount++] = block;
    }

    if(( found = ( i->blockIndex < i->blockCount )))
        *setme = i->blocks[i->blockIndex++];

    return found;
}

static void
blockIteratorFree( struct tr_blockIterator * i )
{
    tr_free( i->blocks );
    tr_free( i->pieces );
    tr_free( i );
}

static tr_peer**
getPeersUploadingToClient( Torrent * t,
                           int *     setmeCount )
{
    int j;
    int peerCount = 0;
    int retCount = 0;
    tr_peer ** peers = (tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );
    tr_peer ** ret = tr_new( tr_peer *, peerCount );

    j = 0; /* this is a temporary test to make sure we walk through all the peers */
    if( peerCount )
    {
        /* Get a list of peers we're downloading from.
           Pick a different starting point each time so all peers
           get a chance at being the first in line */
        const int fencepost = tr_cryptoWeakRandInt( peerCount );
        int i = fencepost;
        do {
            if( clientIsDownloadingFrom( peers[i] ) )
                ret[retCount++] = peers[i];
            i = ( i + 1 ) % peerCount;
            ++j;
        } while( i != fencepost );
    }
    assert( j == peerCount );
    *setmeCount = retCount;
    return ret;
}

static uint32_t
getBlockOffsetInPiece( const tr_torrent * tor, uint64_t b )
{
    const uint64_t piecePos = tor->info.pieceSize * tr_torBlockPiece( tor, b );
    const uint64_t blockPos = tor->blockSize * b;
    assert( blockPos >= piecePos );
    return (uint32_t)( blockPos - piecePos );
}

static int
refillPulse( void * vtorrent )
{
    tr_block_index_t block;
    int peerCount;
    int webseedCount;
    tr_peer ** peers;
    tr_webseed ** webseeds;
    struct tr_blockIterator * blockIterator;
    Torrent * t = vtorrent;
    tr_torrent * tor = t->tor;

    if( !t->isRunning )
        return TRUE;
    if( tr_torrentIsSeed( t->tor ) )
        return TRUE;

    torrentLock( t );
    tordbg( t, "Refilling Request Buffers..." );

    blockIterator = blockIteratorNew( t );
    peers = getPeersUploadingToClient( t, &peerCount );
    webseedCount = tr_ptrArraySize( t->webseeds );
    webseeds = tr_memdup( tr_ptrArrayBase( t->webseeds ),
                          webseedCount * sizeof( tr_webseed* ) );

    while( ( webseedCount || peerCount )
        && blockIteratorNext( blockIterator, &block ) )
    {
        int j;
        int handled = FALSE;

        const tr_piece_index_t index = tr_torBlockPiece( tor, block );
        const uint32_t offset = getBlockOffsetInPiece( tor, block );
        const uint32_t length = tr_torBlockCountBytes( tor, block );

        /* find a peer who can ask for this block */
        for( j=0; !handled && j<peerCount; )
        {
            const tr_addreq_t val = tr_peerMsgsAddRequest( peers[j]->msgs, index, offset, length );
            switch( val )
            {
                case TR_ADDREQ_FULL:
                case TR_ADDREQ_CLIENT_CHOKED:
                    peers[j] = peers[--peerCount];
                    break;

                case TR_ADDREQ_MISSING:
                case TR_ADDREQ_DUPLICATE:
                    ++j;
                    break;

                case TR_ADDREQ_OK:
                    incrementPieceRequests( t, index );
                    handled = TRUE;
                    break;

                default:
                    assert( 0 && "unhandled value" );
                    break;
            }
        }

        /* maybe one of the webseeds can do it */
        for( j=0; !handled && j<webseedCount; )
        {
            const tr_addreq_t val = tr_webseedAddRequest( webseeds[j], index, offset, length );
            switch( val )
            {
                case TR_ADDREQ_FULL:
                    webseeds[j] = webseeds[--webseedCount];
                    break;

                case TR_ADDREQ_OK:
                    incrementPieceRequests( t, index );
                    handled = TRUE;
                    break;

                default:
                    assert( 0 && "unhandled value" );
                    break;
            }
        }
    }

    /* cleanup */
    blockIteratorFree( blockIterator );
    tr_free( webseeds );
    tr_free( peers );

    t->refillTimer = NULL;
    torrentUnlock( t );
    return FALSE;
}

static void
broadcastGotBlock( Torrent * t, uint32_t index, uint32_t offset, uint32_t length )
{
    int i, size;
    tr_peer ** peers;

    assert( torrentIsLocked( t ) );

    tordbg( t, "got a block; cancelling any duplicate requests from peers %"PRIu32":%"PRIu32"->%"PRIu32, index, offset, length );
    peers = getConnectedPeers( t, &size );
    for( i=0; i<size; ++i )
        tr_peerMsgsCancel( peers[i]->msgs, index, offset, length );
    tr_free( peers );
}

static void
addStrike( Torrent * t,
           tr_peer * peer )
{
    tordbg( t, "increasing peer %s strike count to %d",
            tr_peerIoAddrStr( &peer->addr,
                              peer->port ), peer->strikes + 1 );

    if( ++peer->strikes >= MAX_BAD_PIECES_PER_PEER )
    {
        struct peer_atom * atom = getExistingAtom( t, &peer->addr );
        atom->myflags |= MYFLAG_BANNED;
        peer->doPurge = 1;
        tordbg( t, "banning peer %s", tr_peerIoAddrStr( &atom->addr, atom->port ) );
    }
}

static void
gotBadPiece( Torrent *        t,
             tr_piece_index_t pieceIndex )
{
    tr_torrent *   tor = t->tor;
    const uint32_t byteCount = tr_torPieceCountBytes( tor, pieceIndex );

    tor->corruptCur += byteCount;
    tor->downloadedCur -= MIN( tor->downloadedCur, byteCount );
}

static void
refillSoon( Torrent * t )
{
    if( t->refillTimer == NULL )
        t->refillTimer = tr_timerNew( t->manager->session,
                                      refillPulse, t,
                                      REFILL_PERIOD_MSEC );
}

static void
peerSuggestedPiece( Torrent            * t UNUSED,
                    tr_peer            * peer UNUSED,
                    tr_piece_index_t     pieceIndex UNUSED,
                    int                  isFastAllowed UNUSED )
{
#if 0
    assert( t );
    assert( peer );
    assert( peer->msgs );

    /* is this a valid piece? */
    if(  pieceIndex >= t->tor->info.pieceCount )
        return;

    /* don't ask for it if we've already got it */
    if( tr_cpPieceIsComplete( t->tor->completion, pieceIndex ) )
        return;

    /* don't ask for it if they don't have it */
    if( !tr_bitfieldHas( peer->have, pieceIndex ) )
        return;

    /* don't ask for it if we're choked and it's not fast */
    if( !isFastAllowed && peer->clientIsChoked )
        return;

    /* request the blocks that we don't have in this piece */
    {
        tr_block_index_t block;
        const tr_torrent * tor = t->tor;
        const tr_block_index_t start = tr_torPieceFirstBlock( tor, pieceIndex );
        const tr_block_index_t end = start + tr_torPieceCountBlocks( tor, pieceIndex );

        for( block=start; block<end; ++block )
        {
            if( !tr_cpBlockIsComplete( tor->completion, block ) )
            {
                const uint32_t offset = getBlockOffsetInPiece( tor, block );
                const uint32_t length = tr_torBlockCountBytes( tor, block );
                tr_peerMsgsAddRequest( peer->msgs, pieceIndex, offset, length );
                incrementPieceRequests( t, pieceIndex );
            }
        }
    }
#endif
}

static void
peerCallbackFunc( void * vpeer, void * vevent, void * vt )
{
    tr_peer * peer = vpeer; /* may be NULL if peer is a webseed */
    Torrent * t = vt;
    const tr_peer_event * e = vevent;

    torrentLock( t );

    switch( e->eventType )
    {
        case TR_PEER_UPLOAD_ONLY:
            /* update our atom */
            if( peer ) {
                struct peer_atom * a = getExistingAtom( t, &peer->addr );
                a->uploadOnly = e->uploadOnly ? UPLOAD_ONLY_YES : UPLOAD_ONLY_NO;
            }
            break;

        case TR_PEER_NEED_REQ:
            refillSoon( t );
            break;

        case TR_PEER_CANCEL:
            decrementPieceRequests( t, e->pieceIndex );
            break;

        case TR_PEER_PEER_GOT_DATA:
        {
            const time_t now = time( NULL );
            tr_torrent * tor = t->tor;

            tor->activityDate = now;

            if( e->wasPieceData )
                tor->uploadedCur += e->length;

            /* update the stats */
            if( e->wasPieceData )
                tr_statsAddUploaded( tor->session, e->length );

            /* update our atom */
            if( peer ) {
                struct peer_atom * a = getExistingAtom( t, &peer->addr );
                if( e->wasPieceData )
                    a->piece_data_time = now;
            }

            break;
        }

        case TR_PEER_CLIENT_GOT_SUGGEST:
            if( peer )
                peerSuggestedPiece( t, peer, e->pieceIndex, FALSE );
            break;

        case TR_PEER_CLIENT_GOT_ALLOWED_FAST:
            if( peer )
                peerSuggestedPiece( t, peer, e->pieceIndex, TRUE );
            break;

        case TR_PEER_CLIENT_GOT_DATA:
        {
            const time_t now = time( NULL );
            tr_torrent * tor = t->tor;
            tor->activityDate = now;

            /* only add this to downloadedCur if we got it from a peer --
             * webseeds shouldn't count against our ratio.  As one tracker
             * admin put it, "Those pieces are downloaded directly from the
             * content distributor, not the peers, it is the tracker's job
             * to manage the swarms, not the web server and does not fit
             * into the jurisdiction of the tracker." */
            if( peer && e->wasPieceData )
                tor->downloadedCur += e->length;

            /* update the stats */ 
            if( e->wasPieceData )
                tr_statsAddDownloaded( tor->session, e->length );

            /* update our atom */
            if( peer ) {
                struct peer_atom * a = getExistingAtom( t, &peer->addr );
                if( e->wasPieceData )
                    a->piece_data_time = now;
            }

            break;
        }

        case TR_PEER_PEER_PROGRESS:
        {
            if( peer )
            {
                struct peer_atom * atom = getExistingAtom( t, &peer->addr );
                const int peerIsSeed = e->progress >= 1.0;
                if( peerIsSeed ) {
                    tordbg( t, "marking peer %s as a seed", tr_peerIoAddrStr( &atom->addr, atom->port ) );
                    atom->flags |= ADDED_F_SEED_FLAG;
                } else {
                    tordbg( t, "marking peer %s as a non-seed", tr_peerIoAddrStr( &atom->addr, atom->port ) );
                    atom->flags &= ~ADDED_F_SEED_FLAG;
                }
            }
            break;
        }

        case TR_PEER_CLIENT_GOT_BLOCK:
        {
            tr_torrent *     tor = t->tor;

            tr_block_index_t block = _tr_block( tor, e->pieceIndex, e->offset );

            tr_cpBlockAdd( tor->completion, block );
            decrementPieceRequests( t, e->pieceIndex );

            broadcastGotBlock( t, e->pieceIndex, e->offset, e->length );

            if( tr_cpPieceIsComplete( tor->completion, e->pieceIndex ) )
            {
                const tr_piece_index_t p = e->pieceIndex;
                const tr_bool ok = tr_ioTestPiece( tor, p );

                if( !ok )
                {
                    tr_torerr( tor, _( "Piece %lu, which was just downloaded, failed its checksum test" ),
                               (unsigned long)p );
                }

                tr_torrentSetHasPiece( tor, p, ok );
                tr_torrentSetPieceChecked( tor, p, TRUE );
                tr_peerMgrSetBlame( tor->session->peerMgr, tor->info.hash, p, ok );

                if( !ok )
                    gotBadPiece( t, p );
                else
                {
                    int i, peerCount;
                    tr_peer ** peers = getConnectedPeers( t, &peerCount );
                    for( i = 0; i < peerCount; ++i )
                        tr_peerMsgsHave( peers[i]->msgs, p );
                    tr_free( peers );
                }

                tr_torrentRecheckCompleteness( tor );
            }
            break;
        }

        case TR_PEER_ERROR:
            if( e->err == EINVAL )
            {
                addStrike( t, peer );
                peer->doPurge = 1;
                tordbg( t, "setting %s doPurge flag because we got an EINVAL error", tr_peerIoAddrStr( &peer->addr, peer->port ) );
            }
            else if( ( e->err == ERANGE )
                  || ( e->err == EMSGSIZE )
                  || ( e->err == ENOTCONN ) )
            {
                /* some protocol error from the peer */
                peer->doPurge = 1;
                tordbg( t, "setting %s doPurge flag because we got an ERANGE, EMSGSIZE, or ENOTCONN error", tr_peerIoAddrStr( &peer->addr, peer->port ) );
            }
            else /* a local error, such as an IO error */
            {
                t->tor->error = e->err;
                tr_strlcpy( t->tor->errorString,
                            tr_strerror( t->tor->error ),
                            sizeof( t->tor->errorString ) );
                tr_torrentStop( t->tor );
            }
            break;

        default:
            assert( 0 );
    }

    torrentUnlock( t );
}

static void
ensureAtomExists( Torrent          * t,
                  const tr_address * addr,
                  tr_port            port,
                  uint8_t            flags,
                  uint8_t            from )
{
    if( getExistingAtom( t, addr ) == NULL )
    {
        struct peer_atom * a;
        a = tr_new0( struct peer_atom, 1 );
        a->addr = *addr;
        a->port = port;
        a->flags = flags;
        a->from = from;
        tordbg( t, "got a new atom: %s", tr_peerIoAddrStr( &a->addr, a->port ) );
        tr_ptrArrayInsertSorted( t->pool, a, comparePeerAtoms );
    }
}

static int
getMaxPeerCount( const tr_torrent * tor )
{
    return tor->maxConnectedPeers;
}

static int
getPeerCount( const Torrent * t )
{
    return tr_ptrArraySize( t->peers ) + tr_ptrArraySize( t->outgoingHandshakes );
}

/* FIXME: this is kind of a mess. */
static tr_bool
myHandshakeDoneCB( tr_handshake  * handshake,
                   tr_peerIo     * io,
                   int             isConnected,
                   const uint8_t * peer_id,
                   void          * vmanager )
{
    tr_bool            ok = isConnected;
    tr_bool            success = FALSE;
    tr_port            port;
    const tr_address * addr;
    tr_peerMgr       * manager = vmanager;
    Torrent          * t;
    tr_handshake     * ours;

    assert( io );
    assert( isConnected == 0 || isConnected == 1 );

    t = tr_peerIoHasTorrentHash( io )
        ? getExistingTorrent( manager, tr_peerIoGetTorrentHash( io ) )
        : NULL;

    if( tr_peerIoIsIncoming ( io ) )
        ours = tr_ptrArrayRemoveSorted( manager->incomingHandshakes,
                                        handshake, handshakeCompare );
    else if( t )
        ours = tr_ptrArrayRemoveSorted( t->outgoingHandshakes,
                                        handshake, handshakeCompare );
    else
        ours = handshake;

    assert( ours );
    assert( ours == handshake );

    if( t )
        torrentLock( t );

    addr = tr_peerIoGetAddress( io, &port );

    if( !ok || !t || !t->isRunning )
    {
        if( t )
        {
            struct peer_atom * atom = getExistingAtom( t, addr );
            if( atom )
                ++atom->numFails;
        }
    }
    else /* looking good */
    {
        struct peer_atom * atom;
        ensureAtomExists( t, addr, port, 0, TR_PEER_FROM_INCOMING );
        atom = getExistingAtom( t, addr );
        atom->time = time( NULL );
        atom->piece_data_time = 0;

        if( atom->myflags & MYFLAG_BANNED )
        {
            tordbg( t, "banned peer %s tried to reconnect",
                    tr_peerIoAddrStr( &atom->addr, atom->port ) );
        }
        else if( tr_peerIoIsIncoming( io )
               && ( getPeerCount( t ) >= getMaxPeerCount( t->tor ) ) )

        {
        }
        else
        {
            tr_peer * peer = getExistingPeer( t, addr );

            if( peer ) /* we already have this peer */
            {
            }
            else
            {
                peer = getPeer( t, addr );
                tr_free( peer->client );

                if( !peer_id )
                    peer->client = NULL;
                else {
                    char client[128];
                    tr_clientForId( client, sizeof( client ), peer_id );
                    peer->client = tr_strdup( client );
                }

                peer->port = port;
                peer->io = tr_handshakeStealIO( handshake );
                tr_peerMsgsNew( t->tor, peer, peerCallbackFunc, t, &peer->msgsTag );
                tr_peerIoSetBandwidth( io, peer->bandwidth );

                success = TRUE;
            }
        }
    }

    if( !success )
        tr_ptrArrayAppend( manager->finishedHandshakes, handshake );

    if( t )
        torrentUnlock( t );

    return success;
}

void
tr_peerMgrAddIncoming( tr_peerMgr * manager,
                       tr_address * addr,
                       tr_port      port,
                       int          socket )
{
    managerLock( manager );

    if( tr_sessionIsAddressBlocked( manager->session, addr ) )
    {
        tr_dbg( "Banned IP address \"%s\" tried to connect to us", inet_ntoa( *addr ) );
        tr_netClose( socket );
    }
    else if( getExistingHandshake( manager->incomingHandshakes, addr ) )
    {
        tr_netClose( socket );
    }
    else /* we don't have a connetion to them yet... */
    {
        tr_peerIo *    io;
        tr_handshake * handshake;

        io = tr_peerIoNewIncoming( manager->session, addr, port, socket );

        handshake = tr_handshakeNew( io,
                                     manager->session->encryptionMode,
                                     myHandshakeDoneCB,
                                     manager );

        tr_ptrArrayInsertSorted( manager->incomingHandshakes, handshake,
                                 handshakeCompare );
    }

    managerUnlock( manager );
}

static tr_bool
tr_isPex( const tr_pex * pex )
{
    return pex && tr_isAddress( &pex->addr );
}

void
tr_peerMgrAddPex( tr_peerMgr *    manager,
                  const uint8_t * torrentHash,
                  uint8_t         from,
                  const tr_pex *  pex )
{
    if( tr_isPex( pex ) ) /* safeguard against corrupt data */
    {
        Torrent * t;
        managerLock( manager );

        t = getExistingTorrent( manager, torrentHash );
        if( !tr_sessionIsAddressBlocked( t->manager->session, &pex->addr ) )
            ensureAtomExists( t, &pex->addr, pex->port, pex->flags, from );

        managerUnlock( manager );
    }
}

tr_pex *
tr_peerMgrCompactToPex( const void *    compact,
                        size_t          compactLen,
                        const uint8_t * added_f,
                        size_t          added_f_len,
                        size_t *        pexCount )
{
    size_t          i;
    size_t          n = compactLen / 6;
    const uint8_t * walk = compact;
    tr_pex *        pex = tr_new0( tr_pex, n );

    for( i = 0; i < n; ++i )
    {
        memcpy( &pex[i].addr, walk, 4 ); walk += 4;
        memcpy( &pex[i].port, walk, 2 ); walk += 2;
        if( added_f && ( n == added_f_len ) )
            pex[i].flags = added_f[i];
    }

    *pexCount = n;
    return pex;
}

/**
***
**/

void
tr_peerMgrSetBlame( tr_peerMgr *     manager,
                    const uint8_t *  torrentHash,
                    tr_piece_index_t pieceIndex,
                    int              success )
{
    if( !success )
    {
        int        peerCount, i;
        Torrent *  t = getExistingTorrent( manager, torrentHash );
        tr_peer ** peers;

        assert( torrentIsLocked( t ) );

        peers = (tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );
        for( i = 0; i < peerCount; ++i )
        {
            tr_peer * peer = peers[i];
            if( tr_bitfieldHas( peer->blame, pieceIndex ) )
            {
                tordbg( t, "peer %s contributed to corrupt piece (%d); now has %d strikes",
                        tr_peerIoAddrStr( &peer->addr, peer->port ),
                        pieceIndex, (int)peer->strikes + 1 );
                addStrike( t, peer );
            }
        }
    }
}

int
tr_pexCompare( const void * va, const void * vb )
{
    const tr_pex * a = va;
    const tr_pex * b = vb;
    int i;

    assert( tr_isPex( a ) );
    assert( tr_isPex( b ) );

    if(( i = tr_compareAddresses( &a->addr, &b->addr )))
        return i;

    if( a->port != b->port )
        return a->port < b->port ? -1 : 1;

    return 0;
}

static int
peerPrefersCrypto( const tr_peer * peer )
{
    if( peer->encryption_preference == ENCRYPTION_PREFERENCE_YES )
        return TRUE;

    if( peer->encryption_preference == ENCRYPTION_PREFERENCE_NO )
        return FALSE;

    return tr_peerIoIsEncrypted( peer->io );
}

int
tr_peerMgrGetPeers( tr_peerMgr      * manager,
                    const uint8_t   * torrentHash,
                    tr_pex         ** setme_pex )
{
    int peerCount = 0;
    const Torrent *  t;

    managerLock( manager );

    t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    if( t == NULL )
    {
        *setme_pex = NULL;
    }
    else
    {
        int i;
        const tr_peer ** peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );
        tr_pex * pex = tr_new( tr_pex, peerCount );
        tr_pex * walk = pex;

        for( i=0; i<peerCount; ++i, ++walk )
        {
            const tr_peer * peer = peers[i];
            const struct peer_atom * atom = getExistingAtom( t, &peer->addr );

            assert( tr_isAddress( &peer->addr ) );
            walk->addr = peer->addr;
            walk->port = peer->port;
            walk->flags = 0;
            if( peerPrefersCrypto( peer ) ) 
                walk->flags |= ADDED_F_ENCRYPTION_FLAG;
            if( ( atom->uploadOnly == UPLOAD_ONLY_YES ) || ( peer->progress >= 1.0 ) )
                walk->flags |= ADDED_F_SEED_FLAG;
        }

        assert( ( walk - pex ) == peerCount );
        qsort( pex, peerCount, sizeof( tr_pex ), tr_pexCompare );
        *setme_pex = pex;
    }

    managerUnlock( manager );
    return peerCount;
}

static int reconnectPulse( void * vtorrent );

static int rechokePulse( void * vtorrent );

void
tr_peerMgrStartTorrent( tr_peerMgr *    manager,
                        const uint8_t * torrentHash )
{
    Torrent * t;

    managerLock( manager );

    t = getExistingTorrent( manager, torrentHash );

    assert( t );
    assert( ( t->isRunning != 0 ) == ( t->reconnectTimer != NULL ) );
    assert( ( t->isRunning != 0 ) == ( t->rechokeTimer != NULL ) );

    if( !t->isRunning )
    {
        t->isRunning = 1;

        t->reconnectTimer = tr_timerNew( t->manager->session,
                                         reconnectPulse, t,
                                         RECONNECT_PERIOD_MSEC );

        t->rechokeTimer = tr_timerNew( t->manager->session,
                                       rechokePulse, t,
                                       RECHOKE_PERIOD_MSEC );

        reconnectPulse( t );

        rechokePulse( t );

        if( !tr_ptrArrayEmpty( t->webseeds ) )
            refillSoon( t );
    }

    managerUnlock( manager );
}

static void
stopTorrent( Torrent * t )
{
    assert( torrentIsLocked( t ) );

    t->isRunning = 0;
    tr_timerFree( &t->rechokeTimer );
    tr_timerFree( &t->reconnectTimer );

    /* disconnect the peers. */
    tr_ptrArrayForeach( t->peers, (PtrArrayForeachFunc)peerDestructor );
    tr_ptrArrayClear( t->peers );

    /* disconnect the handshakes.  handshakeAbort calls handshakeDoneCB(),
     * which removes the handshake from t->outgoingHandshakes... */
    while( !tr_ptrArrayEmpty( t->outgoingHandshakes ) )
        tr_handshakeAbort( tr_ptrArrayNth( t->outgoingHandshakes, 0 ) );
}

void
tr_peerMgrStopTorrent( tr_peerMgr *    manager,
                       const uint8_t * torrentHash )
{
    managerLock( manager );

    stopTorrent( getExistingTorrent( manager, torrentHash ) );

    managerUnlock( manager );
}

void
tr_peerMgrAddTorrent( tr_peerMgr * manager,
                      tr_torrent * tor )
{
    Torrent * t;

    managerLock( manager );

    assert( tor );
    assert( getExistingTorrent( manager, tor->info.hash ) == NULL );

    t = torrentConstructor( manager, tor );
    tr_ptrArrayInsertSorted( manager->torrents, t, torrentCompare );

    managerUnlock( manager );
}

void
tr_peerMgrRemoveTorrent( tr_peerMgr *    manager,
                         const uint8_t * torrentHash )
{
    Torrent * t;

    managerLock( manager );

    t = getExistingTorrent( manager, torrentHash );
    assert( t );
    stopTorrent( t );
    tr_ptrArrayRemoveSorted( manager->torrents, t, torrentCompare );
    torrentDestructor( t );

    managerUnlock( manager );
}

void
tr_peerMgrTorrentAvailability( const tr_peerMgr * manager,
                               const uint8_t *    torrentHash,
                               int8_t *           tab,
                               unsigned int       tabCount )
{
    tr_piece_index_t   i;
    const Torrent *    t;
    const tr_torrent * tor;
    float              interval;
    tr_bool            isSeed;
    int                peerCount;
    const tr_peer **   peers;

    managerLock( manager );

    t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    tor = t->tor;
    interval = tor->info.pieceCount / (float)tabCount;
    isSeed = tor && ( tr_cpGetStatus ( tor->completion ) == TR_CP_COMPLETE );
    peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &peerCount );

    memset( tab, 0, tabCount );

    for( i = 0; tor && i < tabCount; ++i )
    {
        const int piece = i * interval;

        if( isSeed || tr_cpPieceIsComplete( tor->completion, piece ) )
            tab[i] = -1;
        else if( peerCount ) {
            int j;
            for( j = 0; j < peerCount; ++j )
                if( tr_bitfieldHas( peers[j]->have, i ) )
                    ++tab[i];
        }
    }

    managerUnlock( manager );
}

/* Returns the pieces that are available from peers */
tr_bitfield*
tr_peerMgrGetAvailable( const tr_peerMgr * manager,
                        const uint8_t *    torrentHash )
{
    int           i, size;
    Torrent *     t;
    tr_peer **    peers;
    tr_bitfield * pieces;
    managerLock( manager );

    t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    pieces = tr_bitfieldNew( t->tor->info.pieceCount );
    peers = getConnectedPeers( t, &size );
    for( i = 0; i < size; ++i )
        tr_bitfieldOr( pieces, peers[i]->have );

    managerUnlock( manager );
    tr_free( peers );
    return pieces;
}

int
tr_peerMgrHasConnections( const tr_peerMgr * manager,
                          const uint8_t *    torrentHash )
{
    int ret;
    const Torrent * t;
    managerLock( manager );

    t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    ret = t && ( !tr_ptrArrayEmpty( t->peers ) || !tr_ptrArrayEmpty( t->webseeds ) );

    managerUnlock( manager );
    return ret;
}

void
tr_peerMgrTorrentStats( const tr_peerMgr * manager,
                        const uint8_t    * torrentHash,
                        int              * setmePeersKnown,
                        int              * setmePeersConnected,
                        int              * setmeSeedsConnected,
                        int              * setmeWebseedsSendingToUs,
                        int              * setmePeersSendingToUs,
                        int              * setmePeersGettingFromUs,
                        int              * setmePeersFrom )
{
    int i, size;
    const Torrent * t;
    const tr_peer ** peers;
    const tr_webseed ** webseeds;

    managerLock( manager );

    t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    peers = (const tr_peer **) tr_ptrArrayPeek( t->peers, &size );

    *setmePeersKnown           = tr_ptrArraySize( t->pool );
    *setmePeersConnected       = 0;
    *setmeSeedsConnected       = 0;
    *setmePeersGettingFromUs   = 0;
    *setmePeersSendingToUs     = 0;
    *setmeWebseedsSendingToUs  = 0;

    for( i=0; i<TR_PEER_FROM__MAX; ++i )
        setmePeersFrom[i] = 0;

    for( i=0; i<size; ++i )
    {
        const tr_peer * peer = peers[i];
        const struct peer_atom * atom = getExistingAtom( t, &peer->addr );

        if( peer->io == NULL ) /* not connected */
            continue;

        ++*setmePeersConnected;

        ++setmePeersFrom[atom->from];

        if( clientIsDownloadingFrom( peer ) )
            ++*setmePeersSendingToUs;

        if( clientIsUploadingTo( peer ) )
            ++*setmePeersGettingFromUs;

        if( atom->flags & ADDED_F_SEED_FLAG )
            ++*setmeSeedsConnected;
    }

    webseeds = (const tr_webseed **) tr_ptrArrayPeek( t->webseeds, &size );
    for( i=0; i<size; ++i )
        if( tr_webseedIsActive( webseeds[i] ) )
            ++*setmeWebseedsSendingToUs;

    managerUnlock( manager );
}

float*
tr_peerMgrWebSpeeds( const tr_peerMgr * manager,
                     const uint8_t *    torrentHash )
{
    const Torrent * t;
    const tr_webseed ** webseeds;
    int i;
    int webseedCount;
    float * ret;

    assert( manager );
    managerLock( manager );

    t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    webseeds = (const tr_webseed**) tr_ptrArrayPeek( t->webseeds,
                                                     &webseedCount );
    assert( webseedCount == t->tor->info.webseedCount );
    ret = tr_new0( float, webseedCount );

    for( i=0; i<webseedCount; ++i )
        if( !tr_webseedGetSpeed( webseeds[i], &ret[i] ) )
            ret[i] = -1.0;

    managerUnlock( manager );
    return ret;
}

double
tr_peerGetPieceSpeed( const tr_peer * peer, tr_direction direction )
{
    assert( peer );
    assert( direction==TR_CLIENT_TO_PEER || direction==TR_PEER_TO_CLIENT );

    return tr_bandwidthGetPieceSpeed( peer->bandwidth, direction );
}


struct tr_peer_stat *
tr_peerMgrPeerStats( const   tr_peerMgr  * manager,
                     const   uint8_t     * torrentHash,
                     int                 * setmeCount UNUSED )
{
    int             i, size;
    const Torrent * t;
    tr_peer **      peers;
    tr_peer_stat *  ret;

    assert( manager );
    managerLock( manager );

    t = getExistingTorrent( (tr_peerMgr*)manager, torrentHash );
    peers = getConnectedPeers( (Torrent*)t, &size );
    ret = tr_new0( tr_peer_stat, size );

    for( i = 0; i < size; ++i )
    {
        char *                   pch;
        const tr_peer *          peer = peers[i];
        const struct peer_atom * atom = getExistingAtom( t, &peer->addr );
        tr_peer_stat *           stat = ret + i;

        tr_netNtop( &peer->addr, stat->addr, sizeof( stat->addr ) );
        tr_strlcpy( stat->client, ( peer->client ? peer->client : "" ),
                   sizeof( stat->client ) );
        stat->port               = ntohs( peer->port );
        stat->from               = atom->from;
        stat->progress           = peer->progress;
        stat->isEncrypted        = tr_peerIoIsEncrypted( peer->io ) ? 1 : 0;
        stat->rateToPeer         = tr_peerGetPieceSpeed( peer, TR_CLIENT_TO_PEER );
        stat->rateToClient       = tr_peerGetPieceSpeed( peer, TR_PEER_TO_CLIENT );
        stat->peerIsChoked       = peer->peerIsChoked;
        stat->peerIsInterested   = peer->peerIsInterested;
        stat->clientIsChoked     = peer->clientIsChoked;
        stat->clientIsInterested = peer->clientIsInterested;
        stat->isIncoming         = tr_peerIoIsIncoming( peer->io );
        stat->isDownloadingFrom  = clientIsDownloadingFrom( peer );
        stat->isUploadingTo      = clientIsUploadingTo( peer );
        stat->isSeed             = ( atom->uploadOnly == UPLOAD_ONLY_YES ) || ( peer->progress >= 1.0 );

        pch = stat->flagStr;
        if( t->optimistic == peer ) *pch++ = 'O';
        if( stat->isDownloadingFrom ) *pch++ = 'D';
        else if( stat->clientIsInterested ) *pch++ = 'd';
        if( stat->isUploadingTo ) *pch++ = 'U';
        else if( stat->peerIsInterested ) *pch++ = 'u';
        if( !stat->clientIsChoked && !stat->clientIsInterested ) *pch++ = 'K';
        if( !stat->peerIsChoked && !stat->peerIsInterested ) *pch++ = '?';
        if( stat->isEncrypted ) *pch++ = 'E';
        if( stat->from == TR_PEER_FROM_PEX ) *pch++ = 'X';
        if( stat->isIncoming ) *pch++ = 'I';
        *pch = '\0';
    }

    *setmeCount = size;
    tr_free( peers );

    managerUnlock( manager );
    return ret;
}

/**
***
**/

struct ChokeData
{
    tr_bool         doUnchoke;
    tr_bool         isInterested;
    tr_bool         isChoked;
    int             rate;
    tr_peer *       peer;
};

static int
compareChoke( const void * va,
              const void * vb )
{
    const struct ChokeData * a = va;
    const struct ChokeData * b = vb;

    if( a->rate != b->rate ) /* prefer higher overall speeds */
        return a->rate > b->rate ? -1 : 1;

    if( a->isChoked != b->isChoked ) /* prefer unchoked */
        return a->isChoked ? 1 : -1;

    return 0;
}

static int
isNew( const tr_peer * peer )
{
    return peer && peer->io && tr_peerIoGetAge( peer->io ) < 45;
}

static int
isSame( const tr_peer * peer )
{
    return peer && peer->client && strstr( peer->client, "Transmission" );
}

/**
***
**/

static void
rechoke( Torrent * t )
{
    int                i, peerCount, size, unchokedInterested;
    tr_peer **         peers = getConnectedPeers( t, &peerCount );
    struct ChokeData * choke = tr_new0( struct ChokeData, peerCount );
    const int          chokeAll = !tr_torrentIsPieceTransferAllowed( t->tor, TR_CLIENT_TO_PEER );

    assert( torrentIsLocked( t ) );

    /* sort the peers by preference and rate */
    for( i = 0, size = 0; i < peerCount; ++i )
    {
        tr_peer * peer = peers[i];
        struct peer_atom * atom = getExistingAtom( t, &peer->addr );

        if( peer->progress >= 1.0 ) /* choke all seeds */
        {
            tr_peerMsgsSetChoke( peer->msgs, TRUE );
        }
        else if( atom->uploadOnly == UPLOAD_ONLY_YES ) /* choke partial seeds */
        {
            tr_peerMsgsSetChoke( peer->msgs, TRUE );
        }
        else if( chokeAll ) /* choke everyone if we're not uploading */
        {
            tr_peerMsgsSetChoke( peer->msgs, TRUE );
        }
        else
        {
            struct ChokeData * n = &choke[size++];
            n->peer         = peer;
            n->isInterested = peer->peerIsInterested;
            n->isChoked     = peer->peerIsChoked;
            n->rate         = tr_peerGetPieceSpeed( peer, TR_CLIENT_TO_PEER ) * 1024;
        }
    }

    qsort( choke, size, sizeof( struct ChokeData ), compareChoke );

    /**
     * Reciprocation and number of uploads capping is managed by unchoking
     * the N peers which have the best upload rate and are interested.
     * This maximizes the client's download rate. These N peers are
     * referred to as downloaders, because they are interested in downloading
     * from the client.
     *
     * Peers which have a better upload rate (as compared to the downloaders)
     * but aren't interested get unchoked. If they become interested, the
     * downloader with the worst upload rate gets choked. If a client has
     * a complete file, it uses its upload rate rather than its download
     * rate to decide which peers to unchoke.
     */
    unchokedInterested = 0;
    for( i=0; i<size && unchokedInterested<MAX_UNCHOKED_PEERS; ++i ) {
        choke[i].doUnchoke = 1;
        if( choke[i].isInterested )
            ++unchokedInterested;
    }

    /* optimistic unchoke */
    if( i < size )
    {
        int n;
        struct ChokeData * c;
        tr_ptrArray * randPool = tr_ptrArrayNew( );

        for( ; i<size; ++i )
        {
            if( choke[i].isInterested )
            {
                const tr_peer * peer = choke[i].peer;
                int x = 1, y;
                if( isNew( peer ) ) x *= 3;
                if( isSame( peer ) ) x *= 3;
                for( y=0; y<x; ++y )
                    tr_ptrArrayAppend( randPool, &choke[i] );
            }
        }

        if(( n = tr_ptrArraySize( randPool )))
        {
            c = tr_ptrArrayNth( randPool, tr_cryptoWeakRandInt( n ));
            c->doUnchoke = 1;
            t->optimistic = c->peer;
        }

        tr_ptrArrayFree( randPool, NULL );
    }

    for( i=0; i<size; ++i )
        tr_peerMsgsSetChoke( choke[i].peer->msgs, !choke[i].doUnchoke );

    /* cleanup */
    tr_free( choke );
    tr_free( peers );
}

static int
rechokePulse( void * vtorrent )
{
    Torrent * t = vtorrent;

    torrentLock( t );
    rechoke( t );
    torrentUnlock( t );
    return TRUE;
}

/***
****
****  Life and Death
****
***/

static int
shouldPeerBeClosed( const Torrent * t,
                    const tr_peer * peer,
                    int             peerCount )
{
    const tr_torrent *       tor = t->tor;
    const time_t             now = time( NULL );
    const struct peer_atom * atom = getExistingAtom( t, &peer->addr );

    /* if it's marked for purging, close it */
    if( peer->doPurge )
    {
        tordbg( t, "purging peer %s because its doPurge flag is set",
                tr_peerIoAddrStr( &atom->addr, atom->port ) );
        return TRUE;
    }

    /* if we're seeding and the peer has everything we have,
     * and enough time has passed for a pex exchange, then disconnect */
    if( tr_torrentIsSeed( tor ) )
    {
        int peerHasEverything;
        if( atom->flags & ADDED_F_SEED_FLAG )
            peerHasEverything = TRUE;
        else if( peer->progress < tr_cpPercentDone( tor->completion ) )
            peerHasEverything = FALSE;
        else {
            tr_bitfield * tmp = tr_bitfieldDup( tr_cpPieceBitfield( tor->completion ) );
            tr_bitfieldDifference( tmp, peer->have );
            peerHasEverything = tr_bitfieldCountTrueBits( tmp ) == 0;
            tr_bitfieldFree( tmp );
        }

        if( peerHasEverything && ( !tr_torrentAllowsPex(tor) || (now-atom->time>=30 )))
        {
            tordbg( t, "purging peer %s because we're both seeds",
                    tr_peerIoAddrStr( &atom->addr, atom->port ) );
            return TRUE;
        }
    }

    /* disconnect if it's been too long since piece data has been transferred.
     * this is on a sliding scale based on number of available peers... */
    {
        const int relaxStrictnessIfFewerThanN = (int)( ( getMaxPeerCount( tor ) * 0.9 ) + 0.5 );
        /* if we have >= relaxIfFewerThan, strictness is 100%.
         * if we have zero connections, strictness is 0% */
        const float strictness = peerCount >= relaxStrictnessIfFewerThanN
            ? 1.0
            : peerCount / (float)relaxStrictnessIfFewerThanN;
        const int lo = MIN_UPLOAD_IDLE_SECS;
        const int hi = MAX_UPLOAD_IDLE_SECS;
        const int limit = hi - ( ( hi - lo ) * strictness );
        const int idleTime = now - MAX( atom->time, atom->piece_data_time );
/*fprintf( stderr, "strictness is %.3f, limit is %d seconds... time since connect is %d, time since piece is %d ... idleTime is %d, doPurge is %d\n", (double)strictness, limit, (int)(now - atom->time), (int)(now - atom->piece_data_time), idleTime, idleTime > limit );*/
        if( idleTime > limit ) {
            tordbg( t, "purging peer %s because it's been %d secs since we shared anything",
                       tr_peerIoAddrStr( &atom->addr, atom->port ), idleTime );
            return TRUE;
        }
    }

    return FALSE;
}

static tr_peer **
getPeersToClose( Torrent * t, int * setmeSize )
{
    int i, peerCount, outsize;
    tr_peer ** peers = (tr_peer**) tr_ptrArrayPeek( t->peers, &peerCount );
    struct tr_peer ** ret = tr_new( tr_peer *, peerCount );

    assert( torrentIsLocked( t ) );

    for( i = outsize = 0; i < peerCount; ++i )
        if( shouldPeerBeClosed( t, peers[i], peerCount ) )
            ret[outsize++] = peers[i];

    *setmeSize = outsize;
    return ret;
}

static int
compareCandidates( const void * va,
                   const void * vb )
{
    const struct peer_atom * a = *(const struct peer_atom**) va;
    const struct peer_atom * b = *(const struct peer_atom**) vb;

    /* <Charles> Here we would probably want to try reconnecting to
     * peers that had most recently given us data. Lots of users have
     * trouble with resets due to their routers and/or ISPs. This way we
     * can quickly recover from an unwanted reset. So we sort
     * piece_data_time in descending order.
     */

    if( a->piece_data_time != b->piece_data_time )
        return a->piece_data_time < b->piece_data_time ? 1 : -1;

    if( a->numFails != b->numFails )
        return a->numFails < b->numFails ? -1 : 1;

    if( a->time != b->time )
        return a->time < b->time ? -1 : 1;

    /* all other things being equal, prefer peers whose
     * information comes from a more reliable source */
    if( a->from != b->from )
        return a->from < b->from ? -1 : 1;

    return 0;
}

static int
getReconnectIntervalSecs( const struct peer_atom * atom )
{
    int          sec;
    const time_t now = time( NULL );

    /* if we were recently connected to this peer and transferring piece
     * data, try to reconnect to them sooner rather that later -- we don't
     * want network troubles to get in the way of a good peer. */
    if( ( now - atom->piece_data_time ) <= ( MINIMUM_RECONNECT_INTERVAL_SECS * 2 ) )
        sec = MINIMUM_RECONNECT_INTERVAL_SECS;

    /* don't allow reconnects more often than our minimum */
    else if( ( now - atom->time ) < MINIMUM_RECONNECT_INTERVAL_SECS )
        sec = MINIMUM_RECONNECT_INTERVAL_SECS;

    /* otherwise, the interval depends on how many times we've tried
     * and failed to connect to the peer */
    else switch( atom->numFails ) {
        case 0: sec = 0; break;
        case 1: sec = 5; break;
        case 2: sec = 2 * 60; break;
        case 3: sec = 15 * 60; break;
        case 4: sec = 30 * 60; break;
        case 5: sec = 60 * 60; break;
        default: sec = 120 * 60; break;
    }

    return sec;
}

static struct peer_atom **
getPeerCandidates( Torrent * t, int * setmeSize )
{
    int                 i, atomCount, retCount;
    struct peer_atom ** atoms;
    struct peer_atom ** ret;
    const time_t        now = time( NULL );
    const int           seed = tr_torrentIsSeed( t->tor );

    assert( torrentIsLocked( t ) );

    atoms = (struct peer_atom**) tr_ptrArrayPeek( t->pool, &atomCount );
    ret = tr_new( struct peer_atom*, atomCount );
    for( i = retCount = 0; i < atomCount; ++i )
    {
        int                interval;
        struct peer_atom * atom = atoms[i];

        /* peer fed us too much bad data ... we only keep it around
         * now to weed it out in case someone sends it to us via pex */
        if( atom->myflags & MYFLAG_BANNED )
            continue;

        /* peer was unconnectable before, so we're not going to keep trying.
         * this is needs a separate flag from `banned', since if they try
         * to connect to us later, we'll let them in */
        if( atom->myflags & MYFLAG_UNREACHABLE )
            continue;

        /* we don't need two connections to the same peer... */
        if( peerIsInUse( t, &atom->addr ) )
            continue;

        /* no need to connect if we're both seeds... */
        if( seed && ( ( atom->flags & ADDED_F_SEED_FLAG ) ||
                      ( atom->uploadOnly == UPLOAD_ONLY_YES ) ) )
            continue;

        /* don't reconnect too often */
        interval = getReconnectIntervalSecs( atom );
        if( ( now - atom->time ) < interval )
        {
            tordbg( t, "RECONNECT peer %d (%s) is in its grace period of %d seconds..",
                    i, tr_peerIoAddrStr( &atom->addr, atom->port ), interval );
            continue;
        }

        /* Don't connect to peers in our blocklist */
        if( tr_sessionIsAddressBlocked( t->manager->session, &atom->addr ) )
            continue;

        ret[retCount++] = atom;
    }

    qsort( ret, retCount, sizeof( struct peer_atom* ), compareCandidates );
    *setmeSize = retCount;
    return ret;
}

static int
reconnectPulse( void * vtorrent )
{
    Torrent *     t = vtorrent;
    static time_t prevTime = 0;
    static int    newConnectionsThisSecond = 0;
    time_t        now;

    torrentLock( t );

    now = time( NULL );
    if( prevTime != now )
    {
        prevTime = now;
        newConnectionsThisSecond = 0;
    }

    if( !t->isRunning )
    {
        removeAllPeers( t );
    }
    else
    {
        int i, nCandidates, nBad;
        struct peer_atom ** candidates = getPeerCandidates( t, &nCandidates );
        struct tr_peer ** connections = getPeersToClose( t, &nBad );

        if( nBad || nCandidates )
            tordbg( t, "reconnect pulse for [%s]: %d bad connections, "
                    "%d connection candidates, %d atoms, max per pulse is %d",
                    t->tor->info.name, nBad, nCandidates,
                    tr_ptrArraySize( t->pool ),
                    (int)MAX_RECONNECTIONS_PER_PULSE );

        /* disconnect some peers.
           if we transferred piece data, then they might be good peers,
           so reset their `numFails' weight to zero.  otherwise we connected
           to them fruitlessly, so mark it as another fail */
        for( i = 0; i < nBad; ++i ) {
            tr_peer * peer = connections[i];
            struct peer_atom * atom = getExistingAtom( t, &peer->addr );
            if( atom->piece_data_time )
                atom->numFails = 0;
            else
                ++atom->numFails;
            tordbg( t, "removing bad peer %s", tr_peerIoGetAddrStr( peer->io ) );
            removePeer( t, peer );
        }

        /* add some new ones */
        for( i = 0;    ( i < nCandidates )
           && ( i < MAX_RECONNECTIONS_PER_PULSE )
           && ( getPeerCount( t ) < getMaxPeerCount( t->tor ) )
           && ( newConnectionsThisSecond < MAX_CONNECTIONS_PER_SECOND ); ++i )
        {
            tr_peerMgr *       mgr = t->manager;
            struct peer_atom * atom = candidates[i];
            tr_peerIo *        io;

            tordbg( t, "Starting an OUTGOING connection with %s",
                   tr_peerIoAddrStr( &atom->addr, atom->port ) );

            io = tr_peerIoNewOutgoing( mgr->session, &atom->addr, atom->port, t->hash );
            if( io == NULL )
            {
                atom->myflags |= MYFLAG_UNREACHABLE;
            }
            else
            {
                tr_handshake * handshake = tr_handshakeNew( io,
                                                            mgr->session->encryptionMode,
                                                            myHandshakeDoneCB,
                                                            mgr );

                assert( tr_peerIoGetTorrentHash( io ) );

                ++newConnectionsThisSecond;

                tr_ptrArrayInsertSorted( t->outgoingHandshakes, handshake,
                                         handshakeCompare );
            }

            atom->time = time( NULL );
        }

        /* cleanup */
        tr_free( connections );
        tr_free( candidates );
    }

    torrentUnlock( t );
    return TRUE;
}

/****
*****
*****  BANDWIDTH ALLOCATION
*****
****/

static void
pumpAllPeers( tr_peerMgr * mgr )
{
    const int torrentCount = tr_ptrArraySize( mgr->torrents );
    int       i, j;

    for( i=0; i<torrentCount; ++i )
    {
        Torrent * t = tr_ptrArrayNth( mgr->torrents, i );
        for( j=0; j<tr_ptrArraySize( t->peers ); ++j )
        {
            tr_peer * peer = tr_ptrArrayNth( t->peers, j );
            tr_peerMsgsPulse( peer->msgs );
        }
    }
}

static int
bandwidthPulse( void * vmgr )
{
    tr_handshake * handshake;
    tr_peerMgr * mgr = vmgr;
    managerLock( mgr );

    /* FIXME: this next line probably isn't necessary... */
    pumpAllPeers( mgr );

    /* allocate bandwidth to the peers */
    tr_bandwidthAllocate( mgr->session->bandwidth, TR_UP, BANDWIDTH_PERIOD_MSEC );
    tr_bandwidthAllocate( mgr->session->bandwidth, TR_DOWN, BANDWIDTH_PERIOD_MSEC );

    /* free all the finished handshakes */
    while(( handshake = tr_ptrArrayPop( mgr->finishedHandshakes )))
        tr_handshakeFree( handshake );

    managerUnlock( mgr );
    return TRUE;
}
