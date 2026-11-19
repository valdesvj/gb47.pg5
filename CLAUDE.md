# GB47: working rules

- Work on a new branch; Victor creates and merges the PRs. Never force-push or rewrite history.
- Commit author: "Victor Valdes" <valdesvj@users.noreply.github.com>.
- The same base as ZX47, HP47, Chess 47, NAV47 and TERM47 (dmcp/, tools/pg5tool.py, test/sim): keep them in step.
- third_party/peanut_gb is Peanut-GB (MIT), unchanged: if it ever changes, mark the changes at its top and run make test.
- DMCP5: 8 KB of static RAM. Big buffers go on the heap (the machine, the ROM or its cache, the picture); check .bss.
- make test and make shots before committing; look at every screen. The simulator's timings mean nothing:
  speed is measured on the DM42n.
- Tools (tests, probes) live in tools/<name>/ with their own main.h, and build with their own make target (FPS47).
- No Nintendo code, BIOS or games in the repo: only the test cartridges in test/roms and our own GB47DEMO.GB.
- Ask before anything that can't be undone. Pull requests only for gb47.pg5.
