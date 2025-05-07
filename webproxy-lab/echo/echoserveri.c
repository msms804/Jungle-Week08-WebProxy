#include "csapp.h"          // robust I/O 함수와 네트워크 함수 래퍼가 포함된 헤더 파일

void echo(int connfd);

int main(int argc, char **argv)
{
    int listenfd, connfd;   // 서버의 listen용 듣기 식별자와 클라이언트 연결용 연결 식별자
    socklen_t clientlen;    // 클라이언트 주소 구조체의 크기
    struct sockaddr_storage clientaddr; // 클라이언트 주소 or domain name 저장할 구조체
    char client_hostname[MAXLINE], client_port[MAXLINE]; // 클라이언트 호스트명과 포트 번호를 저장할 버퍼

    // 명령줄 인자가 2개가 아니면 사용법 출력 후 종료
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]); // 사용법 출력
        exit(0);
    }

    // 포트 번호를 인자로 받아 서버 리슨 소켓 열기
    listenfd = Open_listenfd(argv[1]); // argv[1] 포트 번호로 리슨 소켓 열기

    while (1)   // 반복형 서버: 클라이언트가 연결 올 때마다 반복
    {
        clientlen = sizeof(struct sockaddr_storage); // 클라이언트 주소 구조체 크기 초기화
        // 클라이언트 연결 요청을 수락하고 연결된 소켓 식별자(connfd) 반환
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        // 클라이언트 주소를 문자열로 변환하여 client_hostname과 client_port에 저장
        Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE,
                    client_port, MAXLINE, 0);
        // 클라이언트의 호스트명과 포트 번호를 출력
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
        // 클라이언트와 연결된 connfd를 인자로 하여 echo 함수 호출
        echo(connfd);
        // 클라이언트와의 연결 종료
        Close(connfd);
    }
    exit(0); // 프로그램 종료
}