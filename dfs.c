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

#define MAXBUF   1024  /* max I/O buffer size */
#define MAXLINE  256  /* max text line length */
#define LISTENQ  1024  /* second argument to listen() */

char **g_users;
char **g_passwords;
int g_numusers;


int read_from_socket(int connfd, char *buf, size_t len, FILE *log){
    int n_read;

    n_read = recv(connfd, &buf[0], len, 0);
    if (n_read == -1){
        fprintf(log, "Failed to receive from socket: %s\n", strerror(errno));
        return -1;
    } else if (n_read == 0){
        fprintf(log, "Peer hung up\n");
    } else {
        fprintf(log, "Read %d bytes from socket\n", n_read);
        return n_read;
    }
    return -1;
}

int send_to_socket(int connfd, char *buf, size_t len, FILE *log){
    int n_sent;

    n_sent = send(connfd, &buf[0], len, 0);
    if(n_sent < 0){
        fprintf(log, "Failed to send to socket: %s\n", strerror(errno));
        return -1;
    } else if (n_sent < len){
        fprintf(log, "Sent partial message, please fix\n");
    } else {
        fprintf(log, "Sent %d bytes to socket\n", n_sent);
        return n_sent;
    }
    return -1;
}

// void mkdir(){

// }

void store_piece(){

}

void put(int connfd, char *filename, FILE *log){
    fprintf(log, "put %s\n", filename);
    int n_read;
    int n_written;
    char recvbuf[MAXBUF];
    FILE *fp;
    bzero(recvbuf, MAXBUF);

    if((fp = fopen(filename, "w")) == NULL){
        fprintf(log, "Error creating file %s\n", filename);
        return;
    }
    
    while((n_read = read_from_socket(connfd, &recvbuf[0], MAXBUF, log)) > 0){
        n_written = fwrite(&recvbuf[0], 1, n_read, fp);
        if(n_written != n_read)
            fprintf(log, "Error writing bytes to file\n");
        bzero(recvbuf, MAXBUF);
    }
    fclose(fp);
}

void get(){

}

void ls(){

}


bool authenticate(int connfd, char *username, char *password, FILE *log){
    fprintf(log, "Authenticating %s:%s\n", username, password);
    char ok[3] = "ok";
    char no[3] = "no";

    for(int i=0; i<g_numusers; i++){
        if (strcmp(username, g_users[i]) == 0){
            if (strcmp(password, g_passwords[i]) == 0){
                fprintf(log, "authenticated\n");
                mkdir(username, 0777);
                send_to_socket(connfd, &ok[0], 2, log);
                return true;
            }
        }
    }
    send_to_socket(connfd, &no[0], 2, log);
    return false;
}


void parse_request(int connfd, FILE *log){
    int n_read;
    char recvbuf[MAXBUF];
    char username[MAXLINE];
    char password[MAXLINE];
    char filename[MAXLINE];
    char *bufptr;
    char *splitptr;
    char *filenameptr;

    bzero(recvbuf, MAXBUF);
    bzero(username, MAXLINE);
    bzero(password, MAXLINE);
    bzero(filename, MAXLINE);

    if ((n_read = read_from_socket(connfd, &recvbuf[0], MAXBUF, log)) <= 0)
        return;

    // Expect credentials
    bufptr = &recvbuf[0];
    splitptr = strsep(&bufptr, " ");
    strcpy(&username[0], splitptr);
    splitptr = strsep(&bufptr, " ");
    strcpy(&password[0], splitptr);

    splitptr = strsep(&bufptr, " ");
    if (bufptr){
        filenameptr = strsep(&bufptr, " ");
        sprintf(&filename[0], "%s/%s", username, filenameptr);
        fprintf(log, "Found filename %s\n", &filename[0]);
    }
    if (!authenticate(connfd, &username[0], &password[0], log))
        return;
    printf("Authenticated!\n");

    if(strncmp(splitptr, "put", 3) == 0)
        put(connfd, &filename[0], log);
}

FILE *create_logfile(connfd){
    char logname[MAXLINE];
    bzero(logname, MAXLINE);
    sprintf(logname, "logs/connfd_%i.txt", connfd);
    FILE *threadlog = fopen(logname, "a");
    if(!threadlog){
        perror("Error opening logfile");
        return NULL;
    } else {
        fprintf(threadlog, "Opened new process for connection %i\n", connfd);
        return threadlog;
    }
}

/*
Allocate thread buffer and launch thread specific actions
Cleanup memory afterwards
*/
void *thread(void *connfdp){
    int connfd = *(int *)connfdp;
    printf("Created client connection %i\n", connfd);
    pthread_detach(pthread_self());

    FILE *log;
    log = create_logfile(connfd);
    parse_request(connfd, log);

    fprintf(log, "Closing client connection %i\n\n", connfd);
    printf("Closing client connection %i\n", connfd);
    close(connfd);
    fclose(log);
    return NULL;
}

void read_conf(){
    FILE *fp;
    char linebuf[MAXLINE];
    char *line;
    char *splitptr;
    int useridx = 0;
    int len;
    int n_users;

    fp = fopen("dfs.conf", "r");
    if(!fp){
        perror("Error opening dfs.conf");
        return;
    }

    // Identify number of users and allocate globals
    line = fgets(&linebuf[0], MAXLINE, fp);
    n_users = atoi(line);
    g_users = (char **)malloc(sizeof(char *) * n_users);
    g_passwords = (char **)malloc(sizeof(char *) * n_users);
    g_numusers = n_users;

    line = fgets(&linebuf[0], MAXLINE, fp);
    while(line){
        linebuf[strcspn(linebuf, "\r\n")] = 0; // remove newlines from fgets
        splitptr = strsep(&line, " ");
        if (!line){
            perror("No password provided");
            return;
        }
        len = strlen(splitptr) + 1; // length of username
        g_users[useridx] = (char *)malloc(sizeof(char) * len);
        strncpy(g_users[useridx], splitptr, len); // copy username to global
        len = strlen(line) + 1; // length of password
        g_passwords[useridx] = (char *)malloc(sizeof(char) * len);
        strncpy(g_passwords[useridx], line, len); // coppy password to global

        useridx++;
        line = fgets(&linebuf[0], MAXLINE, fp);
    }

    // everything is deallocated when process exits
}

/* 
 * open_listenfd - open and return a listening socket on port
 * Returns -1 in case of failure 
 */
int open_listenfd(int port) 
{
    int listenfd, optval=1;
    struct sockaddr_in serveraddr;
  
    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

    /* Eliminates "Address already in use" error from bind. */
    // if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int)) < 0)
    //     return -1;

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (bind(listenfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0)
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0)
        return -1;
    return listenfd;
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
    int connfd;
    char *dirname;

    if (argc != 3) {
	    fprintf(stderr, "usage: %s <folder> <port>\n", argv[0]);
	    exit(0);
    }

    read_conf();

    dirname = argv[1];
    strsep(&dirname, "/");
    if (dirname){
        chdir(dirname);
        mkdir("logs", 0777);
        printf("Moving into directory %s\n", dirname);
    }

    port = atoi(argv[2]);
    listenfd = open_listenfd(port);
    if (listenfd < 0){
        printf("Error getting listen FD\n");
        return 0;
    }
    while (1) {
        connfdp = (int *)malloc(sizeof(int));
        *connfdp = accept(listenfd, (struct sockaddr*)&clientaddr, (socklen_t *)&clientlen);
        pthread_create(&tid, NULL, thread, (void *)connfdp);
    }
}
