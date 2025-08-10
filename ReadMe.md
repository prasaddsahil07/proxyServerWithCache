# Proxy Server with LRU Cache

## Overview
This is a multi-threaded HTTP proxy server implemented in C with an LRU (Least Recently Used) cache mechanism. The proxy acts as an intermediary between clients and web servers, caching responses to improve performance and reduce network traffic.

## Features
- **Multi-threaded Architecture**: Handles up to 10 concurrent client connections
- **LRU Cache**: Implements Least Recently Used caching algorithm
- **HTTP/1.1 Support**: Handles GET requests with proper HTTP protocol
- **Thread-Safe Operations**: Uses mutexes and semaphores for synchronization
- **Error Handling**: Comprehensive error responses (400, 403, 404, 500, 501, 505)

## System Architecture

### Core Components

1. **Main Server Loop**
   - Creates socket and binds to specified port
   - Accepts incoming client connections
   - Spawns new threads for each client

2. **Thread Management**
   - Uses semaphore to limit concurrent connections (MAX_CLIENTS = 10)
   - Each client connection handled by separate thread
   - Thread-safe operations using pthread mutexes

3. **LRU Cache System**
   - Linked list implementation
   - Cache key: `hostname + path` (e.g., "example.com/")
   - Cache size limits: MAX_SIZE = 200MB, MAX_ELEMENT_SIZE = 10KB
   - Thread-safe with mutex locks

4. **HTTP Request Processing**
   - Parses incoming HTTP requests
   - Forwards requests to remote servers
   - Caches responses for future use

## Key Data Structures

```c
struct cache_element {
    char* data;              // Cached response data
    int len;                 // Length of data in bytes
    char* url;              // Cache key (host + path)
    time_t lru_time_track;  // Timestamp for LRU algorithm
    cache_element* next;    // Pointer to next element
};
```

## Core Functions

### Network Functions
- `connectRemoteServer()`: Establishes connection to target web server
- `sendErrorMessage()`: Sends HTTP error responses to clients

### Cache Functions
- `find()`: Searches for URL in cache, updates LRU timestamp
- `add_cache_element()`: Adds new response to cache
- `remove_cache_element()`: Removes least recently used element

### Request Handling
- `handle_request()`: Forwards request to server, caches response
- `thread_fn()`: Main thread function for client handling
- `checkHTTPversion()`: Validates HTTP version

## Compilation and Usage

### Prerequisites
- GCC compiler
- pthread library
- proxy_parse.h and proxy_parse.c files

### Compilation
```bash
gcc -o proxy proxy_server_with_cache.c proxy_parse.c -lpthread
```

### Running the Server
```bash
./proxy <port_number>
```
Example:
```bash
./proxy 8080
```

### Testing the Proxy

#### Method 1: Using curl
```bash
curl -x localhost:8080 http://example.com
```

#### Method 2: Using telnet
```bash
telnet localhost 8080
```
Then type:
```
GET http://example.com HTTP/1.1
Host: example.com
Connection: close

```
(Press Enter twice)

#### Method 3: Browser Configuration
Configure your browser to use `localhost:8080` as HTTP proxy

## Cache Behavior

### Cache Hit
- When same URL requested again
- Response served directly from cache
- Console shows: "Cache HIT for: hostname/path"
- LRU timestamp updated

### Cache Miss
- When URL not in cache or expired
- Request forwarded to remote server
- Response cached for future requests
- Console shows: "Cache MISS for: hostname/path"

## Thread Synchronization

### Semaphore Usage
```c
sem_t semaphore;           // Limits concurrent connections
sem_wait(&semaphore);      // Acquire connection slot
sem_post(&semaphore);      // Release connection slot
```

### Mutex Usage
```c
pthread_mutex_t lock;      // Protects cache operations
pthread_mutex_lock(&lock); // Acquire cache lock
pthread_mutex_unlock(&lock); // Release cache lock
```

## Memory Management

### Dynamic Allocation
- Cache elements dynamically allocated
- Response buffers grow as needed
- Proper cleanup on thread termination

### Cache Eviction
- Automatic removal when cache size exceeds MAX_SIZE
- LRU algorithm ensures most relevant data retained
- Memory freed when elements removed

## Error Handling

### HTTP Status Codes
- **400**: Bad Request - Malformed HTTP request
- **403**: Forbidden - Permission denied
- **404**: Not Found - Resource not available
- **500**: Internal Server Error - Proxy processing error
- **501**: Not Implemented - Unsupported HTTP method
- **505**: HTTP Version Not Supported - Invalid HTTP version

### Network Errors
- Connection failures to remote servers
- Socket creation errors
- Send/receive operation failures

## Configuration Constants

```c
#define MAX_CLIENTS 10              // Maximum concurrent connections
#define MAX_BYTES 4096             // Buffer size for network operations
#define MAX_ELEMENT_SIZE 10*(1<<10) // 10KB - Maximum cache element size
#define MAX_SIZE 200*(1<<20)       // 200MB - Maximum total cache size
```

## Performance Considerations

### Advantages
- **Caching**: Reduces network traffic and improves response times
- **Multi-threading**: Handles multiple clients simultaneously
- **Memory Efficient**: LRU eviction prevents memory overflow

### Limitations
- Only supports HTTP GET requests
- No HTTPS support
- Cache stored in memory (lost on restart)
- Fixed connection limit (10 clients)

## Debugging and Monitoring

### Console Output
- Client connection information
- Cache hit/miss statistics
- Thread synchronization status
- Error messages and debugging info

### Key Log Messages
```
Client connected with port number XXXX and IP address X.X.X.X
Looking for URL in cache: example.com/
Cache HIT for: example.com/
Cache MISS for: example.com/
Added to cache: example.com/
Cache element added. Cache size: XXXX
```

## Common Interview Questions & Answers

### Q: How does the LRU cache work?
**A**: The cache uses a linked list where each element has a timestamp (`lru_time_track`). When an element is accessed, its timestamp is updated. When cache is full, the element with the oldest timestamp is removed.

### Q: How do you handle thread safety?
**A**: We use:
- Semaphore to limit concurrent connections
- Mutex locks to protect cache operations
- Each thread operates on separate socket descriptors

### Q: What happens when cache is full?
**A**: The `add_cache_element()` function calls `remove_cache_element()` in a loop until there's enough space for the new element.

### Q: How do you handle different HTTP methods?
**A**: Currently only GET requests are supported. Other methods return a 501 "Not Implemented" error.

### Q: What's the cache key strategy?
**A**: Cache key is formed by concatenating hostname and path (e.g., "example.com/index.html"), ensuring same URLs are cached regardless of other headers.

## Potential Improvements
- HTTPS support with SSL/TLS
- Support for POST, PUT, DELETE methods
- Persistent cache storage
- Configuration file support
- Load balancing capabilities
- Better error recovery mechanisms
-
-
-
- <img width="853" height="459" alt="Screenshot (354)" src="https://github.com/user-attachments/assets/72c29133-f477-4d37-b588-91cf2f385fc9" />



