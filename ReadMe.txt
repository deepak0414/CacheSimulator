I did this project to help out some friends with their studies.
Making this public so that others can use this as well.

CacheSimulator is a simple project to simulate cache algorithm & data structures used by processors
to speedup the computations. This project only aims at calculating the cache misses and cache hits
in particular cache settings. 

By default cache lines are 16K and data size is 64 bytes and associativity is 8.
If one need to modify the associativity and cache line sizes, they can do so by modifying the appropriate macro definitions.
It implements MESI protocol for simulating cache accesses.

Before reading the work, you must understand the MESI protocol

Build:
Code is build using visual studio, so you would need microsoft windows for this.
However code is pretty simple and will be fairly easy to port on linux and compile using gcc.

Input:
Input is a trace file specifying series of memory accesses on shared bus in following format


Input of the program is in following format:--

"Access" "Address"
0 - Read from data cache
1 - Write request from data cache
2 - Read from instruction cache (which is same here because project hasn't implemented seperate instruction cache)
3 - Snoop invalidate command (This should be coming from other processor but for simulation purpose it is the input in trace file)
4 - Snooped read request
5 - Snooped write request
6 - Snooped read with intent to modify
8 - clear the cache and reset all state
9 - print the content and state of each valid cache line (allow next activity in trace file)

Example

2 609ed6 
0 20119e98 
2 507ed9 
1 3021ed8c 


There is already a sample input file (testmod.txt) provided.

Usage:

cachesimulator.exe < testmod.txt

