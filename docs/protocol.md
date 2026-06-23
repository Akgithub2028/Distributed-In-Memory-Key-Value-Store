# Protocol and API Specification

## The RESP Protocol

The server uses the standard **RESP (REdis Serialization Protocol)**. This ensures compatibility with existing Redis clients (e.g., `redis-cli`, Python's `redis-py`).

### Supported Data Types
- **Simple Strings:** `+OK\r\n`
- **Errors:** `-Error message\r\n`
- **Integers:** `:1000\r\n`
- **Bulk Strings:** `$5\r\nhello\r\n` (Null: `$-1\r\n`)
- **Arrays:** `*2\r\n$4\r\nECHO\r\n$5\r\nhello\r\n` (Null: `*-1\r\n`)

Clients will always send Arrays of Bulk Strings representing commands.

## Command Set

The following command subset constitutes the core MVP scope. Commands out of this scope will return a standard RESP error indicating the command is unsupported.

### Phase 1: Connection & Smoke Testing
- `PING [message]`: Returns PONG or the message.
- `ECHO message`: Returns the message.
- `QUIT`: Closes the connection.

### Phase 2: Core Key-Value Engine
- `SET key value [EX seconds|PX ms] [NX|XX]`: Stores string value.
- `GET key`: Retrieves string value.
- `DEL key [key ...]`: Deletes keys.
- `EXISTS key [key ...]`: Returns number of existing keys.
- `INCR key`: Increments an integer string.
- `DECR key`: Decrements an integer string.

### Phase 3: TTL
- `EXPIRE key seconds`: Set timeout in seconds.
- `PEXPIRE key ms`: Set timeout in milliseconds.
- `TTL key`: Get remaining TTL in seconds.
- `PTTL key`: Get remaining TTL in milliseconds.
- `PERSIST key`: Remove expiration.

### Later Phases
- `INFO`: Server statistics and metrics.
- `FLUSHDB`: Clear the database.
- `SCAN cursor`: Minimal non-blocking key iteration.
