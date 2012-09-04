/*****************************************************************************
 * vsl.h: video segment list access
 *****************************************************************************
 * Copyright (C) 2012 Rui Zhang
 *
 * Author: Rui Zhang <bbcallen _AT_ gmail _DOT_ com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifndef VSL_H
#define VSL_H

#define ACCESS_VSL_INDEX        "vslindex"
#define ACCESS_VSL_SEGMENT      "vslsegment"

#define ACCESS_SINA_INDEX       "sinaindex"
#define ACCESS_SINA_SEGMENT     "sinasegment"

#define ACCESS_YOUKU_INDEX      "youkuindex"
#define ACCESS_YOUKU_SEGMENT    "youkusegment"

#define ACCESS_CNTV_INDEX       "cntvindex"
#define ACCESS_CNTV_SEGMENT     "cntvsegment"

#define ACCESS_SOHU_INDEX       "sohuindex"
#define ACCESS_SOHU_SEGMENT     "sohusegment"

#define ACCESS_LETV_INDEX       "letvindex"
#define ACCESS_LETV_SEGMENT     "letvsegment"

#define ACCESS_IQIYI_INDEX       "iqiyiindex"
#define ACCESS_IQIYI_SEGMENT     "iqiyisegment"

#define VSL_SAFE_FREE( x__ ) do { free( x__ ); ( x__ ) = NULL; } while( 0 )

#include <vlc/libvlc_structures.h>
#if 0
typedef int     (*libvlc_vsl_load_cb)( void *p_cb_data, bool b_force_reload );
typedef int     (*libvlc_vsl_load_segment_cb)( void *p_cb_data, bool b_force_reload, int segment );
typedef int     (*libvlc_vsl_get_count_cb)( void *p_cb_data );
typedef char   *(*libvlc_vsl_get_mrl_cb)( void *p_cb_data, int i_order );
typedef char   *(*libvlc_vsl_get_url_cb)( void *p_cb_data, int i_order );
typedef int     (*libvlc_vsl_get_duration_cb)( void *p_cb_data, int i_order );
typedef int64_t (*libvlc_vsl_get_bytes_cb)( void *p_cb_data, int i_order );
#endif

typedef struct {
    void                        *p_cb_data;
    libvlc_vsl_load_cb          pfn_load;
    libvlc_vsl_load_segment_cb  pfn_load_segment;
    libvlc_vsl_get_count_cb     pfn_get_count;
    libvlc_vsl_get_mrl_cb       pfn_get_mrl;
    libvlc_vsl_get_url_cb       pfn_get_url;
    libvlc_vsl_get_duration_cb  pfn_get_duration;
    libvlc_vsl_get_bytes_cb     pfn_get_bytes;
} vsl_cb_t;

int     vsl_cbGetCallbacks( vlc_object_t *p_this, vsl_cb_t *p_vsl_cb );
int     vsl_cbLoad( vsl_cb_t *p_vsl_cb, bool b_force_reload );
int     vsl_cbLoadSegment( vsl_cb_t *p_vsl_cb, bool b_force_reload, int segment );
int     vsl_cbGetCount( vsl_cb_t *p_vsl_cb );
char   *vsl_cbGetMrl( vsl_cb_t *p_vsl_cb, int i_order );
char   *vsl_cbGetUrl( vsl_cb_t *p_vsl_cb, int i_order );
int64_t vsl_cbGetDurationMilli( vsl_cb_t *p_vsl_cb, int i_order );
int64_t vsl_cbGetBytes( vsl_cb_t *p_vsl_cb, int i_order );

#endif
