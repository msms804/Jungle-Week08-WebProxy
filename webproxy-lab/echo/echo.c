#include "csapp.h"

// 클라이언트 하나와의 연결을 처리하는 함수
void echo(int connfd) // accept()로부터 전달된 연결용 소켓
{
    size_t n;          // 읽은 바이트 수
    char buf[MAXLINE]; // 데이터 저장 버퍼(입출력 겸용)
    rio_t rio;         // robust I/O를 위한 구조체

    Rio_readinitb(&rio, connfd); // connfd와 연결된 robust I/O 구조체를 초기화

    // 클라이언트가 보낸 한 줄 단위 데이터를 읽음
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0)
    {
        // 서버 쪽 로그 출력: 받은 데이터의 바이트 수 표시
        printf("server received %d bytes\n", (int)n);
        // 클라이언트로 받은 데이터를 그대로 다시 보냄 (에코 기능)
        Rio_writen(connfd, buf, n);
    }
}

// [클라이언트] -> "hello\n" -> [서버: 읽음 + 로그 출력] -> "hello\n" 다시 전송 -> [클라이언트]