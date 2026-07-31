# background

## Overview

The background task owns periodic task-context servicing that does not justify a
dedicated task. It runs at a 10 ms base period so the non-blocking logic-expander
transaction queue progresses promptly.

The existing blue LED heartbeat remains scheduled at 1 Hz using a separate
cycle counter; shortening the task period does not change its visible behavior.

At task startup, a one-time initialiser callback list runs before the task takes
its periodic timing reference. It currently creates the Logic Expander's
task-level mutex. Periodic processing does not begin unless every required
initialiser succeeds; on failure, the background task suspends itself.

After successful initialisation, each background cycle loops through a separate
function-pointer array containing the logic-expander and status-LED processes.
Additional one-time or periodic work can be added to the corresponding array
without changing the task loop. The expander mutex allows configuration-manager
and console calls to overlap this periodic servicing safely.
