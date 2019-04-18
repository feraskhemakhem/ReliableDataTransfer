/*
 * main.cpp
 * CSCE 463 - Spring 2019
 * Feras Khemakhem
 */

#include "stdafx.h"
#include "sender_socket.h"
#include "checksum.h"

void transfer(char* argv[]) {

	// variables for later
	int status;

	/////////////////////////////// argument utilization ///////////////////////////////

	char *targetHost = argv[1]; // the destination IP/hostname
	int power = atoi(argv[2]); // power of the number of DWORDS
	int senderWindow = atoi(argv[3]); // window size
	Checksum cs{}; // checksum for later!

	printf("Main:\tsender W = %d, RTT %.3f, loss %g / %g, link %d Mbps\n", senderWindow, atof(argv[4]), atof(argv[5]), atof(argv[6]), atoi(argv[7]));
	printf("Main:\tinitializing DWORD array with 2^%d elements... ", power);
	clock_t buffer_timer = clock();
	UINT64 dwordBufSize = (UINT64)1 << power;
	DWORD *dwordBuf = new DWORD[dwordBufSize]; // user-requested buffer
	for (UINT64 i = 0; i < dwordBufSize; i++) { // required initialization for array with unique values
		dwordBuf[i] = i;
		// printf("%d ", dwordBuf[i]);
	}

	printf("done in %d ms\n", clock() - buffer_timer);

	/////////////////////////////// open ///////////////////////////////

	LinkProperties lp;
	lp.RTT = atof(argv[4]);
	lp.speed = 1e6 * atof(argv[7]); // convert to megabits
	lp.pLoss[FORWARD_PATH] = atof(argv[5]);
	lp.pLoss[RETURN_PATH] = atof(argv[6]);
	lp.bufferSize = senderWindow + 3; // window size + max number of retransmissions, which is 3 for Open
	SenderSocket ss; // instance of your class
	if ((status = ss.Open(targetHost, MAGIC_PORT, senderWindow, &lp)) != STATUS_OK) {
		// error handling: print status and quit
		printf("Main:\tconnect failed with status %d\n", status);
		exit(-1);
	}

	printf("Main:\tconnected to %s in %.3f sec, pkt size %d bytes\n", targetHost, ss.get_elapsed_time(), MAX_PKT_SIZE);

	/////////////////////////////// send ///////////////////////////////

	char *charBuf = (char*)dwordBuf; // this buffer goes into socket
	// printf("charBuf is %s\n", charBuf);
	UINT64 byteBufferSize = dwordBufSize << 2; // convert to bytes

	UINT64 off = 0; // current position in buffer
	while (off < byteBufferSize)
	{
		// decide the size of next chunk
		int bytes = min(byteBufferSize - off, MAX_PKT_SIZE - sizeof(SenderDataHeader));
		// send chunk into socket
		if ((status = ss.Send(charBuf + off, bytes)) != STATUS_OK) {
			// error handling: print status and quit
			printf("Main:\t connect failed with status %d\n", status);
			exit(-1);
		}
		off += bytes;
	}

	double elapsed_time; // elapsed time from first send to last ACK non-FIN

	/////////////////////////////// close ///////////////////////////////

	if ((status = ss.Close(elapsed_time)) != STATUS_OK) {
		// error handing: print status and quit
		printf("Main:\t connect failed with status %d\n", status);
		exit(-1);
	}

	DWORD check = cs.CRC32((unsigned char*)charBuf, byteBufferSize);
	printf("Main:\ttransfer finished in %.3f sec, %.2f Kbps, checksum %X\n", elapsed_time, off/(elapsed_time*1e3), check); // elapsed time is between first non-SYN sent and last non-FIN ACK
	printf("Main:\testRTT %.3f, ideal rate %.2f Kbbps\n", ss.get_estRTT(), ss.calcualte_ideal_rate());
		
}

int main(int argc, char* argv[])
{

	/* 
	 * args: 
	 * (1) destination server
	 * (2) power of 2 of buffer size
	 * (3) sender window size
	 * (4) round-trip propogration delay (RTT)
	 * (5) probability of loss in forward path
	 * (6) probability of loss in return path
	 * (6) speed of bottlenecked link (Mbps)
	*/

	WSADATA wsaData;

	//Initialize WinSock; once per program run
	WORD wVersionRequested = MAKEWORD(2, 2);
	if (WSAStartup(wVersionRequested, &wsaData) != 0) {
		printf("WSAStartup error %d\n", WSAGetLastError());
		WSACleanup();
		return 1;
	}
	
	/////////////////////////////// command line error checking ///////////////////////////////
	// parse command-line parameters
	if (argc != 8) { // check for valid number of parameters
		printf("Incorrect number of arguments. Please rerun with seven command-line arguments\n");
		exit(-1);
	}
	
	// error checking
	if (atoi(argv[7]) <= 0 || atoi(argv[7]) > 1e4) { // assuming 1000 Mb is 1 Gb
		// 2.3(a) link speed must be greater than 0 and less than 10 Gbps
		printf("Invalid bottleneck-link speed. Please rerun with speed greater than 0 Gbps and less than 10 Gbps\n");
		exit(-1);
	}
	else if (atoi(argv[4]) < 0 || atoi(argv[4]) > 30) {
		// 2.3(b) RTT must be positive and less than 30 s
		printf("Invalid RTT. Please rerun with positive RTT less than 30s\n");
		exit(-1);
	}
	else if (atof(argv[5]) < 0 || atof(argv[5]) >= 1 || atof(argv[6]) < 0 || atof(argv[6]) >= 1) {
		// 2.3(c) probabilities of loss must bet in [0, 1)
		printf("Invalid loss probability. Please rerun with loss probabilities in [0, 1)\n");
		exit(-1);
	}
	else if (atoi(argv[3]) < 1 || atoi(argv[3]) > 1e6) {
		// 2.3(d) window size / router buffer size must be between 1 and 1M packets
		printf("Invalid window size. Please rerun with a window size between 1 packet and 1M packets\n");
		exit(-1);
	}

	// opening, sending, closing
	transfer(argv);


	WSACleanup();
	return 0;
}