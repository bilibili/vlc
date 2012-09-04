/*****************************************************************************
 * segment.c: video segment list segment access
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

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_access.h>
#include <vlc_stream.h>

#include <ctype.h>
#include <assert.h>

#include "vsl.h"

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
static int  Open ( vlc_object_t * );
static void Close( vlc_object_t * );

vlc_module_begin ()
    set_shortname( N_("vslsegment") )
    set_description( N_("video segment list segment access demux") )
    set_category( CAT_INPUT )
    set_subcategory( SUBCAT_INPUT_ACCESS )
    set_capability( "access", 1 )
    add_shortcut( ACCESS_VSL_SEGMENT, ACCESS_SINA_SEGMENT,
                  ACCESS_YOUKU_SEGMENT, ACCESS_CNTV_SEGMENT,
                  ACCESS_SOHU_SEGMENT, ACCESS_LETV_SEGMENT,
                  ACCESS_IQIYI_SEGMENT )
    set_callbacks( Open, Close )
vlc_module_end ()

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/

#pragma pack(push)
#pragma pack(1)

/* http://osflash.org/flv */
typedef struct
{
    uint8_t     signature[3];   /* Always “FLV” */
    uint8_t     version;        /* Currently 1 for known FLV files */
    uint8_t     bitmask;        /* “\x05” (5, audio+video)	 Bitmask: 4 is audio, 1 is video */
    uint32_t    be_offset;      /* BE, Total size of header (always 9 for known FLV files) */
    /* followed by uint32_t BE */
    /* uint32_t header_size BE */
} flv_header_t;

#define FLV_TAG_AUDIO   0x08
#define FLV_TAG_VIDEO   0x09
#define FLV_TAG_META    0x12
typedef struct
{
    uint8_t     type;               /* Determines the layout of Body, see below for tag types */
    uint8_t     be_body_length[3];  /* BE, Size of Body (total tag size - 11) */
    uint8_t     be_timestamp[3];    /* BE, Timestamp of tag (in milliseconds) */
    uint8_t     timestamp_extended; /* Timestamp extension to form a uint32_be. This field has the upper 8 bits. */
    uint8_t     be_stream_id[3];    /* BE, Always 0 */
    /* followed by uint8_t[body_length] and uint32_t BE */
    /* uint32_t tag_size BE */
} flv_tag_t;

#define SINA_FLV_FRONT_MIN_SIZE 94

#pragma pack(pop)

typedef stream_t *(*new_seeked_stream_cb)( access_t* p_access, uint64_t i_pos );

struct access_sys_t
{
    vsl_cb_t    cb;
    int         i_order;
    int64_t     i_duration_milli;
    int64_t     i_bytes_per_second;

    bool        b_seekable;
    bool        b_continuous;
    bool        b_retry_for_broken_stream;  /* sina cdn may broke file */
    bool        b_require_content_length;   /* to detect broken file */
    bool        b_reload_index_when_retry;  /* try to get good file */

    char        *psz_url;

    stream_t    *p_stream;

    new_seeked_stream_cb pfn_new_seekd_stream;
};

/* */
static ssize_t Read( access_t *, uint8_t *, size_t );
static int Seek( access_t *, uint64_t );
static int Control( access_t *, int, va_list );

static stream_t *stream_vslNewSeek( access_t* p_access, uint64_t i_pos );
static stream_t *stream_vslNewSinaSeek( access_t* p_access, uint64_t i_pos );
static stream_t *stream_vslNewYoukuSeek( access_t* p_access, uint64_t i_pos );

/*****************************************************************************
 * Open/Close
 *****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    access_t     *p_access = (access_t*)p_this;
    access_sys_t *p_sys = NULL;

    msg_Dbg( p_access, "Open:check access: %s", p_access->psz_access );
    if( !p_access->psz_access || !*p_access->psz_access )
        goto EXIT_ERROR;

    msg_Dbg( p_access, "Open:check location: %s", p_access->psz_location );
    if( !p_access->psz_location || !*p_access->psz_location )
        goto EXIT_ERROR;

    if( !isdigit( *p_access->psz_location ) )
    {
        msg_Err( p_access, "vsl does not support location %s", p_access->psz_location );
        goto EXIT_ERROR;
    }

    /* Set up p_access */
    STANDARD_READ_ACCESS_INIT;
    memset( p_sys, 0, sizeof(access_sys_t) );
    p_access->info.i_size = 0;
    p_access->info.i_pos  = 0;
    p_access->info.b_eof  = false;

    p_sys->b_seekable = true;
    p_sys->b_continuous = true;
    p_sys->pfn_new_seekd_stream = NULL;
    if( 0 == strcmp( p_access->psz_access, ACCESS_SINA_SEGMENT ) )
    {
        msg_Dbg( p_access, "segment: sina seek" );
        p_sys->pfn_new_seekd_stream = stream_vslNewSinaSeek;
        /* sina cdn may broke file */
        p_sys->b_retry_for_broken_stream = true;
        p_sys->b_require_content_length = true;
    }
    else if( 0 == strcmp( p_access->psz_access, ACCESS_YOUKU_SEGMENT ) )
    {
        msg_Dbg( p_access, "segment: youku seek" );
        p_sys->pfn_new_seekd_stream = stream_vslNewYoukuSeek;
    }
    else if( 0 == strcmp( p_access->psz_access, ACCESS_CNTV_SEGMENT ) )
    {
        // cntv is similar to youku
        msg_Dbg( p_access, "segment: cntv not seekable" );
        p_sys->pfn_new_seekd_stream = stream_vslNewYoukuSeek;
        p_sys->b_seekable = false;
    }
    else if( 0 == strcmp( p_access->psz_access, ACCESS_SOHU_SEGMENT ) )
    {
        msg_Dbg( p_access, "segment: vsl seek (sohu) " );
        p_sys->b_continuous = false;
    }
    else if( 0 == strcmp( p_access->psz_access, ACCESS_LETV_SEGMENT ) )
    {
        msg_Dbg( p_access, "segment: vsl seek (letv) " );
        p_sys->b_continuous = false;
    }
    else if( 0 == strcmp( p_access->psz_access, ACCESS_IQIYI_SEGMENT ) )
    {
        msg_Dbg( p_access, "segment: vsl seek (iqiyi) " );
        p_sys->b_continuous = false;
    }
    else
    {
        msg_Dbg( p_access, "segment: vsl seek" );
        p_sys->b_continuous = false;
    }

    p_sys->i_order = atoi( p_access->psz_location );
    if( p_sys->i_order < 0 )
    {
        msg_Err( p_access, "vsl invalid order %s", p_access->psz_location );
        goto EXIT_ERROR;
    }

    if( VLC_SUCCESS != vsl_cbGetCallbacks( VLC_OBJECT( p_access ), &p_sys->cb ) )
        goto EXIT_ERROR;

    int i_count = vsl_cbGetCount( &p_sys->cb );
    if( p_sys->i_order >= i_count )
    {
        msg_Err( p_access, "vsl order %d is out of range %d", p_sys->i_order, i_count );
        goto EXIT_ERROR;
    }

    for( int i = 0; i < 3; ++i )
    {
        if( i > 0 && p_sys->b_reload_index_when_retry )
        {
            msg_Warn( p_access, "retry vsl_cbLoad" );
            if( VLC_SUCCESS != vsl_cbLoad( &p_sys->cb, true ) )
            {
                msg_Err( p_access, "retry vsl_cbLoad: failed" );
                goto STREAM_OPEN_ERROR;
            }
        }

        vsl_cbLoadSegment( &p_sys->cb, i == 0, p_sys->i_order );

        p_sys->psz_url = vsl_cbGetUrl( &p_sys->cb, p_sys->i_order );
        if( !p_sys->psz_url || !*p_sys->psz_url )
        {
            msg_Err( p_access, "vsl empty url for segment %d", p_sys->i_order );
            goto EXIT_ERROR;
        }
        p_sys->i_duration_milli = vsl_cbGetDurationMilli( &p_sys->cb, p_sys->i_order );

        p_sys->p_stream = stream_UrlNew( p_access, p_sys->psz_url );
        if( !p_sys->p_stream )
            goto STREAM_OPEN_ERROR;

        p_access->info.i_size = stream_Size( p_sys->p_stream );
        if( p_access->info.i_size <= 0 )
        {
            if( p_sys->b_require_content_length )
            {
                msg_Warn( p_access, "segment: stream_Size <= 0, require content length" );
                goto STREAM_OPEN_ERROR;
            }

            msg_Warn( p_access, "segment: stream_Size <= 0, try cbGetBytes" );
            p_access->info.i_size = vsl_cbGetBytes( &p_sys->cb, p_sys->i_order );
            if( p_access->info.i_size <= 0 )
            {
                msg_Warn( p_access, "segment: vsl_cbGetBytes <= 0, unknown size" );
                goto EXIT_ERROR;
            }
        } else if( p_sys->b_retry_for_broken_stream ) {
            if( p_access->info.i_size < 64 * 1000 && p_sys->i_duration_milli > 10 * 1000 )
            {
                msg_Err( p_access, "possible 6-min cursor, retry" );
                goto STREAM_OPEN_ERROR;
            }
        }

        /* Succeeded */
        break;
STREAM_OPEN_ERROR:
        if( p_sys->psz_url )
            VSL_SAFE_FREE( p_sys->psz_url );

        if( p_sys->p_stream )
            stream_Delete( p_sys->p_stream );
    }

    if( p_sys->i_duration_milli > 0 )
    {
        p_sys->i_bytes_per_second = p_access->info.i_size / ( p_sys->i_duration_milli / 1000 );
    }

    /* at least 25KBps ~= 200kbps */
    if( p_sys->i_bytes_per_second < 25000 )
        p_sys->i_bytes_per_second = 25000;
    msg_Info( p_access, "segment: %"PRId64"KB/s = %"PRId64"KB / %"PRId64" sec",
              (int64_t)p_sys->i_bytes_per_second / 1000,
               p_access->info.i_size / 1000,
               p_sys->i_duration_milli / 1000 );

    if( p_sys->b_continuous )
    {
        /* disable http range */
        var_Create( p_access, "http-continuous", VLC_VAR_BOOL );
        var_SetBool( p_access, "http-continuous", true );
    }

    return VLC_SUCCESS;

EXIT_ERROR:
    free( p_sys->psz_url );
    p_sys->psz_url = NULL;

    if( p_sys->p_stream )
    {
        stream_Delete( p_sys->p_stream );
        p_sys->p_stream = NULL;
    }

    return VLC_EGENERIC;
}

static void Close( vlc_object_t *p_this )
{
    access_t     *p_access = (access_t*)p_this;
    access_sys_t *p_sys = p_access->p_sys;

    if( p_sys->p_stream )
    {
        stream_Delete( p_sys->p_stream );
        p_sys->p_stream = NULL;
    }

    free( p_sys->psz_url );
    p_sys->psz_url = NULL;
}

static ssize_t Read( access_t *p_access, uint8_t *p_buffer, size_t i_len )
{
    access_sys_t *p_sys = p_access->p_sys;
    stream_t     *p_stream = p_sys->p_stream;
    int          i_read_ret = -1;

    //msg_Err( p_stream, "Read %d", i_len );

    if( !p_stream )
        goto EXIT_FAIL;

    if( p_access->info.i_size != 0 )
    {
        /* Remaining bytes in the file */
        uint64_t i_remaining = p_access->info.i_size - p_access->info.i_pos;
        if( i_remaining < i_len )
            i_len = i_remaining;
    }
    if( i_len <= 0 )
    {
        i_read_ret = 0;
        goto EXIT_FAIL;
    }

    i_read_ret = stream_Read( p_stream, p_buffer, i_len );
    if( i_read_ret <= 0 )
        goto EXIT_FAIL;

    assert( i_read_ret > 0 );
    assert( p_access->info.i_pos <= p_access->info.i_size );
    assert( p_access->info.i_pos + i_read_ret <= p_access->info.i_size );
    p_access->info.i_pos += i_read_ret;

    //msg_Err( p_stream, "Read ok %d", i_read_ret );
    return i_read_ret;

EXIT_FAIL:
    //msg_Err( p_stream, "Read failed %d", i_read_ret );

    assert( i_read_ret <= 0 );
    if( i_read_ret <= 0 )
        p_access->info.b_eof = true;

    return i_read_ret;
}

static int stream_ReadSeek( stream_t *p_stream, uint64_t i_seek_forward_bytes ) {
    int i_skipped = 0;

    while( (uint64_t) i_skipped < i_seek_forward_bytes )
    {
        char buf[4096];
        int i_to_read = sizeof( buf );
        int64_t i_unread = i_seek_forward_bytes - i_skipped;
        if( i_to_read > i_unread )
            i_to_read = i_unread;

        int i_skip_ret = stream_Read( p_stream, buf, i_to_read );
        if( i_skip_ret <= 0 )
            return -1;

        i_skipped += i_skip_ret;
    }

    return i_skipped;
}

static int SkipSeekedUnknownHeader( access_t *p_access, stream_t *p_stream, uint64_t i_seek_pos )
{
    if( i_seek_pos > p_access->info.i_size )
        return -1;

    if( i_seek_pos == p_access->info.i_size )
        return 0;

    int64_t i_stream_len = stream_Size( p_stream );
    if( i_stream_len < 0 )
        return -1;

    int64_t i_whole_remain = p_access->info.i_size - i_seek_pos;
    if( i_stream_len < i_whole_remain )
    {
        msg_Err( p_access, "flvhead: too small seeked size: %"PRId64" < %"PRId64,
                 i_stream_len,
                 i_whole_remain);
        return -1;
    }

    int64_t i_align_diff = i_stream_len - i_whole_remain;
    msg_Dbg( p_access, "flvhead: align diff %"PRId64, i_align_diff );

    int64_t i_skipped = 0;
    while( i_skipped < i_align_diff )
    {
        int i_skip_ret = stream_ReadSeek( p_stream, i_align_diff - i_skipped );
        if( i_skip_ret <= 0 )
            return -1;

        i_skipped += i_skip_ret;
    }

    return i_skipped;
}

/* return skipped bytes */
static int SkipSeekedFlvHeader( access_t *p_access, stream_t *p_stream, uint64_t i_seek_pos )
{
    if( i_seek_pos >= p_access->info.i_size )
        return -1;

    uint64_t i_stream_len = stream_Size( p_stream );
    if( i_stream_len < sizeof( flv_header_t ) + 4 )
        return -1;

    /* Simple align with header check */
#if 1
#endif

    int i_peek = ( i_stream_len < 1024 ) ? (int) i_stream_len : 1024;
    const uint8_t *p_peek = NULL;
    int i_peek_ret = stream_Peek( p_stream, &p_peek, i_peek );
    if( i_peek_ret < i_peek )
        return -1;

    /* Look for flv tag */
    flv_header_t* p_header = (flv_header_t *) p_peek;
    if( 0 != memcmp( p_header->signature, "FLV", 3 ) )
    {
        msg_Warn( p_access, "flvhead: not a valid flv stream" );
        return -1;
    }

    uint64_t i_whole_remain = p_access->info.i_size - i_seek_pos;
    if( i_stream_len < i_whole_remain )
    {
        msg_Err( p_access, "flvhead: too small seeked size: %"PRId64" < %"PRId64,
                i_stream_len,
                i_whole_remain);
        return -1;
    }

    uint64_t i_align_diff = i_stream_len - i_whole_remain;
    msg_Dbg( p_access, "flvhead: align diff %"PRId64, i_align_diff );

    uint64_t i_skipped = 0;
    while( i_skipped < i_align_diff )
    {
        int i_skip_ret = stream_ReadSeek( p_stream, i_align_diff - i_skipped );
        if( i_skip_ret <= 0 )
            return -1;

        i_skipped += i_skip_ret;
    }

    return i_skipped;
#if 0
    /* TODO: reliable header check */

    int i_peek = ( i_stream_len < 1024 ) ? (int) i_stream_len : 1024;
    const uint8_t *p_peek = NULL;
    int i_peek_ret = stream_Peek( p_stream, &p_peek, i_peek );
    if( i_peek_ret < i_peek )
        return -1;

    /* Look for flv tag */
    flv_header_t* p_header = (flv_header_t *) p_peek;
    if( 0 != memcmp( p_header->signature, "FLV", 3 ) )
    {
        msg_Warn( p_access, "flvhead: not a valid flv stream" );
        return -1;
    }

    /* TODO: more check for flv header */

    /* Look for flv tag */
    bool b_audio_tag_skipped = false;
    bool b_video_tag_skipped = false;
    bool b_meta_tag_skipped = false;
    int i_scan = sizeof( flv_header_t ) + 4;
    msg_Dbg( p_access, "flvhead: skip flv_header %d", i_scan );
    for( int i = 0; i < 2; ++i )
    {
        flv_tag_t* p_tag = (flv_tag_t *) ( p_peek + i_scan );
        if( p_tag->type == FLV_TAG_AUDIO)
        {
            b_audio_tag_skipped = true;
            msg_Dbg( p_access, "flvhead: seeked audio tag skipped");
        }
        else if( p_tag->type == FLV_TAG_VIDEO )
        {
            b_video_tag_skipped = true;
            msg_Dbg( p_access, "flvhead: seeked video tag skipped");
        }
        else if( p_tag->type == FLV_TAG_META )
        {
            b_meta_tag_skipped = true;
            msg_Dbg( p_access, "flvhead: seeked meta tag skipped");
        }
        else
        {
            msg_Dbg( p_access, "flvhead: seeked unknown tag %x", (int) p_tag->type);
            break;
        }

        int i_body_len =
        ( ( (uint32_t) p_tag->be_body_length[0] ) << 16 ) |
        ( ( (uint32_t) p_tag->be_body_length[1] ) << 8 ) |
        ( ( (uint32_t) p_tag->be_body_length[2] ) );

        const uint8_t *p_previous_tag_size = p_peek + i_scan + sizeof( flv_tag_t ) + i_body_len;
        int i_previous_tag_size =
        ( ( (uint32_t) p_previous_tag_size[0] ) << 24 ) |
        ( ( (uint32_t) p_previous_tag_size[1] ) << 16 ) |
        ( ( (uint32_t) p_previous_tag_size[2] ) << 8 ) |
        ( ( (uint32_t) p_previous_tag_size[3] ) );

        int tag_size = sizeof( flv_tag_t ) + i_body_len + 4;
        msg_Dbg( p_access, "flvhead: skip flv_tag %d=%d+%d+4, (%d) now at %d",
                tag_size, (int) sizeof( flv_tag_t ), i_body_len,
                i_previous_tag_size,
                i_scan + tag_size );
        if( i_scan + tag_size >= i_peek_ret )
            break;

        i_scan += tag_size;
    }

    if( !b_audio_tag_skipped )
        msg_Dbg( p_access, "flvhead: seeked audio tag not found");

    if( !b_audio_tag_skipped )
        msg_Dbg( p_access, "flvhead: seeked audio tag not found");

    if( !b_audio_tag_skipped )
        msg_Dbg( p_access, "flvhead: seeked audio tag not found");

    msg_Dbg( p_access, "flvhead: skip %d bytes", i_scan );
    int i_skip_ret = stream_ReadSeek( p_stream, i_scan );
    return i_skip_ret;
#endif
}

static stream_t *stream_vslNewSinaSeek( access_t* p_access, uint64_t i_pos )
{
    access_sys_t *p_sys = p_access->p_sys;
    char* p_seek_url = NULL;
    stream_t *p_stream = NULL;

    msg_Info( p_access, "segment: sina: seek to %"PRId64, i_pos );

    /* */
    const char* p_prefix = strchr( p_sys->psz_url, '?' ) ? "&" : "?";
    if( asprintf( &p_seek_url, "%s%sstart=%"PRId64, p_sys->psz_url, p_prefix, i_pos ) < 0)
        goto EXIT_FAIL;

    p_stream = stream_UrlNew( p_access, p_seek_url );
    if( !p_stream )
    {
        msg_Err( p_access, "segment: sina: seek: failed to open stream %s", p_seek_url );
        goto EXIT_FAIL;
    }

    /* Sina add extra flv header and flv tag after seeked */
    int i_skip_ret = SkipSeekedFlvHeader( p_access, p_stream, i_pos );
    if( i_skip_ret <= 0 )
    {
        msg_Err( p_access, "segment: sina: seek: failed to skip flv header" );
        goto EXIT_FAIL;
    }

    return p_stream;

EXIT_FAIL:
    if( p_stream != NULL )
    {
        stream_Delete( p_stream );
        p_stream = NULL;
    }

    VSL_SAFE_FREE( p_seek_url );

    return NULL;
}

static stream_t *stream_vslNewYoukuSeek( access_t* p_access, uint64_t i_pos )
{
    access_sys_t *p_sys = p_access->p_sys;
    char* p_seek_url = NULL;
    stream_t *p_stream = NULL;
    bool b_seeked_forward = false;

    int64_t i_seek_to_second = 0;
    int64_t i_last_backward_seeked_second = p_sys->i_duration_milli / 1000;

    msg_Info( p_access, "segment: youku: seek to %"PRId64, i_pos );
    if( p_access->info.i_size <= 0 )
    {
        msg_Err( p_access, "segment: youku: seek: invalid info.i_size < 0" );
        goto EXIT_FAIL;
    }

    i_seek_to_second = i_pos / p_sys->i_bytes_per_second;
    if( i_seek_to_second >= 15 )
        i_seek_to_second -= 15;
    else
        i_seek_to_second = 0;

    do {
        if( p_stream != NULL )
        {
            stream_Delete( p_stream );
            p_stream = NULL;
        }
        VSL_SAFE_FREE( p_seek_url );

        /* */
        msg_Info( p_access, "segment: youku: seek to %"PRId64" seconds", i_seek_to_second );
        const char* p_prefix = strchr( p_sys->psz_url, '?' ) ? "&" : "?";
        if( i_seek_to_second > 0 )
        {
            if( asprintf( &p_seek_url, "%s%sstart=%"PRId64, p_sys->psz_url, p_prefix, i_seek_to_second ) < 0)
                goto EXIT_FAIL;
        }
        else
        {
            p_seek_url = strdup( p_sys->psz_url );
        }

        p_stream = stream_UrlNew( p_access, p_seek_url );
        if( !p_stream )
        {
            msg_Err( p_access, "segment: youku: seek: failed to open stream %s", p_seek_url );
            goto EXIT_FAIL;
        }

        uint64_t i_stream_len = stream_Size( p_stream );
        if( i_stream_len <= 0 )
        {
            msg_Err( p_access, "segment: youku: seek: invalid stream size" );
            goto EXIT_FAIL;
        }

        int64_t i_seeked = p_access->info.i_size - i_stream_len;
        if( i_seeked < 0 )
        {
            msg_Err( p_access, "segment: youku: seek: seeked before stream start?" );
            goto EXIT_FAIL;
        }
        else if( i_seeked > (int64_t) i_pos )
        {
            int64_t i_diff_bytes = i_seeked - i_pos;
            msg_Warn( p_access, "segment: youku: seek: after seeking pos %"PRId64" > %"PRId64", need reseeking",
                    i_seeked,
                    i_pos);
            int64_t i_diff_seconds = i_diff_bytes / p_sys->i_bytes_per_second;

            /* seek to 5 seconds before guessed seek point */
            if( i_seek_to_second > i_diff_seconds )
                i_seek_to_second = i_seek_to_second - i_diff_seconds - 5;
            else
                i_seek_to_second = 0;

            i_last_backward_seeked_second = i_seek_to_second;
            continue;
        }
        else if( !b_seeked_forward && i_seeked + 1000 * 1000 > (int64_t) i_pos )
        {
            int64_t i_diff_bytes = i_pos - i_seeked;
            msg_Warn( p_access, "segment: youku: seeked too far before seeking pos %"PRId64" < %"PRId64", need reseeking",
                     i_seeked,
                     i_pos);
            int64_t i_diff_seconds = ( i_diff_bytes - 500 * 1000 ) / p_sys->i_bytes_per_second;
            if( i_seek_to_second + i_diff_seconds > i_last_backward_seeked_second )
            {
                /* there is no sense to seek after last seeked second */
                break;
            }

            i_seek_to_second += i_diff_seconds;
        }

        break;
    } while( i_seek_to_second > 5 );

    if( !p_stream )
        goto EXIT_FAIL;

    /* Youku seeked by seconds, should align to stream seek position */
    int i_skip_ret = SkipSeekedUnknownHeader( p_access, p_stream, i_pos );
    if( i_skip_ret < 0 )
    {
        msg_Err( p_access, "segment: youku: seek: failed to skip unknown header" );
        goto EXIT_FAIL;
    }

    VSL_SAFE_FREE( p_seek_url );
    return p_stream;

EXIT_FAIL:
    if( p_stream != NULL )
    {
        stream_Delete( p_stream );
        p_stream = NULL;
    }

    VSL_SAFE_FREE( p_seek_url );
    return NULL;
}

static stream_t *stream_vslNewSeek( access_t* p_access, uint64_t i_pos )
{
    access_sys_t *p_sys = p_access->p_sys;

    msg_Info( p_access, "segment: vsl: seek to %"PRId64, i_pos );
    stream_t *p_stream = stream_UrlNew( p_access, p_sys->psz_url);
    if( !p_stream )
    {
        msg_Err( p_access, "segment: failed to open stream %s", p_sys->psz_url );
        goto EXIT_FAIL;
    }

    int i_ret = stream_Seek( p_stream, i_pos );
    if( i_ret != VLC_SUCCESS )
    {
        msg_Err( p_access, "segment: failed to seek" );
        goto EXIT_FAIL;
    }

    return p_stream;

EXIT_FAIL:
    if( p_stream != NULL )
    {
        stream_Delete( p_stream );
        p_stream = NULL;
    }

    return NULL;
}

static int Seek( access_t *p_access, uint64_t i_pos )
{
    access_sys_t *p_sys = p_access->p_sys;
    stream_t *p_stream = NULL;

    msg_Info( p_access, "segment: seek %"PRId64"/%"PRId64, i_pos, p_access->info.i_size );

    if( i_pos > p_access->info.i_size )
        goto EXIT_FAIL;

    if( i_pos == p_access->info.i_size )
    {
        p_access->info.i_pos = i_pos;
        p_access->info.b_eof = true;
        return VLC_SUCCESS;
    }

    if( i_pos == p_access->info.i_pos )
        return VLC_SUCCESS;

    /* optimize short seek */
    if( i_pos > p_access->info.i_pos &&
        i_pos < p_access->info.i_pos + 128 * 1024 )
    {
        int i_seek_forward = p_access->info.i_pos - i_pos;
        int i_seek_ret = stream_ReadSeek( p_sys->p_stream, i_seek_forward );
        if( i_seek_ret <= 0 )
            goto EXIT_FAIL;

        p_access->info.i_pos += i_seek_ret;
        p_access->info.b_eof = ( p_access->info.i_pos >= p_access->info.i_size );
        return VLC_SUCCESS;
    }

    if( p_sys->pfn_new_seekd_stream )
    {
        /* close previous stream first */
        if( p_sys->p_stream )
        {
            stream_Delete( p_sys->p_stream );
            p_sys->p_stream = NULL;
        }

        /* */
        p_stream = p_sys->pfn_new_seekd_stream( p_access, i_pos );
        if( !p_stream )
        {
            msg_Err( p_access, "segment: failed to open seeked stream %s", p_sys->psz_url );
            goto EXIT_FAIL;
        }

        p_sys->p_stream = p_stream;
    }
    else
    {
        /* direct seek */
        int i_ret = stream_Seek( p_sys->p_stream, i_pos );
        if( i_ret != VLC_SUCCESS )
        {
            msg_Err( p_access, "segment: failed to seek" );
            goto EXIT_FAIL;
        }
    }

    p_access->info.i_pos = i_pos;
    p_access->info.b_eof = ( i_pos >= p_access->info.i_size );

    return VLC_SUCCESS;

EXIT_FAIL:
    if( p_stream )
        stream_Delete( p_stream );

    return VLC_EGENERIC;
}

static int Control( access_t *p_access, int i_query, va_list args )
{
    access_sys_t *p_sys = p_access->p_sys;
    stream_t     *p_stream = p_sys->p_stream;

    switch( i_query )
    {
            /* */
        case ACCESS_CAN_SEEK:
            *(bool*)va_arg( args, bool* ) = p_sys->b_seekable;
            return VLC_SUCCESS;

        case ACCESS_CAN_FASTSEEK:
            *(bool*)va_arg( args, bool* ) = false;
            return VLC_SUCCESS;

        case ACCESS_CAN_PAUSE:
        case ACCESS_CAN_CONTROL_PACE:
            *(bool*)va_arg( args, bool* ) = true;
            return VLC_SUCCESS;

            /* */
        case ACCESS_GET_PTS_DELAY:
        {
            int64_t *pi_64 = (int64_t*)va_arg( args, int64_t * );
            *pi_64 = INT64_C(1000)
            * var_InheritInteger( p_access, "network-caching" );
            return VLC_SUCCESS;
        }

            /* */
        case ACCESS_SET_PAUSE_STATE:
            return VLC_SUCCESS;

        case ACCESS_GET_CONTENT_TYPE:
            return stream_vaControl( p_stream, STREAM_GET_CONTENT_TYPE, args );

        case ACCESS_GET_TITLE_INFO:
        case ACCESS_SET_TITLE:
        case ACCESS_SET_SEEKPOINT:
        case ACCESS_SET_PRIVATE_ID_STATE:
            return VLC_EGENERIC;

        default:
            msg_Warn( p_access, "unimplemented query in control" );
            return VLC_EGENERIC;
    }
}