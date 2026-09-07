# CodeRabbit CLI, last commit only (4e7e8f5..8cdf6f4)

Run 2026-09-07 01:40Z under grant GR-2026-0907-0004, as the discriminator that showed the full-diff refusals were about payload size, not the pool.

## [major] CMakeLists.txt

In @CMakeLists.txt at line 386, Update the Xvfb startup command to pass XVFB_PID_FILE and XVFB_LOG_FILE as quoted positional parameters to bash, then reference them as "$1" and "$2" throughout the script for rm, redirects, and cat so paths containing whitespace are handled safely.

