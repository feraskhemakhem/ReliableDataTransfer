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
	int packet_size; // variable packet size for last packet received --> can be used in many functions
	int initialize_sockaddr(struct sockaddr_in& server, char* host = NULL, int port = 0);
	double elapsed_time;
	struct sockaddr_in request;
	int seq_number;
	LinkProperties* link_prop;


	// testing
	char* targetHost;
	int port;
public:
	SenderSocket() { start_time = clock(); seq_number = 0; sock = INVALID_SOCKET; elapsed_time = 0.0; } // start timer
	int Open(char* targetHost, int port, int window_size, LinkProperties* lp); // targetHost, MAGIC_PORT, senderWindow, &lp
	int Send(char* charBuf, int bytes); // charBuf + off, bytes
	int Close();
	double get_elapsed_time() { return elapsed_time; }
	int get_packet_size() { return packet_size; }
};