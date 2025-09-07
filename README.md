RedisCraft In-Memory Database Server

A from-scratch implementation of a lightweight, Redis-compatible in-memory database server written in C++. This project is an educational endeavor to deeply understand the internals of distributed systems, concurrent data structures, and network programming by building a core subset of the Redis protocol and functionality.
✨ Features

    Full RESP (Redis Serialization Protocol) Support: Fully compliant client-server communication.

    Rich Data Types:

        Strings: Basic GET/SET operations with optional millisecond-level expiry.

        Lists: LPUSH, RPUSH, LPOP, LRANGE, LLEN, and BLPOP operations.

        Streams: XADD, XRANGE, and blocking XREAD for time-series data.

    Advanced Operations:

        Transactions: Support for MULTI/EXEC command blocks.

        Blocking Commands: BLPOP and XREAD with timeout for building real-time applications.

        Persistence: RDB-style snapshotting for saving and restoring the database state.

    Concurrency: Thread-safe design using mutexes to handle multiple concurrent clients.

    Server Management: SAVE and BGSAVE commands for persistence management.

🛠️ Tech Stack

    Language: C++17

    Networking: POSIX Sockets, poll()

    Concurrency: std::mutex, std::thread

    Persistence: Custom RDB-like binary format

    Build System: CMake or Make (depending on your setup)

📦 Installation & Build
Prerequisites

    A Linux/macOS environment (Windows WSL2 should work)

    GCC/G++ (version 9 or higher) or Clang

    GNU Make

Build Instructions

    Clone the repository:
    bash

git clone <https://github.com/bikashxtech/RedisCraft>
cd redis-cpp-server

Build the project:
bash

make

    This will compile the source files and generate two binaries: Server and client.

🚀 Usage
1. Starting the Server

Launch the server on the default port (6379):
bash

./Server

You should see output indicating the server is loading any existing RDB file and is now listening for connections.

Server Options:
The server currently uses default settings. You can configure them by modifying these constants in storage.cpp before building:

    rdb_filename: Path for the persistence file (default: "dump.rdb").

    rdb_save_interval: Interval for automatic background saves in seconds (default: 60).

2. Using the Command-Line Client

The project includes a built-in CLI client for interacting with the server.

Connect to the local server:
bash

./client

Connect to a specific host and port:
bash

./client -h 127.0.0.1 -p 6379

You will be greeted with a prompt where you can type Redis commands.
3. Example Session
bash

# Start the client
$ ./client
127.0.0.1:6379> SET mykey "Hello World"
+OK
127.0.0.1:6379> GET mykey
"Hello World"
127.0.0.1:6379> RPUSH mylist A B C
:3
127.0.0.1:6379> LRANGE mylist 0 -1
1) "A"
2) "B"
3) "C"
127.0.0.1:6379> XADD mystream * field1 value1
"1691234567890-0"
127.0.0.1:6379> SAVE
+OK
127.0.0.1:6379> QUIT

4. Using with redis-cli or Other Clients

You can use the official redis-cli or any Redis client library in other languages to connect to your server, as it speaks the standard RESP protocol.
bash

redis-cli -p 6379
127.0.0.1:6379> PING
PONG

📖 Supported Commands
Command	Description	Example
PING	Check if the server is alive	PING
ECHO <message>	Echo back the message	ECHO "Hello"
SET <key> <value> [PX milliseconds]	Set a string value	SET key value PX 10000
GET <key>	Get a string value	GET key
INCR <key>	Increment an integer value	INCR counter
RPUSH <key> <value> [value ...]	Append values to a list	RPUSH mylist A B
LPUSH <key> <value> [value ...]	Prepend values to a list	LPUSH mylist first
LPOP <key> [count]	Remove and get the first element(s)	LPOP mylist 2
LRANGE <key> <start> <stop>	Get a range of elements	LRANGE mylist 0 -1
LLEN <key>	Get the length of a list	LLEN mylist
BLPOP <key> <timeout>	Block until an element is popped	BLPOP mylist 5.0
XADD <key> <ID> <field> <value> [...]	Add an entry to a stream	XADD mystream * name John
XRANGE <key> <start> <end>	Get a range of stream entries	XRANGE mystream - +
XREAD [BLOCK ms] STREAMS <key> <ID>	Read from streams	XREAD BLOCK 5000 STREAMS mystream 0-0
MULTI	Start a transaction	MULTI
EXEC	Execute all commands in a transaction	EXEC
TYPE <key>	Determine the type of a value	TYPE mykey
SAVE	Perform a synchronous save to disk	SAVE
BGSAVE	Perform an asynchronous save to disk	BGSAVE
🗂️ Project Structure
text

.
├── Server.cpp              # Main server application and event loop
├── client.cpp              # Command-line client for testing
├── commands.cpp / .hpp     # Implementation of all Redis commands
├── parser.cpp / .hpp       # RESP protocol parsing and serialization
├── storage.cpp / .hpp      # Data storage structures and persistence logic
├── rdb.cpp / .hpp          # RDB file format encoding/decoding
├── StreamHandler.cpp / .hpp # Stream data type specific logic
└── Makefile                # Build configuration

🧪 Testing Persistence

The server automatically saves and loads data from dump.rdb.

    Test Basic Persistence:
    bash

# Start server, set some data, and SAVE
./Server &
./client -c "SET persistent_key 'This will survive a restart'"
./client -c "SAVE"
killall Server

# Restart the server and check the data
./Server &
./client -c "GET persistent_key" # Should return your value

Test Background Save:
bash

./client -c "BGSAVE"
# Check server logs for "Background saving started/completed"

⚠️ Limitations & Disclaimer

This is an educational project, not a production-ready database. Please be aware of its limitations:

    Persistence: The RDB implementation is a simplified version. CRC checksum validation is a placeholder.

    Security: There is no authentication, authorization, or encryption.

    Scalability: Data is stored in memory with a single global lock, which will become a bottleneck under very high load.

    Compatibility: While it supports a core set of commands, it is not 100% compatible with all Redis options and edge cases.

    Data Eviction: No support for Redis's maxmemory policies.

Do not use this to store important or sensitive data.
🎯 Future Enhanceances

Potential areas for future development:

    Append-Only File (AOF) persistence for better durability.

    REPLICATION and leader-follower setup.

    Support for more data types (Sets, Sorted Sets, Hashes).

    Lua scripting support.

    Improved connection handling with async I/O.

👨‍💻 Developer

Created by Bikash Kumar Giri as a deep dive into systems programming.

Feel free to contribute by submitting issues or pull requests!
