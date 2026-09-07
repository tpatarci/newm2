# CodeRabbit CLI, src chunk, at bcbc5f2 (base 0fec5db)

Run 2026-09-07 03:01Z under bookkeeper grant GR-2026-0907-0007: `cr review --agent -t committed --base-commit 0fec5db --dir src`. The full diff was refused at connect three times, so the pre-flight is chunked by directory. Findings are data and were verified against the code before any fix.

## [minor] src/Config.cpp

In @src/Config.cpp around lines 784 - 799, Update configMenuEntriesValue() to prevent manual entries containing ';' in any serialized field from being rendered, so the emitted value remains parseable as the same entry list. Skip the invalid entry or refuse rendering consistently with the existing configuration validation behavior.

## [minor] src/Events.cpp

In @src/Events.cpp at line 402, Update the bounded-wait logic around clampPollTimeoutForSocket so poll() returning zero only produces ModalWait::Timeout after the caller’s original deadline has elapsed, not merely after the reduced socket hint. Preserve the existing deadline check at the loop head and unbounded-wait behavior, and avoid relying on the clamped wait value to determine timeout completion.

## [minor] src/Manager.cpp

In @src/Manager.cpp at line 2235, Update the reload handling around the tabFontChanged and frameThickness conditions so clients with both changes use relayoutFrameForFont() rather than only the thickness relayout path; make the thickness branch skip clients when tabFontChanged is true to avoid reshaping the frame twice, while preserving thickness-only handling for other clients.

Reviewed files: src/AppCache.cpp, src/Border.cpp, src/Buttons.cpp, src/Client.cpp, src/Config.cpp, src/ConfigFileWriter.cpp, src/DesktopEntry.cpp, src/Events.cpp, src/Manager.cpp, src/SocketServer.cpp, src/main.cpp. Findings: 3.
