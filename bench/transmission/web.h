/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id: web.h 6795 2008-09-23 19:11:04Z charles $
 */

#ifndef TR_HTTP_H
#define TR_HTTP_H

struct tr_handle;
typedef struct tr_web tr_web;

tr_web*      tr_webInit( struct tr_handle * session );

void         tr_webClose( tr_web ** );

typedef void ( tr_web_done_func )( struct tr_handle * session,
                                   long               response_code,
                                   const void *       response,
                                   size_t             response_byte_count,
                                   void *             user_data );

const char * tr_webGetResponseStr( long response_code );

void         tr_webRun( struct tr_handle * session,
                        const char *       url,
                        const char *       range,
                        tr_web_done_func   done_func,
                        void *             done_func_user_data );


#endif
