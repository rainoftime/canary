/******************************************************************************
 * $Id: ptrarray.h 7175 2008-11-29 16:32:10Z charles $
 *
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
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

#ifndef _TR_PTR_ARRAY_H_
#define _TR_PTR_ARRAY_H_

/**
 * A simple pointer array that resizes itself dynamically.
 */
typedef struct tr_ptrArray tr_ptrArray;

typedef void ( *PtrArrayForeachFunc )( void * );

tr_ptrArray * tr_ptrArrayNew( void );

tr_ptrArray * tr_ptrArrayDup( tr_ptrArray* );

void          tr_ptrArrayForeach( tr_ptrArray         * array,
                                  PtrArrayForeachFunc   func );

void          tr_ptrArrayFree( tr_ptrArray         * array,
                               PtrArrayForeachFunc   func );

void*         tr_ptrArrayNth( tr_ptrArray   * array,
                              int             nth );

void*         tr_ptrArrayBack( tr_ptrArray  * array );

void**        tr_ptrArrayPeek( tr_ptrArray  * array,
                               int          * size );

void**        tr_ptrArrayBase( tr_ptrArray  * array );

void          tr_ptrArrayClear( tr_ptrArray * array );

int           tr_ptrArrayInsert( tr_ptrArray * array,
                                 void        * insertMe,
                                 int           pos );

int           tr_ptrArrayAppend( tr_ptrArray * array,
                                 void        * appendMe );

void*         tr_ptrArrayPop( tr_ptrArray    * array );

void          tr_ptrArrayErase( tr_ptrArray  * array,
                                int            begin,
                                int            end );

int           tr_ptrArraySize( const tr_ptrArray* );

int           tr_ptrArrayEmpty( const tr_ptrArray* );

int           tr_ptrArrayInsertSorted( tr_ptrArray * array,
                                       void        * value,
                                       int compare(const void*, const void*) );

void*         tr_ptrArrayRemoveSorted( tr_ptrArray * array,
                                       void        * value,
                                       int compare(const void*, const void*) );

void*         tr_ptrArrayFindSorted( tr_ptrArray * array,
                                     const void  * key,
                                     int compare(const void*, const void*) );

#endif
