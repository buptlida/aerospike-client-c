# Visual Studio Environment for Aerospike C Client.

## Prerequisites

- Windows 7 or greater.
- Visual Studio 2015 or greater.

The Aerospike C client library is dependent on several third party libraries.
Since many of these libraries are difficult to build on Windows,
pre-built third party libraries are provided in [Libraries](lib).
Their corresponding include files are located in [Include](include).

### Git Submodules

This project uses git submodules, so you will need to initialize and update 
submodules before building this project.

	$ cd ..
	$ git submodule update --init

## Build Instructions

Various build targets are supported depending on the use of async database
calls and the chosen event framework.  The C client supports 64 bit targets only.

- Double click aerospike.sln
- Click one of the following solution configuration names:

	Configuration    | Usage
	---------------- | -----
	Debug            | Use synchronous methods only.
	Debug libevent   | Use libevent for async event framework.
	Debug libuv      | Use libuv for async event framework.
	Release          | Use synchronous methods only.
	Release libevent | Use libevent for async event framework.
	Release libuv    | Use libuv for async event framework (Recommended).

- Click Build -> Build Solution
