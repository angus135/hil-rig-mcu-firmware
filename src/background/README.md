# background

## Overview

The background task owns periodic task-context servicing that does not justify a
dedicated task. It runs at a 10 ms base period so the non-blocking logic-expander
transaction queue progresses promptly.

The existing blue LED heartbeat remains scheduled at 1 Hz using a separate
cycle counter; shortening the task period does not change its visible behavior.

Application startup initializes the Logic Expander and starts its asynchronous
self-configuration before the scheduler runs. Each background cycle then loops
through a function-pointer array containing the logic-expander and status-LED
processes. Additional periodic work can be added to this array without changing
the task loop. The expander mutex allows Run State Manager, driver, and console
calls to overlap this periodic servicing safely.
