/* $begin select */
#define MAXEVENTS 64

#include "csapp.h"

#include<errno.h>
#include<fcntl.h>
#include<stdlib.h>
#include<stdio.h>
#include<sys/epoll.h>
#include<sys/socket.h>
#include<string.h>

void command(void);
int handle_client(int connfd);
int handle_new_client(int listenfd);

struct event_action {
	int (*callback)(int);
	void *arg;
};

int efd;

int main(int argc, char **argv) 
{
	int listenfd, connfd;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	struct epoll_event event;
	struct epoll_event *events;
	int i;
	int len;
	int *argptr;
	struct event_action *ea;

	size_t n; 
	char buf[MAXLINE]; 

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}

	listenfd = Open_listenfd(argv[1]);

	// set fd to non-blocking (set flags while keeping existing flags)
	if (fcntl(listenfd, F_SETFL, fcntl(listenfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
		fprintf(stderr, "error setting socket option\n");
		exit(1);
	}

	if ((efd = epoll_create1(0)) < 0) {
		fprintf(stderr, "error creating epoll fd\n");
		exit(1);
	}


	ea = malloc(sizeof(struct event_action));
	ea->callback = handle_new_client;
	argptr = malloc(sizeof(int));
	*argptr = listenfd;

	ea->arg = argptr;
	event.data.ptr = ea;
	event.events = EPOLLIN | EPOLLET; // use edge-triggered monitoring
	if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &event) < 0) {
		fprintf(stderr, "error adding event\n");
		exit(1);
	}

	/* Buffer where events are returned */
	events = calloc(MAXEVENTS, sizeof(event));

	while (1) {
		// wait for event to happen (no timeout)
		n = epoll_wait(efd, events, MAXEVENTS, -1);

		for (i = 0; i < n; i++) {
			ea = (struct event_action *)events[i].data.ptr;
			argptr = ea->arg;
			if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
				/* An error has occured on this fd */
				fprintf (stderr, "epoll error on fd %d\n", *argptr);
				close(*argptr);
				free(ea->arg);
				free(ea);
				continue;
			}

			if (!ea->callback(*argptr)) {
				close(*argptr);
				free(ea->arg);
				free(ea);
			}

		}
	}
	free(events);
}

int handle_new_client(int listenfd) {
	socklen_t clientlen;
	int connfd;
	struct sockaddr_storage clientaddr;
	struct epoll_event event;
	int *argptr;
	struct event_action *ea;

	clientlen = sizeof(struct sockaddr_storage); 

	// loop and get all the connections that are available
	while ((connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen)) > 0) {

		// set fd to non-blocking (set flags while keeping existing flags)
		if (fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
			fprintf(stderr, "error setting socket option\n");
			exit(1);
		}

		ea = malloc(sizeof(struct event_action));
		ea->callback = handle_client;
		argptr = malloc(sizeof(int));
		*argptr = connfd;

		// add event to epoll file descriptor
		ea->arg = argptr;
		event.data.ptr = ea;
		event.events = EPOLLIN | EPOLLET; // use edge-triggered monitoring
		if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &event) < 0) {
			fprintf(stderr, "error adding event\n");
			exit(1);
		}
	}

	if (errno == EWOULDBLOCK || errno == EAGAIN) {
		// no more clients to accept()
		return 1;
	} else {
		perror("error accepting");
		return 0;
	}
}

int handle_client(int connfd) {
	int len;
	char buf[MAXLINE]; 
	while ((len = recv(connfd, buf, MAXLINE, 0)) > 0) {
		printf("Received %d bytes\n", len);
		send(connfd, buf, len, 0);
	}
	if (len == 0) {
		// EOF received.
		// Closing the fd will automatically unregister the fd
		// from the efd
		return 0;
	} else if (errno == EWOULDBLOCK || errno == EAGAIN) {
		return 1;
		// no more data to read()
	} else {
		perror("error reading");
		return 0;
	}
}
/* $end select */
