#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#pragma warning(disable : 4996)

#define WM_SOCKET  (WM_USER+1)

#define MULTICAST_RECV_IPv4 "235.7.8.1"
#define MULTICAST_SEND_TO_CLIENT_IPv4 "235.7.8.2"


#define MULTICAST_RECV_IPv6 "FF12::1:2:3:9"
#define MULTICAST_SEND_TO_CLIENT_IPv6 "FF12::1:2:3:4"

#define SERVERPORT 9000
#define REMOTEPORT 9000
#define BUFSIZE    256+1

// 소켓 정보 저장을 위한 구조체와 변수
struct SOCKETINFO
{
	SOCKET sock;
	bool   isIPv6;
	char   buf[BUFSIZE];
	int    recvbytes;
};

int nTotalSockets = 0;
SOCKETINFO* SocketInfoArray[FD_SETSIZE];

struct SOCKET_SendnRecv {
	SOCKET recv;
	SOCKET send;
};

// 소켓 관리 함수
BOOL AddSocketInfo(SOCKET sock, bool isIPv6);
void RemoveSocketInfo(int nIndex);

// <수정> : UDP 스레드함수
DWORD WINAPI UDPv4_Multicast(LPVOID);
DWORD WINAPI UDPv6_Multicast(LPVOID);

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// 오류 출력 함수
void err_quit(char* msg);
void err_display(char* msg);

int main(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	int retval, retvalUDP;

	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	/*----- IPv4 소켓 초기화 시작 -----*/
	// socket()
	//printf("IPv4 TCP\n");
	SOCKET listen_sockv4 = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sockv4 == INVALID_SOCKET) err_quit("socket()");

	// bind()
	SOCKADDR_IN serveraddrv4;
	ZeroMemory(&serveraddrv4, sizeof(serveraddrv4));
	serveraddrv4.sin_family = AF_INET;
	serveraddrv4.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddrv4.sin_port = htons(SERVERPORT);
	retval = bind(listen_sockv4, (SOCKADDR*)&serveraddrv4, sizeof(serveraddrv4));
	if (retval == SOCKET_ERROR) err_quit("bind()");

	// listen()
	retval = listen(listen_sockv4, SOMAXCONN);
	if (retval == SOCKET_ERROR) err_quit("listen()");
	/*----- IPv4 소켓 초기화 끝 -----*/

	/*----- IPv6 소켓 초기화 시작 -----*/
	// socket()
	//printf("IPv6 TCP\n");
	SOCKET listen_sockv6 = socket(AF_INET6, SOCK_STREAM, 0);
	if (listen_sockv6 == INVALID_SOCKET) err_quit("socket()");

	// bind()
	SOCKADDR_IN6 serveraddrv6;
	ZeroMemory(&serveraddrv6, sizeof(serveraddrv6));
	serveraddrv6.sin6_family = AF_INET6;
	serveraddrv6.sin6_addr = in6addr_any;
	serveraddrv6.sin6_port = htons(SERVERPORT);
	retval = bind(listen_sockv6, (SOCKADDR*)&serveraddrv6, sizeof(serveraddrv6));
	if (retval == SOCKET_ERROR) err_quit("bind()");

	// listen()
	retval = listen(listen_sockv6, SOMAXCONN);
	if (retval == SOCKET_ERROR) err_quit("listen()");
	/*----- IPv6 소켓 초기화 끝 -----*/


	/*----- UDP IPv4 소켓 초기화 시작 -----*/
	//printf("IPv4 UDP\n");
	SOCKET listen_sock_UDPv4 = socket(AF_INET, SOCK_DGRAM, 0);
	if (listen_sock_UDPv4 == INVALID_SOCKET) err_quit("socket()");

	//printf("IPv4 UDP Send\n");
	SOCKET send_sock_UDPv4 = socket(AF_INET, SOCK_DGRAM, 0);
	if (send_sock_UDPv4 == INVALID_SOCKET) err_quit("socket()");

	struct SOCKET_SendnRecv UDPv4 = { listen_sock_UDPv4, send_sock_UDPv4 };
	SOCKET_SendnRecv* UDPv4_P = &UDPv4;

	/*----- UDP IPv4 소켓 초기화 끝 -----*/


	/*----- UDP IPv6 소켓 초기화 시작 -----*/
	//printf("IPv6 UDP\n");
	SOCKET listen_sock_UDPv6 = socket(AF_INET6, SOCK_DGRAM, 0);
	if (listen_sock_UDPv6 == INVALID_SOCKET) err_quit("socket()");

	//printf("IPv6 UDP Send\n");
	SOCKET send_sock_UDPv6 = socket(AF_INET6, SOCK_DGRAM, 0);
	if (send_sock_UDPv6 == INVALID_SOCKET) err_quit("socket()");

	struct SOCKET_SendnRecv UDPv6 = { listen_sock_UDPv6, send_sock_UDPv6 };
	SOCKET_SendnRecv* UDPv6_P = &UDPv6;

	/*----- UDP IPv6 소켓 초기화 끝 -----*/
	//printf("main : %p %p\n", UDPv4_P, UDPv6_P);
	//printf("%d %d\n", UDPv4, UDPv6);

	HANDLE hThread[3];
	hThread[0] = CreateThread(NULL, 0, UDPv4_Multicast, (LPVOID)UDPv4_P, 0, NULL);
	hThread[1] = CreateThread(NULL, 0, UDPv6_Multicast, (LPVOID)UDPv6_P, 0, NULL);
	DWORD please = WaitForMultipleObjects(2, hThread, TRUE, INFINITE);


	/* -------- 스레드화 해야함! --------- */
	// 데이터 통신에 사용할 변수(공통)
	FD_SET rset;
	SOCKET client_sock;
	int addrlen, i, j;
	// 데이터 통신에 사용할 변수(IPv4)
	SOCKADDR_IN clientaddrv4;
	// 데이터 통신에 사용할 변수(IPv6)
	SOCKADDR_IN6 clientaddrv6;


	while (1) {
		// 소켓 셋 초기화
		FD_ZERO(&rset);
		FD_SET(listen_sockv4, &rset);
		FD_SET(listen_sockv6, &rset);

		//<수정>
		FD_SET(listen_sock_UDPv4, &rset);
		FD_SET(listen_sock_UDPv6, &rset);

		for (i = 0; i < nTotalSockets; i++) {
			FD_SET(SocketInfoArray[i]->sock, &rset);
		}

		// select()
		retval = select(0, &rset, NULL, NULL, NULL);
		if (retval == SOCKET_ERROR) {
			err_display("select()");
			break;
		}

		// 소켓 셋 검사(1): 클라이언트 접속 수용
		if (FD_ISSET(listen_sockv4, &rset)) {
			addrlen = sizeof(clientaddrv4);
			client_sock = accept(listen_sockv4, (SOCKADDR*)&clientaddrv4, &addrlen);
			if (client_sock == INVALID_SOCKET) {
				err_display("accept()");
				break;
			}
			else {
				// 접속한 클라이언트 정보 출력
				printf("[TCPv4 서버] 클라이언트 접속: [%s]:%d\n",
					inet_ntoa(clientaddrv4.sin_addr), ntohs(clientaddrv4.sin_port));
				// 소켓 정보 추가
				AddSocketInfo(client_sock, false);
			}
		}
		if (FD_ISSET(listen_sockv6, &rset)) {
			addrlen = sizeof(clientaddrv6);
			client_sock = accept(listen_sockv6, (SOCKADDR*)&clientaddrv6, &addrlen);
			if (client_sock == INVALID_SOCKET) {
				err_display("accept()");
				break;
			}
			else {
				// 접속한 클라이언트 정보 출력
				char ipaddr[50];
				DWORD ipaddrlen = sizeof(ipaddr);
				WSAAddressToString((SOCKADDR*)&clientaddrv6, sizeof(clientaddrv6),
					NULL, ipaddr, &ipaddrlen);
				printf("[TCPv6 서버] 클라이언트 접속: %s\n", ipaddr);
				// 소켓 정보 추가
				AddSocketInfo(client_sock, true);
			}
		}

		// 소켓 셋 검사(2): 데이터 통신
		for (i = 0; i < nTotalSockets; i++) {
			SOCKETINFO* ptr = SocketInfoArray[i];
			if (FD_ISSET(ptr->sock, &rset)) {
				// 데이터 받기
				retval = recv(ptr->sock, ptr->buf + ptr->recvbytes,
					BUFSIZE - ptr->recvbytes, 0);
				if (retval == 0 || retval == SOCKET_ERROR) {
					RemoveSocketInfo(i);
					continue;
				}

				// 받은 바이트 수 누적
				ptr->recvbytes += retval;
				if (ptr->recvbytes == BUFSIZE) {
					// 받은 바이트 수 리셋
					ptr->recvbytes = 0;

					// 현재 접속한 모든 클라이언트에게 데이터를 보냄!
					for (j = 0; j < nTotalSockets; j++) {
						SOCKETINFO* ptr2 = SocketInfoArray[j];
						retval = send(ptr2->sock, ptr->buf, BUFSIZE, 0);
						if (retval == SOCKET_ERROR) {
							err_display("send()");
							RemoveSocketInfo(j);
							--j; // 루프 인덱스 보정
							continue;
						}
					}
				}
			}
		}
	}/* -------- 스레드화 해야함! --------- */

	return 0;
}



DWORD WINAPI UDPv4_Multicast(LPVOID arg) {
	SOCKET_SendnRecv* socks = (SOCKET_SendnRecv*)arg;
	SOCKET listen_sock_UDPv4 = socks->recv;
	SOCKET send_sock_UDPv4 = socks->send;
	SOCKADDR_IN peeraddr_v4;
	int addrlen_UDP;
	char buf_UDP[BUFSIZE];

	// <receiving>
	// SO_REUSEADDR 소켓 옵션 설정
	BOOL optval = TRUE;
	int retvalUDP = setsockopt(listen_sock_UDPv4, SOL_SOCKET,
		SO_REUSEADDR, (char*)&optval, sizeof(optval));
	if (retvalUDP == SOCKET_ERROR) {
		printf("sockopt()1\n");
		err_quit("setsockopt()");
	}

	SOCKADDR_IN serveraddrUDPv4;
	ZeroMemory(&serveraddrUDPv4, sizeof(serveraddrUDPv4));
	serveraddrUDPv4.sin_family = AF_INET;
	serveraddrUDPv4.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddrUDPv4.sin_port = htons(SERVERPORT);
	retvalUDP = bind(listen_sock_UDPv4, (SOCKADDR*)&serveraddrUDPv4, sizeof(serveraddrUDPv4));
	if (retvalUDP == SOCKET_ERROR) err_quit("bind()");

	// 멀티캐스트 그룹 가입
	struct ip_mreq mreq_v4;
	mreq_v4.imr_multiaddr.s_addr = inet_addr(MULTICAST_RECV_IPv4);
	mreq_v4.imr_interface.s_addr = htonl(INADDR_ANY);
	retvalUDP = setsockopt(listen_sock_UDPv4, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		(char*)&mreq_v4, sizeof(mreq_v4));
	if (retvalUDP == SOCKET_ERROR) {
		printf("sockopt()2\n");
		err_quit("setsockopt()");
	}

	// <sending>
	//int ttl_v4 = 2; // 문제
	//retvalUDP = setsockopt(send_sock_UDPv4, IPPROTO_IP, IP_MULTICAST_TTL,
	//	(char*)ttl_v4, sizeof(ttl_v4));
	//if (retvalUDP == SOCKET_ERROR) {
	//	printf("ttlv4\n");
	//	err_quit("setsockopt()");
	//}

	SOCKADDR_IN remoteaddr_v4;
	ZeroMemory(&remoteaddr_v4, sizeof(remoteaddr_v4));
	remoteaddr_v4.sin_family = AF_INET;
	remoteaddr_v4.sin_addr.s_addr = inet_addr(MULTICAST_SEND_TO_CLIENT_IPv4);
	remoteaddr_v4.sin_port = htons(REMOTEPORT);
	char ipaddr[50];
	DWORD ipaddrlen = sizeof(ipaddr);

	while (1) {
		ZeroMemory(buf_UDP, BUFSIZE);
		addrlen_UDP = sizeof(peeraddr_v4);

		retvalUDP = recvfrom(listen_sock_UDPv4, buf_UDP, BUFSIZE, 0,
			(SOCKADDR*)&peeraddr_v4, &addrlen_UDP); // peeraddr NULL 로 해도 ㄱc 

		// 서버에서는 BUFSIZE+1 만큼 더 보냄
		// 클라이언트는 buf에 저장되어있는 글자수만큼만 보냄
		if (retvalUDP == SOCKET_ERROR) {
			err_display("recvfrom()");
			continue;
		}

		WSAAddressToString((SOCKADDR*)&peeraddr_v4, sizeof(peeraddr_v4), NULL, ipaddr, &ipaddrlen);
		printf("[UDPv4 서버] 클라이언트 데이터 수신: %s\n", ipaddr); // 서버가 보낸게 아니라면, 서버는 맨끝 바이트를 -1로 초기화
		if (buf_UDP[BUFSIZE - 1] == -1) continue;

		buf_UDP[BUFSIZE - 1] = -1;
		retvalUDP = sendto(send_sock_UDPv4, buf_UDP, BUFSIZE, 0,
			(SOCKADDR*)&remoteaddr_v4, sizeof(remoteaddr_v4));
		if (retvalUDP == SOCKET_ERROR) {
			err_display("sendto()");
			continue;
		}
	}
}

DWORD WINAPI UDPv6_Multicast(LPVOID arg) {
	SOCKET_SendnRecv* socks = (SOCKET_SendnRecv*)arg;
	SOCKET listen_sock_UDPv6 = socks->recv;
	SOCKET send_sock_UDPv6 = socks->send;
	SOCKADDR_IN6 peeraddr_v6;
	int addrlen_UDP;
	char buf_UDP[BUFSIZE];

	char ipaddr[50];
	DWORD ipaddrlen = sizeof(ipaddr);

	// <receiving>
	// SO_REUSEADDR 소켓 옵션 설정
	bool optval = TRUE;
	int retvalUDP = setsockopt(listen_sock_UDPv6, SOL_SOCKET,
		SO_REUSEADDR, (char*)&optval, sizeof(optval));
	if (retvalUDP == SOCKET_ERROR) {
		err_quit("setsockopt()");
	}

	SOCKADDR_IN6 serveraddrUDPv6;
	ZeroMemory(&serveraddrUDPv6, sizeof(serveraddrUDPv6));
	serveraddrUDPv6.sin6_family = AF_INET6;
	serveraddrUDPv6.sin6_addr = in6addr_any;
	serveraddrUDPv6.sin6_port = htons(SERVERPORT);
	retvalUDP = bind(listen_sock_UDPv6, (SOCKADDR*)&serveraddrUDPv6, sizeof(serveraddrUDPv6));
	if (retvalUDP == SOCKET_ERROR) err_quit("bind()");

	// 주소 변환(문자열 -> IPv6)
	SOCKADDR_IN6 tmpaddr;
	int templen = sizeof(tmpaddr);
	WSAStringToAddress(MULTICAST_RECV_IPv6, AF_INET6, NULL,
		(SOCKADDR*)&tmpaddr, &templen);

	// 멀티캐스트 그룹 가입
	struct ipv6_mreq mreq_v6;
	mreq_v6.ipv6mr_multiaddr = tmpaddr.sin6_addr;
	mreq_v6.ipv6mr_interface = 0;
	retvalUDP = setsockopt(listen_sock_UDPv6, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
		(char*)&mreq_v6, sizeof(mreq_v6));
	if (retvalUDP == SOCKET_ERROR) {
		err_quit("setsockopt()");
	}

	//<sending>
	// 멀티캐스트 TTL 설정
	int ttl_v6 = 2;
	retvalUDP = setsockopt(send_sock_UDPv6, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
		(char*)&ttl_v6, sizeof(ttl_v6));
	if (retvalUDP == SOCKET_ERROR) {
		printf("ttlv6\n");
		err_quit("setsockopt()");
	}

	// 소켓 주소 구조체 초기화
	SOCKADDR_IN6 remoteaddr_v6;
	ZeroMemory(&remoteaddr_v6, sizeof(remoteaddr_v6));
	remoteaddr_v6.sin6_family = AF_INET6;
	int remoteaddr6_len = sizeof(remoteaddr_v6);
	WSAStringToAddress(MULTICAST_SEND_TO_CLIENT_IPv6, AF_INET6, NULL,
		(SOCKADDR*)&remoteaddr_v6, &remoteaddr6_len);
	remoteaddr_v6.sin6_port = htons(REMOTEPORT);

	while (1) {
		addrlen_UDP = sizeof(peeraddr_v6);
		retvalUDP = recvfrom(listen_sock_UDPv6, buf_UDP, BUFSIZE, 0,
			(SOCKADDR*)&peeraddr_v6, &addrlen_UDP);
		if (retvalUDP == SOCKET_ERROR) {
			err_display("recvfrom()");
			continue;
		}

		WSAAddressToString((SOCKADDR*)&peeraddr_v6, sizeof(peeraddr_v6), NULL, ipaddr, &ipaddrlen);
		printf("[UDPv6 서버] 클라이언트 데이터 수신: %s\n", ipaddr); // 서버가 보낸게 아니라면, 서버는 맨끝 바이트를 -1로 초기화
		if (buf_UDP[BUFSIZE - 1] == -1) continue;

		buf_UDP[BUFSIZE - 1] = -1;
		retvalUDP = sendto(send_sock_UDPv6, buf_UDP, BUFSIZE, 0,
			(SOCKADDR*)&remoteaddr_v6, sizeof(remoteaddr_v6));
		if (retvalUDP == SOCKET_ERROR) {
			err_display("sendto()");
			continue;
		}
	}
}

// 소켓 정보 추가
BOOL AddSocketInfo(SOCKET sock, bool isIPv6)
{
	if (nTotalSockets >= FD_SETSIZE) {
		printf("[오류] 소켓 정보를 추가할 수 없습니다!\n");
		return FALSE;
	}

	SOCKETINFO* ptr = new SOCKETINFO;
	if (ptr == NULL) {
		printf("[오류] 메모리가 부족합니다!\n");
		return FALSE;
	}

	ptr->sock = sock;
	ptr->isIPv6 = isIPv6;
	ptr->recvbytes = 0;
	SocketInfoArray[nTotalSockets++] = ptr;

	return TRUE;
}

// 소켓 정보 삭제
void RemoveSocketInfo(int nIndex)
{
	SOCKETINFO* ptr = SocketInfoArray[nIndex];

	// 종료한 클라이언트 정보 출력
	if (ptr->isIPv6 == false) {
		SOCKADDR_IN clientaddrv4;
		int addrlen = sizeof(clientaddrv4);
		getpeername(ptr->sock, (SOCKADDR*)&clientaddrv4, &addrlen);
		printf("[TCPv4 서버] 클라이언트 종료: [%s]:%d\n",
			inet_ntoa(clientaddrv4.sin_addr), ntohs(clientaddrv4.sin_port));
	}
	else {
		SOCKADDR_IN6 clientaddrv6;
		int addrlen = sizeof(clientaddrv6);
		getpeername(ptr->sock, (SOCKADDR*)&clientaddrv6, &addrlen);

		char ipaddr[50];
		DWORD ipaddrlen = sizeof(ipaddr);
		WSAAddressToString((SOCKADDR*)&clientaddrv6, sizeof(clientaddrv6),
			NULL, ipaddr, &ipaddrlen);
		printf("[TCPv6 서버] 클라이언트 종료: %s\n", ipaddr);
	}

	closesocket(ptr->sock);
	delete ptr;

	if (nIndex != (nTotalSockets - 1))
		SocketInfoArray[nIndex] = SocketInfoArray[nTotalSockets - 1];

	--nTotalSockets;
}

// 소켓 함수 오류 출력 후 종료
void err_quit(char* msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	MessageBox(NULL, (LPCTSTR)lpMsgBuf, msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(1);
}

// 소켓 함수 오류 출력
void err_display(char* msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[%s] %s", msg, (char*)lpMsgBuf);
	LocalFree(lpMsgBuf);
}