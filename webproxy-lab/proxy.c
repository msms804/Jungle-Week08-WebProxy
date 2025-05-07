#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

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
  while ((n = Rio_readlineb(&rio_server, buf, MAXLINE)) > 0)
  {
    Rio_writen(connfd, buf, n);
  }
  Close(serverfd);
}

// void doit(int connfd)
// {
//     char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
//     char hostname[MAXLINE], path[MAXLINE], port[10];
//     char http_header[MAXLINE];
//     rio_t rio_client, rio_server;
//     int serverfd;

//     Rio_readinitb(&rio_client, connfd);
//     if (!Rio_readlineb(&rio_client, buf, MAXLINE))
//         return;

//     sscanf(buf, "%s %s %s", method, uri, version);

//     printf("Received URI path: %s\n", uri);

//     if (strcasecmp(method, "GET"))
//     {
//         printf("Proxy does not implement the method %s\n", method);
//         return;
//     }

//     // 고정된 최종 서버 정보
//     strcpy(hostname, "3.38.98.70");
//     strcpy(port, "8000");
//     strcpy(path, uri); // 예: "/home.html", "/godzilla.jpg"

//     build_http_header(http_header, hostname, path);
//     serverfd = Open_clientfd(hostname, port);
//     if (serverfd < 0)
//     {
//         printf("Connection to server %s:%s failed.\n", hostname, port);
//         return;
//     }

//     Rio_readinitb(&rio_server, serverfd);
//     Rio_writen(serverfd, http_header, strlen(http_header));

//     size_t n;
//     while ((n = Rio_readlineb(&rio_server, buf, MAXLINE)) > 0)
//     {
//         Rio_writen(connfd, buf, n);
//     }
//     Close(serverfd);
// }

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