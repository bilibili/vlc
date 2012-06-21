/*****************************************************************************
 * vlc_es_out_managed.h
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_es.h>
#include <vlc_es_out.h>

/****************************************************************************
 * ES_OUT managed
 *
 * Created a managed es_out, which can be destroyed
 * without destroying original es_out_t
 *
 * Share a single es_out for multiple demux, at least working for demux/avformat
 *
 * call es_out_destroy() to destroy managed es_out
 ****************************************************************************/

VLC_API es_out_t *demux_EsOutManagedNew( demux_t *p_demux, es_out_t *p_backend_es_out );