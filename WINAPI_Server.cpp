#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "resource.h"
#include <commctrl.h>

#define WM_SOCKET  (WM_USER+1)

#define MULTICAST_RECV_IPv4 "235.7.8.1"
#define MULTICAST_SEND_TO_CLIENT_IPv4 "235.7.8.2"

#define MULTICAST_RECV_IPv6 "FF12::1:2:3:9"
#define MULTICAST_SEND_TO_CLIENT_IPv6 "FF12::1:2:3:4"

#define SERVERIPV4  "127.0.0.1"
#define SERVERIPV6  "::1"
#define SERVERPORT  9000
#define REMOTEPORT  9000

#define BUFSIZE     257                  
#define MSGSIZE     (BUFSIZE-sizeof(int)-ID_SIZE-CHECK_WHO) 
#define ID_SIZE     20
#define CHECK_WHO   1


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
//static SOCKET	g_sockv4;

typedef struct ALL_SERVER_SOCKET_ {
	SOCKET tcp_v4;
	SOCKET tcp_v6;
	SOCKET udp_recv_v4;
	SOCKET udp_send_v4;
	SOCKET udp_recv_v6;
	SOCKET udp_send_v6;

	SOCKADDR_IN remoteaddr_v4;
	SOCKADDR_IN6 remoteaddr_v6;
}ALL_SERVER_SOCKET;

struct SOCKET_SendnRecv {
	SOCKET recv;
	SOCKET send;
};

struct CHAT_MSG
{
	int  type;
	char client_id[ID_SIZE];
	char buf[MSGSIZE];
	char whoSent;
};

/* <------------------    [1] 컨트롤 전역변수    ------------------> */
static HINSTANCE     g_hInst; // 응용 프로그램 인스턴스 핸들
static HANDLE		 g_hServerThread;
static HWND			 hEdit_User_Send;
static HWND			 hEdit_Serv_Send;


static int			 userIndex_name;
static int			 userIndex_out;
static int			 userIndex_addr;

static CHAT_MSG		 g_chatmsg;

/* <------------------    [1] 컨트롤 전역변수    ------------------> */


/* <------------------    [2] 콜백 및 통신 스레드 함수 선언    ------------------> */
// A) 소켓 관리 함수
BOOL AddSocketInfo(SOCKET sock, bool isIPv6);
void RemoveSocketInfo(int nIndex);

// B)출력부
void DisplayText_Acc(char* fmt, ...);
void DisplayText_Send(char* fmt, ...);
void err_quit(char* msg);
void err_display(char* msg);

// C) 사용자 정의함수
BOOL CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM); // 컨트롤 핸들러 (서버 GUI 프로시저)
DWORD WINAPI TCP(LPVOID); // TCP 통신 스레드(소켓 입출력 모델 사용)
DWORD WINAPI UDPv4_Multicast(LPVOID); // UDPv4 통신 스레드
DWORD WINAPI UDPv6_Multicast(LPVOID); // UDPv6 통신 스레드

int SendtoAll(); // 프로토콜 통합 데이터 전송함수

DWORD WINAPI ServerMain(LPVOID arg);

/* <------------------    [2] 콜백 및 통신 스레드 함수 선언    ------------------> */



int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	// 변수초기화
	g_chatmsg.type = 0;

	// 대화상자 생성
	g_hInst = hInstance;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, WndProc);

	// 윈속 종료
	WSACleanup();
	return 0;
}
// 대화상자 프로시저
BOOL CALLBACK WndProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {

/* <-------------------- [1] 컨트롤 핸들 지역변수 ------------------------> */

	// 2) IDC_USERNAME1 ~ IDC_USERNAME5
	static HWND		     hUserNames[5];

	// 3) IDC_USERADDR1 ~ IDC_USERADDR5
	static HWND			 hUserAddrs[5];
	
	// 4) IDC_BUTTON_OUT1 ~ IDC_BUTTON_OUT1 : 추방버튼
	static HWND			 hUserOutBtns[5];

	// 5) 
	// IDC_COMBO1 : 보낼 유저 선택 콤보박스
	// IDC_EDIT_SENDTO_C : 유저에게 보낼 메시지
	// IDC_BUTTON_SEND : 유저에게 메시지 보내기 버튼
	static HWND          hUserCombo;
	static HWND			 hSendtoUserMsg;
	static HWND			 hSendtoUserBtn;

	// 6) 게임 관련 버튼
	// IDC_BUTTON_GAME
	static HWND			 hGameBtn;

/* <-------------------- [1] 컨트롤 핸들 지역변수 ------------------------> */

	switch (uMsg) {
	case WM_INITDIALOG:
		//컨트롤 핸들 얻기
		hEdit_User_Send = GetDlgItem(hDlg, IDC_EDIT_ALLMSG);
		hEdit_Serv_Send = GetDlgItem(hDlg, IDC_EDIT_ALLMSG2);

		hUserNames[0] = GetDlgItem(hDlg, IDC_USERNAME1);
		hUserNames[1] = GetDlgItem(hDlg, IDC_USERNAME2);
		hUserNames[2] = GetDlgItem(hDlg, IDC_USERNAME3);
		hUserNames[3] = GetDlgItem(hDlg, IDC_USERNAME4);
		hUserNames[4] = GetDlgItem(hDlg, IDC_USERNAME5);

		hUserAddrs[0] = GetDlgItem(hDlg, IDC_USERADDR1);
		hUserAddrs[1] = GetDlgItem(hDlg, IDC_USERADDR2);
		hUserAddrs[2] = GetDlgItem(hDlg, IDC_USERADDR3);
		hUserAddrs[3] = GetDlgItem(hDlg, IDC_USERADDR4);
		hUserAddrs[4] = GetDlgItem(hDlg, IDC_USERADDR5);

		hUserOutBtns[0] = GetDlgItem(hDlg, IDC_BUTTON_OUT1);
		hUserOutBtns[1] = GetDlgItem(hDlg, IDC_BUTTON_OUT2);
		hUserOutBtns[2] = GetDlgItem(hDlg, IDC_BUTTON_OUT3);
		hUserOutBtns[3] = GetDlgItem(hDlg, IDC_BUTTON_OUT4);
		hUserOutBtns[4] = GetDlgItem(hDlg, IDC_BUTTON_OUT5);

		hUserCombo = GetDlgItem(hDlg, IDC_COMBO1);
		hSendtoUserMsg = GetDlgItem(hDlg, IDC_EDIT_SENDTO_C);
		hSendtoUserBtn = GetDlgItem(hDlg, IDC_BUTTON_SEND);

		// 컨트롤 초기화
		g_hServerThread = CreateThread(NULL, 0, ServerMain, NULL, 0, NULL);

		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_BUTTON_SEND:
			GetDlgItemText(hDlg, IDC_EDIT_SENDTO_C, (LPSTR)g_chatmsg.buf, MSGSIZE);
			DisplayText_Send("[To All] %s \r\n", g_chatmsg.buf);
			return TRUE;
		}

	default:
		return DefWindowProc(hDlg, uMsg, wParam, lParam);
	}
	return FALSE;
}
DWORD WINAPI ServerMain(LPVOID arg) {
	int retval;

	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	/*----- IPv4, 6 소켓 초기화 시작 -----*/
	// socket()
	SOCKET listen_sockv4 = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sockv4 == INVALID_SOCKET) err_quit("socket()");

	SOCKET listen_sockv6 = socket(AF_INET6, SOCK_STREAM, 0);
	if (listen_sockv6 == INVALID_SOCKET) err_quit("socket()");
	
	/*----- IPv4, 6 소켓 초기화 끝 -----*/


	/*----- UDP IPv4 소켓 초기화 시작 -----*/
	SOCKET listen_sock_UDPv4 = socket(AF_INET, SOCK_DGRAM, 0);
	if (listen_sock_UDPv4 == INVALID_SOCKET) err_quit("socket()");

	SOCKET send_sock_UDPv4 = socket(AF_INET, SOCK_DGRAM, 0);
	if (send_sock_UDPv4 == INVALID_SOCKET) err_quit("socket()");

	// 소켓 주소 구조체 초기화
	SOCKADDR_IN remoteaddr_v4;
	ZeroMemory(&remoteaddr_v4, sizeof(remoteaddr_v4));
	remoteaddr_v4.sin_family = AF_INET;
	remoteaddr_v4.sin_addr.s_addr = inet_addr(MULTICAST_SEND_TO_CLIENT_IPv4);
	remoteaddr_v4.sin_port = htons(REMOTEPORT);

	/*----- UDP IPv4 소켓 초기화 끝 -----*/


	/*----- UDP IPv6 소켓 초기화 시작 -----*/
	SOCKET listen_sock_UDPv6 = socket(AF_INET6, SOCK_DGRAM, 0);
	if (listen_sock_UDPv6 == INVALID_SOCKET) err_quit("socket()");

	SOCKET send_sock_UDPv6 = socket(AF_INET6, SOCK_DGRAM, 0);
	if (send_sock_UDPv6 == INVALID_SOCKET) err_quit("socket()");

	// 소켓 주소 구조체 초기화
	SOCKADDR_IN6 remoteaddr_v6;
	ZeroMemory(&remoteaddr_v6, sizeof(remoteaddr_v6));
	remoteaddr_v6.sin6_family = AF_INET6;
	int remoteaddr6_len = sizeof(remoteaddr_v6);
	WSAStringToAddress(MULTICAST_SEND_TO_CLIENT_IPv6, AF_INET6, NULL,
		(SOCKADDR*)&remoteaddr_v6, &remoteaddr6_len);
	remoteaddr_v6.sin6_port = htons(REMOTEPORT);

	/*----- UDP IPv6 소켓 초기화 끝 -----*/

	ALL_SERVER_SOCKET All_Sock = { 
		listen_sockv4, listen_sockv6,
		listen_sock_UDPv4, send_sock_UDPv4,
		listen_sock_UDPv6, send_sock_UDPv6,
		remoteaddr_v4, remoteaddr_v6};

	ALL_SERVER_SOCKET* All_Sock_P = &All_Sock;

	HANDLE hThread[3];
	hThread[0] = CreateThread(NULL, 0, TCP, (LPVOID)All_Sock_P, 0, NULL);
	hThread[1] = CreateThread(NULL, 0, UDPv4_Multicast, (LPVOID)All_Sock_P, 0, NULL);
	hThread[2] = CreateThread(NULL, 0, UDPv6_Multicast, (LPVOID)All_Sock_P, 0, NULL);
	DWORD please = WaitForMultipleObjects(3, hThread, TRUE, INFINITE);
	
	return 0;
}

DWORD WINAPI SENDTOALL(LPVOID arg) {
	DisplayText_Send("%s \r\n", g_chatmsg.buf);

	return 0;
}

DWORD WINAPI TCP(LPVOID arg) {
	ALL_SERVER_SOCKET* socks = (ALL_SERVER_SOCKET*) arg;
	SOCKET listen_sockv4 = socks->tcp_v4;
	SOCKET listen_sockv6 = socks->tcp_v6;
	SOCKET send_sock_UDPv4 = socks->udp_send_v4;
	SOCKET send_sock_UDPv6 = socks->udp_send_v6;

	SOCKADDR_IN remoteaddr_v4 = socks->remoteaddr_v4;
	SOCKADDR_IN6 remoteaddr_v6 = socks->remoteaddr_v6;

	// 데이터 통신에 사용할 변수(공통)
	FD_SET rset;
	SOCKET client_sock;
	int addrlen, i, j;
	int retval, retvalUDP;
	SOCKADDR_IN clientaddrv4;
	SOCKADDR_IN6 clientaddrv6;

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
	Sleep(3000);

	while (1) {
		// 소켓 셋 초기화
		FD_ZERO(&rset);
		FD_SET(listen_sockv4, &rset);
		FD_SET(listen_sockv6, &rset);
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
				DisplayText_Acc("[TCPv4 서버] 클라이언트 접속: [%s]:%d \r\n",
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
				DisplayText_Acc("[TCPv6 서버] 클라이언트 접속: %s \r\n", ipaddr);
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

					// UDP 에게도 보내기
					retvalUDP = sendto(send_sock_UDPv4, ptr->buf, BUFSIZE, 0,
						(SOCKADDR*)&remoteaddr_v4, sizeof(remoteaddr_v4));

					// UDP 에게도 보내기
					retvalUDP = sendto(send_sock_UDPv6, ptr->buf, BUFSIZE, 0,
						(SOCKADDR*)&remoteaddr_v6, sizeof(remoteaddr_v6));
				}
			}
		}
	}

	return 0;
}

DWORD WINAPI UDPv4_Multicast(LPVOID arg) {
	ALL_SERVER_SOCKET* socks = (ALL_SERVER_SOCKET*)arg;
	SOCKET listen_sock_UDPv4 = socks->udp_recv_v4;

	SOCKET send_sock_UDPv4 = socks->udp_send_v4;
	SOCKET send_sock_UDPv6 = socks->udp_send_v6;

	SOCKADDR_IN remoteaddr_v4 = socks->remoteaddr_v4;
	SOCKADDR_IN6 remoteaddr_v6 = socks->remoteaddr_v6;

	SOCKADDR_IN peeraddr_v4;
	int addrlen_UDP;
	char buf_UDP[BUFSIZE];

	// <receiving>
	// SO_REUSEADDR 소켓 옵션 설정
	BOOL optval = TRUE;
	int retvalUDP = setsockopt(listen_sock_UDPv4, SOL_SOCKET,
		SO_REUSEADDR, (char*)&optval, sizeof(optval));
	int retvalTCP;
	if (retvalUDP == SOCKET_ERROR) {
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
		err_quit("setsockopt()");
	}

	// <sending>
	//int ttl_v4 = 2; // 문제
	//retvalUDP = setsockopt(send_sock_UDPv4, IPPROTO_IP, IP_MULTICAST_TTL,
	//	(char*)ttl_v4, sizeof(ttl_v4));
	//if (retvalUDP == SOCKET_ERROR) {
	//	err_quit("setsockopt()");
	//}

	char ipaddr[50];
	DWORD ipaddrlen = sizeof(ipaddr);
	
	Sleep(3000);
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

		if (strcmp(buf_UDP, "UDP_CLIENT_ACK") == 0) {
			WSAAddressToString((SOCKADDR*)&peeraddr_v4, sizeof(peeraddr_v4), NULL, ipaddr, &ipaddrlen);
			DisplayText_Acc("[UDPv4 서버] 클라이언트 접속: %s\n", ipaddr); // 서버가 보낸게 아니라면, 서버는 맨끝 바이트를 -1로 초기화
			continue;
		}
	
		if (buf_UDP[BUFSIZE - 1] == -1) continue;

		buf_UDP[BUFSIZE - 1] = -1;

		// UDP v4에게 보냄
		retvalUDP = sendto(send_sock_UDPv4, buf_UDP, BUFSIZE, 0,
			(SOCKADDR*)&remoteaddr_v4, sizeof(remoteaddr_v4));

		if (retvalUDP == SOCKET_ERROR) {
			err_display("sendto()");
			continue;
		}

		// UDP v6 에게도 보냄
		retvalUDP = sendto(send_sock_UDPv6, buf_UDP, BUFSIZE, 0,
			(SOCKADDR*)&remoteaddr_v6, sizeof(remoteaddr_v6));
		if (retvalUDP == SOCKET_ERROR) {
			err_display("sendto()");
			continue;
		}

		// to TCP
		for (int j = 0; j < nTotalSockets; j++) {
			SOCKETINFO* ptr2 = SocketInfoArray[j];
			retvalTCP = send(ptr2->sock, buf_UDP, BUFSIZE, 0);
			if (retvalTCP == SOCKET_ERROR) {
				err_display("send()");
				RemoveSocketInfo(j);
				--j; // 루프 인덱스 보정
				continue;
			}
		}
	}
}

DWORD WINAPI UDPv6_Multicast(LPVOID arg) {
	ALL_SERVER_SOCKET* socks = (ALL_SERVER_SOCKET*)arg;
	SOCKET listen_sock_UDPv6 = socks->udp_recv_v6;

	SOCKET send_sock_UDPv4 = socks->udp_send_v4;
	SOCKET send_sock_UDPv6 = socks->udp_send_v6;

	SOCKADDR_IN remoteaddr_v4 = socks->remoteaddr_v4;
	SOCKADDR_IN6 remoteaddr_v6 = socks->remoteaddr_v6;

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
	int retvalTCP;
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
		err_quit("setsockopt()");
	}

	Sleep(3000);
	while (1) {
		addrlen_UDP = sizeof(peeraddr_v6);
		retvalUDP = recvfrom(listen_sock_UDPv6, buf_UDP, BUFSIZE, 0,
			(SOCKADDR*)&peeraddr_v6, &addrlen_UDP);
		if (retvalUDP == SOCKET_ERROR) {
			err_display("recvfrom()");
			continue;
		}

		if (strcmp(buf_UDP, "UDP_CLIENT_ACK") == 0) {
			WSAAddressToString((SOCKADDR*)&peeraddr_v6, sizeof(peeraddr_v6), NULL, ipaddr, &ipaddrlen);
			DisplayText_Acc("[UDPv6 서버] 클라이언트 접속: %s\n", ipaddr); // 서버가 보낸게 아니라면, 서버는 맨끝 바이트를 -1로 초기화
			continue;
		}

		if (buf_UDP[BUFSIZE - 1] == -1) continue;
		
		buf_UDP[BUFSIZE - 1] = -1;

		// UDP v6에게 보냄
		retvalUDP = sendto(send_sock_UDPv6, buf_UDP, BUFSIZE, 0,
			(SOCKADDR*)&remoteaddr_v6, sizeof(remoteaddr_v6));
		if (retvalUDP == SOCKET_ERROR) {
			err_display("sendto()");
			continue;
		}

		// UDP v4 에게도 보냄
		retvalUDP = sendto(send_sock_UDPv4, buf_UDP, BUFSIZE, 0,
			(SOCKADDR*)&remoteaddr_v4, sizeof(remoteaddr_v4));

		if (retvalUDP == SOCKET_ERROR) {
			err_display("sendto()");
			continue;
		}

		// to TCP
		for (int j = 0; j < nTotalSockets; j++) {
			SOCKETINFO* ptr2 = SocketInfoArray[j];
			retvalTCP = send(ptr2->sock, buf_UDP, BUFSIZE, 0);
			if (retvalTCP == SOCKET_ERROR) {
				err_display("send()");
				RemoveSocketInfo(j);
				--j; // 루프 인덱스 보정
				continue;
			}
		}
	}
}

// 소켓 정보 추가
BOOL AddSocketInfo(SOCKET sock, bool isIPv6)
{
	if (nTotalSockets >= FD_SETSIZE) {
		DisplayText_Acc("[오류] 소켓 정보를 추가할 수 없습니다! \r\n");
		return FALSE;
	}

	SOCKETINFO* ptr = new SOCKETINFO;
	if (ptr == NULL) {
		DisplayText_Acc("[오류] 메모리가 부족합니다! \r\n");
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
		DisplayText_Acc("[TCPv4 서버] 클라이언트 종료: [%s]:%d \r\n",
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
		DisplayText_Acc("[TCPv6 서버] 클라이언트 종료: %s \r\n", ipaddr);
	}

	closesocket(ptr->sock);
	delete ptr;

	if (nIndex != (nTotalSockets - 1))
		SocketInfoArray[nIndex] = SocketInfoArray[nTotalSockets - 1];

	--nTotalSockets;
}

// 에디트 컨트롤에 문자열 출력
void DisplayText_Acc(char* fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);

	char cbuf[1024];
	vsprintf(cbuf, fmt, arg);

	int nLength = GetWindowTextLength(hEdit_User_Send);
	SendMessage(hEdit_User_Send, EM_SETSEL, nLength, nLength);
	SendMessage(hEdit_User_Send, EM_REPLACESEL, FALSE, (LPARAM)cbuf);
	va_end(arg);
}

void DisplayText_Send(char* fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);

	char cbuf[1024];
	vsprintf(cbuf, fmt, arg);

	int nLength = GetWindowTextLength(hEdit_Serv_Send);
	SendMessage(hEdit_Serv_Send, EM_SETSEL, nLength, nLength);
	SendMessage(hEdit_Serv_Send, EM_REPLACESEL, FALSE, (LPARAM)cbuf);
	va_end(arg);
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
	DisplayText_Acc("[%s] %s", msg, (char*)lpMsgBuf);
	LocalFree(lpMsgBuf);
}