/******************************************************************************
 * $Id: ratecontrol.h 7175 2008-11-29 16:32:10Z charles $
 *
 * Copyright (c) 2006-2008 Transmission authors and contributors
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

#ifndef _TR_RATECONTROL_H_
#define _TR_RATECONTROL_H_

enum
{
    TR_RATECONTROL_HISTORY_MSEC = 2000
};

typedef struct tr_ratecontrol tr_ratecontrol;


tr_ratecontrol * tr_rcInit        ( void );

void             tr_rcClose       ( tr_ratecontrol         * ratecontrol );

void             tr_rcTransferred ( tr_ratecontrol         * ratecontrol,
                                    size_t                   byteCount );

float            tr_rcRate        ( const tr_ratecontrol   * ratecontrol );


#endif
