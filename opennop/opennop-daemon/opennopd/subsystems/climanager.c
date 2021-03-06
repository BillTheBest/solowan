/*

  climanager.c

  This file is part of OpenNOP-SoloWAN distribution.
  It is a modified version of the file originally distributed inside OpenNOP.

  Original code Copyright (C) 2014 OpenNOP.org (yaplej@opennop.org)

  Modifications Copyright (C) 2014 Center for Open Middleware (COM)
                                   Universidad Politecnica de Madrid, SPAIN

  Modifications description: <mod_description>

    OpenNOP is an open source Linux based network accelerator designed 
    to optimise network traffic over point-to-point, partially-meshed and 
    full-meshed IP networks.

    OpenNOP-SoloWAN is an enhanced version of the Open Network Optimization
    Platform (OpenNOP) developed to add it deduplication capabilities using
    a modern dictionary based compression algorithm.

    SoloWAN is a project of the Center for Open Middleware (COM) of Universidad
    Politecnica de Madrid which aims to experiment with open-source based WAN
    optimization solutions.

  References:

    OpenNOP: http://www.opennop.org
    SoloWAN: solowan@centeropenmiddleware.com
             https://github.com/centeropenmiddleware/solowan/wiki
    Center for Open Middleware (COM): http://www.centeropenmiddleware.com
    Universidad Politecnica de Madrid (UPM): http://www.upm.es

  License:

    OpenNOP and OpenNOP-SoloWAN are free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenNOP and OpenNOP-SoloWAN are distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Foobar.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <strings.h>
#include <pthread.h> // for multi-threading
#include <sys/socket.h>
#include <sys/un.h>

#include <linux/types.h>

#include "climanager.h"
#include "clicommands.h"
#include "clisocket.h"
#include "logger.h"

void start_cli_server() {
	int socket_desc, client_sock, c, *new_sock, length;
	struct sockaddr_un server, client;
        char message[LOGSZ];
        pthread_attr_t th_attr;
        pthread_t sniffer_thread;
        int thread_res = 0;
        int res;

        sprintf(message, "Inizialize num_tot_sock, num_sock = 0.\n");
        logger(LOG_INFO, message);
        num_sock = 0;
        num_tot_sock = 0;

	//Create socket
	socket_desc = socket(AF_UNIX, SOCK_STREAM, 0);
	if (socket_desc == -1) {
		printf("Could not create socket");
	}
	puts("[CLIMANAGER] Socket created");

	//Prepare the sockaddr_in structure
	server.sun_family = AF_UNIX;
	strcpy(server.sun_path, OPENNOP_SOCK);
	unlink(server.sun_path);

	//Bind the socket
	length = strlen(server.sun_path) + sizeof(server.sun_family);

	if (bind(socket_desc, (struct sockaddr *) &server, length) < 0) {
		//print the error message
		perror("bind failed. Error");
		exit(1);
	}
	puts("[CLIMANAGER] bind done");

	//Listen
	if (listen(socket_desc, 1) == -1) {
		perror("[cli_manager]:Listen");
		exit(1);
	}

	//Accept and incoming connection
	puts("Waiting for incoming connections...");
	c = sizeof(struct sockaddr_un);

	while ((client_sock = accept(socket_desc, (struct sockaddr *) &client,
			(socklen_t*) &c))) {
		puts("Connection accepted");

		new_sock = malloc(sizeof(client_sock));
		if (new_sock == NULL) {
			perror("Not enought memory to create client socket");
                        exit(1);
		}
		*new_sock = client_sock;

                res = pthread_attr_init(&th_attr);
                if (res != 0) {
                        sprintf(message, "Attribute init failed");
                        logger(LOG_INFO, message);
                }
                res = pthread_attr_setdetachstate(&th_attr, PTHREAD_CREATE_DETACHED);
                if (res != 0) {
                        sprintf(message, "Setting detached state failed");
                        logger(LOG_INFO, message);
                }

		if (pthread_create(&sniffer_thread, &th_attr, client_handler,
				(void*) new_sock) < 0) {
                        sprintf(message, "Could not create thread");
                        logger(LOG_INFO, message);
		}

		//Now join the thread , so that we dont terminate before the thread
		//pthread_join( sniffer_thread , NULL);
                sprintf(message, "Result of thread creation: %d\n", thread_res);
                logger(LOG_INFO, message);
                puts("Handler assigned.");
                pthread_attr_destroy(&th_attr);
	}

	if (client_sock < 0) {
		perror("accept failed");
		exit(1);
	}

	return;
}
void *client_handler(void *socket_desc) {
	//Get the socket descriptor
	int sock = *(int*) socket_desc;
	int read_size, finish = 0;
	char client_message[MAX_BUFFER_SIZE];
	char message[LOGSZ];

	sprintf(message, "Started cli connection.\n");
	logger(LOG_INFO, message);

        num_sock++;
        num_tot_sock++;
        sprintf(message, "Established. Number of established sockets: %d. Total: %d.\n", num_sock, num_tot_sock);
        logger(LOG_INFO, message);

	//cli_prompt(sock);

	//Receive a message from client
	while ((finish != CLI_SHUTDOWN) && ((read_size = recv(sock, client_message,
			MAX_BUFFER_SIZE, 0)) > 0)) {

		client_message[read_size] = '\0';

		if (read_size)
			finish = execute_commands(sock, client_message, read_size);

		switch (finish) {
			case    CLI_SHUTDOWN:
				shutdown(sock, SHUT_RDWR);
				break;
	                case    CLI_SUCCESS:
				break;
			case    CLI_ERR_CMD:
                                sprintf(message,"%s","Command arguments are incorrect or command not executed properly\n");
                                logger(LOG_INFO, message);
                                cli_send_feedback(sock, message);
                                break;
	                case    CLI_INV_CMD:
                                sprintf(message,"%s","Command Not Found\n");
                                logger(LOG_INFO, message);
                                cli_send_feedback(sock, message);
                                break;
			default:
                                break;
		}
		sprintf(message, "%s\n", END_OF_COMMAND);
		cli_send_feedback(sock, message);

		if (read_size == 0) {
			puts("Client disconnected");
			fflush(stdout);
		} else if (read_size == -1) {
			perror("recv failed");
		}
	}
        num_sock--;
        sprintf(message, "Disconnected.Number of established sockets: %d.\n", num_sock);
        logger(LOG_INFO, message);

	close(sock);
	//Free the socket pointer
	free(socket_desc);

	sprintf(message, "Closing cli connection.\n");
	logger(LOG_INFO, message);

        pthread_exit(NULL);
}

int cli_quit(int client_fd, char **parameters, int numparameters) {

	/*
	 * Received a quit command so return 1 to shutdown this cli session.
	 */
	return 1;
}

void *cli_manager_init(void *dummyPtr) {
	//register_command("help", cli_help, false, false); //todo: This needs integrated into the cli.
	register_command("quit", cli_quit, false, true);
	register_command("show parameters", cli_show_param, true, true);

	/* Sharwan Joram:t We'll call this in last as we will never return from here */
	start_cli_server();
	return NULL;
}

