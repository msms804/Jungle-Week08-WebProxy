#include "csapp.h" // robust I/O 함수들과 네트워크 래퍼 함수들이 정의된 헤더

int main(int argc, char **argv)
{
    int clientfd; // 클라이언트 소켓 식별자
    char *host, *port, *buf[MAXLINE]; // 서버 호스트명, 포트 번호, 버퍼
    rio_t rio; // robust I/O 구조체

    // 명령줄 인자가 3개가 아니면 사용법 출력 후 종료
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]); // 사용법 출력
        exit(0);
    }

    // 호스트명과 포트 번호를 명령줄 인자에서 가져옴
    host = argv[1]; // 호스트명
    port = argv[2]; // 포트 번호

    // 서버에 연결하기 위한 클라이언트 소켓 열기
    clientfd = Open_clientfd(host, port); // 호스트와 포트 번호로 클라이언트 소켓 열기

    // 클라이언트 소켓을 기반으로 robust I/O 구조체 초기화
    Rio_readinitb(&rio, clientfd); // 클라이언트 소켓을 기반으로 rio 구조체 초기화

    // 사용자로부터 한 줄씩 입력받아 서버에 전송하는 반복문
    while (Fgets(buf, MAXLINE, stdin) != NULL) // 표준 입력으로부터 한 줄 읽기
    {
        // 읽은 데이터를 서버에 전송
        Rio_writen(clientfd, buf, strlen(buf)); // 클라이언트 소켓을 통해 서버에 데이터 전송

        // 서버로부터 응답을 읽어와서 출력
        Rio_readlineb(&rio, buf, MAXLINE); // 서버로부터 한 줄 읽기
        
        // 읽은 데이터를 표준 출력으로 출력
        Fputs(buf, stdout);
    }

    Close(clientfd); // 클라이언트 소켓 닫기
    exit(0); // 프로그램 종료
}