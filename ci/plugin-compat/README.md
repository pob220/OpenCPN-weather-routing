# Windows plugin toolkit qualification

This workflow builds the checksum-pinned plugin sources in `sources.json` with
matching official OpenCPN hosts and wxWidgets development/runtime archives.
The two hosts are 5.14.2 / wxWidgets 3.2.9 (`de7e706`) and 5.15.0 /
wxWidgets 3.3.4 (`e41f0de5d`, containing OpenCPN #5499).

Each job checks AMD64 architecture and actual host/toolkit imports, runs the
plugin tests, packages the plugin, imports it with the stock Plugin Manager,
and requires normal initialization, reinstall/restart and exit code 0.
Weather Routing additionally checks all seven routing/reinstall cases and
installed data/translations. Celestial Navigation provisions its existing,
checksum-verified DE440 test fixture and preserves source line endings.

Final qualification of the pinned sources (28 October 2026):

| Plugin | Source | wx3.2.9 | wx3.3.4 | Evidence |
| --- | --- | --- | --- | --- |
| Celestial Navigation 2.9.11 | `5ec5264` | Pass | Pass | [Native tests and lifecycle](https://github.com/pob220/OpenCPN-weather-routing/actions/runs/40603917915) |
| Climatology 2.0 | `f6b8872` | Pass | Pass | [Matched-host matrix](https://github.com/pob220/OpenCPN-weather-routing/actions/runs/40601493115) (the two Climatology jobs) |
| xGRIB 0.4.1 | `7373672` | Pass | Pass | [Final source qualification](https://github.com/pob220/OpenCPN-weather-routing/actions/runs/40608917343) |
| Weather Routing 1.35 | `0b985e5` | Pass | Pass | [Production package and routing checks](https://github.com/pob220/OpenCPN-weather-routing/actions/runs/40603696692) |

Weather Routing's pinned tree is identical to contribution PR #723 at
`042697b`; the separate commit preserves the qualification branch history.
The Celestial Navigation suite includes 317 main C++ cases plus the smaller
standalone suites; all eight CTest entries pass in both jobs.

The xGRIB qualification reuses its unchanged, checksum-pinned 0.4.0 generator
and runtime. Its production CircleCI release jobs rebuild that same generator
and additionally test the final CPack archive with embedded metadata.

The two x64 hosts advertise the same `msvc-64` catalogue target despite their
incompatible wxWidgets ABIs. Qualification archives therefore have distinct
`wx329`/`wx334` filenames and remain separate artifacts. Do not insert both
variants as interchangeable catalogue entries. The xGRIB 0.4.1 production x64
package targets the current official wx3.3.4 host; its catalogue PR must make
that host requirement explicit.

Use the workflow's `plugins` JSON-array input for focused reruns. The default
is all four plugins. This tooling branch does not publish packages.
