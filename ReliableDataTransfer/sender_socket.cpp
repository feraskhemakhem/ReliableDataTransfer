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
	this->link_prop = link_prop;
	struct sockaddr_in local;
	initialize_sockaddr(local);

	if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) { // let the OS select next available port for me
		// handle errors
		printf("Bind errors\n");
		exit(-1);
	}

	// for sendto
	initialize_sockaddr(request, targetHost, port);

	// for select
	timeval tp;
	fd_set fd;
	// set timeout to 10
	tp.tv_usec = (RTO - int(RTO))*1e3; // get the decimals of the RTO
	tp.tv_sec = int(RTO); // get the full seconds of RTO

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
		if ((packet_size = sendto(sock, (char*)&sh, sizeof(SenderSynHeader), 0, (struct sockaddr*)&request, sizeof(request))) == SOCKET_ERROR) {
			printf("[%.3f] --> failed sendto with %d\n", (clock() - this->start_time) / 1000.0, WSAGetLastError());
			return FAILED_SEND;
		}

		elapsed_open = (clock() - this->start_time) / 1000.0; // elapsed open is used for elapsed time if SYN and SYN-ACK work
		printf("[%.3f] --> SYN %d (attempt %d of %d, RTO %.3f) to %s\n", elapsed_open, seq_number, i, 3, this->RTO, inet_ntoa(request.sin_addr));

		// return of the handshake

		////////////////////////// get ready to recieve response and check for timeout //////////////////////////

		FD_ZERO(&fd);					// clear set
		FD_SET(sock, &fd);				// add your socket to the set
		if ((packet_size = select(0, &fd, NULL, NULL, &tp)) != 0) {
			// packet_size == 0 is when a timeout occurs
			if (packet_size > 0) {
				break; // break from the for loop because no more attempts are needed
			}
			// error checking
			if (packet_size < 0) {
				printf("socket error in select %d\n", WSAGetLastError());
				return -1; // returning -1 because this error is unacceptable but not covered
			}
		}
	}

	////////////////////////// reading received data //////////////////////////
	// initializations for receiving
	char ans[MAX_PKT_SIZE];
	struct sockaddr_in response;
	int response_size = sizeof(response);

	// in theory select told us that sock is ready to recvfrom, so no need to reattempt
	if ((packet_size = recvfrom(sock, ans, MAX_PKT_SIZE, 0, (struct sockaddr*)&response, &response_size)) == SOCKET_ERROR) {
		printf("[%.3f] <-- failed recvfrom with %d\n", (clock() - this->start_time) / 1000.0, WSAGetLastError());
		return FAILED_RECV;
	}
	ReceiverHeader *rh = (ReceiverHeader*)ans;
	double elapsed_synack = (clock() - this->start_time) / 1000.0;
	this->RTO = (elapsed_synack) * 3.0; // RTO = RTT * 3
	printf("[%.3f] <-- SYN-ACK %d window %d; setting initial RTO to %.3f\n", elapsed_synack, rh->ackSeq, rh->recvWnd, this->RTO);

	elapsed_open = elapsed_synack - elapsed_open; // updating to get elapsed time for print after function is complete, in the main
	elapsed_close = -1 * elapsed_synack;
	return STATUS_OK; // implement error checking for part 5
}

int SenderSocket::Send(char* charBuf, int bytes) {
	// if socket not open... aka if send called without open
	// return NOT_CONNECTED;
	return STATUS_OK; // implement error checking for part 5
}

int SenderSocket::Close() {
	// for select
	timeval tp;
	fd_set fd;
	// set timeout to 10
	tp.tv_usec = (this->RTO - int(this->RTO))*1e3; // get the decimals of the RTO
	tp.tv_sec = int(this->RTO); // get the full seconds of RTO

	// send request for handshake
	SenderSynHeader sh{};
	SenderDataHeader sender_data{};
	sender_data.flags = Flags(0, 0, 1); // FIN is 1, rest are 0
	sender_data.seq = this->seq_number; // WIP
	sh.lp = *(this->link_prop);
	sh.sdh = sender_data;
	for (int i = 1; i <=6; ++i) {
		// tried too many times
		if (i == 6) return TIMEOUT;

		////////////////////////// send request to the server //////////////////////////
		if ((packet_size = sendto(sock, (char*)&sh, sizeof(SenderSynHeader), 0, (struct sockaddr*)&request, sizeof(request))) == SOCKET_ERROR) {
			printf("[%.3f] --> failed sendto with %d\n", (clock() - this->start_time) / 1000.0, WSAGetLastError());
			return FAILED_SEND;
		}

		double elapsed_send = (clock() - this->start_time) / 1000.0; // elapsed open is used for elapsed time if SYN and SYN-ACK work
		printf("[%.3f] --> FIN %d (attempt %d of %d, RTO %.3f)\n", elapsed_send, seq_number, i, 5, this->RTO);

		if (i == 1)
			elapsed_close = elapsed_send - elapsed_close;

		// return of the handshake

		////////////////////////// get ready to recieve response and check for timeout //////////////////////////

		FD_ZERO(&fd);					// clear set
		FD_SET(sock, &fd);				// add your socket to the set
		if ((packet_size = select(0, &fd, NULL, NULL, &tp)) != 0) {
			// packet_size == 0 is when a timeout occurs
			if (packet_size > 0) {
				break; // break from the for loop because no more attempts are needed
			}
			// error checking
			if (packet_size < 0) {
				printf("socket error in select %d\n", WSAGetLastError());
				return -1; // returning -1 because this error is unacceptable but not covered
			}
		}
	}

	////////////////////////// reading received data //////////////////////////
	// initializations for receiving
	char ans[MAX_PKT_SIZE];
	struct sockaddr_in response;
	int response_size = sizeof(response);

	// in theory select told us that sock is ready to recvfrom, so no need to reattempt
	if ((packet_size = recvfrom(sock, ans, MAX_PKT_SIZE, 0, (struct sockaddr*)&response, &response_size)) == SOCKET_ERROR) {
		printf("[%.3f] <-- failed recvfrom with %d\n", (clock() - this->start_time) / 1000.0, WSAGetLastError());
		return FAILED_RECV;
	}
	ReceiverHeader *rh = (ReceiverHeader*)ans;
	double elapsed_synack = (clock() - this->start_time) / 1000.0;
	this->RTO = (elapsed_synack) * 3.0; // RTO = RTT * 3
	printf("[%.3f] <-- FIN-ACK %d window %d; setting initial RTO to %.3f\n", elapsed_synack, rh->ackSeq, rh->recvWnd, this->RTO);

	elapsed_open = elapsed_synack - elapsed_open; // updating to get elapsed time for print after function is complete, in the main


	// close socket to end communication... if already closed or not open, error will yield
	if (closesocket(sock) == SOCKET_ERROR) {
		printf("[%.3f] --> failed to close socket with %d", WSAGetLastError()); // check if this is how they want it
		return NOT_CONNECTED;
	}
	return STATUS_OK; // implement error checking for part 5
}