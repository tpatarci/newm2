# CodeRabbit CLI, tests chunk, at e66e612 (base 0fec5db)

Run 2026-09-07 06:12Z under grant GR-2026-0907-0011: `cr review --agent -t committed --base-commit 0fec5db --dir tests`. The one finding is fixed in the commit that follows this archive.

## [minor] tests/support/WmFixture.h

In @tests/support/WmFixture.h at line 996, Update the calculation assigning out to multiply resident and page in a long long intermediate before dividing by 1024, preserving fractional-page contributions and avoiding 32-bit long overflow.

Reviewed files: tests/support/WmFixture.h, tests/test_config.cpp, tests/test_config_protocol.cpp, tests/test_config_socket.cpp, tests/test_config_writer.cpp, tests/test_desktopentry.cpp, tests/test_menupaint.cpp, tests/test_wm2_config_smoke.cpp, tests/test_wm_config_live.cpp, tests/test_wm_resource.cpp, tests/test_wm_runtime.cpp, tests/test_wm_socket.cpp. Findings: 1.
