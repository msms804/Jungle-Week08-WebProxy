#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int connfd);
int parse_uri(char *uri, char *hostname, char *path, char *port);
void build_http_header(char *http_header, char *hostname, char *path);

int main(int argc, char **argv) {
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char port[10], host[MAXLINE];

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, host, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", host, port);
        doit(connfd);
        Close(connfd);
    }
}

void doit(int connfd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[10];
    char http_header[MAXLINE];
    rio_t rio_client, rio_server;
    int serverfd;

    Rio_readinitb(&rio_client, connfd);
    if (!Rio_readlineb(&rio_client, buf, MAXLINE)) return;
    sscanf(buf, "%s %s %s", method, uri, version);

    // URI 앞 '/' 제거 ("/http://..." → "http://...")
    if (uri[0] == '/')
        memmove(uri, uri + 1, strlen(uri));

    printf("Received URI: %s\n", uri);

    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement the method %s\n", method);
        return;
    }

    if (parse_uri(uri, hostname, path, port) < 0) {
        printf("URI parsing failed: %s\n", uri);
        return;
    }

    build_http_header(http_header, hostname, path);
    serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0) {
        printf("Connection to server %s:%s failed.\n", hostname, port);
        return;
    }

    Rio_readinitb(&rio_server, serverfd);
    Rio_writen(serverfd, http_header, strlen(http_header));

    size_t n;
    while ((n = Rio_readlineb(&rio_server, buf, MAXLINE)) > 0) {
        Rio_writen(connfd, buf, n);
    }
    Close(serverfd);
}

int parse_uri(char *uri, char *hostname, char *path, char *port) {
  char *hostbegin, *pathbegin, *portpos;
  
  if (strncasecmp(uri, "http://", 7) != 0)
      return -1;

  hostbegin = uri + 7;
  pathbegin = strchr(hostbegin, '/');

  if (pathbegin) {
      strcpy(path, pathbegin);
  } else {
      strcpy(path, "/");
  }

  // 이제 host:port만 분리할 수 있게 별도로 복사해놓자
  char hostcopy[MAXLINE];
  if (pathbegin) {
      int len = pathbegin - hostbegin;
      strncpy(hostcopy, hostbegin, len);
      hostcopy[len] = '\0';
  } else {
      strcpy(hostcopy, hostbegin);
  }

  portpos = strchr(hostcopy, ':');
  if (portpos) {
      *portpos = '\0';
      strcpy(hostname, hostcopy);
      strcpy(port, portpos + 1);
  } else {
      strcpy(hostname, hostcopy);
      strcpy(port, "80");
  }

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
