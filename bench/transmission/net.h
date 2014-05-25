/******************************************************************************
 * $Id: net.h 7455 2008-12-22 00:51:14Z charles $
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

#ifndef _TR_NET_H_
#define _TR_NET_H_

#ifdef WIN32
 #include <inttypes.h>
 #include <winsock2.h>
 typedef int socklen_t;
 typedef uint16_t tr_port_t;
#elif defined( __BEOS__ )
 #include <sys/socket.h>
 #include <netinet/in.h>
 typedef unsigned short tr_port_t;
 typedef int socklen_t;
#else
 #include <sys/types.h>
 #include <sys/socket.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
 typedef in_port_t tr_port_t;
#endif

#ifdef WIN32
 #define ECONNREFUSED WSAECONNREFUSED
 #define ECONNRESET   WSAECONNRESET
 #define EHOSTUNREACH WSAEHOSTUNREACH
 #define EINPROGRESS  WSAEINPROGRESS
 #define ENOTCONN     WSAENOTCONN
 #define EWOULDBLOCK  WSAEWOULDBLOCK
 #define sockerrno WSAGetLastError( )
#else
 #include <errno.h>
 #define sockerrno errno
#endif

struct in_addr;
struct sockaddr_in;
struct tr_session;

typedef struct in_addr tr_address;

tr_bool tr_isAddress( const tr_address * a );

/***********************************************************************
 * DNS resolution
 **********************************************************************/
int  tr_netResolve( const  char *,
                    struct in_addr * );


/***********************************************************************
 * Sockets
 **********************************************************************/
int  tr_netOpenTCP( struct tr_handle     * session,
                    const struct in_addr * addr,
                    tr_port_t              port );

int  tr_netBindTCP( int port );

int  tr_netAccept( struct tr_handle  * session,
                   int                 bound,
                   struct in_addr    * setme_addr,
                   tr_port_t         * setme_port );

int  tr_netSetTOS( int s,
                   int tos );

void tr_netClose( int s );

void tr_netNtop( const struct in_addr * addr,
                 char *                 buf,
                 int                    len );

int tr_compareAddresses( const struct in_addr * a,
                         const struct in_addr * b );

void tr_netInit( void );

#endif /* _TR_NET_H_ */
