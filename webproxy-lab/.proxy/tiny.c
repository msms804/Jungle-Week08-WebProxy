/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

void doit(int fd)
{ // fd : 클라이언트 소켓 파일 디스크립터(웹브라우저 연결)
  // 이 함수를 통해 요청을 읽고 응답을 보내는 한 사이클이 이루어짐.

  // 변수 선언
  int is_static;    // 정적인 파일 요청인지 여부
  struct stat sbuf; // stat()으로 얻는 파일 정보 구조체
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  // buf : 전체 요청 줄 저장
  //  method : GET, POST 같은 요청 방식
  //  uri : 요청된 자원경로 ex) index.html
  char filename[MAXLINE], cgiargs[MAXLINE];
  // filename : 실제 서버 상의 파일경로
  // cgiargs : CGI 프로그램에 넘길 인자
  rio_t rio;

  // 클라이언트 요청 한줄읽기
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request line: %s", buf);
  printf("%s", buf);
  // HTTP 요청의 첫줄은 이런 형태 : GET /index.html HTTP/1.1.
  // 이 줄을 buf에 저장한다.

  // 요청 파싱  파싱 : 어떤 문자열 데이터를 구조적으로 쪼개서 의미를 이해하는 과정
  sscanf(buf, "%s %s %s", method, uri, version);

  // GET이외의 메서드는 거절한다
  if (strcasecmp(method, "GET"))
  {
    clienterror(fd, method, "501", "Not inplemented", "Tiny does not implement this method");
    return;
  } // 대소문자 구분 없이 "GET"인지 확인, POST,PUT 등이 오면 501 에러 반환

  // 요청 헤더 읽기
  read_requesthdrs(&rio); // 나머지 요청 헤더들을 읽고 무시함

  // URI 파싱(정적VS동적)
  is_static = parse_uri(uri, filename, cgiargs);
  // ex) index,html -> 정적 | cgi-bin/adder?arg1=1&arg2=2 -> 동적
  //  filename : 실제 서버 경로로 바뀜 | cgiargs : CGI 인자 저장

  // 파일 존재 확인
  if (stat(filename, &sbuf) < 0)
  {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return; // 파일이 없으면 404 Not Found
  }
  // 정적 파일 제공
  if (is_static)
  {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return; // 정적 파일이 일반 파일인지, 읽을 수 있는지 확인
    }
    serve_static(fd, filename, sbuf.st_size);
    // serve_static()으로 파일 전송
  }
  else
  { // CGI 프로그램 실행
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return; // 실행 가능한 CGI 프로그램인지 확인
    }
    serve_dynamic(fd, filename, cgiargs);
    // serve_dynamic()으로 CGI 실행 후 결과 전송
  }
}
// 함수 헤더 ,이 함수는 에러 응답을 만드는 데 필요한 모든 정보를 받아서 사용한다.
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  // fd:클라이언트와 연결된 소켓, cause:에러 원인 ex파일이름 , errnum : 상태코드 ex)404
  // shortmsg:짧은 설명 ex)Not Found , longmsg : 긴 설명 ex) Tiny couldn't find this file
  //  이 함수는 에러 응답을 만드는 데 필요한 모든 정보를 받아서 사용한다.

  char buf[MAXLINE], body[MAXBUF];
  // buf : HTTP 헤더를 담는 용도 | body : HTML 본문(에러 메세지 페이지)

  // HTML 본문(body) 작성
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
  // HTML 페이지를 한 줄씩 누적해서 작성

  // HTTP헤더와 본문을 출력
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  // HTTP/1.0 404 Not Found 같은 상태 줄 출력
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  // Content-type은 text/html(브라우저가 HTML로 렌더링 하도록)
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  // Content-length는 본문 길이
  Rio_writen(fd, body, strlen(body));
  // 마지막에 실제 HTML 본물 출력
}
void read_requesthdrs(rio_t *rp)
{
  //	•	rp: robust I/O 구조체 포인터 (Rio_readlineb 사용을 위한 스트림)
  //  •	역할: HTTP 요청 헤더를 한 줄씩 읽다가 빈 줄 "\r\n"이 나오면 멈춰.
  //  •	이유: HTTP 요청 헤더는 빈 줄로 끝난다는 규칙이 있기 때문.

  // 버퍼 선언
  char buf[MAXLINE];

  // 첫번째 줄 읽기
  Rio_readlineb(rp, buf, MAXLINE);

  // 빈 줄까지 반복해서 읽기
  while (strcmp(buf, "\r\n"))
  {                                  // 지금 읽은 줄이 빈 줄인지 확인
    printf("Header: %s",buf);
    Rio_readlineb(rp, buf, MAXLINE); // 빈줄(\r\n)이 나오기 전까지 계속 읽고 출력
    //printf("%s", buf);               // 헤더의 끝 줄은 빈 줄로 표시되므로 이 조건으로 반복 종료
  }
  return;
}
int parse_uri(char *uri, char *filename, char *cgiargs)
{ // 요청된 URI가 정적인지(dynamic인지) 판단하고,해당하는 파일 이름과 CGI 인자를 추출
  //	•	uri: 클라이언트가 요청한 경로 (예: /index.html, /cgi-bin/adder?x=1&y=2)
  //  •	filename: 실제 서버 파일 경로로 변환되어 담길 곳
  //  •	cgiargs: CGI 인자 문자열이 담길 곳
  //  •	리턴값:
  //    •	1이면 정적(static) 콘텐츠
  //    •	0이면 동적(dynamic) 콘텐츠
  char *ptr;
  if (!strstr(uri, "cgi-bin"))
  { // 정적 콘텐츠라면
    //  •	strstr(uri, "cgi-bin")ㄹ이 NULL이면, "cgi-bin"이 없다는 뜻 → 정적 콘텐츠
    //  •	예: /index.html, /images/logo.png 등
    strcpy(cgiargs, "");             // CGI 인자는 없음
    strcpy(filename, ".");           // 현재 디렉터리 기준 시작
    strcat(filename, uri);           // 파일 경로 완성
    if (uri[strlen(uri) - 1] == '/') // URI가 폴더로 끝나면
      strcat(filename, "home.html"); // 기본 파일로 home.html 사용
    return 1;                        // 정적 콘텐츠임을 반환
  }
  else
  {
    ptr = index(uri, '?'); // '?' 위치 찾기 (cgi 인자 구분자)

    if (ptr)
    {                           // 인자가 있는 경우
      strcpy(cgiargs, ptr + 1); // ? 뒤 내용 복사
      *ptr = '\0';              // ? 기준으로 문자열 자르기
    }
    else
    {
      strcpy(cgiargs, ""); // 인자 없음
    }

    strcpy(filename, "."); // 경로 시작
    strcat(filename, uri); // 파일 경로 완성
    return 0;              // 동적 콘텐츠임을 반환
  }
  /*어떤 친구가 웹서버에 와서 무언가를 요청했어.
    •	요청 주소에 "cgi-bin"이 없으면 "파일 보여달라"는 거고,
    •	있으면 "계산 좀 해줘"라는 뜻이야.

  그래서 서버는 이렇게 판단해:
    •	"정적이면, 파일 경로만 만들어주고"
    •	"동적이면, 실행파일이랑 인자도 준비해!"*/
}
void serve_static(int fd, char *filename, int filesize)
{
  // 정적인 파일(HTML, 이미지 등)을 클라이언트에게 보내는 핵심 함수
  /*•	fd: 클라이언트와 연결된 소켓
    •	filename: 클라이언트가 요청한 파일 이름
    •	filesize: 파일 크기 (이미 stat으로 구했음)  */

  // 변수 선언
  int srcfd;                                  // 파일 디스크립터
  char *srcp, filetype[MAXLINE], buf[MAXBUF]; // srcp:메모리에 매핑된 파일 주소
  // filetype : MIME 타입 저장할 버퍼 , buf : 응답 헤더 저장용

  get_filetype(filename, filetype); // MIME 타입 결정
  // •	filetype에 따라 브라우저가 어떻게 해석할지 결정됨 (text/html, image/jpeg 등)

  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf + strlen(buf), "Server: Tiny Web Server\r\n");
  sprintf(buf + strlen(buf), "Connection: close\r\n");
  sprintf(buf + strlen(buf), "Content-length: %d\r\n", filesize);
  sprintf(buf + strlen(buf), "Content-type: %s\r\n\r\n", filetype);
  //	마지막 줄에 빈 줄 \r\n\r\n은 본문 시작을 알리는 신호
  Rio_writen(fd, buf, strlen(buf)); // 클라이언트로 헤더 전송

  printf("Response headers:\n%s", buf);

  srcfd = Open(filename, O_RDONLY, 0);                        // 파일 열기
  srcp = malloc(filesize);
  if (srcp == NULL) {
      Close(srcfd);
      fprintf(stderr, "Error: malloc failed for file %s (size: %d)\n", filename, filesize);

      // 클라이언트에 HTTP 500 오류 응답 전송
      sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
      sprintf(buf + strlen(buf), "Content-type: text/html\r\n\r\n");
      sprintf(buf + strlen(buf), "<html><body><p>Server error: memory allocation failed.</p></body></html>\r\n");
      Rio_writen(fd, buf, strlen(buf));
      return;
  }

  //srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 파일을 메모리에 매핑

  Rio_readn(srcfd,srcp,filesize);
  // Mmap : 파일을 메모리에 통째로 올림(성능 좋고 코드 간결함)
  Close(srcfd); // 파일 디스크립터는 닫아도 메모리엔 살아있음

  Rio_writen(fd, srcp, filesize); // 파일 내용을 클라이언트로 전송
  free(srcp);
  //Munmap(srcp, filesize);         // 메모리 해제, 매핑 해제
}
void get_filetype(char *filename, char *filetype)
//  •	확장자를 보고 MIME 타입 결정
//  •	브라우저가 어떻게 렌더링할지를 결정하는 중요한 역할
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".mpg"))
    strcpy(filetype, "video/mpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else
    strcpy(filetype, "text/plain");
}
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  //  •	fd: 클라이언트와 연결된 소켓
  //  •	filename: 실행할 CGI 프로그램 경로 (예: ./cgi-bin/adder)
  //  •	cgiargs: CGI 프로그램에 넘겨줄 인자 (예: x=1&y=2)

  // 버퍼와 exec 인자 준비
  char buf[MAXLINE], *emptylist[] = {NULL};
  // buf : HTTP 헤더를 담을 임시 버퍼 | emptylist:execve()함수에서 사용할 프로그램 인자 리스트(여기선없대)

  // HTTP 응답 헤더 전송
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  // CGI 프로그램을 실행하기 전에 최소한의 응답 헤더를 먼저 전송
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));
  // 이후에 실제 프로그램의 출력 stdout이 이어짐

  // 자식 프로세스 생성 후 CGI 실행
  if (Fork() == 0){  // 자식 프로세스 | fork()로 새 프로세스를 생성
    setenv("QUERY_STRING", cgiargs, 1);   // 환경 변수 설정 (인자 전달) | adder.c에서 사용하는 getenv("QUERY_STRING")가능하게 설정
    Dup2(fd, STDOUT_FILENO);              // Dup2()를 통해 stdout을 클라이언트 소켓으로 리다이렉션
    Execve(filename, emptylist, environ); // Execve()로 CGI 실행 (출력은 fd로 감) , 실행되면 그 아래 코드는 실행 안됨
  }
  Wait(NULL); //부모 프로세스는 자식이 끝날 때 까지 기다림(좀비 프로세스 방지)
}