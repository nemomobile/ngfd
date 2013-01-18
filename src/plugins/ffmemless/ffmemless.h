/*
 * ngfd - Non-graphic feedback daemon, ffmemless backend helper functions
 *
 * Copyright (C) 2013 Jolla Ltd.
 * Contact: Kalle Jokiniemi <kalle.jokiniemi@jollamobile.com>
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
#ifndef __FFMEMLESS_H
#define __FFMEMLESS_H

#define FF_DIR_FORWARD	0x4000
#define FF_DIR_REVERSE	0xC000

int ffmemless_play(int effect_id, int device_file, int play);
int ffmemless_upload_effect(struct ff_effect *effect, int device_file);

/**
 * ffmemless_evdev_file_open - Open given device file.
 *
 * Opens a file and checks whether it supports FF_RUMBLE and FF_PERIODIC type
 * of force feedback effects and returns the file pointer if it does.
 *
 * @param device_file_name A pointer to file name. E.g. "/dev/input/event2"
 * @returns File descriptor on success, -1 on error.
 */
int ffmemless_evdev_file_open(const char *device_file_name);

/**
 * ffmemless_evdev_file_search - Search first device node with FF support.
 *
 * This function searches through /dev/input/event? files for first one that
 * supports FF_RUMBLE and FF_PRERIODIC type of force feedback effects. Once
 * a device node is found, the function returns a open file descriptor
 * to the device node.
 *
 * @returns	File descriptor on success, -1 if no suitable device node found.
 */
int ffmemless_evdev_file_search(void);

/**
 * ffmemless_evdev_file_close - Close a event device node.
 * NOTE: closing the file descriptor will erase all uploaded effects.
 *
 * @param device_file An open file descriptor to be closed
 * @returns 0 on success, -1 on error.
 */
int ffmemless_evdev_file_close(int device_file);

#endif