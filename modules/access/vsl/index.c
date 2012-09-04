/*****************************************************************************
 * index.c: video segment list index access
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
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_threads.h>
#include <vlc_access.h>
#include <vlc_demux.h>
#include <vlc_block.h>
#include <vlc_input.h>
#include <vlc_es_out_managed.h>
#include <assert.h>

/* FIXME */
#include "../../../src/input/demux.h"
#include "../../../src/input/event.h"
#include "../../../src/input/es_out.h"

#include "vsl.h"

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
static int  Open ( vlc_object_t * );
static void Close( vlc_object_t * );

vlc_module_begin ()
    set_shortname( N_("vslindex") )
    set_description( N_("video segment list index access demux") )
    set_category( CAT_INPUT )
    set_subcategory( SUBCAT_INPUT_ACCESS )
    set_capability( "access_demux", 100 )
    add_shortcut( ACCESS_VSL_INDEX, ACCESS_SINA_INDEX,
                  ACCESS_YOUKU_INDEX, ACCESS_CNTV_INDEX,
                  ACCESS_SOHU_INDEX, ACCESS_LETV_INDEX,
                  ACCESS_IQIYI_INDEX )
    set_callbacks( Open, Close )
vlc_module_end ()

/*****************************************************************************
 * Documentation
 *  example: http://v.iask.com/v_play.php?vid=71442057
 *****************************************************************************/

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
typedef struct
{
    /* will not change after parsed */
    int             i_order;
    int64_t         i_duration_micro;   /* (microsecond) */
    int64_t         i_start_time_micro; /* (microsecond) */
    char            *p_mrl;

    /* segment data */
    vlc_mutex_t     lock;

    stream_t        *p_stream;          /* segment stream */
    demux_t         *p_demux;           /* segment demux */

    stream_t        *p_origin_stream;   /* weak referece */
    stream_t        *p_membuf_filter;   /* weak referece */
} vsl_segment_t;

typedef struct
{
    vlc_array_t     *p_segment_array;       /* list of segments */
    int64_t         i_total_duration_micro; /* (microsecond) */
} vsl_index_t;

struct demux_sys_t
{
    vsl_cb_t        cb;

    /* */
    vlc_mutex_t     lock;
    vsl_index_t     *p_index;

    vsl_segment_t   *p_segment;     /* current playing segment, weak reference */
    int             i_last_total_cached_percent;

    /* */
    es_out_t        *p_out_managed; /* passed to sub demux */

    bool            b_segment_changed;
};

static int Control( demux_t *p_demux, int i_query, va_list args );
static int Demux( demux_t *p_demux );

static demux_sys_t *vsl_Alloc( void );
static void         vsl_Free( demux_sys_t* p_sys);

static vsl_index_t *vsl_OpenIndex( demux_t *p_demux, demux_sys_t *p_sys );
static int vsl_AssureOpenSegment( demux_t *p_demux, int i_demux );

#define msg_VslDebug msg_Err

/****************************************************************************
 * Module Open/Close
 ****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    demux_t     *p_demux = (demux_t*) p_this;
    demux_sys_t *p_sys = NULL;

    msg_Dbg( p_demux, "Open:check access: %s", p_demux->psz_access );
    if( !p_demux->psz_access || !*p_demux->psz_access )
        goto EXIT_ERROR;

    msg_Dbg( p_demux, "Open:check location: %s", p_demux->psz_location );
    if( !p_demux->psz_location || !*p_demux->psz_location )
        goto EXIT_ERROR;

    /* Init p_demux */
    p_sys = vsl_Alloc();
    if( !p_sys )
        goto EXIT_ERROR;

    if( VLC_SUCCESS != vsl_cbGetCallbacks( VLC_OBJECT( p_demux ), &p_sys->cb ) )
        goto EXIT_ERROR;

    msg_Dbg( p_demux, "Open:vsl_OpenIndex" );
    for( int i = 0; i < 3; ++i )
    {
        p_sys->p_index = vsl_OpenIndex( p_demux, p_sys );
        if( !p_sys->p_index )
        {
            msg_Warn( p_demux, "vsl failed to open index, waiting retry" );
            continue;
        }

        break;
    }
    if( p_sys->p_index == NULL )
    {
        msg_Err( p_demux, "vsl failed to open index" );
        goto EXIT_ERROR;
    }

    assert( p_sys->p_index );
    assert( p_sys->p_index->p_segment_array );
    assert( vlc_array_count( p_sys->p_index->p_segment_array ) > 0 );

    msg_Dbg( p_demux, "Open:demux_EsOutManagedNew" );
    p_sys->p_out_managed = demux_EsOutManagedNew( p_demux, p_demux->out );

    p_demux->p_sys = p_sys;
    msg_Dbg( p_demux, "Open:vsl_AssureOpenSegment" );
    if( VLC_SUCCESS != vsl_AssureOpenSegment( p_demux, 0 ) )
    {
        msg_Err( p_demux, "failed to open demux for head segment" );
        goto EXIT_ERROR;
    }

    p_demux->pf_control = Control;
    p_demux->pf_demux = Demux;

    msg_Dbg( p_demux, "Open:succeeded" );
    return VLC_SUCCESS;

EXIT_ERROR:
    if( p_sys )
    {
        if( p_sys->p_out_managed )
            es_out_Delete( p_sys->p_out_managed );
        vsl_Free(p_sys);
    }
    p_demux->p_sys = NULL;

    return VLC_EGENERIC;
}

static void Close(vlc_object_t *p_this)
{
    demux_t *p_demux = (demux_t *) p_this;
    demux_sys_t *p_sys = p_demux->p_sys;

    vsl_Free(p_sys);
}

/****************************************************************************
 * Open/Close
 ****************************************************************************/
static vsl_segment_t *vsl_AllocSegment()
{
    vsl_segment_t *p_segment = (vsl_segment_t *)malloc( sizeof(vsl_segment_t) );
    if( !p_segment )
        return NULL;
    memset( p_segment, 0, sizeof(vsl_segment_t) );

    vlc_mutex_init( &p_segment->lock );

    return p_segment;
}

static void vsl_FreeSegment( vsl_segment_t *p_segment )
{
    if( !p_segment )
        return;

    if( p_segment->p_demux )
        demux_Delete( p_segment->p_demux );

    if( p_segment->p_stream )
        stream_Delete( p_segment->p_stream );

    VSL_SAFE_FREE( p_segment->p_mrl );

    vlc_mutex_destroy( &p_segment->lock );
    VSL_SAFE_FREE( p_segment );
}

static void vsl_FreeSegmentArray( vlc_array_t *p_segment_array )
{
    if( !p_segment_array )
        return;

    int count = vlc_array_count( p_segment_array );
    for(int i = 0; i < count; ++i )
    {
        vsl_segment_t *p_segment = (vsl_segment_t *) vlc_array_item_at_index( p_segment_array, i );
        if( p_segment )
        {
            vsl_FreeSegment( p_segment );
        }
    }

    vlc_array_destroy( p_segment_array );
}

static vsl_index_t *vsl_AllocIndex()
{
    vsl_index_t *p_index = (vsl_index_t *) malloc( sizeof( vsl_index_t ) );
    if( !p_index )
        return NULL;

    memset( p_index, 0, sizeof( vsl_index_t ) );
    return p_index;
}

static void vsl_FreeIndex( vsl_index_t *p_index )
{
    if( !p_index )
        return;

    vlc_array_t *p_segment_array = p_index->p_segment_array;
    if( p_segment_array )
        vsl_FreeSegmentArray( p_segment_array );

    VSL_SAFE_FREE( p_index );
}

static demux_sys_t *vsl_Alloc()
{
    demux_sys_t *p_sys = (demux_sys_t *) malloc( sizeof( demux_sys_t ) );
    if (!p_sys)
        return NULL;
    memset( p_sys, 0, sizeof( demux_sys_t ) );

    vlc_mutex_init( &p_sys->lock );

    return p_sys;
}

static void vsl_Free( demux_sys_t* p_sys )
{
    if( !p_sys )
        return;

    if( p_sys->p_out_managed )
        es_out_Delete( p_sys->p_out_managed );

    if( p_sys->p_index)
        vsl_FreeIndex( p_sys->p_index );

    VSL_SAFE_FREE( p_sys );
}

/****************************************************************************
 * Resolve
 ****************************************************************************/
static vsl_index_t *vsl_OpenIndex( demux_t *p_demux, demux_sys_t *p_sys )
{
    vsl_cb_t *p_cb = &p_sys->cb;
    vlc_array_t *p_segment_array = NULL;
    vsl_index_t *p_index = NULL;
    int i_count = 0;

    msg_Dbg( p_demux, "vsl_cbLoad" );
    if( VLC_SUCCESS != vsl_cbLoad( p_cb, false ) )
    {
        msg_Err( p_demux, "vsl_cbLoad: failed" );
        goto EXIT_ERROR;
    }

    p_index = vsl_AllocIndex();
    if( !p_index )
        goto EXIT_ERROR;

    p_segment_array = vlc_array_new();
    if( !p_segment_array )
        goto EXIT_ERROR;
    p_index->p_segment_array = p_segment_array;

    msg_Dbg( p_demux, "vsl_cbGetCount" );
    i_count = vsl_cbGetCount( p_cb );
    if( i_count <= 0 )
    {
        msg_Err( p_demux, "0 segments?" );
        goto EXIT_ERROR;
    }
    msg_Dbg( p_demux, "vsl_cbGetCount: %d", i_count );

    for( int i = 0; i < i_count; ++i )
    {
        vsl_segment_t *p_segment = vsl_AllocSegment();
        if( !p_segment_array )
            goto EXIT_ERROR;

        p_segment->i_order = i;
        p_segment->p_mrl = vsl_cbGetMrl( p_cb, i );
        p_segment->i_duration_micro = vsl_cbGetDurationMilli( p_cb, i ) * 1000;
        p_segment->i_start_time_micro = p_index->i_total_duration_micro;

        p_index->i_total_duration_micro += p_segment->i_duration_micro;

        msg_Dbg( p_demux, "vsl_cbGetSegment: %d, %"PRId64", %s",
                p_segment->i_order,
                (int64_t) p_segment->i_duration_micro,
                p_segment->p_mrl ? p_segment->p_mrl : "NULL");

        vlc_array_append( p_segment_array, p_segment );
        if( !p_segment->p_mrl || !*p_segment->p_mrl )
        {
            msg_Err( p_demux, "empty segment mrl" );
            goto EXIT_ERROR;
        }
    }

    return p_index;
EXIT_ERROR:
    if( p_index )
        vsl_FreeIndex( p_index );

    return NULL;
}

/****************************************************************************
 * Demux
 ****************************************************************************/
static int vsl_AssureOpenSegment( demux_t *p_demux, int i_segment )
{
    demux_sys_t *p_sys = p_demux->p_sys;
    assert( p_sys );

    int i_playing_segment = 0;
    if( p_sys->p_segment )
        i_playing_segment = p_sys->p_segment->i_order;

    assert( p_sys->p_index );
    vlc_array_t *p_segment_array = p_sys->p_index->p_segment_array;
    assert( p_segment_array );

    if( p_sys->p_segment )
    {
        if( i_playing_segment == i_segment )
        {   /* Same segment, just return */
            return VLC_SUCCESS;
        }
        else
        {   /* Different segment, close current */
            msg_Info( p_demux, "vsl_AssureOpenSegment: change segment" );
            if( p_sys->p_segment->p_demux )
            {
                demux_Delete( p_sys->p_segment->p_demux );
                p_sys->p_segment->p_demux = NULL;

                /* Reset PCR before use next demux */
                // es_out_Control( p_demux->out, ES_OUT_RESET_PCR );
                //if( p_sys->p_out_managed )
                //    es_out_Control( p_sys->p_out_managed, ES_OUT_POST_DISCONTINUITY );
            }
            if( p_sys->p_segment->p_stream )
            {
                stream_Delete( p_sys->p_segment->p_stream );
                p_sys->p_segment->p_stream = NULL;
            }

            p_sys->b_segment_changed = true;
        }
    }
    else
    {
        msg_Info( p_demux, "vsl_AssureOpenSegment: new segment" );
    }

    /* Open new segment */
    input_thread_t *p_parent_input = demux_GetParentInput( p_demux );
    assert( p_parent_input );

    int i_segment_count = vlc_array_count( p_segment_array );
    if( i_segment < 0 || i_segment > i_segment_count )
    {
        msg_Err( p_demux, "vsl_AssureOpenSegment: invalid segment %d", i_segment );
        return VLC_EGENERIC;
    }

    vsl_segment_t *p_new_segment = (vsl_segment_t *) vlc_array_item_at_index( p_segment_array, i_segment );
    assert( p_new_segment );

    if( !p_new_segment->p_stream )
    {
        /* Create segment stream */
        msg_Info( p_demux, "vsl open segment stream %d", p_new_segment->i_order );
        stream_t *p_stream = stream_UrlNew( p_demux, p_new_segment->p_mrl );
        if( !p_stream )
        {
            msg_Err( p_demux, "failed to open stream %s", p_new_segment->p_mrl );
            return VLC_EGENERIC;
        }

        p_new_segment->p_origin_stream = p_stream;

        /* Use membuf to prebuffer segment */
        stream_t *p_stream_filter = stream_FilterNew( p_stream, "asyncbuf" );
        if( p_stream_filter )
        {
            msg_Dbg( p_demux, "open asyncbuf" );
            p_stream = p_stream_filter;
            p_new_segment->p_membuf_filter = p_stream_filter;
        }
        else
        {
            msg_Err( p_demux, "failed to open asyncbuf" );
            /* ignore membuf */
        }

        int64_t segment_size = stream_Size( p_stream );
        msg_Info( p_demux, "vsl segment size %"PRId64, segment_size );

        /* try to peek some data, retry if failed */
        const uint8_t* p_buf = NULL;
        int i_peek = stream_Peek( p_stream, &p_buf, 1024 );
        if( i_peek <= 0 )
        {
            stream_Delete( p_stream );
            return VLC_EGENERIC;
        }

        p_new_segment->p_stream = p_stream;
    }

    /* Set current segment */
    p_sys->p_segment = p_new_segment;

    if( !p_new_segment->p_stream )
    {
        msg_Err( p_demux, "vsl_AssureOpenSegment: failed to open stream %s", p_new_segment->p_mrl );
        return VLC_EGENERIC;
    }

    if( !p_new_segment->p_demux )
    {
        msg_Info( p_demux, "vsl open segment demux %d", p_new_segment->i_order );
        p_new_segment->p_demux = demux_New( p_new_segment->p_stream,
                                           p_new_segment->p_stream->p_input,
                                           "", "any", "",
                                           p_new_segment->p_stream,
                                           //p_demux->out,
                                           p_sys->p_out_managed,
                                           false);
        if( !p_new_segment->p_demux )
        {
            msg_Err( p_demux, "failed to open demux %s", p_new_segment->p_mrl );
            return VLC_EGENERIC;
        }

        /* Set current segment */
        p_sys->p_segment = p_new_segment;
    }

    assert( p_sys->p_segment );
    msg_Info( p_demux, "vsl_AssureOpenSegment: succeeded" );
    return VLC_SUCCESS;
}

static int Demux( demux_t *p_demux )
{
    demux_sys_t *p_sys = p_demux->p_sys;

    vsl_index_t *p_index = p_sys->p_index;
    assert( p_index );
    assert( p_index->p_segment_array );

    bool b_stop_buffering = false;
    while( p_sys->p_segment && vlc_object_alive(p_demux) )
    {
        if( !p_sys->p_segment || !p_sys->p_segment->p_demux )
        {
            msg_Err( p_demux, "NULL segment or demux, maybe EOF" );
            return 0;
        }

        int i_demux_ret = demux_Demux( p_sys->p_segment->p_demux );
        if( i_demux_ret != 0 )
        {
            if( i_demux_ret > 0 && b_stop_buffering)
            {
                es_out_GetEmpty( p_demux->out );
                b_stop_buffering = false;
            }

            int64_t i_cached_size = 0;
            int i_ret = stream_Control( p_sys->p_segment->p_stream,
                                        STREAM_GET_CACHED_SIZE,
                                        &i_cached_size );

            /* Send cached total event */
            if( VLC_SUCCESS == i_ret &&
               i_cached_size > 0 &&
               p_sys->p_segment->i_duration_micro > 0 )
            {
                int64_t i_size = stream_Size( p_sys->p_segment->p_stream );
                if ( i_size > 0 && i_size >= i_cached_size )
                {
                    float f_total_cached = i_cached_size;
                    f_total_cached /= i_size;
                    f_total_cached *= p_sys->p_segment->i_duration_micro;
                    f_total_cached += p_sys->p_segment->i_start_time_micro;
                    f_total_cached /= p_sys->p_index->i_total_duration_micro;

                    int i_total_cached_percent = f_total_cached * 100;
                    if( p_sys->i_last_total_cached_percent != i_total_cached_percent &&
                        i_total_cached_percent >= 0 && i_total_cached_percent <= 100 )
                    {
                        input_thread_t *p_input = demux_GetParentInput( p_demux );
                        if( p_input )
                        {
                            input_SendEventCacheTotal( p_input, f_total_cached );
                            p_sys->i_last_total_cached_percent = i_total_cached_percent;
                        }
                    }
                }
            }

            return i_demux_ret;  /* success or fail */
        }

        /* segment EOF */
        assert( p_sys->p_index );
        vlc_array_t *p_segment_array = p_sys->p_index->p_segment_array;
        assert( p_segment_array );

        /* test if whole stream EOF */
        if( p_sys->p_segment->i_order + 1 >= vlc_array_count( p_sys->p_index->p_segment_array ) )
            break;

        msg_Info( p_demux, "vsl segment EOF, try next segment %d", p_sys->p_segment->i_order + 1 );

        p_sys->b_segment_changed = false;
        int i_ret = vsl_AssureOpenSegment( p_demux, p_sys->p_segment->i_order + 1 );
        if( VLC_SUCCESS != i_ret )
            return -1;

        if( p_sys->b_segment_changed )
        {
            /* wait es out empty, up to 'network-caching' */

#define VSL_SEG_WAIT_STEP ( 50 * 1000 )
            int64_t i_pts_delay = INT64_C(1000) * var_InheritInteger( p_demux, "network-caching" );
            if( i_pts_delay < 0 )
                i_pts_delay = 0;

            for( int64_t i = 0; i < i_pts_delay; i += VSL_SEG_WAIT_STEP )
            {
                if( es_out_GetEmpty( p_demux->out ) )
                    break;

                msleep( VSL_SEG_WAIT_STEP );
            }

            es_out_Control( p_demux->out, ES_OUT_RESET_PCR );
            b_stop_buffering = true;
        }
    }

    /* whole stream EOF */
    return 0;
}

/****************************************************************************
 * Control
 ****************************************************************************/
#if 0
static void vsl_DebugControlCode( demux_t *p_demux, int i_query )
{
    const char* p_debug_msg = "unknown";
    switch( i_query )
    {
        case DEMUX_GET_POSITION:            p_debug_msg = "DEMUX_GET_POSITION"; break;
        case DEMUX_SET_POSITION:            p_debug_msg = "DEMUX_SET_POSITION"; break;
        case DEMUX_GET_LENGTH:              p_debug_msg = "DEMUX_GET_LENGTH"; break;
        case DEMUX_GET_TIME:                p_debug_msg = "DEMUX_GET_TIME"; break;
        case DEMUX_SET_TIME:                p_debug_msg = "DEMUX_SET_TIME"; break;
        case DEMUX_GET_TITLE_INFO:          p_debug_msg = "DEMUX_GET_TITLE_INFO"; break;
        case DEMUX_SET_TITLE:               p_debug_msg = "DEMUX_SET_TITLE"; break;
        case DEMUX_SET_SEEKPOINT:           p_debug_msg = "DEMUX_SET_SEEKPOINT"; break;
        case DEMUX_SET_GROUP:               p_debug_msg = "DEMUX_SET_GROUP"; break;
        case DEMUX_SET_NEXT_DEMUX_TIME:     p_debug_msg = "DEMUX_SET_NEXT_DEMUX_TIME"; break;
        case DEMUX_GET_FPS:                 p_debug_msg = "DEMUX_GET_FPS"; break;
        case DEMUX_GET_META:                p_debug_msg = "DEMUX_GET_META"; break;
        case DEMUX_HAS_UNSUPPORTED_META:    p_debug_msg = "DEMUX_HAS_UNSUPPORTED_META"; break;
        case DEMUX_GET_ATTACHMENTS:         p_debug_msg = "DEMUX_GET_ATTACHMENTS"; break;
        case DEMUX_CAN_RECORD:              p_debug_msg = "DEMUX_CAN_RECORD"; break;
        case DEMUX_SET_RECORD_STATE:        p_debug_msg = "DEMUX_SET_RECORD_STATE"; break;
        case DEMUX_CAN_PAUSE:               p_debug_msg = "DEMUX_CAN_PAUSE"; break;
        case DEMUX_SET_PAUSE_STATE:         p_debug_msg = "DEMUX_SET_PAUSE_STATE"; break;
        case DEMUX_GET_PTS_DELAY:           p_debug_msg = "DEMUX_GET_PTS_DELAY"; break;
        case DEMUX_CAN_CONTROL_PACE:        p_debug_msg = "DEMUX_CAN_CONTROL_PACE"; break;
        case DEMUX_CAN_CONTROL_RATE:        p_debug_msg = "DEMUX_CAN_CONTROL_RATE"; break;
        case DEMUX_SET_RATE:                p_debug_msg = "DEMUX_SET_RATE"; break;
        case DEMUX_CAN_SEEK:                p_debug_msg = "DEMUX_CAN_SEEK"; break;
        case DEMUX_GET_SIGNAL:              p_debug_msg = "DEMUX_GET_SIGNAL"; break;
    }

    msg_VslDebug( p_demux, "vsl:Control:%s", p_debug_msg );
}
#endif

static int vsl_FindSegmentByTime( demux_t *p_demux, int64_t i_time )
{
    if( i_time <= 0 )
        return 0;

    demux_sys_t *p_sys = p_demux->p_sys;
    assert( p_sys );

    vsl_index_t *p_index = p_sys->p_index;
    assert( p_index );

    vlc_array_t *p_segment_array = p_index->p_segment_array;
    assert( p_segment_array );

    int count = vlc_array_count( p_segment_array );
    if( 0 == count )
        return 0;

    for( int i = 0; i < count; ++i )
    {
        vsl_segment_t *p_segment = vlc_array_item_at_index( p_segment_array, i );
        assert( p_segment );

        if( i_time < p_segment->i_start_time_micro + p_segment->i_duration_micro )
            return i;
    }

    /* not found, return last segment */
    return count - 1;
}

static int vsl_ControlGetDuration( demux_t *p_demux, int64_t *p_duration )
{
    demux_sys_t *p_sys = p_demux->p_sys;
    assert( p_sys);

    *p_duration = (int64_t) p_sys->p_index->i_total_duration_micro;
    return VLC_SUCCESS;
}

static int vsl_ControlGetTime( demux_t *p_demux, int64_t *p_time )
{
    demux_sys_t *p_sys = p_demux->p_sys;
    assert( p_sys);

    vsl_segment_t *p_segment = p_sys->p_segment;
    if( !p_segment || !p_segment->p_demux )
        return VLC_EGENERIC;

    int rc = demux_Control( p_segment->p_demux, DEMUX_GET_TIME, p_time );
    if( rc != VLC_SUCCESS )
        return rc;

    *p_time += p_segment->i_start_time_micro;
    return rc;
}

static int vsl_ControlSetTime( demux_t *p_demux, int64_t i_time )
{
    demux_sys_t *p_sys = p_demux->p_sys;
    assert( p_sys);
    msg_Info( p_demux, "vsl seek %"PRId64, i_time );

    int i_seek_to_segment = vsl_FindSegmentByTime( p_demux, i_time );
    msg_Info( p_demux, "vsl seek to segment %d", i_seek_to_segment );

    int i_ret = vsl_AssureOpenSegment( p_demux, i_seek_to_segment );
    if( i_ret != VLC_SUCCESS )
        return i_ret;

    vsl_segment_t *p_segment = p_sys->p_segment;
    if( !p_segment || !p_segment->p_demux )
        return VLC_EGENERIC;

    assert( i_time >= p_sys->p_segment->i_start_time_micro );
    if( i_time < p_sys->p_segment->i_start_time_micro )
    {
        msg_Warn( p_demux, "vsl seek time: %"PRId64" less than segment start time: %"PRId64,
                 i_time,
                 p_sys->p_segment->i_start_time_micro );
        i_time = p_sys->p_segment->i_start_time_micro;
    }

    int64_t i_segment_duration_micro = 0;
    int rc = demux_Control( p_segment->p_demux, DEMUX_GET_TIME, &i_segment_duration_micro );
    if( rc != VLC_SUCCESS )
    {
        msg_Info( p_demux, "vsl unable to get duration of segment %d", i_seek_to_segment );
        return rc;
    }

    msg_Info( p_demux, "vsl segment seek to %"PRId64" of %"PRId64,
              i_time - p_sys->p_segment->i_start_time_micro,
              i_segment_duration_micro );
    rc = demux_Control( p_segment->p_demux,
                        DEMUX_SET_TIME,
                        i_time - p_sys->p_segment->i_start_time_micro );
    if( rc != VLC_SUCCESS )
        return rc;

    return rc;
}

static int vsl_ControlGetPosition( demux_t *p_demux, double *p_position )
{
    demux_sys_t *p_sys = p_demux->p_sys;
    assert( p_sys);

    int64_t i_total_duration = 0;
    int rc = vsl_ControlGetDuration( p_demux, &i_total_duration );
    if( rc != VLC_SUCCESS )
        return rc;

    if( i_total_duration <= 0 )
        return VLC_EGENERIC;

    int64_t i_playing_time = 0;
    rc = vsl_ControlGetTime( p_demux, &i_playing_time );
    if( rc != VLC_SUCCESS )
        return rc;

    *p_position = (double) i_playing_time / i_total_duration;
    return rc;
}

static int vsl_ControlSetPosition( demux_t *p_demux, double d_position )
{
    demux_sys_t *p_sys = p_demux->p_sys;
    assert( p_sys);

    int64_t i_total_duration = 0;
    int rc = vsl_ControlGetDuration( p_demux, &i_total_duration );
    if( rc != VLC_SUCCESS )
        return rc;

    if( i_total_duration <= 0 )
        return VLC_EGENERIC;

    int64_t i_segment_playing_time = i_total_duration * d_position;
    return vsl_ControlSetTime( p_demux, i_segment_playing_time );
}

static void vsl_ControlSetPauseState( demux_t *p_demux, bool b_pause_state )
{
    demux_sys_t *p_sys = p_demux->p_sys;
    assert( p_sys);

    vsl_segment_t *p_segment = p_sys->p_segment;
    if( !p_segment || !p_segment->p_demux )
        return;

    if( p_segment->p_demux )
        demux_Control( p_segment->p_demux, DEMUX_SET_PAUSE_STATE, b_pause_state );
}

static int Control( demux_t *p_demux, int i_query, va_list args )
{
    demux_sys_t *p_sys = p_demux->p_sys;
    assert( p_sys);
    assert( p_sys->p_segment );

    if( !p_sys->p_segment->p_demux )
    {
        msg_Err( p_demux, "vsl Control, null segment demux " );
        return VLC_EGENERIC;
    }

    switch( i_query )
    {
        case DEMUX_GET_POSITION:
        {
            double *p_position = (double *) va_arg( args, double * );
            return vsl_ControlGetPosition( p_demux, p_position );
        }
        case DEMUX_SET_POSITION:
        {
            double d_position = (double) va_arg( args, double );
            return vsl_ControlSetPosition( p_demux, d_position );
        }
        case DEMUX_GET_TIME:
        {
            int64_t *p_time = (int64_t *) va_arg( args, int64_t * );
            return vsl_ControlGetTime( p_demux, p_time );
        }
        case DEMUX_SET_TIME:
        {
            int64_t i_time = (int64_t) va_arg( args, int64_t );
            return vsl_ControlSetTime( p_demux, i_time );
        }
        case DEMUX_GET_LENGTH:
        {
            int64_t *p_duration = (int64_t *) va_arg( args, int64_t * );
            return vsl_ControlGetDuration( p_demux, p_duration );
        }
        case DEMUX_CAN_PAUSE:
        case DEMUX_CAN_SEEK:
        case DEMUX_CAN_CONTROL_PACE:
        {
            *va_arg( args, bool * ) = true;
            return VLC_SUCCESS;
        }
        case DEMUX_CAN_CONTROL_RATE:
        {
            *va_arg( args, bool * ) = false;
            return VLC_SUCCESS;
        }
        case DEMUX_SET_PAUSE_STATE:
        {
            bool b_pause_state = va_arg( args, int );
            vsl_ControlSetPauseState( p_demux, b_pause_state );
            return VLC_SUCCESS;
        }
        case DEMUX_GET_PTS_DELAY:
        {
            int64_t *p_pts_delay = va_arg( args, int64_t * );
            *p_pts_delay = INT64_C(1000) * var_InheritInteger( p_demux, "network-caching" );

            return VLC_SUCCESS;
        }
        default:
        {
            if( p_sys->p_segment && p_sys->p_segment->p_demux )
                return demux_vaControl( p_sys->p_segment->p_demux, i_query, args );

            return VLC_EGENERIC;
        }
    }

    return VLC_EGENERIC;
}