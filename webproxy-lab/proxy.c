#include "csapp.h"
#include <limits.h>  /* ULONG_MAX 정의를 위해 추가 */

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* 캐시 구조체 및 관련 데이터 정의 */
typedef struct {
    char *url;          /* 캐시된 URL */
    char *content;      /* 캐시된 웹 객체 내용 */
    size_t content_size; /* 객체 크기 */
    unsigned long timestamp; /* LRU를 위한 타임스탬프 */
    int is_valid;       /* 유효한 캐시 항목인지 여부 */
    int readers;        /* 현재 읽고 있는 스레드 수 */
    pthread_rwlock_t rwlock; /* 읽기/쓰기 락 */
} cache_entry_t;

/* 캐시 구조체 */
typedef struct {
    cache_entry_t *entries; /* 캐시 항목 배열 */
    int num_entries;       /* 총 항목 수 */
    int max_entries;       /* 최대 허용 항목 수 */
    size_t current_size;   /* 현재 캐시 크기 (바이트) */
    pthread_mutex_t mutex; /* 캐시 전체 락 */
} cache_t;

/* 전역 캐시 변수 */
cache_t cache;

/* 스레드 함수 인자를 위한 구조체 정의 */
typedef struct {
    int connfd;
} thread_args;

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/* 함수 프로토타입 */
void doit(int connfd);
int parse_uri(char *uri, char *hostname, char *path, char *port);
void build_http_header(char *http_header, char *hostname, char *path);
void *thread(void *vargp);

/* 캐시 관련 함수 프로토타입 */
void cache_init(int max_entries);
void cache_free(void);
cache_entry_t *cache_find(char *url);
void cache_add(char *url, char *content, size_t content_size);
void cache_evict_lru(size_t required_size);
unsigned long get_timestamp(void);

int main(int argc, char **argv) {
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char port[10], host[MAXLINE];
    pthread_t tid;
    thread_args *args;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // SIGPIPE 신호 무시 설정 (연결이 끊어진 소켓에 쓰기 시도할 때 발생)
    Signal(SIGPIPE, SIG_IGN);
    
    // 캐시 초기화 (MAX_CACHE_SIZE / MAX_OBJECT_SIZE 객체의 10배)
    cache_init(100);
    printf("Cache initialized with max size %d bytes\n", MAX_CACHE_SIZE);

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, host, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", host, port);
        
        // 스레드 인자 구조체 할당
        args = (thread_args *)Malloc(sizeof(thread_args));
        args->connfd = connfd;
        
        // 새 스레드 생성하여 클라이언트 요청 처리
        Pthread_create(&tid, NULL, thread, args);
        // 메인 스레드는 바로 다음 연결을 기다림 (connfd를 닫지 않음)
    }
    
    // 여기에 도달하지 않지만 안전을 위해 추가
    cache_free();
    return 0;
}

void doit(int connfd) {
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE], port[10];
  rio_t rio_client;
  int serverfd;

  // 클라이언트 요청 라인 읽기
  Rio_readinitb(&rio_client, connfd);
  if (!Rio_readlineb(&rio_client, buf, MAXLINE)) return;
  printf("Request line: %s", buf);

  // 요청 라인 파싱
  sscanf(buf, "%s %s %s", method, uri, version);

  // GET 요청만 처리
  if (strcasecmp(method, "GET")) {
      printf("Proxy does not implement the method %s\n", method);
      return;
  }

  // URI 파싱하여 hostname, path, port 추출
  if (parse_uri(uri, hostname, path, port) < 0) {
      printf("URI parsing failed: %s\n", uri);
      return;
  }
  
  // 전체 URL을 캐시 키로 사용
  char url_key[MAXLINE];
  sprintf(url_key, "http://%s:%s%s", hostname, port, path);
  
  // 캐시에서 URL 검색
  cache_entry_t *entry = cache_find(url_key);
  if (entry) {
      // 캐시 히트: 캐시된 내용을 클라이언트에게 전송
      printf("Cache hit for %s\n", url_key);
      Rio_writen(connfd, entry->content, entry->content_size);
      cache_read_complete(entry);
      return;
  }
  
  // 캐시 미스: 서버에 요청
  printf("Cache miss for %s\n", url_key);
  
  // 서버 연결
  serverfd = Open_clientfd(hostname, port);
  if (serverfd < 0) {
      printf("Connection to server %s:%s failed.\n", hostname, port);
      return;
  }
  
  // 서버에 보낼 HTTP 요청 헤더 작성
  char request_hdrs[MAXLINE], host_hdr[MAXLINE], other_hdrs[MAXLINE];
  sprintf(request_hdrs, "GET %s HTTP/1.0\r\n", path);
  
  // 클라이언트 헤더 읽기 및 필요한 헤더 수정 또는 추가
  int is_host_hdr_seen = 0;
  other_hdrs[0] = '\0';
  
  while (Rio_readlineb(&rio_client, buf, MAXLINE) > 0) {
      // 헤더의 끝 확인 (빈 줄)
      if (!strcmp(buf, "\r\n")) break;
      
      // Host 헤더 확인
      if (!strncasecmp(buf, "Host:", 5)) {
          is_host_hdr_seen = 1;
          strcpy(host_hdr, buf);
      }
      // Connection 또는 Proxy-Connection 헤더는 건너뜀
      else if (!strncasecmp(buf, "Connection:", 11) || 
               !strncasecmp(buf, "Proxy-Connection:", 17)) {
          continue;
      }
      // User-Agent 헤더는 건너뜀 (나중에 추가됨)
      else if (!strncasecmp(buf, "User-Agent:", 11)) {
          continue;
      }
      // 그 외 헤더는 그대로 전달
      else {
          strcat(other_hdrs, buf);
      }
  }
  
  // HTTP 요청 헤더 완성
  if (!is_host_hdr_seen) {
      sprintf(host_hdr, "Host: %s\r\n", hostname);
  }
  
  // 최종 HTTP 요청 헤더 조합
  strcat(request_hdrs, host_hdr);
  strcat(request_hdrs, user_agent_hdr);
  strcat(request_hdrs, "Connection: close\r\n");
  strcat(request_hdrs, "Proxy-Connection: close\r\n");
  strcat(request_hdrs, other_hdrs);
  strcat(request_hdrs, "\r\n");  // 헤더의 끝
  
  printf("Forwarding request to server %s:%s\n%s", hostname, port, request_hdrs);
  
  // 서버에 요청 전송
  rio_t rio_server;
  Rio_readinitb(&rio_server, serverfd);
  Rio_writen(serverfd, request_hdrs, strlen(request_hdrs));
  
  // 서버로부터 응답을 받아 클라이언트에게 전달하고 캐싱
  size_t n;
  size_t total_size = 0;
  char cache_buf[MAX_OBJECT_SIZE];
  int cacheable = 1;  // 객체가 캐시 가능한지 여부
  
  // 응답을 버퍼 단위로 읽어 전달
  while ((n = Rio_readnb(&rio_server, buf, MAXLINE)) > 0) {
      // 클라이언트에게 전송
      Rio_writen(connfd, buf, n);
      
      // 캐시 가능한 크기이면 응답을 캐시 버퍼에 저장
      if (cacheable && total_size + n <= MAX_OBJECT_SIZE) {
          memcpy(cache_buf + total_size, buf, n);
          total_size += n;
      } else if (total_size + n > MAX_OBJECT_SIZE) {
          cacheable = 0;  // 최대 객체 크기를 초과하여 캐시 불가능
      }
  }
  
  // 모든 응답을 받았으면 캐시에 저장
  if (cacheable && total_size > 0) {
      cache_add(url_key, cache_buf, total_size);
      printf("Cached %zu bytes for %s\n", total_size, url_key);
  }
  
  Close(serverfd);
}

int parse_uri(char *uri, char *hostname, char *path, char *port) {
    char *hostbegin, *hostend, *pathbegin;
    
    // URI에 http:// 접두사가 없는 경우 (driver.sh는 완전한 URL을 요구함)
    if (strncasecmp(uri, "http://", 7) != 0) {
        // 이미 경로만 있는 경우 (예: /home.html)
        if (uri[0] == '/') {
            // localhost로 간주하고 기본 경로 사용
            strcpy(hostname, "localhost");
            strcpy(path, uri);
            strcpy(port, "80");
            return 0;
        }
        fprintf(stderr, "Error: Invalid URI format (no http://): %s\n", uri);
        return -1;
    }

    // http:// 이후의 호스트 시작 위치
    hostbegin = uri + 7;
    
    // 경로 부분 찾기 (첫 번째 '/')
    pathbegin = strchr(hostbegin, '/');
    
    if (pathbegin) {
        // 호스트 부분의 마지막 위치
        hostend = pathbegin;
        // 경로 복사
        strcpy(path, pathbegin);
    } else {
        // 경로가 없으면 루트 경로로 설정
        hostend = hostbegin + strlen(hostbegin);
        strcpy(path, "/");
    }

    // 임시 호스트 문자열 생성 및 복사
    char hostcopy[MAXLINE];
    strncpy(hostcopy, hostbegin, hostend - hostbegin);
    hostcopy[hostend - hostbegin] = '\0';

    // 호스트에서 포트 번호 분리
    char *portPos = strchr(hostcopy, ':');
    if (portPos) {
        *portPos = '\0';
        strcpy(hostname, hostcopy);
        strcpy(port, portPos + 1);
    } else {
        strcpy(hostname, hostcopy);
        strcpy(port, "80");
    }

    printf("Parsed URI - Host: '%s', Path: '%s', Port: '%s'\n", hostname, path, port);
    return 0;
}

void build_http_header(char *http_header, char *hostname, char *path) {
    char buf[MAXLINE];

    sprintf(http_header, "GET %s HTTP/1.0\r\n", path);
    sprintf(buf, "Host: %s\r\n", hostname);
    strcat(http_header, buf);
    strcat(http_header, user_agent_hdr);
    strcat(http_header, "Connection: close\r\n");
    strcat(http_header, "Proxy-Connection: close\r\n\r\n");
}

/* 스레드 함수 구현 */
void *thread(void *vargp) {
    thread_args *args = (thread_args *)vargp;
    int connfd = args->connfd;
    
    // 스레드를 detach 상태로 만들어 자원을 자동으로 반환하도록 함
    Pthread_detach(pthread_self());
    
    // 메모리 누수 방지를 위해 할당된 인자 구조체 해제
    Free(vargp);
    
    // 클라이언트 요청 처리
    doit(connfd);
    
    // 연결 종료
    Close(connfd);
    
    return NULL;
}

/* 캐시 초기화 함수 */
void cache_init(int max_entries) {
    cache.entries = (cache_entry_t *)Calloc(max_entries, sizeof(cache_entry_t));
    cache.num_entries = 0;
    cache.max_entries = max_entries;
    cache.current_size = 0;
    pthread_mutex_init(&cache.mutex, NULL);
    
    for (int i = 0; i < max_entries; i++) {
        cache.entries[i].is_valid = 0;
        cache.entries[i].url = NULL;
        cache.entries[i].content = NULL;
        cache.entries[i].content_size = 0;
        cache.entries[i].timestamp = 0;
        cache.entries[i].readers = 0;
        pthread_rwlock_init(&cache.entries[i].rwlock, NULL);
    }
}

/* 캐시 해제 함수 */
void cache_free(void) {
    pthread_mutex_lock(&cache.mutex);
    for (int i = 0; i < cache.max_entries; i++) {
        if (cache.entries[i].is_valid) {
            Free(cache.entries[i].url);
            Free(cache.entries[i].content);
        }
        pthread_rwlock_destroy(&cache.entries[i].rwlock);
    }
    Free(cache.entries);
    pthread_mutex_unlock(&cache.mutex);
    pthread_mutex_destroy(&cache.mutex);
}

/* 현재 시간 반환 */
unsigned long get_timestamp(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

/* 캐시에서 URL에 해당하는 항목 찾기 */
cache_entry_t *cache_find(char *url) {
    pthread_mutex_lock(&cache.mutex);
    for (int i = 0; i < cache.max_entries; i++) {
        if (cache.entries[i].is_valid && strcmp(cache.entries[i].url, url) == 0) {
            // 읽기 락 획득
            pthread_rwlock_rdlock(&cache.entries[i].rwlock);
            // 타임스탬프 갱신
            cache.entries[i].timestamp = get_timestamp();
            pthread_mutex_unlock(&cache.mutex);
            return &cache.entries[i];
        }
    }
    pthread_mutex_unlock(&cache.mutex);
    return NULL;
}

/* 캐시 항목 읽기 완료 */
void cache_read_complete(cache_entry_t *entry) {
    pthread_rwlock_unlock(&entry->rwlock);
}

/* LRU 정책에 따라 캐시에서 항목 제거 */
void cache_evict_lru(size_t required_size) {
    unsigned long min_timestamp = ULONG_MAX;
    int lru_index = -1;

    // 가장 오래 사용되지 않은 항목 찾기
    for (int i = 0; i < cache.max_entries; i++) {
        if (cache.entries[i].is_valid && cache.entries[i].timestamp < min_timestamp) {
            min_timestamp = cache.entries[i].timestamp;
            lru_index = i;
        }
    }

    if (lru_index != -1) {
        // 쓰기 락 획득
        pthread_rwlock_wrlock(&cache.entries[lru_index].rwlock);
        
        // 해당 항목의 메모리 해제
        Free(cache.entries[lru_index].url);
        Free(cache.entries[lru_index].content);
        
        // 캐시 항목 무효화
        cache.entries[lru_index].url = NULL;
        cache.entries[lru_index].content = NULL;
        cache.entries[lru_index].is_valid = 0;
        
        // 캐시 크기 갱신
        cache.current_size -= cache.entries[lru_index].content_size;
        cache.num_entries--;
        
        // 쓰기 락 해제
        pthread_rwlock_unlock(&cache.entries[lru_index].rwlock);
    }
}

/* 캐시에 새로운 항목 추가 */
void cache_add(char *url, char *content, size_t content_size) {
    if (content_size > MAX_OBJECT_SIZE) {
        return; // 최대 객체 크기 초과하면 캐시하지 않음
    }

    pthread_mutex_lock(&cache.mutex);

    // 필요한 경우 공간 확보
    while (cache.current_size + content_size > MAX_CACHE_SIZE || cache.num_entries >= cache.max_entries) {
        cache_evict_lru(content_size);
    }

    // 빈 슬롯 찾기
    int empty_slot = -1;
    for (int i = 0; i < cache.max_entries; i++) {
        if (!cache.entries[i].is_valid) {
            empty_slot = i;
            break;
        }
    }

    if (empty_slot == -1) {
        pthread_mutex_unlock(&cache.mutex);
        return; // 빈 슬롯이 없음
    }

    // 쓰기 락 획득
    pthread_rwlock_wrlock(&cache.entries[empty_slot].rwlock);
    
    // 새 항목 초기화
    cache.entries[empty_slot].url = strdup(url);
    cache.entries[empty_slot].content = Malloc(content_size);
    memcpy(cache.entries[empty_slot].content, content, content_size);
    cache.entries[empty_slot].content_size = content_size;
    cache.entries[empty_slot].timestamp = get_timestamp();
    cache.entries[empty_slot].is_valid = 1;
    
    // 캐시 상태 갱신
    cache.current_size += content_size;
    cache.num_entries++;
    
    // 쓰기 락 해제
    pthread_rwlock_unlock(&cache.entries[empty_slot].rwlock);
    pthread_mutex_unlock(&cache.mutex);
}
