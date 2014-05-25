/******************************************************************************
 * $Id: net.c 7455 2008-12-22 00:51:14Z charles $
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#ifdef WIN32
 #include <winsock2.h> /* inet_addr */
#else
 #include <arpa/inet.h> /* inet_addr */
 #include <netdb.h>
 #include <fcntl.h>
#endif

#include "libevent/evutil.h"

#include "transmission.h"
#include "fdlimit.h"
#include "natpmp.h"
#include "net.h"
#include "peer-io.h"
#include "platform.h"
#include "utils.h"


void
tr_netInit( void )
{
    static int initialized = FALSE;

    if( !initialized )
    {
#ifdef WIN32
        WSADATA wsaData;
        WSAStartup( MAKEWORD( 2, 2 ), &wsaData );
#endif
        initialized = TRUE;
    }
}

tr_bool
tr_isAddress( const tr_address * a )
{
    return a != NULL; /* this is implemented better in 1.50 */
}


/***********************************************************************
 * DNS resolution
 *
 * Synchronous "resolution": only works with character strings
 * representing numbers expressed in the Internet standard `.' notation.
 * Returns a non-zero value if an error occurs.
 **********************************************************************/
int
tr_netResolve( const char *     address,
               struct in_addr * addr )
{
    addr->s_addr = inet_addr( address );
    return addr->s_addr == 0xFFFFFFFF;
}

/***********************************************************************
 * TCP sockets
 **********************************************************************/

int
tr_netSetTOS( int s,
              int tos )
{
#ifdef IP_TOS
    return setsockopt( s, IPPROTO_IP, IP_TOS, (char*)&tos, sizeof( tos ) );
#else
    return 0;
#endif
}

static int
makeSocketNonBlocking( int fd )
{
    if( fd >= 0 )
    {
#if defined( __BEOS__ )
        int flags = 1;
        if( setsockopt( fd, SOL_SOCKET, SO_NONBLOCK,
                       &flags, sizeof( int ) ) < 0 )
#else
        if( evutil_make_socket_nonblocking( fd ) )
#endif
        {
            tr_err( _( "Couldn't create socket: %s" ),
                   tr_strerror( sockerrno ) );
            tr_netClose( fd );
            fd = -1;
        }
    }

    return fd;
}

static int
createSocket( int type )
{
    return makeSocketNonBlocking( tr_fdSocketCreate( type ) );
}

static void
setSndBuf( tr_session * session UNUSED, int fd UNUSED )
{
#if 0
    if( fd >= 0 )
    {
        const int sndbuf = session->so_sndbuf;
        const int rcvbuf = session->so_rcvbuf;
        setsockopt( fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof( sndbuf ) );
        setsockopt( fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof( rcvbuf ) );
    }
#endif
}

int
tr_netOpenTCP( tr_session            * session,
               const struct in_addr  * addr,
               tr_port_t               port )
{
    int                s;
    struct sockaddr_in sock;
    const int          type = SOCK_STREAM;

    if( ( s = createSocket( type ) ) < 0 )
        return -1;

    setSndBuf( session, s );

    memset( &sock, 0, sizeof( sock ) );
    sock.sin_family      = AF_INET;
    sock.sin_addr.s_addr = addr->s_addr;
    sock.sin_port        = port;

    if( ( connect( s, (struct sockaddr *) &sock,
                  sizeof( struct sockaddr_in ) ) < 0 )
#ifdef WIN32
      && ( sockerrno != WSAEWOULDBLOCK )
#endif
      && ( sockerrno != EINPROGRESS ) )
    {
        tr_err( _(
                   "Couldn't connect socket %d to %s, port %d (errno %d - %s)" ),
               s, inet_ntoa( *addr ), port,
               sockerrno, tr_strerror( sockerrno ) );
        tr_netClose( s );
        s = -1;
    }

    tr_deepLog( __FILE__, __LINE__, NULL, "New OUTGOING connection %d (%s)",
               s, tr_peerIoAddrStr( addr, port ) );

    return s;
}

int
tr_netBindTCP( int port )
{
    int                s;
    struct sockaddr_in sock;
    const int          type = SOCK_STREAM;

#if defined( SO_REUSEADDR ) || defined( SO_REUSEPORT )
    int                optval;
#endif

    if( ( s = createSocket( type ) ) < 0 )
        return -1;

#ifdef SO_REUSEADDR
    optval = 1;
    setsockopt( s, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof( optval ) );
#endif

    memset( &sock, 0, sizeof( sock ) );
    sock.sin_family      = AF_INET;
    sock.sin_addr.s_addr = INADDR_ANY;
    sock.sin_port        = htons( port );

    if( bind( s, (struct sockaddr *) &sock,
             sizeof( struct sockaddr_in ) ) )
    {
        tr_err( _( "Couldn't bind port %d: %s" ), port,
               tr_strerror( sockerrno ) );
        tr_netClose( s );
        return -1;
    }

    tr_dbg(  "Bound socket %d to port %d", s, port );
    return s;
}

int
tr_netAccept( tr_session      * session,
              int               b,
              struct in_addr  * addr,
              tr_port_t       * port )
{
    int fd = makeSocketNonBlocking( tr_fdSocketAccept( b, addr, port ) );
    setSndBuf( session, fd );
    return fd;
}

void
tr_netClose( int s )
{
    tr_fdSocketClose( s );
}

void
tr_netNtop( const struct in_addr * addr,
            char *                 buf,
            int                    len )
{
    const uint8_t * cast;

    cast = (const uint8_t *)addr;
    tr_snprintf( buf, len, "%hhu.%hhu.%hhu.%hhu",
                 cast[0], cast[1], cast[2], cast[3] );
}

int
tr_compareAddresses( const struct in_addr * a, const struct in_addr * b )
{
    if( a->s_addr != b->s_addr )
        return a->s_addr < b->s_addr ? -1 : 1;

    return 0;
}
