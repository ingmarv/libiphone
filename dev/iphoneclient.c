/*
 * main.c
 * Rudimentary interface to the iPhone
 *
 * Copyright (c) 2008 Zach C. All Rights Reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glib.h>

#include <libiphone/libiphone.h>
#include <libiphone/lockdown.h>
#include <libiphone/afc.h>
#include <libiphone/notification_proxy.h>
#include "../src/utils.h"

static void notifier(const char *notification)
{
	printf("---------------------------------------------------------\n");
	printf("------> Notification received: %s\n", notification);
	printf("---------------------------------------------------------\n");
}

static void perform_notification(iphone_device_t phone, lockdownd_client_t client, const char *notification)
{
	int nport = 0;
	np_client_t np;

	lockdownd_start_service(client, "com.apple.mobile.notification_proxy", &nport);
	if (nport) {
		printf("::::::::::::::: np was started ::::::::::::\n");
		np_client_new(phone, nport, &np);
		if (np) {
			printf("::::::::: PostNotification %s\n", notification);
			np_post_notification(np, notification);
			np_client_free(np);
		}
	} else {
		printf("::::::::::::::: np was NOT started ::::::::::::\n");
	}
}

int main(int argc, char *argv[])
{
	unsigned int bytes = 0;
	int port = 0, i = 0;
	int npp;
	lockdownd_client_t client = NULL;
	iphone_device_t phone = NULL;
	uint64_t lockfile = 0;
	np_client_t gnp = NULL;

	if (argc > 1 && !strcasecmp(argv[1], "--debug")) {
		iphone_set_debug_level(1);
		iphone_set_debug_mask(DBGMASK_ALL);
	} else {
		iphone_set_debug_level(0);
		iphone_set_debug_mask(DBGMASK_NONE);
	}

	if (IPHONE_E_SUCCESS != iphone_device_new(&phone, NULL)) {
		printf("No iPhone found, is it plugged in?\n");
		return -1;
	}

	char *uuid = NULL;
	if (IPHONE_E_SUCCESS == iphone_device_get_uuid(phone, &uuid)) {
		printf("DeviceUniqueID : %s\n", uuid);
	}
	if (uuid)
		free(uuid);

	if (LOCKDOWN_E_SUCCESS != lockdownd_client_new(phone, &client)) {
		iphone_device_free(phone);
		printf("Exiting.\n");
		return -1;
	}

	char *nnn = NULL;
	if (LOCKDOWN_E_SUCCESS == lockdownd_get_device_name(client, &nnn)) {
		printf("DeviceName : %s\n", nnn);
		free(nnn);
	}

	lockdownd_start_service(client, "com.apple.afc", &port);

	if (port) {
		afc_client_t afc = NULL;
		afc_client_new(phone, port, &afc);
		if (afc) {
			lockdownd_start_service(client, "com.apple.mobile.notification_proxy", &npp);
			if (npp) {
				printf("Notification Proxy started.\n");
				np_client_new(phone, npp, &gnp);
			} else {
				printf("ERROR: Notification proxy could not be started.\n");
			}
			if (gnp) {
				const char *nspec[5] = {
					NP_SYNC_CANCEL_REQUEST,
					NP_SYNC_SUSPEND_REQUEST,
					NP_SYNC_RESUME_REQUEST,
					NP_ITDBPREP_DID_END,
					NULL
				};
				np_observe_notifications(gnp, nspec);
				np_set_notify_callback(gnp, notifier);
			}

			perform_notification(phone, client, NP_SYNC_WILL_START);

			afc_file_open(afc, "/com.apple.itunes.lock_sync", AFC_FOPEN_RW, &lockfile);
			if (lockfile) {
				printf("locking file\n");
				afc_file_lock(afc, lockfile, AFC_LOCK_EX);

				perform_notification(phone, client, NP_SYNC_DID_START);
			}

			char **dirs = NULL;
			afc_read_directory(afc, "/eafaedf", &dirs);
			if (!dirs)
				afc_read_directory(afc, "/", &dirs);
			printf("Directory time.\n");
			for (i = 0; dirs[i]; i++) {
				printf("/%s\n", dirs[i]);
			}

			g_strfreev(dirs);

			dirs = NULL;
			afc_get_device_info(afc, &dirs);
			if (dirs) {
				for (i = 0; dirs[i]; i += 2) {
					printf("%s: %s\n", dirs[i], dirs[i + 1]);
				}
			}
			g_strfreev(dirs);

			uint64_t my_file = 0;
			char **info = NULL;
			uint64_t fsize = 0;
			if (AFC_E_SUCCESS == afc_get_file_info(afc, "/readme.libiphone.fx", &info) && info) {
				for (i = 0; info[i]; i += 2) {
					printf("%s: %s\n", info[i], info[i+1]);
					if (!strcmp(info[i], "st_size")) {
						fsize = atoll(info[i+1]);
					}
				}
			}

			if (AFC_E_SUCCESS ==
				afc_file_open(afc, "/readme.libiphone.fx", AFC_FOPEN_RDONLY, &my_file) && my_file) {
				printf("A file size: %llu\n", (long long)fsize);
				char *file_data = (char *) malloc(sizeof(char) * fsize);
				afc_file_read(afc, my_file, file_data, fsize, &bytes);
				if (bytes > 0) {
					printf("The file's data:\n");
					fwrite(file_data, 1, bytes, stdout);
				}
				printf("\nClosing my file.\n");
				afc_file_close(afc, my_file);
				free(file_data);
			} else
				printf("couldn't open a file\n");

			afc_file_open(afc, "/readme.libiphone.fx", AFC_FOPEN_WR, &my_file);
			if (my_file) {
				char *outdatafile = strdup("this is a bitchin text file\n");
				afc_file_write(afc, my_file, outdatafile, strlen(outdatafile), &bytes);
				free(outdatafile);
				if (bytes > 0)
					printf("Wrote a surprise. ;)\n");
				else
					printf("I wanted to write a surprise, but... :(\n");
				afc_file_close(afc, my_file);
			}
			printf("Deleting a file...\n");
			bytes = afc_remove_path(afc, "/delme");
			if (bytes)
				printf("Success.\n");
			else
				printf("Failure. (expected unless you have a /delme file on your phone)\n");

			printf("Renaming a file...\n");
			bytes = afc_rename_path(afc, "/renme", "/renme2");
			if (bytes > 0)
				printf("Success.\n");
			else
				printf("Failure. (expected unless you have a /renme file on your phone)\n");

			printf("Seek & read\n");
			afc_file_open(afc, "/readme.libiphone.fx", AFC_FOPEN_RDONLY, &my_file);
			if (AFC_E_SUCCESS != afc_file_seek(afc, my_file, 5, SEEK_CUR))
				printf("WARN: SEEK DID NOT WORK\n");
			char *threeletterword = (char *) malloc(sizeof(char) * 5);
			afc_file_read(afc, my_file, threeletterword, 3, &bytes);
			threeletterword[3] = '\0';
			if (bytes > 0)
				printf("Result: %s\n", threeletterword);
			else
				printf("Couldn't read!\n");
			free(threeletterword);
			afc_file_close(afc, my_file);
		}

		if (gnp && lockfile) {
			printf("XXX sleeping\n");
			sleep(5);

			printf("XXX unlocking file\n");
			afc_file_lock(afc, lockfile, AFC_LOCK_UN);

			printf("XXX closing file\n");
			afc_file_close(afc, lockfile);

			printf("XXX sleeping\n");
			sleep(5);
			//perform_notification(phone, client, NP_SYNC_DID_FINISH);
		}

		if (gnp) {
			np_client_free(gnp);
			gnp = NULL;
		}

		afc_client_free(afc);
	} else {
		printf("Start service failure.\n");
	}

	printf("All done.\n");

	lockdownd_client_free(client);
	iphone_device_free(phone);

	return 0;
}
