#include "csapp.h"          // robust I/O 함수와 관련 구조체를 포함한 헤더 파일 포함

// 클라이언트와 연결된 소켓 파일 디스크립터(connfd)를 인자로 받음
void echo(int connfd)
{
    size_t n;               // 읽은 바이트 수를 저장할 변수
    char buf[MAXLINE];      // 데이터를 읽어올 버퍼 (한 줄 최대 MAXLINE 크기)
    rio_t rio;              // robust I/O 버퍼 구조체

    // connfd(클라이언트와 연결된 소켓)를 기반으로 rio 구조체 초기화
    Rio_readinitb(&rio, connfd);

    // 클라이언트로부터 한 줄씩 데이터를 계속 읽어들이는 반복문
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0)
    {
        // 읽은 바이트 수를 서버 로그로 출력
        printf("server received %zu bytes\n", n);

        // 읽은 데이터를 그대로 다시 클라이언트에게 돌려보냄 (Echo!)
        Rio_writen(connfd, buf, n);
    }
}