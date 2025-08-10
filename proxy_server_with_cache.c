#include <stdio.h>
#include "proxy_parse.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

#define MAX_CLIENTS 10
#define MAX_BYTES 4096
#define MAX_ELEMENT_SIZE 10*(1<<10)
#define MAX_SIZE 200*(1<<20)

typedef struct cache_element cache_element;

// linked list structure
struct cache_element
{
    char* data;
    int len;    // bytes of data
    char* url;
    time_t lru_time_track;
    cache_element* next;
};

cache_element* find(char* url);
int add_cache_element(char* data, int size, char* url);
void remove_cache_element();

int port_number = 8080;
int proxy_socketId;
pthread_t tid[MAX_CLIENTS];     // no. of sockets = no. of threads
sem_t semaphore;            // LRU cache is shared resource
pthread_mutex_t lock;

cache_element* head;
int cache_size;

int connectRemoteServer(char* host_addr, int port_num){
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(remoteSocket < 0){
        printf("Error in creating socket!\n");
        return -1;
    }
    
    struct hostent* host = gethostbyname(host_addr);
    if(host == NULL){
        perror("No such host exists!\n");
        return -1;
    }
    
    struct sockaddr_in server_addr;
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);
    
    bcopy((char*)host->h_addr_list[0], (char*)&server_addr.sin_addr.s_addr, host->h_length);
    if(connect(remoteSocket, (struct sockaddr*)&server_addr, (size_t)sizeof(server_addr)) < 0){
        fprintf(stderr, "Error in connecting!\n");
        return -1;
    }
    return remoteSocket;
}

int sendErrorMessage(int socket, int status_code){
    char str[1024];
    char currentTime[50];
    time_t now = time(0);

    struct tm data = *gmtime(&now);
    strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

    switch(status_code){
        case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Request</H1>\n</BODY></HTML>", currentTime);
                  printf("400 Bad Request\n");
                  send(socket, str, strlen(str), 0);
                  break;

        case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
                  printf("403 Forbidden\n");
                  send(socket, str, strlen(str), 0);
                  break;

        case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
                  printf("404 Not Found\n");
                  send(socket, str, strlen(str), 0);
                  break;

        case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
                  printf("500 Internal Server Error\n");
                  send(socket, str, strlen(str), 0);
                  break;

        case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>501 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
                  printf("501 Not Implemented\n");
                  send(socket, str, strlen(str), 0);
                  break;

        case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
                  printf("505 HTTP Version Not Supported\n");
                  send(socket, str, strlen(str), 0);
                  break;

        default:  return -1;
    }
    return 1;
}

int handle_request(int clientSocketId, struct ParsedRequest* request, char* url_key){
    char* buf = (char*)malloc(sizeof(char)*MAX_BYTES);
    
    // Fix: Proper HTTP request format
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " HTTP/1.1\r\n");

    size_t len = strlen(buf);

    if(ParsedHeader_set(request, "Connection", "close") < 0){
        printf("Set Header key is not working!\n");
    }

    if(ParsedHeader_get(request, "Host") == NULL){
        if(ParsedHeader_set(request, "Host", request->host) < 0){
            printf("Set Host header key is not working!\n");
        }
    }

    if(ParsedRequest_unparse_headers(request, buf+len, (size_t)MAX_BYTES-len) < 0){
        printf("Unparse Failed!\n");
        free(buf);
        return -1;
    }
    
    int server_port = 80;
    if(request->port != NULL){
        server_port = atoi(request->port);
    }
    
    int remoteSocketId = connectRemoteServer(request->host, server_port);
    if(remoteSocketId < 0){
        perror("Connecting to Remote Server Failed!\n");
        free(buf);
        return -1;
    }
    
    int bytes_send = send(remoteSocketId, buf, strlen(buf), 0);
    if(bytes_send < 0){
        perror("Error sending to remote server");
        free(buf);
        close(remoteSocketId);
        return -1;
    }
    
    bzero(buf, MAX_BYTES);
    
    // Fix: Better buffer management for response
    char* temp_buffer = (char*)malloc(MAX_BYTES);
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;
    
    int bytes_recv = recv(remoteSocketId, buf, MAX_BYTES-1, 0);
    
    while(bytes_recv > 0){
        // Send to client
        int bytes_sent_client = send(clientSocketId, buf, bytes_recv, 0);
        if(bytes_sent_client < 0){
            perror("Error in sending data to the client!\n");
            break;
        }
        
        // Store in temp buffer for caching
        if(temp_buffer_index + bytes_recv >= temp_buffer_size){
            temp_buffer_size += MAX_BYTES;
            temp_buffer = (char*)realloc(temp_buffer, temp_buffer_size);
            if(temp_buffer == NULL){
                perror("Memory allocation failed");
                break;
            }
        }
        
        memcpy(temp_buffer + temp_buffer_index, buf, bytes_recv);
        temp_buffer_index += bytes_recv;
        
        bzero(buf, MAX_BYTES);
        bytes_recv = recv(remoteSocketId, buf, MAX_BYTES-1, 0);
    }
    
    if(temp_buffer_index > 0){
        temp_buffer[temp_buffer_index] = '\0';
        add_cache_element(temp_buffer, temp_buffer_index, url_key);
        printf("Added to cache: %s\n", url_key);
    }
    
    free(buf);
    free(temp_buffer);
    close(remoteSocketId);
    return 0;
}

int checkHTTPversion(char* msg){
    int version = -1;
    if(strncmp(msg, "HTTP/1.1", 8) == 0){
        version = 1;
    }
    else if(strncmp(msg, "HTTP/1.0", 8) == 0){
        version = 1;
    }
    else{
        version = -1;
    }
    return version;
}

void* thread_fn(void* socketNew){
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore, &p);
    printf("Semaphore value is %d\n", p);
    
    int* t = (int*) socketNew;
    int socket = *t;
    int bytes_recv_client, len;
    
    char* buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    bzero(buffer, MAX_BYTES);
    bytes_recv_client = recv(socket, buffer, MAX_BYTES-1, 0);

    while(bytes_recv_client > 0){
        len = strlen(buffer);
        if(strstr(buffer, "\r\n\r\n") == NULL){
            bytes_recv_client = recv(socket, buffer+len, MAX_BYTES-len-1, 0);
        }
        else{
            break;
        }
    }
    
    if(bytes_recv_client > 0){
        len = strlen(buffer);
        struct ParsedRequest* request = ParsedRequest_create();
        
        if(ParsedRequest_parse(request, buffer, len) < 0){
            printf("Parsing failed!\n");
            sendErrorMessage(socket, 400);
        }
        else{
            if(!strcmp(request->method, "GET")){
                if(request->host && request->path && checkHTTPversion(request->version) == 1){
                    // Fix: Create proper cache key using host + path
                    char* url_key = (char*)malloc(strlen(request->host) + strlen(request->path) + 10);
                    sprintf(url_key, "%s%s", request->host, request->path);
                    
                    printf("Looking for URL in cache: %s\n", url_key);
                    cache_element* temp = find(url_key);
                    
                    if(temp != NULL){
                        printf("Cache HIT for: %s\n", url_key);
                        // Fix: Proper cache data retrieval
                        int pos = 0;
                        while(pos < temp->len){
                            int bytes_to_send = (temp->len - pos > MAX_BYTES) ? MAX_BYTES : (temp->len - pos);
                            int sent = send(socket, temp->data + pos, bytes_to_send, 0);
                            if(sent < 0){
                                perror("Error sending cached data");
                                break;
                            }
                            pos += sent;
                        }
                        printf("Cache data sent successfully!\n");
                    }
                    else{
                        printf("Cache MISS for: %s\n", url_key);
                        int result = handle_request(socket, request, url_key);
                        if(result == -1){
                            sendErrorMessage(socket, 500);
                        }
                    }
                    
                    free(url_key);
                }
                else{
                    sendErrorMessage(socket, 400);
                }
            }
            else{
                printf("This proxy only supports GET method.\n");
                sendErrorMessage(socket, 501);
            }
        }
        ParsedRequest_destroy(request);
    }
    else if(bytes_recv_client == 0){
        printf("Client disconnected!\n");
    }
    else{
        perror("Error receiving from client");
    }
    
    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    
    sem_post(&semaphore);
    sem_getvalue(&semaphore, &p);
    printf("Semaphore post value is %d\n", p);
    
    return NULL;
}

cache_element* find(char* url){
    cache_element* site = NULL;
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Find cache lock acquired %d\n", temp_lock_val);
    
    if(head != NULL){
        site = head;
        while (site != NULL){
            if(!strcmp(site->url, url)){
                printf("LRU time track before: %ld\n", site->lru_time_track);
                printf("URL found in cache!\n");
                site->lru_time_track = time(NULL);
                printf("LRU time track after: %ld\n", site->lru_time_track);
                break;
            }
            site = site->next;
        }
    }
    else{
        printf("Cache is empty!\n");        
    }
    
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Find cache lock unlocked %d\n", temp_lock_val);
    return site;
}

int add_cache_element(char* data, int size, char* url){
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Add cache lock acquired %d\n", temp_lock_val);
    
    int element_size = size + 1 + strlen(url) + sizeof(cache_element);
    if(element_size > MAX_ELEMENT_SIZE){
        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Element too large for cache\n");
        return 0;
    }
    
    // Remove elements if cache is full
    while(cache_size + element_size > MAX_SIZE){
        remove_cache_element();
    }
    
    cache_element* element = (cache_element*)malloc(sizeof(cache_element));
    element->data = (char*)malloc(size + 1);
    memcpy(element->data, data, size);
    element->data[size] = '\0';
    
    element->url = (char*)malloc(strlen(url) + 1);
    strcpy(element->url, url);
    
    element->lru_time_track = time(NULL);
    element->next = head;
    element->len = size;
    head = element;
    cache_size += element_size;
    
    printf("Cache element added. Cache size: %d\n", cache_size);
    
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Add cache lock unlocked %d\n", temp_lock_val);
    return 1;
}

// lock is already held by the caller
void remove_cache_element() {
    if (head == NULL) {
        return;
    }

    cache_element *temp = head;        // LRU candidate
    cache_element *prev_to_temp = NULL; // Node before LRU
    cache_element *curr = head;
    cache_element *prev = NULL;

    // Find the LRU element
    while (curr != NULL) {
        if (curr->lru_time_track < temp->lru_time_track) {
            temp = curr;
            prev_to_temp = prev;
        }
        prev = curr;
        curr = curr->next;
    }

    // Remove the LRU element from the list
    if (temp == head) {
        head = head->next;
    } else if (prev_to_temp != NULL) {
        prev_to_temp->next = temp->next;
    }

    // Adjust cache size
    cache_size -= (temp->len + 1)                // data size + null
                  + (strlen(temp->url) + 1)      // url size + null
                  + sizeof(cache_element);

    printf("Removing from cache: %s\n", temp->url);

    // Free memory
    free(temp->data);
    free(temp->url);
    free(temp);
}


// void remove_cache_element(){
//     cache_element* p = NULL;    
//     cache_element* q;    
//     cache_element* temp = NULL;
    
//     if(head != NULL){
//         // Find the least recently used element
//         for(q = head; q != NULL; q = q->next){
//             if(temp == NULL || q->lru_time_track < temp->lru_time_track){
//                 temp = q;
//                 p = (q == head) ? NULL : p;  // p should point to the previous node
//             }
//             if(q->next && q->next != temp){
//                 p = q;  // Update p to point to the node before temp
//             }
//         }
        
//         if(temp == head){
//             head = head->next;
//         }
//         else if(p != NULL){
//             p->next = temp->next;
//         }
        
//         cache_size = cache_size - (temp->len) - sizeof(cache_element) - strlen(temp->url) - 1;
//         printf("Removing from cache: %s\n", temp->url);
        
//         free(temp->data);
//         free(temp->url);
//         free(temp);
//     }
// }

int main(int argc, char* argv[]){
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;
    
    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);
    
    if(argc == 2){
        port_number = atoi(argv[1]);
    }
    else{
        printf("Usage: %s <port_number>\n", argv[0]);
        exit(1);
    }
    
    printf("Starting Proxy Server at port : %d\n", port_number);
    
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if(proxy_socketId < 0){
        perror("Failed to create socket!\n");
        exit(1);
    }
    
    int reuse = 1;
    if(setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0){
        perror("setSockOpt failed\n");
    }
    
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    if(bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("Port is not available!\n");
        exit(1);
    }
    
    printf("Binding on port %d\n", port_number);
    
    int listen_status = listen(proxy_socketId, MAX_CLIENTS);
    if(listen_status < 0){
        perror("Error in listening!\n");
        exit(1);
    }
    
    int i = 0;
    int Connected_socketId[MAX_CLIENTS];

    while(1){
        bzero((char*)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);
        client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
        
        if(client_socketId < 0){
            perror("Not able to connect!\n");
            continue;  // Continue instead of exit to handle more clients
        }
        else{
            Connected_socketId[i] = client_socketId;
        }
        
        struct sockaddr_in* client_pt = (struct sockaddr_in*)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);
        printf("Client connected with port number %d and IP address %s\n", ntohs(client_addr.sin_port), str);
        
        pthread_create(&tid[i], NULL, thread_fn, (void*)&Connected_socketId[i]);
        i = (i + 1) % MAX_CLIENTS;  // Wrap around to reuse thread slots
    }
    
    close(proxy_socketId);
    return 0;
}