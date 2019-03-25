/*
 * sender_socket.h
 * CSCE 463 - Spring 2019
 * Feras Khemakhem
 */

#pragma once
#include "stdafx.h"
#include "packet_structs.h"


class SenderSocket {
	// pt 1
	clock_t start_time;
	SOCKET sock;
	double RTO; // RTO that is constantly updated and used in next communication
	double elapsed_open, elapsed_close; // elapsed time between SYN-ACK and SYN, then between FIN and SYN-ACK
	int packet_size; // variable packet size for last packet received --> can be used in many functions
	void initialize_sockaddr(struct sockaddr_in& server, char* host = NULL, int port = 0);
	struct sockaddr_in request;
	int seq_number;
	LinkProperties* link_prop;
public:
	SenderSocket() { start_time = clock(); RTO = 1.0; elapsed_open = 0.0; elapsed_close = 0.0; seq_number = 0; } // start timer
	int Open(char* targetHost, int port, int window_size, LinkProperties* lp); // targetHost, MAGIC_PORT, senderWindow, &lp
	int Send(char* charBuf, int bytes); // charBuf + off, bytes
	int Close();
	double get_elapsed_open() { return elapsed_open; }
	double get_elapsed_close() { return elapsed_close;  }
	int get_packet_size() { return packet_size; }
};