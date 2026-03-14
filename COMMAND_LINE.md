# Command-Line Arguments

```
zerospades [server_address] [protocol_version] [-h|--help] [-v|--version]
```

All arguments are optional and may be specified in any order. Unknown arguments are silently ignored.

---

## Arguments

### `server_address`

Connect directly to a server on launch, bypassing the main menu.

**Format:** `aos://<host>:<port>`

| Component | Description |
|-----------|-------------|
| `host` | Hostname or IP address of the server |
| `port` | Port number (default: `32887`) |

**Examples:**
```
zerospades aos://example.com:32887
zerospades aos://192.168.1.10:32887
```

---

### `protocol_version`

Set the protocol version used when connecting to a server. Has no effect unless a `server_address` is also provided.

**Accepted values:**

| Protocol | Accepted forms |
|----------|----------------|
| v0.75 (default) | `75`, `.75`, `0.75`, `v=75`, `v=.75`, `v=0.75` |
| v0.76 | `76`, `.76`, `0.76`, `v=76`, `v=.76`, `v=0.76` |

**Examples:**
```
zerospades aos://example.com:32887 0.75
zerospades aos://example.com:32887 v=0.76
```

---

### `-h`, `--help`

Print usage information and exit.

```
zerospades --help
zerospades -h
```

---

### `-v`, `--version`

Print the application version and exit.

```
zerospades --version
zerospades -v
```
