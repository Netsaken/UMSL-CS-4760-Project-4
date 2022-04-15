How to Run:
1. Navigate to project folder
2. Run "make"
3. Invoke program using "./oss"
4. A "LOGFile.txt" file should be created to show output

Git Repository: https://github.com/Netsaken/UMSL-CS-4760-Project-4

Problems:
- Just about everything ran into a problem. This project remains unfinished.
- The "system clock" waits until all children have been removed from the queue before exiting,
so it may not stop exactly at the specified time. The maximum program time has been
set to 3 seconds by default, with a process production timer of 0.2 seconds.

Not Implemented:
- Multi-Level Feedback Queue, along with accompanying Priority system and aging algorithm. This is only the round-robin queue.
- User process time calculations and output
- Real-time processes
- Maximum number of processes before exiting