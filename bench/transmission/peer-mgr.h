/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id: peer-mgr.h 7455 2008-12-22 00:51:14Z charles $
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_PEER_MGR_H
#define TR_PEER_MGR_H

#include <inttypes.h> /* uint16_t */

#ifdef WIN32
 #include <winsock2.h> /* struct in_addr */
#else
 #include <netinet/in.h> /* struct in_addr */
#endif

struct in_addr;
struct tr_handle;
struct tr_peer_stat;
struct tr_torrent;
typedef struct tr_peerMgr tr_peerMgr;

enum
{
    /* corresponds to ut_pex's added.f flags */
    ADDED_F_ENCRYPTION_FLAG = 1,

    /* corresponds to ut_pex's added.f flags */
    ADDED_F_SEED_FLAG = 2,
};

typedef struct tr_pex
{
    tr_address  addr;
    uint16_t    port;
    uint8_t     flags;
}
tr_pex;

int tr_pexCompare( const void * a, const void * b );

tr_peerMgr* tr_peerMgrNew( struct tr_handle * );

void tr_peerMgrFree( tr_peerMgr * manager );

tr_bool tr_peerMgrPeerIsSeed( const tr_peerMgr      * mgr,
                              const uint8_t         * torrentHash,
                              const struct in_addr  * addr );

void tr_peerMgrAddIncoming( tr_peerMgr     * manager,
                            struct in_addr * addr,
                            uint16_t         port,
                            int              socket );

tr_pex * tr_peerMgrCompactToPex( const void    * compact,
                                 size_t          compactLen,
                                 const uint8_t * added_f,
                                 size_t          added_f_len,
                                 size_t        * setme_pex_count );

void tr_peerMgrAddPex( tr_peerMgr     * manager,
                       const uint8_t  * torrentHash,
                       uint8_t          from,
                       const tr_pex   * pex );

void tr_peerMgrSetBlame( tr_peerMgr        * manager,
                         const uint8_t     * torrentHash,
                         tr_piece_index_t    pieceIndex,
                         int                 success );

int  tr_peerMgrGetPeers( tr_peerMgr      * manager,
                         const uint8_t   * torrentHash,
                         tr_pex         ** setme_pex );

void tr_peerMgrStartTorrent( tr_peerMgr     * manager,
                             const uint8_t  * torrentHash );

void tr_peerMgrStopTorrent( tr_peerMgr      * manager,
                             const uint8_t  * torrentHash );

void tr_peerMgrAddTorrent( tr_peerMgr         * manager,
                           struct tr_torrent  * tor );

void tr_peerMgrRemoveTorrent( tr_peerMgr     * manager,
                              const uint8_t  * torrentHash );

void tr_peerMgrTorrentAvailability( const tr_peerMgr * manager,
                                    const uint8_t    * torrentHash,
                                    int8_t           * tab,
                                    unsigned int       tabCount );

struct tr_bitfield* tr_peerMgrGetAvailable( const tr_peerMgr * manager,
                                            const uint8_t    * torrentHash );

int tr_peerMgrHasConnections( const tr_peerMgr * manager,
                              const uint8_t    * torrentHash );

void tr_peerMgrTorrentStats( const tr_peerMgr * manager,
                             const uint8_t * torrentHash,
                             int * setmePeersKnown,
                             int * setmePeersConnected,
                             int * setmeSeedsConnected,
                             int * setmeWebseedsSendingToUs,
                             int * setmePeersSendingToUs,
                             int * setmePeersGettingFromUs,
                             int * setmePeersFrom ); /* TR_PEER_FROM__MAX */

struct tr_peer_stat* tr_peerMgrPeerStats( const tr_peerMgr  * manager,
                                          const uint8_t     * torrentHash,
                                          int               * setmeCount );

float* tr_peerMgrWebSpeeds( const tr_peerMgr  * manager,
                            const uint8_t     * torrentHash );


struct tr_bitfield * tr_peerMgrGenerateAllowedSet( const uint32_t setCount,
                                                   const uint32_t pieceCount,
                                                   const uint8_t infohash[20],
                                                   const struct in_addr * ip );


#endif
