/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id: trevent.h 7175 2008-11-29 16:32:10Z charles $
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_EVENT_H
#define TR_EVENT_H

#include <stddef.h> /* size_t */
#include <inttypes.h> /* uint64_t */

/**
**/

void      tr_eventInit( struct tr_handle * tr_handle );

void      tr_eventClose( struct tr_handle * tr_handle );

struct event_base * tr_eventGetBase( struct tr_handle * tr_handle );


typedef struct tr_timer  tr_timer;

/**
 * Calls timer_func(user_data) after the specified interval.
 * The timer is freed if timer_func returns zero.
 * Otherwise, it's called again after the same interval.
 */
tr_timer* tr_timerNew(
    struct tr_handle *               handle,
    int                 func( void * user_data ),
    void *                           user_data,
    uint64_t
                                     timeout_milliseconds );

/**
 * Frees a timer and sets the timer pointer to NULL.
 */
void      tr_timerFree( tr_timer ** timer );


int       tr_amInEventThread( struct tr_handle * handle );

void      tr_runInEventThread( struct tr_handle *       handle,
                               void               func( void* ),
                               void *                   user_data );

#endif
