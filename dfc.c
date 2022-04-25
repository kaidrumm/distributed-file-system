// Client
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
#include "/usr/local/include/node/openssl/md5.h"
//#include <openssl.md5>

#define BUFSIZE 1024
#define MAXLINE 256
#define N_SERVERS 4

const int pairs_table[4][8] = {
    {1, 2, 2, 3, 3, 4, 4, 1},
    {4, 1, 1, 2, 2, 3, 3, 4},
    {3, 4, 4, 1, 1, 2, 2, 3},
    {2, 3, 3, 4, 4, 1, 1, 2}
};

struct dfs {
    char name[5];
    char ip[17];
    char port[8];
    struct sockaddr addr;
    size_t addrlen;
    int socket_a;
    int socket_b;
};

struct dfs g_servers[N_SERVERS]; // Store all 4 connections
char *g_chunkbuffers[N_SERVERS]; // Assume file chunks fit in heap
char g_username[64];
char g_password[64];
size_t g_chunksize;

void encrypt(){

}

bool connect_to_socket(int socket, struct sockaddr *addr, size_t addrlen){
    if (connect(socket, addr, addrlen) == -1){
        perror("connect");
        close(socket);
        return false;
    }
    fprintf(stdout, "Connected successfully to socket %i\n", socket);
    return true; // Connected successfully
}

int read_from_socket(int connfd, char *buf, size_t len, FILE *log){
    int n_read;

    n_read = recv(connfd, &buf[0], MAXLINE, 0);
    if (n_read == -1){
        fprintf(log, "Failed to receive from socket: %s\n", strerror(errno));
        return false;
    } else if (n_read == 0){
        fprintf(log, "Peer hung up\n");
    } else {
        fprintf(log, "Read %d bytes from socket\n", n_read);
        return n_read;
    }
    return 0;
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

void send_by_packets(int connfd, char *buffer, int bytes){
    char sendbuf[BUFSIZE];
    int n_sent;
    int total_sent = 0;
    char *chunkptr;

    printf("Send by packets %i bytes to socket %i\n", bytes, connfd);

    chunkptr = buffer;
    while(total_sent < bytes){
        bzero(sendbuf, BUFSIZE);
        memcpy(sendbuf, chunkptr, BUFSIZE);
        n_sent = send_to_socket(connfd, &sendbuf[0], BUFSIZE, stdout);
        total_sent += n_sent;
        printf("Sent %i, total %i\n", n_sent, total_sent);
        chunkptr += n_sent;
    }
}


bool send_request(int serverfd, char *verb, char *filename){
    int n_sent;
    int n_recv;
    int n_written;
    char sendbuf[MAXLINE];
    char recvbuf[MAXLINE];

    bzero(sendbuf, MAXLINE);
    bzero(recvbuf, MAXLINE);
    n_written = sprintf(&sendbuf[0], "%s %s %s %s ", g_username, g_password, verb, filename);

    n_sent = send_to_socket(serverfd, &sendbuf[0], n_written, stdout);
    if (n_sent < 0){
        printf("Error sending credentials: %s\n", strerror(errno));
        return false;
    }

    // wait for approval
    n_recv = read_from_socket(serverfd, &recvbuf[0], 2, stdout);
    printf("Server response: %s\n", &recvbuf[0]);
    if (n_recv != 2)
        return false;
    else if (strncmp(&recvbuf[0], "no", 2) == 0)
        return false;
    else if (strncmp(&recvbuf[0], "ok", 2) == 0)
        return true;

    return false;;
}

// void mkdir(){

// }

void send_chunk_pair(int serveridx, int hashbucket, char *filename){
    int a;
    int b;
    char fname1[MAXLINE];
    char fname2[MAXLINE];
    struct dfs *server = &g_servers[serveridx];

    bzero(fname1, MAXLINE);
    bzero(fname2, MAXLINE);
    
    a = pairs_table[hashbucket][serveridx * 2];
    b = pairs_table[hashbucket][serveridx * 2 + 1];

    sprintf(fname1, ".%s.%i", filename, a);
    sprintf(fname2, ".%s.%i", filename, b);

    printf("Sending chunk %i, from hashbucket %i, number %i, chunk name %s\n", a, hashbucket, serveridx * 2, fname1);
    // New connection for first chunk
    if (!connect_to_socket(server->socket_a, &server->addr, server->addrlen))
        return;
    if (!send_request(server->socket_a, "put", fname1)){
        close(server->socket_a);
        return;
    }
    send_by_packets(server->socket_a, g_chunkbuffers[a-1], g_chunksize);
    close(server->socket_a);

    printf("Sending chunk %i, from hashbucket %i, number %i, chunk name %s\n", b, hashbucket, serveridx * 2 + 1, fname2);
    // New connection for second chunk
    if (!connect_to_socket(server->socket_b, &server->addr, server->addrlen))
        return;
    if (!send_request(server->socket_b, "put", fname2))
        return;
    send_by_packets(server->socket_b, g_chunkbuffers[b-1], g_chunksize);
    close(server->socket_b);
}

int read_and_hash_file(FILE *fp){
    int filelen;
    int chunksize;
    int n_read;
    MD5_CTX md5;
    char lastdigit;
    int hashbucket;
    unsigned char hash[MD5_DIGEST_LENGTH];

    // check file length and choose chunk size
    fseek(fp, 0, SEEK_END);
    filelen = (int)ftell(fp);
    fseek(fp, 0, SEEK_SET);
    chunksize = filelen / N_SERVERS;
    if (filelen % N_SERVERS != 0)
        chunksize++;
    g_chunksize = chunksize;
    // read chunks into memory blocks and calculate hash
    MD5_Init(&md5);
    for(int i = 0; i < N_SERVERS; i++){
        g_chunkbuffers[i] = (char *)malloc(sizeof(char) * chunksize);
        bzero(g_chunkbuffers[i], chunksize);
        n_read = fread(g_chunkbuffers[i], 1, chunksize, fp);
        MD5_Update(&md5, g_chunkbuffers[i], n_read);
    }
    MD5_Final(hash, &md5);

    // choose pairs and destination and send
    lastdigit = hash[MD5_DIGEST_LENGTH-1];
    hashbucket = lastdigit & 0x7; // modulo 4
    return hashbucket;
}

void put(char *filename){
    FILE *fp;
    int hashbucket;

    // open file
    if((fp = fopen(filename, "r")) == NULL){
        printf("Failed to open file %s", filename);
        return;
    }
    hashbucket = read_and_hash_file(fp);
    for(int i=0; i < N_SERVERS; i++){
        send_chunk_pair(i, hashbucket, filename);
    }
}

bool is_file_complete(){
    return false;
}

void get(){

}

void list(){

}

// void hash_file(FILE *fp){
//     int n_read;
//     MD5_CTX md5;
//     char buf[BUFSIZE];

// }

bool open_sockets(int serveridx){
    struct addrinfo hints;
    struct addrinfo *servinfo;
    int sockfd;
    int status;
    struct dfs *server = &g_servers[serveridx];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if((status = getaddrinfo(server->ip, server->port, &hints, &servinfo)) != 0){
        printf("Getaddrinfo error: %s\n", gai_strerror(errno));
        return false;
    }
    server->addr = *(servinfo->ai_addr);
    server->addrlen = servinfo->ai_addrlen;

    server->socket_a = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (server->socket_a < 0){
        perror("Error opening first socket");
        return false;
    }
    server->socket_b = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (server->socket_b < 0){
        perror("Error opening second socket");
        return false;
    }
    freeaddrinfo(servinfo);
    return true;
}

void store_dfs_info(char *line, int serveridx){
    if(serveridx > N_SERVERS){
        perror("Invalid server ID");
        exit(1);
    }

    char *splitptr;
    struct dfs *server = &g_servers[serveridx];

    // Parse line
    splitptr = strsep(&line, " ");
    strcpy(server->name, splitptr);
    splitptr = strsep(&line, ":");
    strcpy(server->ip, splitptr);
    splitptr = strsep(&line, "\n");
    strcpy(server->port, splitptr);
    printf("Filled in details for server %i: name %s, ip %s, port %s\n", serveridx, server->name, server->ip, server->port);
    open_sockets(serveridx);
}

bool read_conf(char *conf){
    FILE *fp;
    char linebuf[MAXLINE];
    char *line;
    int servercount = 0;

    fp = fopen(conf, "r");
    if (fp == NULL){
        perror("Error opening dfc.conf");
        return false;
    }
    line = fgets(&linebuf[0], MAXLINE, fp);
    while(line){
        linebuf[strcspn(linebuf, "\r\n")] = 0; // remove newlines from fgets
        if(strncmp(line, "Server", 6) == 0){
            if (servercount > N_SERVERS){
                perror("More servers than expected in conf file");
                continue;
            }
            strsep(&line, " "); // move pointer to after first space
            store_dfs_info(line, servercount);
            servercount++;
        } else if (strncmp(line, "Username", 8) == 0)
            strcpy(g_username, &line[10]);
        else if (strncmp(line, "Password", 8) == 0)
            strcpy(g_password, &line[10]);
        line = fgets(&linebuf[0], MAXLINE, fp);
    }
    if (servercount != N_SERVERS){
        perror("Failed to find correct number of servers");
        return false;
    } 
    if(!g_password[0] || !g_username[0]){
        perror("Failed to load username or password");
        return false;
    }
    return true;
    
}

void read_commands(){
    char commandbuf[BUFSIZE];
    char *splitptr;
    char *filename;
    size_t namelen;

    while(1){
        bzero(&commandbuf, BUFSIZE);
        printf("Please enter a command (get <>, put <>, delete <>, ls, exit:\n");
        fgets(commandbuf, BUFSIZE, stdin);
        commandbuf[strcspn(commandbuf, "\r\n")] = 0; // remove newlines
        if(strncmp(&commandbuf[0], "exit", 4) == 0)
            return;
        else if (strncmp(&commandbuf[0], "put", 3) == 0){
            
            splitptr = &commandbuf[0];
            strsep(&splitptr, " ");
            if(splitptr == NULL){
                perror("No filename for put");
                continue;
            }
            namelen = strlen(splitptr);
            filename = malloc(sizeof(char) * (namelen + 1));
            strlcpy(filename, splitptr, namelen+1);
            put(filename);
        }
    }
}

int main(int argc, char **argv){

    if (argc != 2){
        perror("Usage: ./dfc <config file>");
        return(0);
    }
    if (!read_conf(argv[1])){
        perror("Error reading conf file");
        return(1);
    }
    chdir("client");
    read_commands();
    return(0);
}