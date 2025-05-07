#include "csapp.h"

// 클라이언트로부터 받은 데이터를 다시 보내는 echo 함수 선언
void echo(int connfd);

int main(int argc, char **argv)
{
    int listenfd, connfd;                                // 클라이언트의 연결을 수신하기 위한 리스닝 소켓
    socklen_t clientlen;                                 // 주소 길이 전달용
    struct sockaddr_storage clientaddr;                  // 연결된 클라이언트의 주소 저장 (IPv4/IPv6 호환)
    char client_hostname[MAXLINE], client_port[MAXLINE]; // 클라이언트의 IP, 포트를 문자열로 저장

    // 명령행 인자가 부족하면 사용법 출력하고 종료
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    // 듣기식별자를 열어 클라이언트 연결을 기다릴 준비를 함
    listenfd = Open_listenfd(argv[1]);

    // 무한루프로 다수의 클라이언트 요청 반복적으로 처리
    while (1)
    {
        clientlen = sizeof(struct sockaddr_storage);
        // 클라이언트 주소 정보도 clientaddr에 저장됨
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        // 클라이언트의 IP 주소와 포트 번호를 문자열로 변환 (로깅용)
        Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        // 클라이언트 접속 정보를 터미널에 출력
        printf("Connected to (%s %s)\n", client_hostname, client_port);
        echo(connfd);  // 연결된 클라이언트로부터 데이터를 읽고 다시 보내주는 에코 함수 호출
        Close(connfd); // 클라이언트와의 연결 종료
    }
    exit(0); // 이 코드는 도달하지 않지만, 문법상 메인 함수 종료 처리
}