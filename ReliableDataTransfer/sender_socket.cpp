/*
 * sender_socket.cpp
 * CSCE 463 - Spring 2019
 * Feras Khemakhem
 */

#include "sender_socket.h"
#include "stdafx.h"

//////////////////// stat thread function //////////////////////////
UINT SenderSocket::workerThread(LPVOID pParam)
{
	HANDLE events[] = { socketReceiveReady, full };
	while (true)
	{
		DWORD timeout;
		// set timeout
	//	if (pending packets)
	//		timeout = timerExpire - cur_time;
	//	else
	//		timeout = INFINITE;

		// wait to see if times out or not
		int ret = WaitForMultipleObjects(2, events, false, timeout);
		switch (ret)
		{
		// if break occurs, retransmit buffer here
		case timeout: retx buf[senderBase % W];
			break;
		// packet is ready in the socket - get the ACK
		case socket: receiveACK(); // move senderBase; fast retx
			break;
		// packet is ready in the sender - send the packet
		case sender: sendto(buf[nextToSend % W]);
			nextToSend++;
			break;
		// 
		default: handle failed wait;
		}
		if (first packet of window || just did a retx(timeout / 3 - dup ACK)
			|| senderBase moved forward)
			recompute timerExpire;
			
	}
}

//////////////////// stat thread function //////////////////////////
UINT statThread(LPVOID pParam)
{
	StatData* stat = (StatData*)pParam;
	while (WaitForSingleObject(stat->isDone, 2000) == WAIT_TIMEOUT) {
		double time = (clock() - stat->start_time) / 1000;
		printf("%d\n", stat->next_seq);
	}
	return 0;
}

/******************** CONSTRUCTOR ********************/
SenderSocket::SenderSocket() {
	// itializations
	start_time = clock(); 
	seq_number = 0; 
	sock = INVALID_SOCKET; 
	elapsed_time = 0.0;
	this->next_seq = 0;
	this->s = new StatData();
	this->s->start_time = start_time;
	this->s->isDone = CreateEventA(NULL, true, false, NULL);
	this->eventQuit = this->full = CreateEventA(NULL, true, false, NULL);
	this->empty = this->socketReceiveReady = CreateEventA(NULL, true, true, NULL);

	devRTT = 0;
}

/******************** DESTRUCTOR ********************/
SenderSocket::~SenderSocket() {
	if (WaitForSingleObject(this->stat, 0) != WAIT_OBJECT_0) { // if stat thread not terminated
		// sets the stat thread to complete so that it knows it is done printing
		SetEvent(this->s->isDone);
		CloseHandle(stat);
	}
	if (WaitForSingleObject(this->worker, 0) != WAIT_OBJECT_0) {
		CloseHandle(worker);
	}
	delete s;
}

/******************** OPEN ********************/
int SenderSocket::Open(char* targetHost, int port, int window_size, LinkProperties* link_prop) {

	double beginRTT, endRTT;
	this->s->sender_wind_size = window_size;
	this->s->RTT = link_prop->RTT;

	if (sock != INVALID_SOCKET) {
		// if sock value is already set then Open is already called
		return ALREADY_CONNECTED;
	}

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
	this->RTO = max(1.0, 2 * link_prop->RTT);

	if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) { // let the OS select next available port for me
		// handle errors
		printf("Bind errors\n");
		exit(-1);
	}

	// for sendto
	this->targetHost = targetHost;
	this->port = port;
	if ((packet_size = initialize_sockaddr(request, targetHost, port)) != STATUS_OK) {
		return packet_size; // for INVALID_NAME
	}

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

		beginRTT = (clock() - this->start_time) / 1000.0; // elapsed open is used for elapsed time if SYN and SYN-ACK work
		// printf("[%.3f] --> SYN %d (attempt %d of %d, RTO %.3f) to %s\n", this->link_prop->RTT, seq_number, i, 3, this->RTO, inet_ntoa(request.sin_addr));

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

	// update stat thread with receiver info
	update_receiver_info((ReceiverHeader*)ans);

	// RTO calculation
	endRTT = (clock() - this->start_time) / 1000.0;
	this->s->RTT = elapsed_time = endRTT - beginRTT; // updating to get elapsed time for print after function is complete, in the main
	calculate_RTO(elapsed_time);

	// start stat thread
	this->stat = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)statThread, s, 0, NULL);
	this->worker = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)workerThread, s, 0, NULL);
	// start worker thread


	return STATUS_OK; // implement error checking for part 5
}

/******************** SEND ********************/
int SenderSocket::Send(char* charBuf, int bytes) {

	// if socket not open... aka if send called without open
	if (sock == INVALID_SOCKET) {
		return NOT_CONNECTED;
	}
	
	// code provided
	HANDLE arr[] = { eventQuit, empty };
	WaitForMultipleObjects(2, arr, false, INFINITE);
	// no need for mutex as no shared variables are modified
	// int slot = this->next_seq % this->s->get_effective_win_size();
	// Packet *p = pending_pkts + slot; // pointer to packet struct
	// SenderDataHeader *sdh = (SenderDataHeader*)(p->buf);
	// packet->seq = this->next_seq;
	// ... // set up remaining fields in sdh and p
	// memcpy(sdh + 1, charBuf, bytes);
	next_seq++;
	//ReleaseSemaphore(full, 1);
	// end of code provided
	
	// send request

	// receive info

	// update receiver info

	// update stat thread with receiver info
	// update_receiver_info((ReceiverHeader*)ans);

	// update RTO 

	return STATUS_OK; // implement error checking for part 5
}

/******************** CLOSE ********************/
int SenderSocket::Close() {

	double beginRTT, endRTT;
	if (sock == INVALID_SOCKET) {
		// sock set in Open
		return NOT_CONNECTED;
	}

	// for select
	timeval tp;
	fd_set fd;
	// set timeout to 10
	tp.tv_usec = (this->RTO - int(this->RTO))*1e6; // get the decimals of the RTO
	tp.tv_sec = int(this->RTO); // get the full seconds of RTO
	struct sockaddr_in req;
	initialize_sockaddr(req, this->targetHost, this->port);


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
		if ((packet_size = sendto(sock, (char*)&sh, sizeof(SenderSynHeader), 0, (struct sockaddr*)&req, sizeof(req))) == SOCKET_ERROR) {
			printf("[%.3f] --> failed sendto with %d\n", (clock() - this->start_time) / 1000.0, WSAGetLastError());
			return FAILED_SEND;
		}

		beginRTT = (clock() - this->start_time) / 1000.0; // elapsed open is used for elapsed time if SYN and SYN-ACK work
		// printf("[%.3f] --> FIN %d (attempt %d of %d, RTO %.3f)\n", beginRTT, this->seq_number, i, 5, this->RTO);

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
	endRTT = (clock() - this->start_time) / 1000.0;
	calculate_RTO(endRTT - beginRTT);

	printf("[%.3f] <-- FIN-ACK %d window %d\n", endRTT, rh->ackSeq, rh->recvWnd);

	// sets the stat thread to complete so that it knows it is done printing
	SetEvent(this->s->isDone); // trigger waitforoneobject
	CloseHandle(stat); // join

	// semaphore closed
	SetEvent(this->eventQuit);


	// close socket to end communication... if already closed or not open, error will yield
	if (closesocket(sock) == SOCKET_ERROR) {
		// should not even get here
		printf("[%.3f] --> failed to close socket with %d\n", (clock() - start_time) / 1000.0, WSAGetLastError()); // check if this is how they want it
		return NOT_CONNECTED;
	}
	sock = INVALID_SOCKET;
	return STATUS_OK; // implement error checking for part 5
}




////////////////////////////////// H E L P E R   F U N C T I O N S //////////////////////////////////
int SenderSocket::initialize_sockaddr(struct sockaddr_in& server, char* host, int port) {

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
				printf("[%.3f] --> target %s is invalid\n", (clock() - start_time) / 1000.0, host);
				return INVALID_NAME;
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

	return STATUS_OK;
}

void SenderSocket::update_receiver_info(ReceiverHeader* rh) {
	this->s->receiver_wind_size = rh->recvWnd;
	this->s->get_effective_win_size();
	this->s->next_seq = rh->ackSeq;
}

void SenderSocket::calculate_RTO(double sample_RTT) {
	double alpha = 0.125, beta = 0.25;
	this->s->RTT = (1 - alpha) * this->s->RTT + alpha * sample_RTT;
	devRTT = (1 - beta) * devRTT + beta * fabs(sample_RTT - this->s->RTT);
	RTO = this->s->RTT + 4 * max(devRTT, 0.010);
}