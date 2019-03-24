/*
 * sender_socket.cpp
 * CSCE 463 - Spring 2019
 * Feras Khemakhem
 */

#include "sender_socket.h"
#include "stdafx.h"

void SenderSocket::initialize_sockaddr(struct sockaddr_in& server, char* host, int port) {

	// structure used in DNS lookups
	struct hostent *remote;

	// structure for connecting to server
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;					// sets the family to IPv4
	server.sin_port = htons(port);					// provides the port 0 to bind to... 

	if (host != NULL) {
		DWORD IP;
		// inet_pton(AF_INET, targetHost, &IP);
		IP = inet_addr(host);
		if (IP == INADDR_NONE) { //not an IP
			// if not a valid IP, then do a DNS lookup
			if ((remote = gethostbyname(host)) == NULL)
			{
				printf("failed with %d\n", WSAGetLastError());
				exit(-1);
			}
			else { // take the first IP address and copy into sin_addr 
				memcpy((char *)&(server.sin_addr), remote->h_addr, remote->h_length);
				IP = server.sin_addr.S_un.S_addr;
			}
		}
		else {
			server.sin_addr.S_un.S_addr = IP;
		}
	}
	else
		server.sin_addr.s_addr = INADDR_ANY;			// allows me to receive packets on all physics interfaces of the system

}

int SenderSocket::Open(char* targetHost, int port, int window_size, LinkProperties* link_prop) {
	/////////////////////// open UDP socket ///////////////////////
	// initialize socket
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
		// handle errors
		printf("Invalid socket with %d\n", WSAGetLastError());
		exit(-1);
	}

	// bind port 0
	struct sockaddr_in local;
	initialize_sockaddr(local);
	/*memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;					// sets the family to IPv4
	local.sin_addr.s_addr = INADDR_ANY;			// allows me to receive packets on all physics interfaces of the system
	local.sin_port = htons(0);		*/			// provides the port 0 to bind to... 
	if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) { // let the OS select next available port for me
		// handle errors
		printf("Bind errors\n");
		exit(-1);
	}

	// for sendto
	int seq_number = 0;
	int bytes;
	struct sockaddr_in request;
	initialize_sockaddr(request, targetHost, port);

	// for select
	timeval tp;
	fd_set fd;
	// set timeout to 10
	tp.tv_usec = (RTO - int(RTO))*1e3; // get the decimals of the RTO
	tp.tv_sec = int(RTO); // get the full seconds of RTO

	// send request for handshake
	//memset(&request, 0, sizeof(request));
	//request.sin_family = AF_INET;
	//request.sin_addr.s_addr = inet_addr(targetHost);	// server's IP
	//request.sin_port = htons(port);			// port provided
	SenderSynHeader sh{};
	SenderDataHeader sender_data{};
	sender_data.flags = Flags(1, 0, 0); // SYN is 1, rest are 0
	sender_data.seq = seq_number; // WIP
	sh.lp = *link_prop;
	sh.sdh = sender_data;
	for (int i = 1; i <= 4; ++i) {
		// tried too many times
		if (i == 4) return TIMEOUT;


		////////////////////////// send request to the server //////////////////////////
		if ((bytes = sendto(sock, (char*)&sh, sizeof(SenderSynHeader), 0, (struct sockaddr*)&request, sizeof(request))) == SOCKET_ERROR) {
			printf("[%.3f] --> failed sendto with %d\n", (clock() - this->start_time) / 1000.0, WSAGetLastError());
			return FAILED_SEND;
		}
		printf("[%.3f] --> SYN %d (attempt %d of %d, RTO %.3f) to %s\n", (clock() - this->start_time)/1000.0, seq_number, i, 3, this->RTO, inet_ntoa(request.sin_addr));

		// return of the handshake

		////////////////////////// get ready to recieve response and check for timeout //////////////////////////

		FD_ZERO(&fd);					// clear set
		FD_SET(sock, &fd);				// add your socket to the set
		if ((bytes = select(0, &fd, NULL, NULL, &tp)) != 0) {
			// bytes == 0 is when a timeout occurs
			if (bytes > 0) 
				break; // break from the for loop because no more attempts are needed
			// error checking
			if (bytes < 0) {
				printf("socket error in select %d\n", WSAGetLastError());
				return -1; // returning -1 because this error is unacceptable but not covered
			}
		}
	}

	////////////////////////// reading received data //////////////////////////
	// initializations for receiving
	struct sockaddr_in response;
	initialize_sockaddr(response, targetHost, port);
	int response_size = sizeof(response);
	char ans[MAX_PKT_SIZE];

	// in theory select told us that sock is ready to recvfrom, so no need to reattempt
	if ((bytes = recvfrom(sock, ans, MAX_PKT_SIZE, 0, (struct sockaddr*)&response, &response_size)) == SOCKET_ERROR) {
		printf("[%.3f] <-- failed recvfrom with %d\n", (clock() - this->start_time) / 1000.0, WSAGetLastError());
		return FAILED_RECV;
	}
	ReceiverHeader *rh = (ReceiverHeader*)ans;
	double elapsed_synack = (clock() - this->start_time) / 1000.0;
	this->RTO = (elapsed_synack) * 3.0; // RTO = RTT * 3
	printf("[%.3f] <-- SYN-ACK %d window %d; setting initial RTO to %.3f\n", elapsed_synack, seq_number, rh->recvWnd, this->RTO);


	return STATUS_OK; // implement error checking for part 5
}

int SenderSocket::Send(char* charBuf, int bytes) {
	return STATUS_OK; // implement error checking for part 5
}

int SenderSocket::Close() {
	return STATUS_OK; // implement error checking for part 5
}