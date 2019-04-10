/*
 * sender_socket.cpp
 * CSCE 463 - Spring 2019
 * Feras Khemakhem
 */

#include "sender_socket.h"
#include "stdafx.h"

// declaring thread functions for later defining
DWORD WINAPI statThread(LPVOID pParam);
DWORD WINAPI workerThread(LPVOID pParam);

/******************** CONSTRUCTOR ********************/
SenderSocket::SenderSocket() {
	// itializations
	this->start_time = clock(); 
	this->next_seq = this->nextToSend = this->retx_count = this->sameack_count = 0;
	seq_number = 0; 
	sock = INVALID_SOCKET; 
	elapsed_time = 0.0;
	this->s = new StatData(&(this->next_seq));
	this->s->old_sender_wind_base = this->s->sender_wind_base = 0;
	this->s->start_time = this->start_time;
	this->eventQuit = CreateEventA(NULL, true, false, NULL); // TODO: declutter isDone and this into one
	this->socketReceiveReady = CreateEventA(NULL, false, false, NULL);
	devRTT = 0;
}

/******************** DESTRUCTOR ********************/
SenderSocket::~SenderSocket() {
	if (WaitForSingleObject(this->stat, 0) == WAIT_OBJECT_0) { // if stat thread not terminated
		// sets the stat thread to complete so that it knows it is done printing
		SetEvent(this->s->isDone);
		CloseHandle(stat);
	}
	if (WaitForSingleObject(this->worker, 0) == WAIT_OBJECT_0) {
		CloseHandle(worker);
	}

	this->s->isDone = NULL;
	stat = NULL;
	worker = NULL;

	// delete StatData 
	delete s;
	
	// delete buffer
	for (int i = 0; i < this->W; ++i) {
		delete (pending_pkts + i)->buf;
	}
	delete pending_pkts;

}

/******************** OPEN ********************/
int SenderSocket::Open(char* targetHost, int port, int window_size, LinkProperties* link_prop) {

	/////////////////////// initializations ///////////////////////
	this->s->sender_wind_size = this->W = window_size;
	this->s->RTT = link_prop->RTT;
	this->RTO = max(1.0, 2 * this->s->RTT);

	// initalize sempahore handles - TODO: MOVE THIS AROUND SO THAT IT USES EFFECTIVE WINDOW SIZE
	this->full = CreateSemaphore(NULL, 0, window_size, NULL);
	this->empty = CreateSemaphore(NULL, window_size, window_size, NULL);

	// initialize packet buffer and buffer of each inidividual packet
	pending_pkts = new Packet[window_size * MAX_PKT_SIZE];
	for (int i = 0; i < window_size; ++i) {
		(pending_pkts + i)->buf = new char[MAX_PKT_SIZE];
	}

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

	if (WSAEventSelect(sock, socketReceiveReady, FD_READ) == SOCKET_ERROR) {
		printf("socket error in WSAEventSelect %d\n", WSAGetLastError());
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
	this->targetHost = targetHost;
	this->port = port;
	if ((packet_size = initialize_sockaddr(request, targetHost, port)) != STATUS_OK) {
		return packet_size; // for INVALID_NAME
	}

	SenderSynHeader sh{};
	SenderDataHeader sender_data{};
	sender_data.flags = Flags(1, 0, 0); // SYN is 1, rest are 0
	sender_data.seq = next_seq; // 0 basically
	sh.lp = *link_prop;
	sh.sdh = sender_data;
	for (int i = 1; i <= 4; ++i) {
		// tried too many times
		if (i == 4) {
			printf("open timeout\n"); return TIMEOUT;
		}


		////////////////////////// send request to the server //////////////////////////
		if ((packet_size = sendto(sock, (char*)&sh, sizeof(SenderSynHeader), 0, (struct sockaddr*)&request, sizeof(request))) == SOCKET_ERROR) {
			printf("[%.2f] --> failed sendto with %d\n", (clock() - this->start_time) / 1000.0, WSAGetLastError());
			return FAILED_SEND;
		}

		beginRTT = (clock() - this->start_time) / 1000.0; // elapsed open is used for elapsed time if SYN and SYN-ACK work
		// printf("[%.2f] --> SYN %d (attempt %d of %d, RTO %.3f) to %s\n", this->link_prop->RTT, seq_number, i, 3, this->RTO, inet_ntoa(request.sin_addr));

		// return of the handshake

		////////////////////////// get ready to recieve response and check for timeout //////////////////////////


		if ((packet_size = WaitForSingleObject(socketReceiveReady, this->RTO * 1e3)) == WAIT_FAILED) {
			printf("WaitForSingleObject error\n");
			exit(-1);
		}
		else if (packet_size == WAIT_OBJECT_0) {
			break;
		}

		//ResetEvent(this->socketReceiveReady); // reset socket to not ready


	}

	////////////////////////// reading received data //////////////////////////
	// initializations for receiving
	char ans[MAX_PKT_SIZE];
	struct sockaddr_in response;
	int response_size = sizeof(response);

	// in theory select told us that sock is ready to recvfrom, so no need to reattempt
	if ((packet_size = recvfrom(sock, ans, MAX_PKT_SIZE, 0, (struct sockaddr*)&response, &response_size)) == SOCKET_ERROR) {
		printf("[%.2f] <-- failed recvfrom with %d\n", (clock() - this->start_time) / 1000.0, WSAGetLastError());
		return FAILED_RECV;
	}

	// update stat thread with receiver info
	ReceiverHeader *rh = (ReceiverHeader*)ans;
	update_receiver_info(rh);

	// RTO calculation
	endRTT = (clock() - this->start_time) / 1000.0;
	elapsed_time = endRTT - beginRTT; // updating to get elapsed time for print after function is complete, in the main
	calculate_RTO(elapsed_time);

	// start stat thread
	this->stat = CreateThread(NULL, 0, statThread, s, 0, NULL);
	// start worker thread
	this->worker = CreateThread(NULL, 0, workerThread, this, 0, NULL);
	
	
	// flow control
	int lastReleased = min(window_size, rh->recvWnd);
	ReleaseSemaphore(empty, lastReleased, NULL);
	 
	// post-flow control
	this->s->receiver_wind_size = rh->recvWnd;
	this->s->set_effective_win_size();
	// printf("Open complete\n");


	return STATUS_OK; // implement error checking for part 5
}



/******************** SEND ********************/
int SenderSocket::Send(char* charBuf, int bytes, int type) {

	// if socket not open... aka if send called without open
	if (sock == INVALID_SOCKET) {
		return NOT_CONNECTED;
	}
	
	////////////////////////// insert packet to semaphore //////////////////////////
	HANDLE arr[] = { eventQuit, empty };
	WaitForMultipleObjects(2, arr, false, INFINITE);

	// no need for mutex as no shared variables are modified
	int slot = this->next_seq % this->W;
	Packet *p = pending_pkts + slot; // pointer to packet struct
	//SenderDataHeader* sdh = new SenderDataHeader();
	//p->buf = (char*)sdh;
	SenderDataHeader* sdh = (SenderDataHeader*)(p->buf);

	// printf("%d\n", bytes + sizeof(SenderDataHeader));

	// set up remaining fields in sdh and p
	p->seq = this->next_seq;
	p->type = type; // data
	p->size = sizeof(SenderDataHeader) + bytes;
	p->txTime = (clock_t)this->s->RTT * CLOCKS_PER_SEC; // does this work?
	sdh->flags = Flags(); // defaulted to 0's with memset
	if (type == 0) // if ACK
		sdh->flags.SYN = 1;
	if (type == 1) // if FIN
		sdh->flags.FIN = 1;
	sdh->seq = this->next_seq;
	// printf("Size senderdata %d\n", sizeof(SenderDataHeader));
	memcpy(sdh + 1, charBuf, bytes); // copy actual contents after header

	next_seq++; // for next packet
	ReleaseSemaphore(full, 1, NULL);	

	// send request (by worker thread!)

	// receive info (by worker thread!)

	// update stat thread with receiver info (in receiveACK function!)

	return STATUS_OK; // implement error checking for part 5
}



/******************** CLOSE ********************/
int SenderSocket::Close(double &elapsed_time) {
	if (sock == INVALID_SOCKET) {
		// sock set in Open
		return NOT_CONNECTED;
	}
	// this->Send(nullptr, 0, 1);
	// for select
	// timeval tp;
	// fd_set fd;
	// set timeout to RTO
	// tp.tv_usec = (this->RTO - int(this->RTO))*1e6; // get the decimals of the RTO
	// tp.tv_sec = int(this->RTO); // get the full seconds of RTO
	struct sockaddr_in req;
	initialize_sockaddr(req, this->targetHost, this->port);

	////////////////////////// finish threads before sending FIN //////////////////////////
	if ((packet_size = WaitForSingleObject(empty, INFINITE)) == WAIT_FAILED) {// wait for reciever to finish
		printf("WaitForSingleObject error %d\n", WSAGetLastError());
		exit(-1);
	}

	// semaphore closed
	SetEvent(this->eventQuit);
	CloseHandle(worker); // join worker thread

	// sets the stat thread to complete so that it knows it is done printing
	SetEvent(this->s->isDone); // trigger waitforoneobject
	CloseHandle(stat); // join

	// testing Ngyuen's approach
	socketReceiveReady = WSACreateEvent();

	// reinding to socket (select)
	if (WSAEventSelect(sock, socketReceiveReady, FD_READ) == SOCKET_ERROR) {
		printf("socket error in WSAEventSelect %d\n", WSAGetLastError());
		exit(-1);
	}
	

	// send request for handshake
	SenderSynHeader sh{};
	SenderDataHeader sender_data{};
	sender_data.flags = Flags(0, 0, 1); // FIN is 1, rest are 0
	sender_data.seq = this->next_seq; //
	sh.sdh = sender_data;
	sh.lp = *(this->link_prop);

	elapsed_time = (clock() - begin_transfer) / 1000.0;

	for (int i = 1; i <=6; ++i) {
		// tried too many times
		if (i == 6) return TIMEOUT;

		////////////////////////// send request to the server //////////////////////////
		if ((packet_size = sendto(sock, (char*)&sh, sizeof(SenderSynHeader), 0, (struct sockaddr*)&req, sizeof(req))) == SOCKET_ERROR) {
			printf("[%.2f] --> failed sendto with %d\n", (clock() - this->start_time) / 1000.0, WSAGetLastError());
			return FAILED_SEND;
		}

		beginRTT = (clock() - this->start_time) / 1000.0; // elapsed open is used for elapsed time if SYN and SYN-ACK work
		// printf("[%.2f] --> FIN %d (attempt %d of %d, RTO %.3f)\n", beginRTT, this->next_seq, i, 5, this->RTO);

		// return of the handshake
		
		////////////////////////// get ready to recieve response and check for timeout //////////////////////////
		if ((packet_size = WaitForSingleObject(socketReceiveReady, this->RTO * 1e3)) == WAIT_FAILED) {
			printf("WaitForSingleObject error %d\n", WSAGetLastError());
			exit(-1);
		}
		else if (packet_size == WAIT_OBJECT_0) {
			break;
		}
		printf("%X\n", packet_size);
	}
	// printf("%d\n", sizeof(SenderSynHeader));

	////////////////////////// reading received data //////////////////////////
	// initializations for receiving
	char ans[MAX_PKT_SIZE];
	struct sockaddr_in response;
	int response_size = sizeof(response);

	// in theory select told us that sock is ready to recvfrom, so no need to reattempt
	if ((packet_size = recvfrom(sock, ans, MAX_PKT_SIZE, 0, (struct sockaddr*)&response, &response_size)) == SOCKET_ERROR) {
		printf("[%.2f] <-- failed recvfrom with %d\n", (clock() - this->start_time) / 1000.0, WSAGetLastError());
		return FAILED_RECV;
	}
	endRTT = (clock() - this->start_time) / 1000.0;
	calculate_RTO((endRTT - beginRTT)/1000.0);
	update_receiver_info((ReceiverHeader*)ans);

	printf("[%.2f] <-- FIN-ACK %d window %X\n", endRTT, *(this->s->next_seq), this->s->receiver_wind_size);


	// close socket to end communication... if already closed or not open, error will yield
	if (closesocket(sock) == SOCKET_ERROR) {
		// should not even get here
		printf("[%.2f] --> failed to close socket with %d\n", (clock() - start_time) / 1000.0, WSAGetLastError()); // check if this is how they want it
		return NOT_CONNECTED;
	}
	sock = INVALID_SOCKET;
	return STATUS_OK; // implement error checking for part 5
}




////////////////////////////////// H E L P E R   F U N C T I O N S //////////////////////////////////

//////////////////// worker thread functions //////////////////////////
void SenderSocket::runWorker(void)
{
	int old_wind_base = this->s->sender_wind_base;
	bool retx = false;
	DWORD timeout;
	HANDLE events[] = { socketReceiveReady, full };
	while (true)
	{
		// set timeout
		if (this->s->sender_wind_base + this->s->sender_wind_size <= nextToSend) // if worker and sender dont catch up to each other // pending packets
			timeout = this->RTO * 1e3; // also if the sender window + size (end of wind) is not greater than nextToSend (still inside of window)
		else
			timeout = INFINITE;

		// printf("timeout is %d\n", timeout);
		// wait to see if times out or not
		int ret = WaitForMultipleObjects(2, events, false, timeout);
		// ret == array index of the object that satisfied the wait
		// printf("ret is %d\n", ret);
		switch (ret)
		{
		case WAIT_TIMEOUT:  // if break occurs, retx buffer here
			// printf("timeout\n");
			this->s->timeout_counter++;
			retx = true;
			// retx this->pending_pkts[this->s->sender_wind_base % this->W];
			send_packet(this->s->sender_wind_base);
			break;
		case 0:	// packet is ready in the socket - get the ACK (socketReceiveReady)
			// printf("receiveACK\n");
			retx = receive_ACK(); // move senderBase; fast retx if 3 same ACK
			break;
		case 1:	// packet is ready in the sender - send the packet (full)
			// printf("packet send\n");
			retx = false;
			send_packet(nextToSend++);
			break;
			// 
		default:// errored out - WAIT_FAILED D:
			printf("[%.2f] --> WaitForMultipleObjects failed in worker thread.\nExiting...\n", (clock() - start_time) / 1000.0);
			exit(-1);
		}
		if (nextToSend == this->s->sender_wind_base || retx // first packet of window or just did a retx(timeout / 3 - dup ACK
			|| this->s->sender_wind_base != old_wind_base) { // senderBase moved forward
			// printf("timer reset\n");
			old_wind_base = this->s->sender_wind_base; // in case the base moved forward, for later checks
			this->calculate_RTO((endRTT - beginRTT)/1000.0);
		}
	}
}

DWORD WINAPI workerThread(LPVOID pParam) {
	SenderSocket *ss = (SenderSocket *)pParam;
	ss->runWorker();
	return 0;
}



//////////////////// stat thread function //////////////////////////
DWORD WINAPI statThread(LPVOID pParam)
{
	StatData* stat = (StatData*)pParam;
	while (WaitForSingleObject(stat->isDone, 2000) == WAIT_TIMEOUT) {
		int time = (clock() - stat->start_time) / 1000;
		printf("[%2d] B %6d ( %3.1f MB) N %6d T %d F %d W %d S %.3f Mbps RTT %.3f\n", time, stat->sender_wind_base, stat->data_ACKed, *(stat->next_seq),
			stat->timeout_counter, stat->fast_retx_counter, stat->effective_wind_size, stat->get_goodput(), stat->RTT);
	}
	return 0;
}



//////////////////// send packet function //////////////////// 
bool SenderSocket::send_packet(int index) { // for Packet type only! // doesn't need to be a boolean
	if (index == 0) { // if this is the first packet - TODO: if we use buf with SYN, change to 1
		begin_transfer = clock();
	}
	struct sockaddr_in req; // can this be shared across all sends - yes TODO
	initialize_sockaddr(req, this->targetHost, this->port);

	if (sendto(sock, (char*)(this->pending_pkts[index % this->s->sender_wind_size].buf), this->pending_pkts[index % this->s->sender_wind_size].size, 0, (struct sockaddr*)&req, sizeof(req)) == SOCKET_ERROR) {
		printf("Failed in sendto\n");
		exit(-1);
	}
	beginRTT = clock();
	return true;
}

//////////////////// send packet function //////////////////// 
bool SenderSocket::receive_ACK() {	
//	ResetEvent(this->socketReceiveReady); // reset socket to not ready

	// initializations for receiving
	char ans[MAX_PKT_SIZE];
	struct sockaddr_in response;
	int response_size = sizeof(response);

	// in theory select told us that sock is ready to recvfrom, so no need to reattempt
	if ((packet_size = recvfrom(sock, ans, MAX_PKT_SIZE, 0, (struct sockaddr*)&response, &response_size)) == SOCKET_ERROR) {
		printf("[%.2f] <-- failed recvfrom with %d\n", (clock() - this->start_time) / 1000.0, WSAGetLastError());
		exit(-1);
	}
	endRTT = clock();

	// update stat thread with receiver info
	ReceiverHeader *rh = (ReceiverHeader*)ans;
	// printf("rh->ackSeq %d sender_base %d\t", rh->ackSeq, this->s->sender_wind_base);
	// check if same ack
	if (rh->ackSeq == this->s->sender_wind_base+1 && this->sameack_count != 0) { // if window base is same as ACK value (fast retx counter)
		this->sameack_count++;
		if (this->sameack_count >= 3) {
			send_packet(rh->ackSeq); // fast rext
			this->sameack_count = 0;
			this->s->fast_retx_counter++;
			this->retx_count++; // STILL DOING DFJDSFKASHFKLDSAFHDKSAL
			return true; // tells the user to fast rext
		}
	}
	else { // if base is moved
		ReleaseSemaphore(empty, rh->ackSeq - this->s->sender_wind_base, NULL);
		this->retx_count = 0;
		update_receiver_info(rh); // moves the base
	}
	
	return false;
}



//////////////////// misc functions ////////////////////
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
				printf("[%.2f] --> target %s is invalid\n", (clock() - start_time) / 1000.0, host);
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
	this->s->sender_wind_base = rh->ackSeq;
	this->s->data_ACKed += packet_size * 1e-6; // bytes to megabytes
}

void SenderSocket::calculate_RTO(double sample_RTT) {
	double alpha = 0.125, beta = 0.25;
	this->s->RTT = (1 - alpha) * this->s->RTT + alpha * sample_RTT;
	devRTT = (1 - beta) * devRTT + beta * fabs(sample_RTT - this->s->RTT);
	RTO = this->s->RTT + 4 * max(devRTT, 0.010);
}

double SenderSocket::calcualte_ideal_rate() {
	return this->s->sender_wind_size * MAX_PKT_SIZE * 8 / (1000.0 * this->s->RTT); // ideal_rate = (windowSize*MAX_PKT_SIZE*8/1000)/estRTT
}