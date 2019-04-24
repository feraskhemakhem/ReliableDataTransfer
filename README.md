# ReliableDataTransfer

This project uses UDP to model TCP. TCP features include:
* SYN and FIN packets for handshakes
* Receiving acknowledging packets for the recevier (ACKs)
* Fast retransmission after 3 sequential ACKs at the sender base
* Timeouts using Round Trip Timeouts (RTO) recomputed after every ACK
* Support for sender and receiver windows of varying size
* Checksum calculations and verification

In addition, a stat thread updates the user on the status of the transfer every 2 seconds. This includes the window base, the total amount of data reliably transferred, the current packet being processed, the total number of timeouts, fast retransmits, the effective window size [aka min(sender_window_size, receiver_window_size)], rate of goodput per second, and the current RTT.

7 arguments are accepted. They are, in this order:

1. Hostname or IP address of destination
2. The power of 2 for which checksums will be calculated (e.g. 15 makes 2^15 checkums to be sent). Recommended values are between 15 and 30.
3. Sender window size (positive integer)
4. Initial RTT. This will be recalculated with RTO every for unretransmitted ACK (positive value)
5. Loss rate from sender to receiver (between 0 and 1)
6. Loss rate from receiver to sender (between 0 and 1)
7. Bottleneck link speed (positive value)
