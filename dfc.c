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
#define MAXFILES 64
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
};

struct dfs g_servers[N_SERVERS]; // Store all 4 connections
char *g_chunkbuffers[N_SERVERS]; // Assume file chunks fit in heap
uint32_t g_chunklens[N_SERVERS];
char g_username[64];
char g_password[64];
//size_t g_chunksize;
FILE *g_log;

void encrypt(){

}

int connect_to_server(int serveridx){
    struct addrinfo hints;
    struct addrinfo *servinfo;
    int sockfd;
    int status;
    struct dfs *server = &g_servers[serveridx];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if((status = getaddrinfo(server->ip, server->port, &hints, &servinfo)) != 0){
        fprintf(g_log, "Getaddrinfo error: %s\n", gai_strerror(errno));
        return false;
    }

    sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (socket < 0){
        perror("Error opening socket");
        return 0;
    }

    if (connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) == -1){
        perror("connect");
        close(sockfd);
        return 0;
    }
    fprintf(g_log, "Connected successfully to server %i, socket %i\n", serveridx, sockfd);

    freeaddrinfo(servinfo);
    return sockfd;
}


int recv_from_socket(int connfd, char *buf, size_t len){
    int n_read;

    n_read = recv(connfd, &buf[0], len, 0);
    if (n_read == -1){
        fprintf(g_log, "Failed to receive from socket: %s\n", strerror(errno));
        return false;
    } else if (n_read == 0){
        fprintf(g_log, "Peer hung up\n");
    } else {
        fprintf(g_log, "Read %d bytes from socket\n", n_read);
        return n_read;
    }
    return 0;
}

int send_to_socket(int connfd, char *buf, size_t len){
    int n_sent;

    n_sent = send(connfd, &buf[0], len, 0);
    if(n_sent < 0){
        fprintf(g_log, "Failed to send to socket: %s\n", strerror(errno));
        return -1;
    } else if (n_sent < len){
        fprintf(g_log, "Sent partial message, please fix\n");
    } else {
        //fprintf(g_log, "Sent %d bytes to socket\n", n_sent);
        return n_sent;
    }
    return -1;
}

void send_by_packets(int connfd, char *buffer, int bytes){
    char sendbuf[BUFSIZE];
    int n_sent;
    int send_size;
    int total_sent = 0;
    char *chunkptr;

    fprintf(g_log, "Send by packets %i bytes to socket %i\n", bytes, connfd);

    chunkptr = buffer;
    while(total_sent < bytes){
        bzero(sendbuf, BUFSIZE);
        if(total_sent + BUFSIZE < bytes)
            send_size = BUFSIZE;
        else
            send_size = bytes - total_sent;
        memcpy(sendbuf, chunkptr, send_size);
        n_sent = send_to_socket(connfd, &sendbuf[0], send_size);
        total_sent += n_sent;
        // fprintf(g_log, "Sent %i, total %i\n", n_sent, total_sent);
        chunkptr += n_sent;
    }
    fprintf(g_log, "Done sending packets; sent %i bytes\n", total_sent);
}

void recv_by_packets(int connfd, char *destination, int bytes){
    char recvbuf[BUFSIZE];
    int n_recv;
    int total_recv = 0;
    int recv_size;
    char *chunkptr;

    fprintf(g_log, "Receive by packets %i bytes from socket %i\n", bytes, connfd);

    chunkptr = destination;
    while(total_recv < bytes){
        bzero(recvbuf, BUFSIZE);
        if(total_recv + BUFSIZE < bytes)
            recv_size = BUFSIZE;
        else
            recv_size = bytes - total_recv;
        n_recv = recv_from_socket(connfd, &recvbuf[0], recv_size);
        if (n_recv != recv_size)
            fprintf(g_log, "Warning: Received different number of packets than expected\n");
        total_recv += n_recv;
        memcpy(chunkptr, recvbuf, n_recv);
        chunkptr += n_recv;
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

    n_sent = send_to_socket(serverfd, &sendbuf[0], n_written);
    fprintf(g_log, "Sent %i bytes to socket\n", n_sent);
    if (n_sent < 0){
        fprintf(g_log, "Error sending credentials: %s\n", strerror(errno));
        return false;
    }

    // wait for approval
    n_recv = recv_from_socket(serverfd, &recvbuf[0], 3);
    fprintf(g_log, "Server response: %s\n", &recvbuf[0]);
    if (n_recv < 3)
        return false;
    else if (strncmp(&recvbuf[0], "no", 2) == 0)
        return false;
    else if (strncmp(&recvbuf[0], "ok", 2) == 0)
        return true;

    return false;;
}

void send_chunk(char *filename, int chunkidx, int serveridx){
    int sockfd;
    fprintf(g_log, "Sending chunk %i, size %i, chunk name %s, to server %i\n", chunkidx, g_chunklens[chunkidx], filename, serveridx);

    sockfd = connect_to_server(serveridx);
    if (!send_request(sockfd, "put", filename))
        return;
    send_by_packets(sockfd, g_chunkbuffers[chunkidx], g_chunklens[chunkidx]);
    close(sockfd);
}

void send_chunk_pair(int serveridx, int hashbucket, char *filename){
    int a; // Watch out! 1-indexed
    int b; // watch out! 1-indexed
    int socket_a;
    int socket_b;
    char fname1[MAXLINE];
    char fname2[MAXLINE];
    struct dfs *server = &g_servers[serveridx];

    fprintf(g_log, "\nSend two chunks of %s to server %i, given hashbucket %i\n", filename, serveridx, hashbucket);

    bzero(fname1, MAXLINE);
    bzero(fname2, MAXLINE);
    
    a = pairs_table[hashbucket][serveridx * 2]; // 1-indexed
    b = pairs_table[hashbucket][serveridx * 2 + 1]; // 1-indexed

    sprintf(fname1, ".%s.%i", filename, a);
    sprintf(fname2, ".%s.%i", filename, b);

    send_chunk(fname1, a-1, serveridx); // Switch to 0-indexed
    send_chunk(fname2, b-1, serveridx); // Switch to 0-indexed
}

int read_and_hash_file(FILE *fp){
    int filelen;
    int chunksize;
    int n_read;
    MD5_CTX md5;
    char lastdigit;
    int hashbucket;
    unsigned char hash[MD5_DIGEST_LENGTH];
    int lentotal;

    // check file length and choose chunk size
    fseek(fp, 0, SEEK_END);
    filelen = (int)ftell(fp);
    fseek(fp, 0, SEEK_SET);
    chunksize = filelen / N_SERVERS;
    if (filelen % N_SERVERS != 0)
        chunksize++;
    // g_chunksize = chunksize;

    // Hacky way to set different chunk buffer sizes
    lentotal = 0;
    for(int i=0; i<N_SERVERS; i++){
        g_chunklens[i] = chunksize - 1; 
    }
    for(int i=0; i<(filelen % N_SERVERS); i++){
        g_chunklens[i]++;
    }

    // read chunks into memory blocks and calculate hash
    MD5_Init(&md5);
    for(int i = 0; i < N_SERVERS; i++){
        g_chunkbuffers[i] = (char *)malloc(sizeof(char) * g_chunklens[i]);
        bzero(g_chunkbuffers[i], chunksize);
        n_read = fread(g_chunkbuffers[i], 1, g_chunklens[i], fp);
        MD5_Update(&md5, g_chunkbuffers[i], n_read);
    }
    MD5_Final(hash, &md5);

    // choose pairs and destination and send
    lastdigit = hash[MD5_DIGEST_LENGTH-1];
    hashbucket = lastdigit & 0x3; // modulo 4
    if (hashbucket > 3 || hashbucket < 0)
        perror("error hashing");
    return hashbucket;
}

void put(char *filename){
    FILE *fp;
    int hashbucket;

    fprintf(g_log, "\n\n**PUT %s**\n", filename);

    // open file
    if((fp = fopen(filename, "r")) == NULL){
        fprintf(g_log, "Failed to open file %s", filename);
        return;
    }
    hashbucket = read_and_hash_file(fp);
    for(int i=0; i < N_SERVERS; i++){
        send_chunk_pair(i, hashbucket, filename);
    }
    for(int i=0; i < N_SERVERS; i++){
        free(g_chunkbuffers[i]);
    }
    fclose(fp);
}

void query(int sockfd, char *filename, char *buffer){
    int n_recv;

    fprintf(g_log, "\n\n**QUERY %s**\n", filename);

    bzero(buffer, N_SERVERS+1);
    if(!send_request(sockfd, "query", filename)){
        return;
    }
    n_recv = recv_from_socket(sockfd, &buffer[0], 16);
    if(n_recv < 16)
        fprintf(g_log, "Invalid query response\n");
}

bool get_piece(char *filename, int serveridx, int piece_id, uint32_t len){
    int n_recv;
    int sockfd;
    char piecename[MAXLINE];
    int n_written;

    g_chunkbuffers[piece_id] = (char *)malloc(sizeof(char) * len);
    bzero(piecename, MAXLINE);
    n_written = sprintf(piecename, ".%s.%i", filename, piece_id+1);
    fprintf(g_log, "Requesting piece file %s from server %i\n", piecename, serveridx);
    sockfd = connect_to_server(serveridx);
    if(!send_request(sockfd, "get", &piecename[0])){
        close(sockfd);
        return false;
    }
    recv_by_packets(sockfd, g_chunkbuffers[piece_id], len);
    // n_recv = read_from_socket(sockfd, g_chunkbuffers[piece_id], len);
    // if(n_recv != len){
    //     fprintf(g_log, "Get piece did not get the correct number of bytes\n");
    // }
    close(sockfd);
    g_chunklens[piece_id] = len;
    return true;
}

void get_file(char *filename){
    int pieces_found[4] = {0,0,0,0};
    char pieces_returned[N_SERVERS * sizeof(uint32_t)]; // stores length of pieces, 0 if none
    uint32_t *pieces_len = (uint32_t *)pieces_returned;
    char path[MAXLINE];
    int sockfd;
    bool complete;
    FILE *fp;

    fprintf(g_log, "\n\n**GET %s**\n", filename);
    for(int serveridx=0; serveridx<N_SERVERS; serveridx++){
        sockfd = connect_to_server(serveridx);
        query(sockfd, filename, &pieces_returned[0]);
        close(sockfd);
        for(int piece_id=0; piece_id<N_SERVERS; piece_id++){
            fprintf(g_log, "Piece %i has been fetched? %i, found in server? %u\n", piece_id, pieces_found[piece_id], pieces_len[piece_id]);
            if(!pieces_found[piece_id] && pieces_len[piece_id])
                if(get_piece(filename, serveridx, piece_id, pieces_len[piece_id]))
                    pieces_found[piece_id] = 1;
        }
    }
    
    complete = true;
    for(int i=0; i< N_SERVERS; i++){
        if(pieces_found[i] == 0)
            complete = false;
    }
    if (!complete){
        fprintf(g_log, "Could not find all pieces of %s\n", filename);
        printf("File is incomplete\n");
        return;
    }
    sprintf(path, "received/%s", filename);
    fp = fopen(path, "w");
    if (!fp)
        return;
    for(int piece_id=0; piece_id<N_SERVERS; piece_id++){
        fwrite(g_chunkbuffers[piece_id], 1, g_chunklens[piece_id], fp);
    }
    fclose(fp);
    return;
}

void parse_list(char *input, char **file_list, int **pieces_list, int nbytes){
    char *bufptr;
    char *pieceptr;
    char *frontptr;
    int fileidx;
    int piece_id;
    bool found;

    fprintf(g_log, "Parsing list result: %s\n", input);
    bufptr = input;
    while(bufptr){
        frontptr = strsep(&bufptr, " "); // Whole filename is in frontptr: .1.txt.2
        if(frontptr[0] == '.')
            frontptr++; // skip first dot, frontptr is at filename
        pieceptr = strrchr(frontptr, '.'); // points to last period
        if (pieceptr)
            *pieceptr = 0; // replace dot with null
        else
            continue; // skip this one, there's no number
        pieceptr++; // points to number
        piece_id = atoi(pieceptr);
        fileidx = 0;
        found = false;
        fprintf(g_log, "Filename %s, piece %i\n", frontptr, piece_id);
        while(file_list[fileidx]){
            if(strcmp(file_list[fileidx], frontptr) == 0){ // filename is already in index
                //fprintf(g_log, "Found filename in list\n");
                found = true;
                break;
            }
            fileidx++;
        }
        if (!found){
            fprintf(g_log, "Making new entry in file list\n");
            file_list[fileidx] = (char *)malloc(sizeof(char) * strlen(frontptr) + 1);
            strcpy(file_list[fileidx], frontptr); // copy plain filename into list
            pieces_list[fileidx] = (int *)malloc(sizeof(int) * 4);
        }
        pieces_list[fileidx][piece_id-1] = 1;
    }
}

void print_ls(char **file_list, int **pieces_list){
    int fileidx = 0;
    bool complete;

    while(file_list[fileidx]){
        complete = true;
        for(int i=0; i<N_SERVERS; i++){
            if(pieces_list[fileidx][i] != 1)
                complete = false;
        }
        if(complete){
            printf("%s\n", file_list[fileidx]);
        } else {
            printf("%s [incomplete]\n", file_list[fileidx]);
        }
        fileidx++;
    }
}

void list(){
    struct dfs *server;
    char recvbuf[BUFSIZE];
    int n_recv;
    int socket;
    char **file_list;
    int **pieces_list;

    fprintf(g_log, "\n\n**LIST**\n");

    file_list = (char **)malloc(sizeof(char *) * MAXFILES);
    pieces_list = (int **)malloc(sizeof(int *) * MAXFILES);
    bzero(pieces_list, MAXFILES * sizeof(int *));
    bzero(file_list, MAXFILES * sizeof(char *));

    for(int i=0; i<N_SERVERS; i++){
        bzero(recvbuf, BUFSIZE);
        server = &g_servers[i];
        if ((socket = connect_to_server(i)) == 0)
            return;
        if (!send_request(socket, "list", ""))
            return;
        n_recv = recv_from_socket(socket, &recvbuf[0], BUFSIZE);
        parse_list(&recvbuf[0], file_list, pieces_list, n_recv);
        close(socket);
    }
    print_ls(file_list, pieces_list);
    int i = 0;
    while(pieces_list[i]){
        free(pieces_list[i]);
        free(file_list[i]);
        i++;
    }
    free(pieces_list);
    free(file_list);
}

char *extract_allocate_filename_from_command(char *bufptr){
    char *splitptr;
    size_t namelen;
    char *filename;

    splitptr = bufptr;
    strsep(&splitptr, " ");
    if(splitptr == NULL){
        perror("No filename for put");
        return NULL;
    }
    namelen = strlen(splitptr);
    filename = (char *)malloc(sizeof(char) * (namelen + 1));
    strlcpy(filename, splitptr, namelen+1);
    return filename;
}

void read_commands(){
    char commandbuf[BUFSIZE];
    char *filename;

    while(1){
        bzero(&commandbuf, BUFSIZE);
        fflush(stdin);
        printf("Please enter a command (get <>, put <>, delete <>, ls, exit:\n");
        fgets(commandbuf, BUFSIZE, stdin);
        commandbuf[strcspn(commandbuf, "\r\n")] = 0; // remove newlines
        if(strncmp(&commandbuf[0], "exit", 4) == 0)
            return;
        else if (strncmp(&commandbuf[0], "put", 3) == 0){
            filename = extract_allocate_filename_from_command(&commandbuf[0]);
            put(filename);
            free(filename);
        } else if (strncmp(&commandbuf[0], "ls", 2) == 0)
            list();
        else if (strncmp(&commandbuf[0], "get", 3) == 0){
            filename = extract_allocate_filename_from_command(&commandbuf[0]);
            get_file(filename);
            free(filename);
        }
    }
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
    fprintf(g_log, "Filled in details for server %i: name %s, ip %s, port %s\n", serveridx, server->name, server->ip, server->port);
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

FILE *create_logfile(){
    FILE *log = fopen("logs/clientlog.txt", "a");
    if(!log){
        perror("Error opening logfile");
        return NULL;
    }
    return log;
}

int main(int argc, char **argv){

    if (argc != 2){
        perror("Usage: ./dfc <config file>");
        return(0);
    }
    g_log = create_logfile();
    if (!read_conf(argv[1])){
        perror("Error reading conf file");
        return(1);
    }
    chdir("client");
    read_commands();
    fclose(g_log);
    return(0);
}