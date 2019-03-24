/*
 * sender_socket.h
 * CSCE 463 - Spring 2019
 * Feras Khemakhem
 */

#pragma once
#include "stdafx.h"
#include "packet_structs.h"


class SenderSocket {
	// #2
	clock_t start_time;
	SOCKET sock;
	double RTO;
	void initialize_sockaddr(struct sockaddr_in& server, char* host = NULL, int port = 0);
public:
	SenderSocket() { start_time = clock(); RTO = 1.0; } // start timer
	int Open(char* targetHost, int port, int window_size, LinkProperties* lp); // targetHost, MAGIC_PORT, senderWindow, &lp
	int Send(char* charBuf, int bytes); // charBuf + off, bytes
	int Close();
};