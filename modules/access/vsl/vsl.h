/*****************************************************************************
 * vsl.h: video segment list access
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

#ifndef VSL_H
#define VSL_H

#define ACCESS_VSL_INDEX        "vslindex"
#define ACCESS_VSL_SEGMENT      "vslsegment"

#define ACCESS_SINA_INDEX       "sinaindex"
#define ACCESS_SINA_SEGMENT     "sinasegment"

#define ACCESS_YOUKU_INDEX      "youkuindex"
#define ACCESS_YOUKU_SEGMENT    "youkusegment"

#define VSL_SAFE_FREE( x__ ) do { free( x__ ); ( x__ ) = NULL; } while( 0 )

typedef libvlc_segment_list_t *(*libvlc_segment_list_resolve_cb)( const char *p_mrl );
typedef void (*libvlc_segment_list_release_cb)( libvlc_segment_list_t *p_segment_lst );

typedef struct {
    char    *p_index_mrl;
    char    *p_index_url;
    char    *p_pseudo_segment_access;       /* vslsegment */

    char    *p_real_access;                 /* http */
    char    *p_real_access_tag;             /* mp4 */

    int     i_order;                        /* -1 for index */

    libvlc_segment_list_resolve_cb  pfn_resolve;
    libvlc_segment_list_release_cb  pfn_release;
} vsl_info_t;

void free_VslInfo( vsl_info_t *p_index_info );
vsl_info_t *vsl_ResolveInfo( demux_t *p_demux );

libvlc_segment_list_t *vsl_ResolveSegmentList( demux_t *p_demux, vsl_info_t *p_vsl_info );

#endif