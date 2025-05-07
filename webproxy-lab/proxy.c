#include "csapp.h"
#include <pthread.h>
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

// ========================= 캐시 구조체 및 함수 =========================

typedef struct CacheBlock
{
  char url[MAXLINE];
  char object[MAX_OBJECT_SIZE];
  int size;
  struct CacheBlock *prev, *next;
} CacheBlock;

typedef struct
{
  CacheBlock *head, *tail;
  int total_size;
  pthread_rwlock_t lock;
} CacheList;

static CacheList cache;

void cache_init()
{
  cache.head = cache.tail = NULL;
  cache.total_size = 0;
  pthread_rwlock_init(&cache.lock, NULL);
}

void move_to_front(CacheBlock *block)
{
  if (cache.head == block)
    return;
  if (block->prev)
    block->prev->next = block->next;
  if (block->next)
    block->next->prev = block->prev;
  if (cache.tail == block)
    cache.tail = block->prev;

  block->prev = NULL;
  block->next = cache.head;
  if (cache.head)
    cache.head->prev = block;
  cache.head = block;
  if (!cache.tail)
    cache.tail = block;
}

void evict_if_needed(int required_size)
{
  while (cache.total_size + required_size > MAX_CACHE_SIZE)
  {
    CacheBlock *victim = cache.tail;
    if (!victim)
      return;

    if (victim->prev)
      victim->prev->next = NULL;
    cache.tail = victim->prev;
    if (cache.head == victim)
      cache.head = NULL;

    cache.total_size -= victim->size;
    free(victim);
  }
}

int cache_find(const char *url, char *buf)
{
  pthread_rwlock_rdlock(&cache.lock);
  CacheBlock *curr = cache.head;
  while (curr)
  {
    if (strcmp(curr->url, url) == 0)
    {
      memcpy(buf, curr->object, curr->size);
      pthread_rwlock_unlock(&cache.lock);
      return curr->size;
    }
    curr = curr->next;
  }
  pthread_rwlock_unlock(&cache.lock);
  return -1;
}

void cache_store(const char *url, const char *data, int size)
{
  if (size > MAX_OBJECT_SIZE)
    return;

  pthread_rwlock_wrlock(&cache.lock);

  CacheBlock *curr = cache.head;
  while (curr)
  {
    if (strcmp(curr->url, url) == 0)
    {
      pthread_rwlock_unlock(&cache.lock);
      return;
    }
    curr = curr->next;
  }

  evict_if_needed(size);

  CacheBlock *new_block = malloc(sizeof(CacheBlock));
  strcpy(new_block->url, url);
  memcpy(new_block->object, data, size);
  new_block->size = size;

  new_block->prev = NULL;
  new_block->next = cache.head;
  if (cache.head)
    cache.head->prev = new_block;
  cache.head = new_block;
  if (!cache.tail)
    cache.tail = new_block;

  cache.total_size += size;
  printf("[CACHE STORE] %s (%d bytes)\n", url, size); 

  pthread_rwlock_unlock(&cache.lock);
}

// ========================= 기존 proxy 코드 =========================

void doit(int connfd);
int parse_uri(char *uri, char *hostname, char *path, char *port);
void build_http_header(char *http_header, char *hostname, char *path);
void *thread(void *vargp);

int main(int argc, char **argv)
{
  int listenfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  char port[10], host[MAXLINE];

  if (argc != 2)
  {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    exit(1);
  }

  cache_init(); // 캐시 초기화 추가

  listenfd = Open_listenfd(argv[1]);
  if (listenfd < 0)
  {
    fprintf(stderr, "Failed to open listening socket on port %s\n", argv[1]);
    exit(1);
  }
  printf("Proxy listening on port %s\n", argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);

    // 1. 클라이언트 요청을 받기 위한 소켓 연결 수락
    int *connfdp = Malloc(sizeof(int)); // 스레드 인자용 동적할당
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    // 2. 연결된 클라이언트 주소 출력 (디버깅용 로그)
    Getnameinfo((SA *)&clientaddr, clientlen, host, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", host, port);

    // 3. 스레드 생성
    pthread_t tid;
    Pthread_create(&tid, NULL, thread, connfdp); // connfdp는 스레드 인자
  }
}
// 스레드에서 요청 처리 함수
void *thread(void *vargp)
{
  int connfd = *((int *)vargp);   // 인자로 받은 연결 소켓 디스크립터
  Pthread_detach(pthread_self()); // 스레드 리소스를 자동 회수 (join 불필요)
  Free(vargp);                    // malloc으로 할당한 connfd 메모리 해제
  doit(connfd);                   // 실제 프록시 기능 수행 (요청 파싱 → 서버 연결 → 응답 전달)
  Close(connfd);                  // 연결 종료
  return NULL;
}
void doit(int connfd)
{
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE], port[10];
  char http_header[MAXLINE];
  rio_t rio_client, rio_server;
  int serverfd;

  Rio_readinitb(&rio_client, connfd);
  if (!Rio_readlineb(&rio_client, buf, MAXLINE))
    return;
  sscanf(buf, "%s %s %s", method, uri, version);

  // URI 앞 '/' 제거 ("/http://..." → "http://...")
  if (uri[0] == '/')
    memmove(uri, uri + 1, strlen(uri));

  printf("Received URI: %s\n", uri);

  if (strcasecmp(method, "GET"))
  {
    printf("Proxy does not implement the method %s\n", method);
    return;
  }

  // 캐시 체크
  char cache_buf[MAX_OBJECT_SIZE];
  int cached_size = cache_find(uri, cache_buf);
  if (cached_size > 0)
  {
    Rio_writen(connfd, cache_buf, cached_size);
    printf("Cache hit: %s\n", uri);
    return;
  }

  if (parse_uri(uri, hostname, path, port) < 0)
  {
    printf("URI parsing failed: %s\n", uri);
    return;
  }

  build_http_header(http_header, hostname, path);
  serverfd = Open_clientfd(hostname, port);
  if (serverfd < 0)
  {
    printf("Connection to server %s:%s failed.\n", hostname, port);
    return;
  }

  Rio_readinitb(&rio_server, serverfd);
  Rio_writen(serverfd, http_header, strlen(http_header));

  size_t n;
  char object_buf[MAX_OBJECT_SIZE];
  int total_size = 0;
  while ((n = Rio_readlineb(&rio_server, buf, MAXLINE)) > 0)
  {
    Rio_writen(connfd, buf, n);
    if (total_size + n < MAX_OBJECT_SIZE)
      memcpy(object_buf + total_size, buf, n);
    total_size += n;
  }

  if (total_size < MAX_OBJECT_SIZE)
    cache_store(uri, object_buf, total_size);

  Close(serverfd);
}
int parse_uri(char *uri, char *hostname, char *path, char *port)
{
  char *hostbegin, *pathbegin, *portpos;

  if (strncasecmp(uri, "http://", 7) != 0)
    return -1;

  hostbegin = uri + 7;
  pathbegin = strchr(hostbegin, '/');

  if (pathbegin)
  {
    strcpy(path, pathbegin);
  }
  else
  {
    strcpy(path, "/");
  }

  // 이제 host:port만 분리할 수 있게 별도로 복사해놓자
  char hostcopy[MAXLINE];
  if (pathbegin)
  {
    int len = pathbegin - hostbegin;
    strncpy(hostcopy, hostbegin, len);
    hostcopy[len] = '\0';
  }
  else
  {
    strcpy(hostcopy, hostbegin);
  }

  portpos = strchr(hostcopy, ':');
  if (portpos)
  {
    *portpos = '\0';
    strcpy(hostname, hostcopy);
    strcpy(port, portpos + 1);
  }
  else
  {
    strcpy(hostname, hostcopy);
    strcpy(port, "80");
  }

  return 0;
}
void build_http_header(char *http_header, char *hostname, char *path)
{
  char buf[MAXLINE];

  sprintf(http_header, "GET %s HTTP/1.0\r\n", path);
  sprintf(buf, "Host: %s\r\n", hostname);
  strcat(http_header, buf);
  strcat(http_header, user_agent_hdr);
  strcat(http_header, "Connection: close\r\n");
  strcat(http_header, "Proxy-Connection: close\r\n\r\n");
}
