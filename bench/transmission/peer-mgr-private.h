/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id: peer-mgr-private.h 7455 2008-12-22 00:51:14Z charles $
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_PEER_MGR_PRIVATE_H
#define TR_PEER_MGR_PRIVATE_H

#include <inttypes.h> /* uint16_t */

#ifdef WIN32
 #include <winsock2.h> /* struct in_addr */
#else
 #include <netinet/in.h> /* struct in_addr */
#endif

#include "publish.h" /* tr_publisher_tag */

struct tr_bandwidth;
struct tr_bitfield;
struct tr_peerIo;
struct tr_peermsgs;

enum
{
    ENCRYPTION_PREFERENCE_UNKNOWN,
    ENCRYPTION_PREFERENCE_YES,
    ENCRYPTION_PREFERENCE_NO
};

typedef struct tr_peer
{
    tr_bool                  peerIsChoked;
    tr_bool                  peerIsInterested;
    tr_bool                  clientIsChoked;
    tr_bool                  clientIsInterested;
    tr_bool                  doPurge;

    /* number of bad pieces they've contributed to */
    uint8_t                  strikes;

    uint8_t                  encryption_preference;
    uint16_t                 port;
    struct in_addr           addr;
    struct tr_peerIo       * io;

    struct tr_bitfield     * blame;
    struct tr_bitfield     * have;

    /** how complete the peer's copy of the torrent is. [0.0...1.0] */
    float                    progress;

    /* the client name from the `v' string in LTEP's handshake dictionary */
    char                   * client;

    time_t                   chokeChangedAt;

    struct tr_peermsgs     * msgs;
    tr_publisher_tag         msgsTag;

    struct tr_bandwidth    * bandwidth;
}
tr_peer;

double tr_peerGetPieceSpeed( const tr_peer    * peer,
                             tr_direction       direction );

#endif
