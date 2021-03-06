/*
 * afccheck.c
 * creates threads and check communication through AFC is done rigth
 *
 * Copyright (c) 2008 Jonathan Beck All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA 
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>

#include <libiphone/libiphone.h>
#include <libiphone/lockdown.h>
#include <libiphone/afc.h>

#define BUFFER_SIZE 20000
#define NB_THREADS 10


typedef struct {
	afc_client_t afc;
	int id;
} param;


static void check_afc(gpointer data)
{
	//prepare a buffer
	unsigned int buffersize = BUFFER_SIZE * sizeof(unsigned int);
	int *buf = (int *) malloc(buffersize);
	int *buf2 = (int *) malloc(buffersize);
	unsigned int bytes = 0;
	uint64_t position = 0;
	
	//fill buffer
	int i = 0;
	for (i = 0; i < BUFFER_SIZE; i++) {
		buf[i] = ((param *) data)->id * i;
	}

	//now  writes buffer on iphone
	uint64_t file = 0;
	char path[50];
	sprintf(path, "/Buf%i", ((param *) data)->id);
	afc_file_open(((param *) data)->afc, path, AFC_FOPEN_RW, &file);
	afc_file_write(((param *) data)->afc, file, (char *) buf, buffersize, &bytes);
	afc_file_close(((param *) data)->afc, file);
	file = 0;
	if (bytes != buffersize)
		printf("Write operation failed\n");

	//now read it
	bytes = 0;
	afc_file_open(((param *) data)->afc, path, AFC_FOPEN_RDONLY, &file);
	afc_file_read(((param *) data)->afc, file, (char *) buf2, buffersize/2, &bytes);
	afc_file_read(((param *) data)->afc, file, (char *) buf2 + (buffersize/2), buffersize/2, &bytes);
	if(AFC_E_SUCCESS != afc_file_tell(((param *) data)->afc, file, &position))
		printf("Tell operation failed\n");
	afc_file_close(((param *) data)->afc, file);
	if (position != buffersize)
		printf("Read operation failed\n");

	//compare buffers
	for (i = 0; i < BUFFER_SIZE; i++) {
		if (buf[i] != buf2[i]) {
			printf("Buffers are differents, stream corrupted\n");
			break;
		}
	}

	//cleanup
	afc_remove_path(((param *) data)->afc, path);
	g_thread_exit(0);
}

int main(int argc, char *argv[])
{
	lockdownd_client_t client = NULL;
	iphone_device_t phone = NULL;
	GError *err;
	int port = 0;
	afc_client_t afc = NULL;

	if (argc > 1 && !strcasecmp(argv[1], "--debug")) {
		iphone_set_debug_level(1);
		iphone_set_debug_mask(DBGMASK_ALL);
	} else {
		iphone_set_debug_level(0);
		iphone_set_debug_mask(DBGMASK_NONE);
	}

	if (IPHONE_E_SUCCESS != iphone_device_new(&phone, NULL)) {
		printf("No iPhone found, is it plugged in?\n");
		return 1;
	}

	if (LOCKDOWN_E_SUCCESS != lockdownd_client_new(phone, &client)) {
		iphone_device_free(phone);
		return 1;
	}

	if (LOCKDOWN_E_SUCCESS == lockdownd_start_service(client, "com.apple.afc", &port) && !port) {
		lockdownd_client_free(client);
		iphone_device_free(phone);
		fprintf(stderr, "Something went wrong when starting AFC.");
		return 1;
	}

	afc_client_new(phone, port, &afc);

	//makes sure thread environment is available
	if (!g_thread_supported())
		g_thread_init(NULL);

	GThread *threads[NB_THREADS];
	param data[NB_THREADS];

	int i = 0;
	for (i = 0; i < NB_THREADS; i++) {
		data[i].afc = afc;
		data[i].id = i + 1;
		threads[i] = g_thread_create((GThreadFunc) check_afc, data + i, TRUE, &err);
	}

	for (i = 0; i < NB_THREADS; i++) {
		g_thread_join(threads[i]);
	}

	lockdownd_client_free(client);
	iphone_device_free(phone);

	return 0;
}
