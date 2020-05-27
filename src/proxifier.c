/*
 * proxifier.c
 *
 *  Created on: 19-May-2020
 *      Author: Mohith Reddy
 */

#include "proxifier.h"
#include "proxy.h"
#include "proxy_http.h"
#include "proxy_structures.h"
#include <signal.h>
#include <pthread.h>
#include <stdio.h>

int main(int argc, char** argv)
{
	/* Block SIGPIPE, SIGRTMIN, SIGCHLD, SIGTERM */

	sigset_t sigmask;

	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGPIPE);	// get EPIPE instead
	sigaddset(&sigmask, SIGRTMIN);	// use as sync signal
	sigaddset(&sigmask, SIGCHLD);	// use a notifier for child termination
	sigaddset(&sigmask, SIGTERM);	// systemctl signaling for quit
	sigaddset(&sigmask, SIGINT);	// just in case of user interrupt

	if (pthread_sigmask(SIG_BLOCK, &sigmask, NULL) != 0)
		exit(1);

	int exit_status = 0;

	/* Install Proxy Handlers */

	struct proxy_bag* handlers_bag = create_proxy_bag();

	struct proxy_handler* px_handler = create_proxy_handler();
	px_handler->ptid = pthread_self();

	place_proxy_data(handlers_bag, &((struct proxy_data) {(void*) &px_handler, \
		sizeof(struct proxy_handler*)}));

	pthread_create(&px_handler->tid, NULL, http_proxy_init, (void*) px_handler);

	/* Make a signal mask on which main should be listening */

	sigset_t listenmask;

	sigemptyset(&listenmask);
	sigaddset(&listenmask, SIGRTMIN);
	sigaddset(&listenmask, SIGTERM);
	sigaddset(&listenmask, SIGINT);

	/* Cleanup handlers or wait for termination */

	int signo;

	for ( ; ; ) {
		if (sigwait(&listenmask, &signo) < 0) {
			exit_status = 1;
			goto proxifier_quit;
		}
		else if (signo == SIGINT || signo == SIGTERM) {
			exit_status = 0;
			goto proxifier_quit;
		}
	}

	proxifier_quit:

	for (struct proxy_pocket* px_pocket = handlers_bag->start; \
	px_pocket != NULL; px_pocket = px_pocket->next) {
		px_handler = *((struct proxy_handler**) px_pocket->data);

		px_handler->quit = 1;
		pthread_kill(px_handler->tid, SIGRTMIN);
	}

	for (struct proxy_pocket* px_pocket = handlers_bag->start; \
	px_pocket != NULL; px_pocket = px_pocket->next) {
		px_handler = *((struct proxy_handler**) px_pocket->data);

		pthread_join(px_handler->tid, NULL);
		free_proxy_handler((struct proxy_handler**) px_pocket->data);
	}

	free_proxy_bag(&handlers_bag);

	exit(exit_status);
}
