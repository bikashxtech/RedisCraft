
#  RedisCraft 🚀

![Language](https://img.shields.io/badge/Language-C%2B%2B17-blue.svg)
![Build](https://img.shields.io/badge/Build-CMake%20%7C%20Make-lightgrey.svg)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20WSL2-orange.svg)
![License](https://img.shields.io/badge/License-MIT-green.svg)

**RedisCraft** is a from-scratch implementation of a lightweight, Redis-compatible in-memory database server written in modern C++. This project is an educational endeavor to deeply understand the internals of distributed systems, concurrent data structures, and network programming by building a core subset of the Redis protocol and functionality.

---

## ✨ Core Features

* **⚡ Full RESP Protocol Support**: Fully compliant implementation of the Redis Serialization Protocol (RESP) for robust client-server communication.

* **🗂️ Rich Data Types**:
    * **Strings**: Basic `GET`/`SET` operations with optional millisecond-level expiry.
    * **Lists**: `LPUSH`, `RPUSH`, `LPOP`, `LRANGE`, `LLEN`, and blocking `BLPOP` operations.
    * **Streams**: `XADD`, `XRANGE`, and blocking `XREAD` for handling time-series data.

* **⚙️ Advanced Operations**:
    * **Transactions**: Atomic execution of command blocks using `MULTI` and `EXEC`.
    * **Blocking Commands**: Supports `BLPOP` and `XREAD` with timeouts, perfect for building real-time applications.
    * **Persistence**: RDB-style snapshotting (`SAVE`, `BGSAVE`) for saving and restoring the database state across restarts.

* **🔄 Concurrency**: A thread-safe design using `std::mutex` to handle multiple concurrent clients gracefully.

* **🛠️ Server Management**: Includes `SAVE` and `BGSAVE` commands for flexible persistence management.

---

## 🛠️ Tech Stack

| Component         | Technology                               |
| ----------------- | ---------------------------------------- |
| **Language** | C++17                                    |
| **Networking** | POSIX Sockets, `poll()` for I/O multiplexing |
| **Concurrency** | `std::thread`, `std::mutex`              |
| **Persistence** | Custom RDB-like binary format            |
| **Build System** | CMake / Make                             |

---

## 📦 Getting Started

### Prerequisites

* A Linux/macOS environment (Windows WSL2 should also work).
* A C++17 compliant compiler like GCC (version 9+) or Clang.
* `git` and `make`.

### Build Instructions

1.  **Clone the repository:**
    ```bash
    git clone [https://github.com/bikashxtech/RedisCraft.git](https://github.com/bikashxtech/RedisCraft.git)
    cd RedisCraft
    ```

2.  **Build the project:**
    ```bash
    make
    ```
    This command compiles the source and generates two binaries in the root directory: `Server` and `client`.

---

## 🚀 Usage

### 1. Starting the Server

Launch the server on the default port (6379):
```bash
./Server

The server will load any existing dump.rdb file and begin listening for connections.
Server Configuration
You can configure server settings by modifying constants in src/storage.cpp before building:
 * rdb_filename: Path for the persistence file (default: "dump.rdb").
 * rdb_save_interval: Interval for automatic background saves in seconds (default: 60).
2. Using the Built-in Client
The project includes a CLI client for easy interaction.
 * Connect to the local server:
   ./client

 * Connect to a specific host and port:
   ./client -h 127.0.0.1 -p 6379

Example Session:
$ ./client
127.0.0.1:6379> SET mykey "Hello RedisCraft"
+OK
127.0.0.1:6379> GET mykey
"Hello RedisCraft"
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

3. Using redis-cli or Other Clients
Since RedisCraft speaks RESP, you can use standard Redis clients to connect.
redis-cli -p 6379

127.0.0.1:6379> PING
PONG
127.0.0.1:6379> ECHO "Hello from redis-cli"
"Hello from redis-cli"

📖 Supported Commands
| Command | Description | Example |
|---|---|---|
| PING | Check if the server is alive | PING |
| ECHO | Echo back the given message | ECHO "Hello" |
| SET | Set a string value, with optional expiry | SET key value PX 10000 |
| GET | Get the value of a string key | GET key |
| INCR | Increment an integer value by one | INCR counter |
| RPUSH | Append one or more values to a list | RPUSH mylist A B |
| LPUSH | Prepend one or more values to a list | LPUSH mylist first |
| LPOP | Remove and return the first element(s) of a list | LPOP mylist 2 |
| LRANGE | Get a range of elements from a list | LRANGE mylist 0 -1 |
| LLEN | Get the length of a list | LLEN mylist |
| BLPOP | Block until an element can be popped from a list | BLPOP mylist 5.0 |
| XADD | Add a new entry to a stream | XADD mystream * name John |
| XRANGE | Get a range of entries from a stream | XRANGE mystream - + |
| XREAD | Read from one or more streams, optionally blocking | XREAD BLOCK 5000 STREAMS mystream 0-0 |
| MULTI | Start a transaction block | MULTI |
| EXEC | Execute all commands in a transaction | EXEC |
| TYPE | Determine the type of a value stored at a key | TYPE mykey |
| SAVE | Perform a synchronous save to disk | SAVE |
| BGSAVE | Perform an asynchronous (background) save to disk | BGSAVE |
🗂️ Project Structure
.
├── Server.cpp              # Main server application and event loop
├── client.cpp              # Command-line client for testing
├── src/
│   ├── commands.cpp/.hpp   # Implementation of all Redis commands
│   ├── parser.cpp/.hpp     # RESP protocol parsing and serialization
│   ├── storage.cpp/.hpp    # Data storage structures and persistence logic
│   ├── rdb.cpp/.hpp        # RDB file format encoding/decoding
│   └── StreamHandler.cpp/.hpp # Stream data type specific logic
├── .gitignore
├── CMakeLists.txt
├── Makefile                # Build configuration
└── README.md

🧪 Testing Persistence
The server automatically saves to dump.rdb. You can test this functionality as follows:
 * Test Basic Persistence (SAVE)
   # Start server in background
./Server &

# Set a key and save synchronously
./client -c "SET persistent_key 'This will survive a restart'"
./client -c "SAVE"

# Stop the server
killall Server

# Restart the server and check if the data exists
./Server &
./client -c "GET persistent_key" # Should return "This will survive a restart"

 * Test Background Save (BGSAVE)
   # Run the background save command
./client -c "BGSAVE"

# Check server logs for "Background saving started" and "Background saving completed"

⚠️ Limitations & Disclaimer
This is an educational project and is not intended for production use. Please be aware of the following limitations:
 * Persistence: The RDB implementation is simplified. CRC checksum validation is a placeholder.
 * Security: No authentication, authorization, or transport-level encryption.
 * Scalability: Uses a single global lock for data access, which can be a bottleneck under high concurrent load.
 * Compatibility: Supports a core subset of commands but may not be 100% compatible with all Redis options and edge cases.
 * Memory Management: No support for data eviction policies like maxmemory.
Do not use this project to store important or sensitive data.
🎯 Future Enhancements
Potential areas for future development and contributions:
 * [ ] Append-Only File (AOF) persistence for better durability.
 * [ ] Replication with a leader-follower setup.
 * [ ] More Data Types: Hashes, Sets, and Sorted Sets.
 * [ ] Lua Scripting support.
 * [ ] Improved Concurrency with more granular locking.
 * [ ] Async I/O (e.g., using epoll or io_uring) for better connection handling.
🤝 Contributing
Contributions are welcome! If you have suggestions or want to add features, please feel free to open an issue or submit a pull request.
👨‍💻 Developer
 * Bikash Kumar Giri - bikashxtech
Created as a deep dive into systems programming and the internals of modern databases.


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
