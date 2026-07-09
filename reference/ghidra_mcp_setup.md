# Ghidra MCP — Claude Code setup & troubleshooting

## What's configured (working)
- Bridge: `/home/cahb/Apps/Ghidra/MCP/GhidraMCP/bridge_mcp_ghidra.py` (GhidraMCP 1.4, REST-style).
- Dedicated venv (no global installs): `/home/cahb/Apps/Ghidra/MCP/GhidraMCP/.venv`
  with `requests` + `mcp` (PEP 723 deps from the script header).
- Registered as **project-scoped stdio MCP server** in `./.mcp.json`:
  ```
  python(.venv) bridge_mcp_ghidra.py --ghidra-server http://127.0.0.1:8080/
  ```
- Needs **approval**: restart Claude Code / run `claude`, approve `ghidra`, then
  `mcp__ghidra__*` tools load. (Can't hot-load into a running session.)

## Verified end-to-end
Piping a real MCP handshake + `tools/call list_methods` through the bridge:
the bridge handshakes fine and routes the call — proving the Claude/bridge side
works. It returned `Error 405` **from the Ghidra HTTP backend**.

## The blocker (Ghidra side, NOT Claude side)
- `:8080` and `:8081` return an identical generic **405 "HTTP method not allowed"**
  for every path/method (even nonexistent ones). GhidraMCP would 404 unknown
  paths, so that listener is **some other service** (invisible to `/proc`/`lsof`
  → likely a container / reverse proxy).
- Broad scan `8080–8095`, `13100/13101`: **no** port serves GhidraMCP's REST
  endpoints. So the plugin's HTTP server isn't up anywhere reachable — most
  likely it failed to bind `:8080` because that other service already holds it.

## Fix
1. Check Ghidra console for the GhidraMCP startup / bind-error line.
2. Move GhidraMCP to a free port: CodeBrowser → Edit → Tool Options → GhidraMCP →
   set e.g. **8089**, restart the server. (Or free 8080/8081.)
3. Repoint the bridge:
   ```sh
   claude mcp remove ghidra --scope project
   claude mcp add ghidra --scope project -- \
     /home/cahb/Apps/Ghidra/MCP/GhidraMCP/.venv/bin/python \
     /home/cahb/Apps/Ghidra/MCP/GhidraMCP/bridge_mcp_ghidra.py \
     --ghidra-server http://127.0.0.1:<PORT>/
   ```
4. Restart Claude Code, approve, done.

## Manual test command (no Claude restart needed)
```sh
B=/home/cahb/Apps/Ghidra/MCP/GhidraMCP/.venv/bin/python
S=/home/cahb/Apps/Ghidra/MCP/GhidraMCP/bridge_mcp_ghidra.py
printf '%s\n' \
 '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"t","version":"0"}}}' \
 '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
 '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"list_methods","arguments":{"offset":0,"limit":5}}}' \
 | "$B" "$S" --ghidra-server http://127.0.0.1:8080/   # expect a function list, not "Error 405"
```
