pthrlib is a library for writing small, fast and efficient servers in C. It offers a list of advanced features (see below). This library has been used to write a very tiny and fast web server called rws and a closed source chat server. All functions are documented in manual pages, and example servers are included.

It contains the following features:

* *reactor & pseudothreads*: underpinning the whole library is a lightweight cooperative threading library written on top of a Reactor pattern. Typically you can create of the order of thousands of threads (the number of threads is generally limited by other things like how many file descriptors your C library supports -- assuming each thread is handling one client over one socket). Pseudothreads support thread listings and throw/catch-style exception handling.
* *iolib*: a buffered I/O library written on top of the pseudothread "syscalls".
* *http*: a library for writing HTTP/1.1 RFC-compliant servers.
* *cgi*: a library for writing CGI scripts in C which run inside the server [new in pthrlib 2.0.1].
* *dbi*: a PostgreSQL database interface, modelled on Perl's DBI [new in pthrlib 3.0.8].
* *wait_queue*: synchronize between threads using wait queues (several threads go to sleep on the wait queue until some point later when another thread wakes them all up).
* *mutex, rwlock*: simple mutual exclusion locks and multiple-reader/single-writer locks.
* *listener*: a thread class which listens for connections on a given port and throws off threads to process them.
* *ftpc*: an FTP client library.
