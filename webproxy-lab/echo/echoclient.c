#include "csapp.h"

// argv 로 host, port를 인자로 입력받음
int main(int argc, char **argv)
{
    int clientfd;      // 서버와 연결된 소켓 파일 디스크립터
    char *host, *port; // 명령행 인자에서 서버 주소와 포트를 받음
    char buf[MAXLINE]; // 사용자 입력이나 서버 응답을 담을 버퍼
    rio_t rio;         // robust I/O 구조체

    // 명령행 인자 개수가 잘못된 경우, 사용법 출력 후 종료
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <host> <port> \n", argv[0]);
        exit(0);
    }
    // 인자로 받은 host, port를 저장 (예: localhost, 3000)
    host = argv[1];
    port = argv[2];

    // 서버에 연결시도, 내부적으로 socket, getaddrinfo, connect 함수 호출
    clientfd = Open_clientfd(host, port);
    // robust I/O를 쓰기 위해 rio를 초기화
    Rio_readinitb(&rio, clientfd);

    // 사용자 입력 한 줄씩 읽음
    while (Fgets(buf, MAXLINE, stdin) != NULL)
    {
        Rio_writen(clientfd, buf, strlen(buf)); // 입력한 데이터 서버로 전송
        Rio_readlineb(&rio, buf, MAXLINE);      // 서버로부터 한 줄을 읽어옴(에코 응답)
        Fputs(buf, stdout);                     // 읽은 데이터 화면에 출력
    }

    // 소켓 종료하고 프로그램 출력
    Close(clientfd);
    exit(0);
}