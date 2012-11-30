/*
 * ngfd - Non-graphic feedback daemon
 *
 * Copyright (C) 2010 Nokia Corporation.
 * Contact: Xun Chen <xun.chen@nokia.com>
 *
 * This work is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This work is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef N_CORE_PLAYER_H
#define N_CORE_PLAYER_H

#include "request-internal.h"
#include "core-internal.h"
#include "sinkinterface-internal.h"

typedef enum _NCorePlayerState
{
    N_CORE_EVENT_FAILED         = 0,
    N_CORE_EVENT_COMPLETED      = 1,
    N_CORE_EVENT_PLAYING        = 2,
    N_CORE_EVENT_PAUSED         = 3
} NCorePlayerState;

int  n_core_play_request     (NCore *core, NRequest *request);
int  n_core_pause_request    (NCore *core, NRequest *request);
int  n_core_resume_request   (NCore *core, NRequest *request);
void n_core_stop_request     (NCore *core, NRequest *request, guint timeout);

void n_core_set_resync_on_master (NCore *core, NSinkInterface *sink, NRequest *request);
void n_core_resynchronize_sinks  (NCore *core, NSinkInterface *sink, NRequest *request);
void n_core_synchronize_sink     (NCore *core, NSinkInterface *sink, NRequest *request);
void n_core_complete_sink        (NCore *core, NSinkInterface *sink, NRequest *request);
void n_core_fail_sink            (NCore *core, NSinkInterface *sink, NRequest *request);

#endif /* N_CORE_PLAYER_ H */
