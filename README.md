# w26server: Distributed File Query System

A multi-process client-server application written in C for Linux, using socket. The server searches for the file/s in its file directory rooted at its ~ and returns the file/files requested to the client (or an appropriate message otherwise). Multiple clients can connect to the server from different machines and can request file/s as per the commands.

---

## Overview

Clients connect to a central server (`w26server`) and request files or directory information. The server processes commands from its home directory tree and returns results. To handle load, the server transparently routes connections to two mirror servers (`mirror1`, `mirror2`), the client is unaware of this redirection.

Each client connection is handled in a dedicated forked child process, so multiple clients can be served concurrently.

---

## Architecture

| Program | Role | Port |
|---|---|---|
| `w26server` | Main server: Accepts all clients, routes to itself or mirrors | `50000` |
| `mirror1` | Mirror server: Handles identical command set | `50001` |
| `mirror2` | Mirror server: Handles identical command set | `50002` |
| `client` | Interactive client: Always connects to `w26server:50000` | — |

### Connection Routing

Clients always connect to `w26server`. The server routes connections in this order:

| Connection # | Handled by |
|---|---|
| 1, 2 | w26server |
| 3, 4 | mirror1 |
| 5, 6 | mirror2 |
| 7, 8, 9, 10, ... | w26server, mirror1, mirror2 (alternating) |

When a connection is routed to a mirror, `w26server` forks a child that acts as a transparent relay and all data flows between the client and the mirror through the relay socket.

---

## Features

- **Concurrent clients** via `fork()`, each client gets its own child process
- **Transparent mirroring** where clients always connect to one address and routing is internal
- **Command validation** in which the client validates command syntax before sending
- **File packaging** where multi-file results are bundled into a `.tar.gz` archive and sent over the socket

---

## Supported Commands

| Command | Description |
|---|---|
| `dirlist -a` | List all subdirectories under `~` in alphabetical order |
| `dirlist -t` | List all subdirectories under `~` ordered by modified time (oldest first) |
| `fn <filename>` | Find a file by name, returns its size, date, and permissions |
| `fz <size1> <size2>` | Return all files with size between `size1` and `size2` bytes as `fz_temp.tar.gz` |
| `ft <ext1> [ext2] [ext3]` | Return all files matching 1–3 extensions as `ft_temp.tar.gz` |
| `fdb <date>` | Return all files created **on or before** `date` (format: `YYYY-MM-DD`) as `fdb_temp.tar.gz` |
| `fda <date>` | Return all files created **on or after** `date` (format: `YYYY-MM-DD`) as `fda_temp.tar.gz` |
| `quitc` | Disconnect from the server and exit the client |

All received files are saved to `~/project/` on the client machine.

---

## Build

```bash
gcc -o w26server w26server.c
gcc -o mirror1 mirror1.c
gcc -o mirror2 mirror2.c
gcc -o client client.c
```

---

## Run

Start in this order (each in a separate terminal):

```bash
# Terminal 1
./mirror1

# Terminal 2
./mirror2

# Terminal 3
./w26server

# Terminal 4+
./client <server_IP>
```

> **Note:** Create `~/project/` on the client machine before issuing any file-transfer commands:
> ```bash
> mkdir -p ~/project
> ```

---

## Files

```
w26server.c   Main server with routing logic and all command handlers
mirror1.c     Mirror server listening on port 50001
mirror2.c     Mirror server listening on port 50002
client.c      Interactive client shell
```

---

## Note

This application was developed as the final project requirement of the COMP 8567 (Advanced Systems Programming) course at the University of Windsor.
