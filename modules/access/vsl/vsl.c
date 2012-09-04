/*****************************************************************************
 * vsl.c: video segment list access
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc/libvlc_structures.h>
#include <vlc_demux.h>
#include <assert.h>

#include "vsl.h"

int vsl_cbGetCallbacks( vlc_object_t *p_this, vsl_cb_t *p_vsl_cb )
{
    p_vsl_cb->p_cb_data = var_InheritAddress( p_this, "vsl-data" );
    p_vsl_cb->pfn_load = var_InheritAddress( p_this, "vsl-load" );
    p_vsl_cb->pfn_load_segment = var_InheritAddress( p_this, "vsl-load-segment" );
    p_vsl_cb->pfn_get_count = var_InheritAddress( p_this, "vsl-get-count" );
    p_vsl_cb->pfn_get_mrl = var_InheritAddress( p_this, "vsl-get-mrl" );
    p_vsl_cb->pfn_get_url = var_InheritAddress( p_this, "vsl-get-url" );
    p_vsl_cb->pfn_get_duration = var_InheritAddress( p_this, "vsl-get-duration" );
    p_vsl_cb->pfn_get_bytes = var_InheritAddress( p_this, "vsl-get-bytes" );
    if( !p_vsl_cb->p_cb_data ||
        !p_vsl_cb->pfn_load ||
        !p_vsl_cb->pfn_get_count ||
        !p_vsl_cb->pfn_get_mrl ||
        !p_vsl_cb->pfn_get_url ||
        !p_vsl_cb->pfn_get_duration ||
        !p_vsl_cb->pfn_get_bytes )
    {
        msg_Err( p_this, "vsl callback not set %p, %p, %p, %p, %p, %p, %p",
                 p_vsl_cb->p_cb_data,
                 p_vsl_cb->pfn_load,
                 p_vsl_cb->pfn_get_count,
                 p_vsl_cb->pfn_get_mrl,
                 p_vsl_cb->pfn_get_url,
                 p_vsl_cb->pfn_get_duration,
                 p_vsl_cb->pfn_get_bytes );
        goto EXIT_ERROR;
    }

    return VLC_SUCCESS;
EXIT_ERROR:
    return VLC_EGENERIC;
}

int vsl_cbLoad( vsl_cb_t *p_vsl_cb, bool b_force_reload )
{
    assert( p_vsl_cb->p_cb_data );
    assert( p_vsl_cb->pfn_load );
    return p_vsl_cb->pfn_load( p_vsl_cb->p_cb_data, b_force_reload );
}

int vsl_cbLoadSegment( vsl_cb_t *p_vsl_cb, bool b_force_reload, int segment )
{
    assert( p_vsl_cb->p_cb_data );
    assert( p_vsl_cb->pfn_load );
    return p_vsl_cb->pfn_load_segment( p_vsl_cb->p_cb_data, b_force_reload, segment );
}

int vsl_cbGetCount( vsl_cb_t *p_vsl_cb )
{
    assert( p_vsl_cb->p_cb_data );
    assert( p_vsl_cb->pfn_get_count );
    return p_vsl_cb->pfn_get_count( p_vsl_cb->p_cb_data );
}

char *vsl_cbGetMrl( vsl_cb_t *p_vsl_cb, int i_order )
{
    assert( p_vsl_cb->p_cb_data );
    assert( p_vsl_cb->pfn_get_mrl );
    return p_vsl_cb->pfn_get_mrl( p_vsl_cb->p_cb_data, i_order );
}

char *vsl_cbGetUrl( vsl_cb_t *p_vsl_cb, int i_order )
{
    assert( p_vsl_cb->p_cb_data );
    assert( p_vsl_cb->pfn_get_url );
    return p_vsl_cb->pfn_get_url( p_vsl_cb->p_cb_data, i_order );
}

int64_t vsl_cbGetDurationMilli( vsl_cb_t *p_vsl_cb, int i_order )
{
    assert( p_vsl_cb->p_cb_data );
    assert( p_vsl_cb->pfn_get_duration );
    return p_vsl_cb->pfn_get_duration( p_vsl_cb->p_cb_data, i_order );
}

int64_t vsl_cbGetBytes( vsl_cb_t *p_vsl_cb, int i_order )
{
    assert( p_vsl_cb->p_cb_data );
    assert( p_vsl_cb->pfn_get_bytes );
    return p_vsl_cb->pfn_get_bytes( p_vsl_cb->p_cb_data, i_order );
}
