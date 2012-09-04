/*****************************************************************************
 * vsl.c: video segment list access
 *****************************************************************************
 *
 * Authors: Rui Zhang <bbcallen _AT_ gmail _DOT_ com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
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

static vsl_info_t *alloc_VslInfo()
{
    vsl_info_t *p_vsl_info = (vsl_info_t *) malloc( sizeof(vsl_info_t) );
    memset( p_vsl_info, 0, sizeof(vsl_info_t) );

    p_vsl_info->i_order = -1;

    return p_vsl_info;
}

void free_VslInfo( vsl_info_t *p_vsl_info )
{
    if( p_vsl_info == NULL )
        return;

    VSL_SAFE_FREE( p_vsl_info->p_index_url );
    VSL_SAFE_FREE( p_vsl_info->p_pseudo_segment_access );

    VSL_SAFE_FREE( p_vsl_info->p_real_access );
    VSL_SAFE_FREE( p_vsl_info->p_real_access_tag );

    VSL_SAFE_FREE( p_vsl_info );
}

vsl_info_t *vsl_ResolveInfo( demux_t *p_demux )
{
    char        *p_find_real_access_tag = NULL;
    char        *p_find_segment_order = NULL;
    vsl_info_t  *p_vsl_info = NULL;

    msg_Dbg( p_demux, "vsl check access: %s", p_demux->psz_access );
    if( !p_demux->psz_access || !*p_demux->psz_access )
        goto EXIT_ERROR;

    msg_Dbg( p_demux, "vsl check p_demux: %s", p_demux->psz_demux );
    if( !p_demux->psz_demux || !*p_demux->psz_demux )
        goto EXIT_ERROR;

    msg_Dbg( p_demux, "vsl check location: %s", p_demux->psz_location );
    if( !p_demux->psz_location || !*p_demux->psz_location )
        goto EXIT_ERROR;

    p_vsl_info = alloc_VslInfo();
    if( !p_vsl_info )
        goto EXIT_ERROR;

    p_vsl_info->pfn_resolve = var_GetAddress( p_demux, "segment-list-resolve" );
    if( !p_vsl_info->pfn_resolve )
    {
        msg_Err( p_demux, "segment-list-resolve not set" );
        goto EXIT_ERROR;
    }

    p_vsl_info->pfn_release = var_GetAddress( p_demux, "segment-list-release" );
    if( !p_vsl_info->pfn_release )
    {
        msg_Err( p_demux, "segment-list-release not set" );
        goto EXIT_ERROR;
    }

    /* parse pseudo access/demux */
    if( 0 == strcmp( p_demux->psz_access, ACCESS_VSL_INDEX ) )
        p_vsl_info->p_pseudo_segment_access = strdup( ACCESS_VSL_SEGMENT );
    else if( 0 == strcmp( p_demux->psz_access, ACCESS_SINA_INDEX ) )
        p_vsl_info->p_pseudo_segment_access = strdup( ACCESS_SINA_SEGMENT );
    else if( 0 == strcmp( p_demux->psz_access, ACCESS_YOUKU_INDEX ) )
        p_vsl_info->p_pseudo_segment_access = strdup( ACCESS_YOUKU_SEGMENT );
    else
    {
        msg_Err( p_demux, "vsl unknown pseudo access %s", p_demux->psz_access );
        goto EXIT_ERROR;
    }

    /* parse real access/tag/order */
    p_find_real_access_tag = strchr( p_demux->psz_demux, '-' );
    if( !p_find_real_access_tag || !*p_find_real_access_tag )
    {
        msg_Err( p_demux, "vsl can not find real access tag in %s",
                 p_demux->psz_demux );
        goto EXIT_ERROR;
    }

    p_vsl_info->p_real_access = strndup( p_demux->psz_demux,
                                         p_find_real_access_tag - p_demux->psz_demux );
    p_vsl_info->p_real_access_tag = strdup( p_find_real_access_tag + 1 );

    p_find_segment_order = strchr( p_find_real_access_tag + 1, '-' );
    if( p_find_segment_order && *p_find_segment_order )
    {
        p_vsl_info->i_order = atol( p_find_segment_order + 1 );
        msg_Dbg( p_demux, "vsl segment order %d", p_vsl_info->i_order );
    }

    /* save index mrl */
    if( 0 > asprintf( &p_vsl_info->p_index_mrl, "%s:/%s/%s",
                      p_demux->psz_access,
                      p_demux->psz_demux,
                      p_demux->psz_location ) )
    {
        msg_Warn( p_demux, "failed to build vsl mrl" );
        goto EXIT_ERROR;
    }

    /* save index url */
    if( 0 > asprintf( &p_vsl_info->p_index_url, "%s://%s",
                      p_vsl_info->p_real_access,
                      p_demux->psz_location ) )
    {
        msg_Warn( p_demux, "failed to build vsl url" );
        goto EXIT_ERROR;
    }

    return p_vsl_info;
EXIT_ERROR:
    free_VslInfo( p_vsl_info );
    p_vsl_info = NULL;

    return NULL;
}

libvlc_segment_list_t *vsl_ResolveSegmentList( demux_t *p_demux, vsl_info_t *p_vsl_info )
{
    return NULL;
}