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

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define BITS_PER_LONG (sizeof(long) * 8)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

int ffmemless_play(int effect_id, int device_file, int play)
{
	struct input_event event;

	event.type = EV_FF;
	event.code = effect_id;
	event.value = play;

	if (write(device_file, (const void*) &event, sizeof(event)) == -1)
		return -1;
	else
		return 0;
}

int ffmemless_upload_effect(struct ff_effect *effect, int device_file)
{
	if (ioctl(device_file, EVIOCSFF, effect) == -1) {
		perror("Vibra upload effect");
		return -1;
	} else {
		return 0;
	}
}

int ffmemless_erase_effect(int effect_id, int device_file)
{
	if (ioctl(device_file, EVIOCRMFF, effect_id) == -1) {
		perror("Vibra erase effect");
		return -1;
	} else {
		return 0;
	}
}

int ffmemless_evdev_file_open(const char *file_name)
{
	int result, fp;
	unsigned long features[4];

	fp = open(file_name, O_RDWR);

	if (fp == -1) {
		perror("test file open");
		return fp;
	}

	/* Query device */
	if (ioctl(fp, EVIOCGBIT(EV_FF, sizeof(unsigned long) * 4),
							features) < 0) {
		perror("Ioctl query failed");
		close(fp);
		return -1;
	}
	result = test_bit(FF_RUMBLE, features);
	result = result && test_bit(FF_PERIODIC, features);
	if (result) {
		return fp;
	} else {
		close(fp);
		errno = ENOTSUP;
		return -1;
	}
}

int ffmemless_evdev_file_search(void)
{
	int result, i = 0;
	int fp = 1;
	char device_file_name[24];
	unsigned long features[4];

	/* fail safe stop at 256 devices */
	while (fp && i < 256) {
		sprintf(device_file_name, "/dev/input/event%d", i);
		fp = open(device_file_name, O_RDWR);
		if (fp == -1) {
			perror("test file open");
			break;
		}

		/* Query device */
		if (ioctl(fp, EVIOCGBIT(EV_FF, sizeof(unsigned long) * 4),
								features) < 0) {
			perror("Ioctl query failed");
			close(fp);
			i++;
			continue;
		}
		result = test_bit(FF_RUMBLE, features);
		result = result && test_bit(FF_PERIODIC, features);
		if (result)
			return fp;

		close(fp);
		i++;
	}
	return -1;
}

int ffmemless_evdev_file_close(int file)
{
	return close(file);
}
