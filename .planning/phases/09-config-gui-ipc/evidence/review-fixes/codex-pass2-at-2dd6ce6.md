Live appearance changes can leave fullscreen clients with stale decorations, and file reloads do not refresh the values used by Reset. Both produce user-visible state inconsistent with the effective configuration.

Full review comments:

- [P2] Rebuild fullscreen decorations after live changes — src/Client.cpp:1615-1618
  When frame thickness, tab font, or palette changes while a client is fullscreen, these early returns leave its unmapped frame components untouched. `Border::restoreFromFullscreen()` only resizes the parent/client and remaps the existing tab, button, and resize windows; it does not rerun their geometry, shape, or background updates. Exiting fullscreen therefore restores stale decoration despite the effective configuration reporting the new values, so defer and apply the refresh on fullscreen exit.

- [P2] Refresh lower-layer values after rereading files — apps/wm2-config/main.cpp:422-423
  When a system configuration file changes while this window is open, assigning the newly read `m_layers` does not update each form field's `belowUser` value or `m_menuBelowUser`; the subsequent effective-value requests update only `effective`. A later Reset therefore displays and live-applies the old lower-layer value even though removing the user override now reveals the newly loaded system value. Refresh the model's lower-layer snapshots while preserving dirty edits.
