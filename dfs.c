// Server
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <pthread.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <sys/poll.h>
#include <poll.h>

#define MAXBUF   8192  /* max I/O buffer size */
#define MAXLINE  256  /* max text line length */

/*
Allocate thread buffer and launch thread specific actions
Cleanup memory afterwards
*/
void *thread(void *vargp){
    int connfd = *((int *)vargp);
    printf("Created client connection %i\n", connfd);
    pthread_detach(pthread_self());
    //fcntl(connfd, F_SETFL, O_NONBLOCK);
    char logname[MAXLINE];
    sprintf(logname, "logs/connfd_%i.txt", connfd);
    FILE *threadlog = fopen(logname, "a");
    fprintf(threadlog, "\nOpened new process for connection %i\n", connfd);
    fcntl(connfd, F_SETFL, O_NONBLOCK);
    read_whilealive(connfd, threadlog);
    fprintf(threadlog, "Closing client connection %i\n", connfd);
    printf("Closing client connection %i\n", connfd);
    free(vargp);
    close(connfd);
    fclose(threadlog);
    return NULL;
}

/*
Read the input port
Forever loop:
    - Accept connections on that port
    - Create a thread to service the connection, call the thread function on it
*/
int main(int argc, char **argv) 
{
    int listenfd, *connfdp, port, clientlen=sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid; 

    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(0);
    }
    port = atoi(argv[1]);

    listenfd = open_listenfd(port);
    if (listenfd < 0){
        printf("Error getting listen FD\n");
        return 0;
    }
    while (1) {
        connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (struct sockaddr*)&clientaddr, (socklen_t *)&clientlen);
        pthread_create(&tid, NULL, thread, connfdp);
    }
}
