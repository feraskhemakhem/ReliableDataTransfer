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
	double elapsed_connect, RTT, elapsed_finish; // RTT, then time between SYN and SYN-ACK, then between SYN-ACK and FIN
	int packet_size; // variable packet size for last packet received --> can be used in many functions
	int initialize_sockaddr(struct sockaddr_in& server, char* host = NULL, int port = 0);
	struct sockaddr_in request;
	int seq_number;
	LinkProperties* link_prop;


	// testing
	char* targetHost;
	int port;
public:
	SenderSocket() { start_time = clock(); RTO = 1.0; this->RTT = 0.0; elapsed_connect = 0.0; elapsed_finish = 0.0; seq_number = 0; sock = INVALID_SOCKET; } // start timer
	int Open(char* targetHost, int port, int window_size, LinkProperties* lp); // targetHost, MAGIC_PORT, senderWindow, &lp
	int Send(char* charBuf, int bytes); // charBuf + off, bytes
	int Close();
	double get_elapsed_connect() { return elapsed_connect; }
	double get_elapsed_finish() { return elapsed_finish;  }
	int get_packet_size() { return packet_size; }
};