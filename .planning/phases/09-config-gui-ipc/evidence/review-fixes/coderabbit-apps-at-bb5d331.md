# CodeRabbit CLI, apps chunk, at bb5d331 (base 0fec5db)

Run 2026-09-07 04:04Z under grant GR-2026-0907-0009: `cr review --agent -t committed --base-commit 0fec5db --dir apps`. All three findings verified against the code and fixed in e12c0d2, 0a94290 and d51143f.

## [minor] apps/wm2-config/BehaviourPage.cpp

In @apps/wm2-config/BehaviourPage.cpp around lines 236 - 237, Update the fallback upper bound in the range setup used by renderRow so a null configKeySpecFor(key) produces distinct usable lo and hi values, matching the established fallback bounds in AppearancePage::addThicknessRow; preserve the spec-provided minValue and maxValue when the specification exists.

## [minor] apps/wm2-config/FormState.cpp

In @apps/wm2-config/FormState.cpp around lines 289 - 306, Update FormState’s layered-read state so markSaved can assign sourceDetail consistently with source: retain the system and user file paths when seeding fields in seedFromLayers, then use those retained paths in both resetRequested and edit branches of markSaved when source changes. Ensure reset uses the system-layer detail and edits use the user-file detail, preserving the existing source assignments.

## [minor] apps/wm2-config/AppearancePage.cpp

In @apps/wm2-config/AppearancePage.cpp around lines 465 - 472, Preserve the chooser control’s existing tooltip for rows without a raw field by adding a base-tooltip member to Row, storing the thickness tooltip in addThicknessRow when it is set, and updating renderRow to append the origin text to that stored tooltip instead of replacing it.

Reviewed files: apps/wm2-config/AppearancePage.cpp, apps/wm2-config/AppearancePage.h, apps/wm2-config/BehaviourPage.cpp, apps/wm2-config/BehaviourPage.h, apps/wm2-config/ConnectionState.h, apps/wm2-config/FormState.cpp, apps/wm2-config/FormState.h, apps/wm2-config/MenuModel.h, apps/wm2-config/MenuPage.cpp, apps/wm2-config/MenuPage.h, apps/wm2-config/ProtocolClient.cpp, apps/wm2-config/ProtocolClient.h, apps/wm2-config/main.cpp, apps/wm2-ctl/main.cpp. Findings: 3.
