/*
 * sender_socket.cpp
 * CSCE 463 - Spring 2019
 * Feras Khemakhem
 */

#include "sender_socket.h"
#include "stdafx.h"

sockaddr_in SenderSocket::initialize_sockaddr(char* host, int port) {

	// structure used in DNS lookups
	struct hostent *remote;

	// structure for connecting to server
	struct sockaddr_in server;
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


	return server;
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
	local = initialize_sockaddr();
	// memset(&local, 0, sizeof(local));
	// local.sin_family = AF_INET;					// sets the family to IPv4
	// local.sin_addr.s_addr = INADDR_ANY;			// allows me to receive packets on all physics interfaces of the system
	// local.sin_port = htons(0);					// provides the port 0 to bind to... 
	if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) { // let the OS select next available port for me
		// handle errors
		printf("Bind errors\n");
		exit(-1);
	}


	int seq_number = 0;
	int bytes;
	double RTO = 1.0;
	struct sockaddr_in request;
	request = initialize_sockaddr(targetHost, port);

	// send request for handshake
	// memset(&svr, 0, sizeof(svr));
	// svr.sin_family = AF_INET;
	// remote.sin_addr.s_addr = inet_addr(targetHost);	// server's IP /************* DIFFERENT FROM HANDOUD BE CAREFULE *************/
	// svr.sin_port = htons(port);					// port provided
	SenderSynHeader sh{};
	SenderDataHeader sender_data{};
	sender_data.flags = Flags(1, 0, 0); // SYN is 1, rest are 0
	sender_data.seq = seq_number; // WIP
	for (int i = 0; i < 4; ++i) {
		// tried too many times
		if (i == 3) return TIMEOUT;

		printf("[%.3f] --> SYN %d (attempt %d of %d, RTO %.3f) to %s\n", clock() - start_time, seq_number, i+1, 3, RTO, inet_ntoa(svr.sin_addr));
		sh.lp = *link_prop;
		sh.sdh = sender_data;

		////////////////////////// send request to the server //////////////////////////
		if ((bytes = sendto(sock, &sh, sizeof(SenderSynHeader), 0, (struct sockaddr*)&request, sizeof(request)) == SOCKET_ERROR) {
			printf("%.3f --> failed sendto with %d\n", WSAGetLastError());
			return FAILED_SEND;
		}
		
	}

	// return of the handshake
	struct sockaddr_in response;
	response = initialize_sockaddr(targetHost, port);
	int response_size = sizeof(response);
	ReceiverHeader rh{};
	for (int i = 0; i < 4; ++i) {
		// tried too many times
		if (i == 3) return TIMEOUT;

		printf("[%.3f] <-- SYN-ACK %d window %d; setting initial RTO to %.3f\n", clock() - start_time, seq_number, window_size, RTO);
		if ((bytes = recvfrom(sock, &rh, sizeof(ReceiverHeader), 0, (struct sockaddr*)&response, &response_size)) == SOCKET_ERROR) {
			printf("[%.3f] <-- failed recvfrom with %d\n", WSAGetLastError());
			return FAILED_RECV;
		}

	}
	return STATUS_OK; // implement error checking for part 5
}

int SenderSocket::Send(char* charBuf, int bytes) {
	return STATUS_OK; // implement error checking for part 5
}

int SenderSocket::Close() {
	return STATUS_OK; // implement error checking for part 5
}