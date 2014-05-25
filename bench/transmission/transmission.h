/******************************************************************************
 * $Id: transmission.h 7455 2008-12-22 00:51:14Z charles $
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

/*
 * This file defines the public API for the libtransmission library.
 *
 * Other headers suitable for public consumption are bencode.h
 * and utils.h.  Most of the remaining headers in libtransmission
 * should be considered private to libtransmission.
 */
#ifndef TR_TRANSMISSION_H
#define TR_TRANSMISSION_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "version.h"

#include <inttypes.h> /* uintN_t */
#ifndef PRId64
 #define PRId64 "lld"
#endif
#ifndef PRIu64
 #define PRIu64 "llu"
#endif
#include <time.h> /* time_t */

#define SHA_DIGEST_LENGTH 20

typedef uint32_t tr_file_index_t;
typedef uint32_t tr_piece_index_t;
typedef uint64_t tr_block_index_t;
typedef uint16_t tr_port;
typedef uint8_t tr_bool;


/**
 * @brief returns Transmission's default configuration file directory.
 *
 * The default configuration directory is determined this way:
 * 1. If the TRANSMISSION_HOME environmental variable is set, its value is used.
 * 2. On Darwin, "${HOME}/Library/Application Support/Transmission" is used.
 * 3. On Windows, "${CSIDL_APPDATA}/Transmission" is used.
 * 4. If XDG_CONFIG_HOME is set, "${XDG_CONFIG_HOME}/transmission" is used.
 * 5. ${HOME}/.config/transmission" is used as a last resort.
 */
const char* tr_getDefaultConfigDir( void );

typedef struct tr_ctor tr_ctor;
typedef struct tr_handle tr_handle;
typedef struct tr_info tr_info;
typedef struct tr_torrent tr_torrent;
typedef tr_handle tr_session;


/**
 * @addtogroup tr_session Session
 *
 * A libtransmission session is created by calling either tr_sessionInitFull()
 * or tr_sessionInit().  libtransmission creates a thread for itself so that
 * it can operate independently of the caller's event loop.  The session will
 * continue until tr_sessionClose() is called.
 *
 * @{
 */

typedef enum
{
    TR_PROXY_HTTP,
    TR_PROXY_SOCKS4,
    TR_PROXY_SOCKS5
}
tr_proxy_type;

/** @see tr_sessionInitFull */
#define TR_DEFAULT_CONFIG_DIR               tr_getDefaultConfigDir( )
/** @see tr_sessionInitFull */
#ifdef TR_EMBEDDED
 #define TR_DEFAULT_ENCRYPTION              TR_CLEAR_PREFERRED
#else
 #define TR_DEFAULT_ENCRYPTION              TR_ENCRYPTION_PREFERRED
#endif
/** @see tr_sessionInitFull */
#define TR_DEFAULT_PEX_ENABLED              1
/** @see tr_sessionInitFull */
#define TR_DEFAULT_PORT_FORWARDING_ENABLED  0
/** @see tr_sessionInitFull */
#define TR_DEFAULT_PORT                     51413
/** @see tr_sessionInitFull */
#define TR_DEFAULT_PORT_STR                 "51413"
/** @see tr_sessionInitFull */
#define TR_DEFAULT_LAZY_BITFIELD_ENABLED    1
/** @see tr_sessionInitFull */
#define TR_DEFAULT_GLOBAL_PEER_LIMIT        200
/** @see tr_sessionInitFull */
#define TR_DEFAULT_PEER_SOCKET_TOS          8
/** @see tr_sessionInitFull */
#define TR_DEFAULT_PEER_SOCKET_TOS_STR      "8"
/** @see tr_sessionInitFull */
#define TR_DEFAULT_BLOCKLIST_ENABLED        0
/** @see tr_sessionInitFull */
#define TR_DEFAULT_RPC_ENABLED              0
/** @see tr_sessionInitFull */
#define TR_DEFAULT_RPC_PORT                 9091
/** @see tr_sessionInitFull */
#define TR_DEFAULT_RPC_PORT_STR             "9091"
/** @see tr_sessionInitFull */
#define TR_DEFAULT_RPC_WHITELIST            "127.0.0.1"
/** @see tr_sessionInitFull */
#define TR_DEFAULT_RPC_WHITELIST_ENABLED    0
/** @see tr_sessionInitFull */
#define TR_DEFAULT_PROXY_ENABLED            0
/** @see tr_sessionInitFull */
#define TR_DEFAULT_PROXY                    NULL
/** @see tr_sessionInitFull */
#define TR_DEFAULT_PROXY_PORT               80
/** @see tr_sessionInitFull */
#define TR_DEFAULT_PROXY_TYPE               TR_PROXY_HTTP
/** @see tr_sessionInitFull */
#define TR_DEFAULT_PROXY_AUTH_ENABLED       0
/** @see tr_sessionInitFull */
#define TR_DEFAULT_PROXY_USERNAME           NULL
/** @see tr_sessionInitFull */
#define TR_DEFAULT_PROXY_PASSWORD           NULL

typedef enum
{
    TR_CLEAR_PREFERRED,
    TR_ENCRYPTION_PREFERRED,
    TR_ENCRYPTION_REQUIRED
}
tr_encryption_mode;

/**
 * @brief Start a libtransmission session.
 * @return an opaque handle to the new session
 *
 * @param configDir
 *  The config directory where libtransmission config subdirectories
 *  will be found, such as "torrents", "resume", and "blocklists".
 *  #TR_DEFAULT_CONFIG_DIR can be used as a default.
 *
 * @param downloadDir
 *  The default directory to save added torrents.
 *  This can be changed per-session with
 *  tr_sessionSetDownloadDir() and per-torrent with
 *  tr_ctorSetDownloadDir().
 *
 * @param tag
 *  Obsolete.  Only used now for locating legacy fastresume files.
 *  This will be removed at some point in the future.
 *  Valid tags: beos cli daemon gtk macos wx
 *
 * @param isPexEnabled
 *  whether or not PEX is allowed for non-private torrents.
 *  This can be changed per-session with
 *  tr_sessionSetPexEnabled().
 *  #TR_DEFAULT_PEX_ENABLED is the default.
 *
 * @param isPortForwardingEnabled
 *  If true, libtransmission will attempt
 *  to find a local UPnP-enabled or NATPMP-enabled
 *  router and forward a port from there to the local
 *  machine.  This is so remote peers can initiate
 *  connections with us.
 *  #TR_DEFAULT_PORT_FORWARDING_ENABLED is the default.
 *
 * @param publicPort
 *  Port number to open for incoming peer connections.
 *  #TR_DEFAULT_PORT is the default.
 *
 * @param encryptionMode
 *  Must be one of #TR_CLEAR_PREFERRED,
 *  #TR_ENCRYPTION_PREFERRED, or #TR_ENCRYPTION_REQUIRED.
 *
 * @param isUploadLimitEnabled
 *  If true, libtransmission will limit the entire
 *  session's upload speed from "uploadLimit".
 *
 * @param uploadLimit
 *  The speed limit to use for the entire session when
 *  "isUploadLimitEnabled" is true.  Units are KiB/s.
 *
 * @param isDownloadLimitEnabled
 *  If true, libtransmission will limit the entire
 *  session's download speed from "downloadLimit".
 *
 * @param downloadLimit
 *  The speed limit to use for the entire session when
 *  "isDownloadLimitEnabled" is true.  Units are KiB/s.
 *
 * @param peerLimit
 *  The maximum number of peer connections allowed in a session.
 *  #TR_DEFAULT_GLOBAL_PEER_LIMIT can be used as a default.
 *
 * @param messageLevel
 *  Verbosity level of libtransmission's logging mechanism.
 *  Must be one of #TR_MSG_ERR, #TR_MSG_INF, #TR_MSG_DBG.
 *
 * @param isMessageQueueingEnabled
 *  If true, then messages will build up in a queue until
 *  processed by the client application.
 *
 * @param isBlocklistEnabled
 *  If true, then Transmission will not allow peer connections
 *  to the IP addressess specified in the blocklist.
 *
 * @param peerSocketTOS
 *
 * @param rpcIsEnabled
 *  If true, then libtransmission will open an http server port
 *  to listen for incoming RPC requests.
 *
 * @param rpcPort
 *  The port on which to listen for incoming RPC requests
 *
 * @param rpcWhitelist
 *  The list of IP addresses allowed to make RPC connections.
 *  @see tr_sessionSetRPCWhitelist()
 *
 * @see TR_DEFAULT_PEER_SOCKET_TOS
 * @see TR_DEFAULT_BLOCKLIST_ENABLED
 * @see TR_DEFAULT_RPC_ENABLED
 * @see TR_DEFAULT_RPC_PORT
 * @see TR_DEFAULT_RPC_WHITELIST
 * @see TR_DEFAULT_RPC_WHITELIST_ENABLED
 * @see tr_sessionClose()
 */
tr_session * tr_sessionInitFull( const char *       configDir,
                                 const char *       tag,
                                 const char *       downloadDir,
                                 int                isPexEnabled,
                                 int                isPortForwardingEnabled,
                                 int                publicPort,
                                 tr_encryption_mode encryptionMode,
                                 int                useLazyBitfield,
                                 int                useUploadLimit,
                                 int                uploadLimit,
                                 int                useDownloadLimit,
                                 int                downloadLimit,
                                 int                peerLimit,
                                 int                messageLevel,
                                 int                isMessageQueueingEnabled,
                                 int                isBlocklistEnabled,
                                 int                peerSocketTOS,
                                 int                rpcIsEnabled,
                                 uint16_t           rpcPort,
                                 int                rpcWhitelistIsEnabled,
                                 const char *       rpcWhitelist,
                                 int                rpcPasswordIsEnabled,
                                 const char *       rpcUsername,
                                 const char *       rpcPassword,
                                 int                proxyIsEnabled,
                                 const char *       proxy,
                                 int                proxyPort,
                                 tr_proxy_type      proxyType,
                                 int                proxyAuthIsEnabled,
                                 const char *       proxyUsername,
                                 const char *       proxyPassword );


/** @brief Shorter form of tr_sessionInitFull()
    @deprecated Use tr_sessionInitFull() instead. */
tr_session *  tr_sessionInit( const char * configDir,
                              const char * downloadDir,
                              const char * tag );

/** @brief End a libtransmission session
    @see tr_sessionInitFull() */
void         tr_sessionClose( tr_session * );

/**
 * @brief Return the session's configuration directory
 *
 * This is where transmission stores its .torrent files, .resume files,
 * blocklists, etc.
 */
const char * tr_sessionGetConfigDir( const tr_session * );

/**
 * @brief Set the per-session default download folder for new torrents.
 * @see tr_sessionInitFull()
 * @see tr_sessionGetDownloadDir()
 * @see tr_ctorSetDownloadDir()
 */
void tr_sessionSetDownloadDir( tr_session  * session,
                               const char  * downloadDir );

/**
 * @brief Get the default download folder for new torrents.
 *
 * This is set by tr_sessionInitFull() or tr_sessionSetDownloadDir(),
 * and can be overridden on a per-torrent basis by tr_ctorSetDownloadDir().
 */
const char * tr_sessionGetDownloadDir( const tr_session * session );

/**
 * @brief Set whether or not RPC calls are allowed in this session.
 *
 * @details If true, libtransmission will open a server socket to listen
 * for incoming http RPC requests as described in docs/rpc-spec.txt.
 *
 * This is intially set by tr_sessionInitFull() and can be
 * queried by tr_sessionIsRPCEnabled().
 */
void tr_sessionSetRPCEnabled( tr_session  * session,
                              int           isEnabled );

/** @brief Get whether or not RPC calls are allowed in this session.
    @see tr_sessionInitFull()
    @see tr_sessionSetRPCEnabled() */
int  tr_sessionIsRPCEnabled( const tr_session * session );

/** @brief Specify which port to listen for RPC requests on.
    @see tr_sessionInitFull()
    @see tr_sessionGetRPCPort */
void tr_sessionSetRPCPort( tr_session  * session,
                           uint16_t      port );

/** @brief Get which port to listen for RPC requests on.
    @see tr_sessionInitFull()
    @see tr_sessionSetRPCPort */
uint16_t  tr_sessionGetRPCPort( const tr_session * session );

/**
 * @brief Specify a whitelist for remote RPC access
 *
 * The whitelist is a comma-separated list of dotted-quad IP addresses
 * to be allowed.  Wildmat notation is supported, meaning that
 * '?' is interpreted as a single-character wildcard and
 * '*' is interprted as a multi-character wildcard.
 */
void   tr_sessionSetRPCWhitelist( tr_session * session,
                                  const char * whitelist );

/** @brief get the Access Control List for allowing/denying RPC requests.
    @return a comma-separated string of whitelist domains.  tr_free() when done.
    @see tr_sessionInitFull
    @see tr_sessionSetRPCWhitelist */
char* tr_sessionGetRPCWhitelist( const tr_session * );

void  tr_sessionSetRPCWhitelistEnabled( tr_session * session,
                                        int          isEnabled );

int   tr_sessionGetRPCWhitelistEnabled( const tr_session * session );

void  tr_sessionSetRPCPassword( tr_session * session,
                                const char * password );

void  tr_sessionSetRPCUsername( tr_session * session,
                                const char * username );

/** @brief get the password used to restrict RPC requests.
    @return the password string. tr_free() when done.
    @see tr_sessionInitFull()
    @see tr_sessionSetRPCPassword() */
char* tr_sessionGetRPCPassword( const tr_session * session );

char* tr_sessionGetRPCUsername( const tr_session * session  );

void  tr_sessionSetRPCPasswordEnabled( tr_session * session,
                                       int          isEnabled );

int   tr_sessionIsRPCPasswordEnabled( const tr_session * session );


typedef enum
{
    TR_RPC_TORRENT_ADDED,
    TR_RPC_TORRENT_STARTED,
    TR_RPC_TORRENT_STOPPED,
    TR_RPC_TORRENT_REMOVING,
    TR_RPC_TORRENT_CHANGED, /* catch-all for the "torrent-set" rpc method */
    TR_RPC_SESSION_CHANGED
}
tr_rpc_callback_type;

typedef enum
{
    /* no special handling is needed by the caller */
    TR_RPC_OK            = 0,

    /* indicates to the caller that the client will take care of
     * removing the torrent itself.  For example the client may
     * need to keep the torrent alive long enough to cleanly close
     * some resources in another thread. */
    TR_RPC_NOREMOVE   = ( 1 << 1 )
}
tr_rpc_callback_status;

typedef tr_rpc_callback_status (*tr_rpc_func)(tr_session          * session,
                                              tr_rpc_callback_type  type,
                                              struct tr_torrent   * tor_or_null,
                                              void                * user_data );

/**
 * Register to be notified whenever something is changed via RPC,
 * such as a torrent being added, removed, started, stopped, etc.
 *
 * func is invoked FROM LIBTRANSMISSION'S THREAD!
 * This means func must be fast (to avoid blocking peers),
 * shouldn't call libtransmission functions (to avoid deadlock),
 * and shouldn't modify client-level memory without using a mutex!
 */
void tr_sessionSetRPCCallback( tr_session   * session,
                               tr_rpc_func    func,
                               void         * user_data );

/**
***
**/

int           tr_sessionIsProxyEnabled( const tr_session * );

int           tr_sessionIsProxyAuthEnabled( const tr_session * );

const char*   tr_sessionGetProxy( const tr_session * );

int           tr_sessionGetProxyPort( const tr_session * );

tr_proxy_type tr_sessionGetProxyType( const tr_session * );

const char*   tr_sessionGetProxyUsername( const tr_session * );

const char*   tr_sessionGetProxyPassword( const tr_session * );

void          tr_sessionSetProxyEnabled(                     tr_session *,
                                                         int isEnabled );

void          tr_sessionSetProxyAuthEnabled(                     tr_session *,
                                                             int isEnabled );

void          tr_sessionSetProxy(
    tr_session *,
    const char *
    proxy );

void          tr_sessionSetProxyPort(                     tr_session *,
                                                      int port );

void          tr_sessionSetProxyType( tr_session *,
                                      tr_proxy_type );

void          tr_sessionSetProxyUsername(
    tr_session *,
    const char *
    username );

void          tr_sessionSetProxyPassword(
    tr_session *,
    const char *
    password );

/**
***
**/

typedef struct tr_session_stats
{
    float       ratio;        /* TR_RATIO_INF, TR_RATIO_NA, or total up/down */
    uint64_t    uploadedBytes; /* total up */
    uint64_t    downloadedBytes; /* total down */
    uint64_t    filesAdded;   /* number of files added */
    uint64_t    sessionCount; /* program started N times */
    uint64_t    secondsActive; /* how long Transmisson's been running */
}
tr_session_stats;

/* stats from the current session. */
void               tr_sessionGetStats( const tr_session * session,
                                       tr_session_stats * setme );

/* stats from the current and past sessions. */
void               tr_sessionGetCumulativeStats( const tr_session * session,
                                                 tr_session_stats * setme );

void               tr_sessionClearStats( tr_session * session );

/**
 * Set whether or not torrents are allowed to do peer exchanges.
 * PEX is always disabled in private torrents regardless of this.
 * In public torrents, PEX is enabled by default.
 */
void               tr_sessionSetPexEnabled( tr_session  * session,
                                            int           isEnabled );

int                tr_sessionIsPexEnabled( const tr_session * session );

void               tr_sessionSetLazyBitfieldEnabled( tr_session * session,
                                                     int          enabled );

int                tr_sessionIsLazyBitfieldEnabled( const tr_session * session );

tr_encryption_mode tr_sessionGetEncryption( tr_session * session );

void               tr_sessionSetEncryption( tr_session          * session,
                                            tr_encryption_mode    mode );


/***********************************************************************
** Incoming Peer Connections Port
*/

void  tr_sessionSetPortForwardingEnabled( tr_session  * session,
                                          int           enabled );

int   tr_sessionIsPortForwardingEnabled( const tr_session  * session );

void  tr_sessionSetPeerPort( tr_session  * session,
                             int           port);

int   tr_sessionGetPeerPort( const tr_session * session );

typedef enum
{
    TR_PORT_ERROR,
    TR_PORT_UNMAPPED,
    TR_PORT_UNMAPPING,
    TR_PORT_MAPPING,
    TR_PORT_MAPPED
}
tr_port_forwarding;

tr_port_forwarding tr_sessionGetPortForwarding( const tr_session * session );

int                tr_sessionCountTorrents( const tr_session * session );

typedef enum
{
    TR_CLIENT_TO_PEER = 0, TR_UP = 0,
    TR_PEER_TO_CLIENT = 1, TR_DOWN = 1
}
tr_direction;

void       tr_sessionSetSpeedLimitEnabled ( tr_session        * session,
                                            tr_direction        direction,
                                            tr_bool             isEnabled );

tr_bool    tr_sessionIsSpeedLimitEnabled  ( const tr_session  * session,
                                            tr_direction        direction );

void       tr_sessionSetSpeedLimit        ( tr_session        * session,
                                            tr_direction        direction,
                                            int                 KiB_sec );

int        tr_sessionGetSpeedLimit        ( const tr_session  * session,
                                            tr_direction        direction );

double     tr_sessionGetRawSpeed          ( const tr_session  * session,
                                           tr_direction         direction );

double     tr_sessionGetPieceSpeed        ( const tr_session  * session,
                                            tr_direction        direction );


void       tr_sessionSetPeerLimit( tr_session  * session,
                                   uint16_t      maxGlobalPeers );

uint16_t   tr_sessionGetPeerLimit( const tr_session * session );


/**
 *  Load all the torrents in tr_getTorrentDir().
 *  This can be used at startup to kickstart all the torrents
 *  from the previous session.
 */
tr_torrent ** tr_sessionLoadTorrents( tr_session  * session,
                                      tr_ctor     * ctor,
                                      int         * setmeCount );

/** @} */

/**
***
**/


/***********************************************************************
** Message Logging
*/

enum
{
    TR_MSG_ERR = 1,
    TR_MSG_INF = 2,
    TR_MSG_DBG = 3
};
void tr_setMessageLevel( int );

int  tr_getMessageLevel( void );

typedef struct tr_msg_list
{
    /* TR_MSG_ERR, TR_MSG_INF, or TR_MSG_DBG */
    uint8_t level;

    /* The line number in the source file where this message originated */
    int line;

    /* Time the message was generated */
    time_t when;

    /* The torrent associated with this message,
     * or a module name such as "Port Forwarding" for non-torrent messages,
     * or NULL. */
    char *  name;

    /* The message */
    char *  message;

    /* The source file where this message originated */
    const char * file;

    /* linked list of messages */
    struct tr_msg_list * next;
}
tr_msg_list;

void          tr_setMessageQueuing( int isEnabled );

int           tr_getMessageQueuing( void );

tr_msg_list * tr_getQueuedMessages( void );

void          tr_freeMessageList( tr_msg_list * freeme );

/** @addtogroup Blocklists
    @{ */

/**
 * Specify a range of IPs for Transmission to block.
 *
 * filename must be an uncompressed ascii file,
 * using the same format as the bluetack level1 file.
 *
 * libtransmission does not keep a handle to `filename'
 * after this call returns, so the caller is free to
 * keep or delete `filename' as it wishes.
 * libtransmission makes its own copy of the file
 * massaged into a format easier to search.
 *
 * The caller only needs to invoke this when the blocklist
 * has changed.
 *
 * Passing NULL for a filename will clear the blocklist.
 */
int  tr_blocklistSetContent( tr_session * session,
                             const char * filename );

int  tr_blocklistGetRuleCount( const tr_session * session );

int  tr_blocklistExists( const tr_session * session );

int  tr_blocklistIsEnabled( const tr_session * session );

void tr_blocklistSetEnabled( tr_session * session,
                             int          isEnabled );


/** @} */


/** @addtogroup tr_ctor Torrent Instantiation
    @{

    Instantiating a tr_torrent had gotten more complicated as features were
    added.  At one point there were four functions to check metainfo and five
    to create tr_torrent.

    To remedy this, a Torrent Constructor (struct tr_ctor) has been introduced:
    - Simplifies the API to two functions: tr_torrentParse() and tr_torrentNew()
    - You can set the fields you want; the system sets defaults for the rest.
    - You can specify whether or not your fields should supercede resume's.
    - We can add new features to tr_ctor without breaking tr_torrentNew()'s API.

    All the tr_ctor{Get,Set}*() functions with a return value return
    an error number, or zero if no error occurred.

    You must call one of the SetMetainfo() functions before creating
    a torrent with a tr_ctor.  The other functions are optional.

    You can reuse a single tr_ctor to create a batch of torrents --
    just call one of the SetMetainfo() functions between each
    tr_torrentNew() call.

    Every call to tr_ctorSetMetainfo*() frees the previous metainfo.
 */

typedef enum
{
    TR_FALLBACK, /* indicates the ctor value should be used only
                    in case of missing resume settings */

    TR_FORCE, /* indicates the ctor value should be used
                 regardless of what's in the resume settings */
}
tr_ctorMode;

struct tr_benc;

tr_ctor*    tr_ctorNew( const tr_session * session );

void        tr_ctorFree( tr_ctor * ctor );

void        tr_ctorSetDeleteSource( tr_ctor * ctor,
                                    uint8_t   doDelete );

int         tr_ctorSetMetainfo( tr_ctor *       ctor,
                                const uint8_t * metainfo,
                                size_t          len );

int         tr_ctorSetMetainfoFromFile( tr_ctor *    ctor,
                                        const char * filename );

int         tr_ctorSetMetainfoFromHash( tr_ctor *    ctor,
                                        const char * hashString );

/** Set the maximum number of peers this torrent can connect to.
    (Default: 50) */
void        tr_ctorSetPeerLimit( tr_ctor *   ctor,
                                 tr_ctorMode mode,
                                 uint16_t    peerLimit  );

/** Set the download folder for the torrent being added with this ctor.
    @see tr_ctorSetDownloadDir()
    @see tr_sessionInitFull() */
void        tr_ctorSetDownloadDir( tr_ctor *    ctor,
                                   tr_ctorMode  mode,
                                   const char * directory );

/** Set whether or not the torrent begins downloading/seeding when created.
    (Default: not paused) */
void        tr_ctorSetPaused( tr_ctor *   ctor,
                              tr_ctorMode mode,
                              uint8_t     isPaused );

int         tr_ctorGetPeerLimit( const tr_ctor * ctor,
                                 tr_ctorMode     mode,
                                 uint16_t *      setmeCount );

int         tr_ctorGetPaused( const tr_ctor * ctor,
                              tr_ctorMode     mode,
                              uint8_t *       setmeIsPaused );

int         tr_ctorGetDownloadDir( const tr_ctor  * ctor,
                                   tr_ctorMode      mode,
                                   const char    ** setmeDownloadDir );

int         tr_ctorGetMetainfo( const tr_ctor         * ctor,
                                const struct tr_benc ** setme );

int         tr_ctorGetDeleteSource( const tr_ctor  * ctor,
                                    uint8_t        * setmeDoDelete );

/* returns NULL if tr_ctorSetMetainfoFromFile() wasn't used */
const char* tr_ctorGetSourceFile( const tr_ctor * ctor );

#define TR_EINVALID     1
#define TR_EDUPLICATE   2

/**
 * Parses the specified metainfo.
 * Returns 0 if it parsed successfully and can be added to Transmission.
 * Returns TR_EINVALID if it couldn't be parsed.
 * Returns TR_EDUPLICATE if it parsed but can't be added.
 *     "download-dir" must be set to test for TR_EDUPLICATE.
 *
 * If setme_info is non-NULL and parsing is successful
 * (that is, if TR_EINVALID is not returned), then the parsed
 * metainfo is stored in setme_info and should be freed by the
 * caller via tr_metainfoFree().
 */
int tr_torrentParse( const tr_session  * session,
                     const tr_ctor     * ctor,
                     tr_info           * setme_info_or_NULL );

/** Instantiate a single torrent.
    @return 0 on success,
            TR_EINVALID if the torrent couldn't be parsed, or
            TR_EDUPLICATE if there's already a matching torrent object. */
tr_torrent * tr_torrentNew( tr_session      * session,
                            const tr_ctor   * ctor,
                            int             * setmeError );

/** @} */

/***********************************************************************
 ***
 ***  TORRENTS
 **/

/** @addtogroup tr_torrent Torrents
    @{ */

/** @brief Frees memory allocated by tr_torrentNew().
           Running torrents are stopped first.  */
void tr_torrentFree( tr_torrent * torrent );

/** @brief Removes our .torrent and .resume files for
           this torrent, then calls tr_torrentFree(). */
void tr_torrentRemove( tr_torrent * torrent );

/** @brief Start a torrent */
void tr_torrentStart( tr_torrent * torrent );

/** @brief Stop (pause) a torrent */
void tr_torrentStop( tr_torrent * torrent );

/**
 * @brief Iterate through the torrents.
 *
 * Pass in a NULL pointer to get the first torrent.
 */
tr_torrent* tr_torrentNext( tr_session  * session,
                            tr_torrent  * current );


uint64_t tr_torrentGetBytesLeftToAllocate( const tr_torrent * torrent );

/**
 * @brief Returns this torrent's unique ID.
 *
 * IDs are good as simple lookup keys, but are not persistent
 * between sessions.  If you need that, use tr_info.hash or
 * tr_info.hashString.
 */
int tr_torrentId( const tr_torrent * torrent );

/****
*****  Speed Limits
****/

typedef enum
{
    TR_SPEEDLIMIT_GLOBAL,    /* only follow the overall speed limit */
    TR_SPEEDLIMIT_SINGLE,    /* only follow the per-torrent limit */
    TR_SPEEDLIMIT_UNLIMITED  /* no limits at all */
}
tr_speedlimit;

void          tr_torrentSetSpeedMode( tr_torrent     * tor,
                                      tr_direction     up_or_down,
                                      tr_speedlimit    mode );

tr_speedlimit tr_torrentGetSpeedMode( const tr_torrent * tor,
                                      tr_direction       direction );

void          tr_torrentSetSpeedLimit( tr_torrent    * tor,
                                       tr_direction    up_or_down,
                                       int             KiB_sec );

int           tr_torrentGetSpeedLimit( const tr_torrent  * tor,
                                       tr_direction        direction );

/****
*****  Peer Limits
****/

void          tr_torrentSetPeerLimit( tr_torrent * tor,
                                      uint16_t     peerLimit );

uint16_t      tr_torrentGetPeerLimit( const tr_torrent * tor );

/****
*****  File Priorities
****/

enum
{
    TR_PRI_LOW    = -1,
    TR_PRI_NORMAL =  0, /* since NORMAL is 0, memset initializes nicely */
    TR_PRI_HIGH   =  1
};

typedef int8_t tr_priority_t;

/**
 * @brief Set a batch of files to a particular priority.
 *
 * @param priority must be one of TR_PRI_NORMAL, _HIGH, or _LOW
 */
void tr_torrentSetFilePriorities( tr_torrent       * torrent,
                                  tr_file_index_t  * files,
                                  tr_file_index_t    fileCount,
                                  tr_priority_t      priority );

/**
 * @brief Get this torrent's file priorities.
 *
 * @return A malloc()ed array of tor->info.fileCount items,
 *         each holding a TR_PRI_NORMAL, TR_PRI_HIGH, or TR_PRI_LOW.
 *         It's the caller's responsibility to free() this.
 */
tr_priority_t*  tr_torrentGetFilePriorities( const tr_torrent * torrent );

/**
 * @brief Single-file form of tr_torrentGetFilePriorities.
 * @return TR_PRI_NORMAL, TR_PRI_HIGH, or TR_PRI_LOW.
 */
tr_priority_t   tr_torrentGetFilePriority( const tr_torrent  * torrent,
                                           tr_file_index_t     file );

/**
 * @brief See if a file's `download' flag is set.
 * @return true if the file's `download' flag is set.
 */
int tr_torrentGetFileDL( const tr_torrent  * torrent,
                         tr_file_index_t     file );

/** @brief Set a batch of files to be downloaded or not. */
void            tr_torrentSetFileDLs( tr_torrent       * torrent,
                                      tr_file_index_t  * files,
                                      tr_file_index_t    fileCount,
                                      int                do_download );


const tr_info * tr_torrentInfo( const tr_torrent * torrent );

void tr_torrentSetDownloadDir( tr_torrent  * torrent,
                               const char  * path );

const char * tr_torrentGetDownloadDir( const tr_torrent * torrent );

/**
***
**/

typedef struct tr_tracker_info
{
    int     tier;
    char *  announce;
    char *  scrape;
}
tr_tracker_info;

/**
 * @brief Modify a torrent's tracker list.
 *
 * This updates both the `torrent' object's tracker list
 * and the metainfo file in tr_sessionGetConfigDir()'s torrent subdirectory.
 *
 * @param torrent The torrent whose tracker list is to be modified
 * @param trackers An array of trackers, sorted by tier from first to last.
 *                 NOTE: only the `tier' and `announce' fields are used.
 *                 libtransmission derives `scrape' from `announce'.
 * @param trackerCount size of the `trackers' array
 */
void tr_torrentSetAnnounceList( tr_torrent *            torrent,
                                const tr_tracker_info * trackers,
                                int                     trackerCount );


/**
***
**/

typedef enum
{
    TR_CP_INCOMPLETE,   /* doesn't have all the desired pieces */
    TR_CP_DONE,         /* has all the desired pieces, but not all pieces */
    TR_CP_COMPLETE      /* has every piece */
}
tr_completeness;

typedef void ( tr_torrent_completeness_func )( tr_torrent       * torrent,
                                               tr_completeness    completeness,
                                               void             * user_data );

/**
 * Register to be notified whenever a torrent's "completeness"
 * changes.  This will be called, for example, when a torrent
 * finishes downloading and changes from TR_CP_INCOMPLETE to
 * either TR_CP_COMPLETE or TR_CP_DONE.
 *
 * func is invoked FROM LIBTRANSMISSION'S THREAD!
 * This means func must be fast (to avoid blocking peers),
 * shouldn't call libtransmission functions (to avoid deadlock),
 * and shouldn't modify client-level memory without using a mutex!
 *
 * @see tr_completeness
 */
void tr_torrentSetCompletenessCallback(
         tr_torrent                    * torrent,
         tr_torrent_completeness_func    func,
         void                          * user_data );

void tr_torrentClearCompletenessCallback( tr_torrent * torrent );


/**
 * MANUAL ANNOUNCE
 *
 * Trackers usually set an announce interval of 15 or 30 minutes.
 * Users can send one-time announce requests that override this
 * interval by calling tr_torrentManualUpdate().
 *
 * The wait interval for tr_torrentManualUpdate() is much smaller.
 * You can test whether or not a manual update is possible
 * (for example, to desensitize the button) by calling
 * tr_torrentCanManualUpdate().
 */

void tr_torrentManualUpdate( tr_torrent * torrent );

int  tr_torrentCanManualUpdate( const tr_torrent * torrent );

/***********************************************************************
* tr_torrentPeers
***********************************************************************/

typedef struct tr_peer_stat
{
    tr_bool      isEncrypted;
    tr_bool      isDownloadingFrom;
    tr_bool      isUploadingTo;
    tr_bool      isSeed;

    tr_bool      peerIsChoked;
    tr_bool      peerIsInterested;
    tr_bool      clientIsChoked;
    tr_bool      clientIsInterested;
    tr_bool      isIncoming;

    uint8_t      from;
    tr_port      port;

    char         addr[16];
    char         client[80];
    char         flagStr[32];

    float        progress;
    float        rateToPeer;
    float        rateToClient;
}
tr_peer_stat;

tr_peer_stat * tr_torrentPeers( const tr_torrent * torrent,
                                int *              peerCount );

void           tr_torrentPeersFree( tr_peer_stat * peerStats,
                                    int            peerCount );

/**
 * @brief get the download speeds for each of this torrent's webseed sources.
 *
 * @return an array of tor->info.webseedCount floats giving download speeds.
 *         Each speed in the array corresponds to the webseed at the same
 *         array index in tor->info.webseeds.
 *         To differentiate "idle" and "stalled" status, idle webseeds will
 *         return -1 instead of 0 KiB/s.
 *         NOTE: always free this array with tr_free() when you're done with it.
 */
float*         tr_torrentWebSpeeds( const tr_torrent * torrent );

typedef struct tr_file_stat
{
    uint64_t    bytesCompleted;
    float       progress;
}
tr_file_stat;

tr_file_stat * tr_torrentFiles( const tr_torrent  * torrent,
                                tr_file_index_t   * fileCount );

void tr_torrentFilesFree( tr_file_stat     * files,
                          tr_file_index_t    fileCount );


/***********************************************************************
 * tr_torrentAvailability
 ***********************************************************************
 * Use this to draw an advanced progress bar which is 'size' pixels
 * wide. Fills 'tab' which you must have allocated: each byte is set
 * to either -1 if we have the piece, otherwise it is set to the number
 * of connected peers who have the piece.
 **********************************************************************/
void tr_torrentAvailability( const tr_torrent  * torrent,
                             int8_t            * tab,
                             int                  size );

void tr_torrentAmountFinished( const tr_torrent  * torrent,
                               float *             tab,
                               int                 size );

void tr_torrentVerify( tr_torrent * torrent );

/***********************************************************************
 * tr_info
 **********************************************************************/

typedef struct tr_file
{
    uint64_t            length;    /* Length of the file, in bytes */
    char *              name;      /* Path to the file */
    int8_t              priority;  /* TR_PRI_HIGH, _NORMAL, or _LOW */
    int8_t              dnd;       /* nonzero if the file shouldn't be
                                     downloaded */
    tr_piece_index_t    firstPiece; /* We need pieces [firstPiece... */
    tr_piece_index_t    lastPiece; /* ...lastPiece] to dl this file */
    uint64_t            offset;    /* file begins at the torrent's nth byte */
}
tr_file;

typedef struct tr_piece
{
    uint8_t    hash[SHA_DIGEST_LENGTH]; /* pieces hash */
    int8_t     priority;               /* TR_PRI_HIGH, _NORMAL, or _LOW */
    int8_t     dnd;                    /* nonzero if the piece shouldn't be
                                         downloaded */
}
tr_piece;

struct tr_info
{
    /* Flags */
    unsigned int       isPrivate   : 1;
    unsigned int       isMultifile : 1;

    /* General info */
    uint8_t            hash[SHA_DIGEST_LENGTH];
    char               hashString[2 * SHA_DIGEST_LENGTH + 1];
    char            *  name;

    /* Path to torrent Transmission's internal copy of the .torrent file.
       This field exists for compatability reasons in the Mac OS X client
       and should not be used in new code. */
    char            *  torrent;

    /* these trackers are sorted by tier */
    tr_tracker_info *  trackers;
    int                trackerCount;

    char           **  webseeds;
    int                webseedCount;

    /* Torrent info */
    char             * comment;
    char             * creator;
    time_t             dateCreated;

    /* Pieces info */
    uint32_t           pieceSize;
    tr_piece_index_t   pieceCount;
    uint64_t           totalSize;
    tr_piece *         pieces;

    /* Files info */
    tr_file_index_t    fileCount;
    tr_file *          files;
};

/**
 * What the torrent is doing right now.
 *
 * Note: these values will become a straight enum at some point in the future.
 * Do not rely on their current `bitfield' implementation
 */
typedef enum
{
    TR_STATUS_CHECK_WAIT   = ( 1 << 0 ), /* Waiting in queue to check files */
    TR_STATUS_CHECK        = ( 1 << 1 ), /* Checking files */
    TR_STATUS_DOWNLOAD     = ( 1 << 2 ), /* Downloading */
    TR_STATUS_SEED         = ( 1 << 3 ), /* Seeding */
    TR_STATUS_STOPPED      = ( 1 << 4 )  /* Torrent is stopped */
}
tr_torrent_activity;

tr_torrent_activity tr_torrentGetActivity( tr_torrent * );

#define TR_STATUS_IS_ACTIVE( s ) ( ( s ) != TR_STATUS_STOPPED )

typedef enum
{
    TR_LOCKFILE_SUCCESS = 0,
    TR_LOCKFILE_EOPEN,
    TR_LOCKFILE_ELOCK
}
tr_lockfile_state_t;

enum
{
    TR_PEER_FROM_INCOMING  = 0,  /* connections made to the listening port */
    TR_PEER_FROM_TRACKER   = 1,  /* peers received from a tracker */
    TR_PEER_FROM_CACHE     = 2,  /* peers read from the peer cache */
    TR_PEER_FROM_PEX       = 3,  /* peers discovered via PEX */
    TR_PEER_FROM__MAX
};

/** Can be used as a mnemonic for "no error" errno */
#define TR_OK 0

/**
 * The current status of a torrent.
 * @see tr_torrentStat()
 */
typedef struct tr_stat
{
    /** The torrent's unique Id.
        @see tr_torrentId() */
    int    id;

    /** What is this torrent doing right now? */
    tr_torrent_activity activity;

    /** Our current announce URL, or NULL if none.
        This URL may change during the session if the torrent's
        metainfo has multiple trackers and the current one
        becomes unreachable. */
    char *  announceURL;

    /** Our current scrape URL, or NULL if none.
        This URL may change during the session if the torrent's
        metainfo has multiple trackers and the current one
        becomes unreachable. */
    char *  scrapeURL;

    /** The errno status for this torrent.  0 means everything's fine. */
    int     error;

    /** Typically an error string returned from the tracker. */
    char    errorString[128];

    /** When tr_stat.status is TR_STATUS_CHECK or TR_STATUS_CHECK_WAIT,
        this is the percentage of how much of the files has been
        verified.  When it gets to 1, the verify process is done.
        Range is [0..1]
        @see tr_stat.status */
    float    recheckProgress;

    /** How much has been downloaded of the entire torrent.
        Range is [0..1] */
    float    percentComplete;

    /** How much has been downloaded of the files the user wants.  This differs
        from percentComplete if the user wants only some of the torrent's files.
        Range is [0..1]
        @see tr_stat.leftUntilDone */
    float    percentDone;

    /** Speed all data being sent for this torrent. (KiB/s)
        This includes piece data, protocol messages, and TCP overhead */
    double rawUploadSpeed;

    /** Speed all data being received for this torrent. (KiB/s)
        This includes piece data, protocol messages, and TCP overhead */
    double rawDownloadSpeed;

    /** Speed all piece being sent for this torrent. (KiB/s)
        This ONLY counts piece data. */
    double pieceUploadSpeed;

    /** Speed all piece being received for this torrent. (KiB/s)
        This ONLY counts piece data. */
    double pieceDownloadSpeed;

#define TR_ETA_NOT_AVAIL -1
#define TR_ETA_UNKNOWN -2
    /** Estimated number of seconds left until the torrent is done,
        or TR_ETA_NOT_AVAIL or TR_ETA_UNKNOWN */
    int    eta;

    /** Number of peers that the tracker says this torrent has */
    int    peersKnown;

    /** Number of peers that we're connected to */
    int    peersConnected;

    /** How many peers we found out about from the tracker, or from pex,
        or from incoming connections, or from our resume file. */
    int    peersFrom[TR_PEER_FROM__MAX];

    /** Number of peers that are sending data to us. */
    int    peersSendingToUs;

    /** Number of peers that we're sending data to */
    int    peersGettingFromUs;

    /** Number of webseeds that are sending data to us. */
    int    webseedsSendingToUs;

    /** Number of seeders that the tracker says this torrent has */
    int    seeders;

    /** Number of leechers that the tracker says this torrent has */
    int    leechers;

    /** Number of finished downloads that the tracker says torrent has */
    int    timesCompleted;

    /** Byte count of all the piece data we'll have downloaded when we're done,
        whether or not we have it yet. [0...tr_info.totalSize] */
    uint64_t    sizeWhenDone;

    /** Byte count of how much data is left to be downloaded until
        we're done -- that is, until we've got all the pieces we wanted.
        [0...tr_info.sizeWhenDone] */
    uint64_t    leftUntilDone;

    /** Byte count of all the piece data we want and don't have yet,
        but that a connected peer does have. [0...leftUntilDone] */
    uint64_t    desiredAvailable;

    /** Byte count of all the corrupt data you've ever downloaded for
        this torrent.  If you're on a poisoned torrent, this number can
        grow very large. */
    uint64_t    corruptEver;

    /** Byte count of all data you've ever uploaded for this torrent. */
    uint64_t    uploadedEver;

    /** Byte count of all the non-corrupt data you've ever downloaded
        for this torrent.  If you deleted the files and downloaded a second
        time, this will be 2*totalSize.. */
    uint64_t    downloadedEver;

    /** Byte count of all the checksum-verified data we have for this torrent.
      */
    uint64_t    haveValid;

    /** Byte count of all the partial piece data we have for this torrent.
        As pieces become complete, this value may decrease as portions of it
        are moved to `corrupt' or `haveValid'. */
    uint64_t    haveUnchecked;

    /** This is the unmodified string returned by the tracker in response
        to the torrent's most recent scrape request.  If no request was
        sent or there was no response, this string is empty. */
    char    scrapeResponse[64];

    /** The unmodified string returned by the tracker in response
        to the torrent's most recent scrape request.  If no request was
        sent or there was no response, this string is empty. */
    char    announceResponse[64];

    /** Time the most recent scrape request was sent,
        or zero if one hasn't been sent yet. */
    time_t    lastScrapeTime;

    /** Time when the next scrape request will be sent,
        or 0 if an error has occured that stops scraping,
        or 1 if a scrape is currently in progress s.t.
        we haven't set a timer for the next one yet. */
    time_t    nextScrapeTime;

    /** Time the most recent announce request was sent,
        or zero if one hasn't been sent yet. */
    time_t    lastAnnounceTime;

    /** Time when the next reannounce request will be sent,
        or 0 if the torrent is stopped,
        or 1 if an announce is currently in progress s.t.
        we haven't set a timer for the next one yet */
    time_t    nextAnnounceTime;

    /** If the torrent is running, this is the time at which
        the client can manually ask the torrent's tracker
        for more peers,
        or 0 if the torrent is stopped or doesn't allow manual,
        or 1 if an announce is currently in progress s.t.
        we haven't set a timer for the next one yet */
    time_t    manualAnnounceTime;

    /** A very rough estimate in KiB/s of how quickly data is being
        passed around between all the peers we're connected to.
        Don't put too much weight in this number. */
    float    swarmSpeed;

#define TR_RATIO_NA  -1
#define TR_RATIO_INF -2
    /** TR_RATIO_INF, TR_RATIO_NA, or a regular ratio */
    float    ratio;

    /** When the torrent was first added. */
    time_t    addedDate;

    /** When the torrent finished downloading. */
    time_t    doneDate;

    /** When the torrent was last started. */
    time_t    startDate;

    /** The last time we uploaded or downloaded piece data on this torrent. */
    time_t    activityDate;
}
tr_stat;

/** Return a pointer to an tr_stat structure with updated information
    on the torrent.  This is typically called by the GUI clients every
    second or so to get a new snapshot of the torrent's status. */
const tr_stat * tr_torrentStat( tr_torrent * torrent );

/** Like tr_torrentStat(), but only recalculates the statistics if it's
    been longer than a second since they were last calculated.  This can
    reduce the CPU load if you're calling tr_torrentStat() frequently. */
const tr_stat * tr_torrentStatCached( tr_torrent * torrent );

/** @deprecated this method will be removed in 1.40 */
void tr_torrentSetAddedDate( tr_torrent * torrent,
                             time_t       addedDate );

/** @deprecated this method will be removed in 1.40 */
void tr_torrentSetActivityDate( tr_torrent * torrent,
                                time_t       activityDate );

/** @deprecated this method will be removed in 1.40 */
void tr_torrentSetDoneDate( tr_torrent  * torrent,
                            time_t        doneDate );

/** @brief Sanity checker to test that the direction is TR_UP or TR_DOWN */
tr_bool tr_isDirection( tr_direction );

/** @} */

#ifdef __TRANSMISSION__
 #include "session.h"
#endif

#ifdef __cplusplus
}
#endif

#endif
