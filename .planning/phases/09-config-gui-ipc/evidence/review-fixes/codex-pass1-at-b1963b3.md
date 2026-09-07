The patch introduces reachable hangs, valid-protocol connection failures, and a failed-reload path that partially commits visual state. The protocol decoder also accepts messages outside its declared type-specific schema.

Full review comments:

- [P1] Stage palette swaps until font validation succeeds — src/Manager.cpp:2129-2131
  When a reload changes both colours and a font whose face cannot be opened, these calls commit the new border/menu palettes before `openTabFace()` or `openMenuFace()` can fail. The function then returns false with the old `m_config` and without the repaint path, leaving drawing resources and reported configuration inconsistent; this is reproducible with the existing forced font-failure levers. Allocate all potentially failing colour and font resources first, then commit them only after every validation succeeds.

- [P1] Make wm2-ctl's socket non-blocking — apps/wm2-ctl/main.cpp:164-164
  When the listener backlog is full, the window manager is wedged, or a peer completes the handshake and stops reading, this blocking descriptor can make `connect()` or `send()` wait indefinitely. The deadline logic in `send()` only runs after `EAGAIN`, which a blocking socket does not return while waiting, so a shell script using `wm2-ctl` can hang despite the documented 15-second exchange bound. Create a non-blocking socket and use bounded `poll()` handling for connection and writes.

- [P2] Enforce the size limit per frame — src/SocketServer.cpp:650-654
  If a previous read left a partial legal frame and the next `recv()` contains its remainder plus additional complete requests, the aggregate buffer can exceed 4096 bytes even though every newline-delimited frame is within the protocol limit. This branch then reports "message too long" and closes a valid pipelined connection based solely on arbitrary Unix-stream packetization. Append and extract complete lines before applying the limit to the remaining incomplete frame.

- [P2] Validate members against the decoded message type — include/ConfigProtocol.h:671-672
  The decoder returns `Ok` after recognizing member names globally, without checking that the selected type has exactly its required members or rejecting duplicate non-type members. Consequently inputs such as `{"type":"reload","protocol":2}` execute a reload while silently ignoring a qualifier, and repeated `key` members use the last value; this undermines the stated fail-closed behavior for protocol extensions. Track member presence and duplication, then validate the allowed and required set for each message type before returning `Ok`.
