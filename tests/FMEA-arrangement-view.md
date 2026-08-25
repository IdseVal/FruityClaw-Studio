# FMEA — Arrangement view PR (#30), large components

Interview record per the tester role's hybrid strategy. Posted on PR #30 on
2026-08-24; every row and all three scope questions confirmed by the human on
2026-08-25 ("Yes, and I confirm all the items listed on PR30").

| Proposed Component | Failure Mode | Default Recovery Action | Human Confirmation Needed? |
|---|---|---|---|
| ArrangementView geometry (`tick_to_x`/`x_to_tick`/`snap`/`placement_at`) | A mis-mapped click places or moves a Placement at the wrong tick/track | Pin with QTest interaction tests on the offscreen platform (simulated clicks assert the resulting Delta) | Confirmed → `test_ui.cpp` |
| ArrangementView drag commit | Drag previews one thing but commits a different Delta; or a no-move click records a spurious History entry | Simulated press–move–release asserting exactly one Delta matching the preview; no-move commits nothing | Confirmed → `test_ui.cpp` |
| ArrangementView rename editor | Commit lands on a Track deleted mid-edit | `apply_or_hint` routes the failure to the status bar, Project untouched | Confirmed → `test_ui.cpp` |
| MainWindow transport/undo bindings (50 ms poll) | Stale undo/redo labels or enabled state after apply/undo | QTest: apply → assert action text/enabled follow `HistoryState`; poll follows `PlaybackStatus` | Confirmed → `test_ui.cpp` |
| Engine `publish()`/reclaim vs `render()` (atomic model swap, epoch retire) | Use-after-free of a displaced RenderModel; torn transport state | Multithreaded stress test: publish storm against a running render thread; no crash, finite output, no timing assertions | Confirmed in scope → `test_engine_stress.cpp` |
| PortAudioDevice: no usable output device | App crashes instead of running silent | `main.cpp` falls back to silent 48 kHz; evidence is the manual smoke test (app runs with and without a device) | Confirmed: manual smoke test accepted; no fake-device suite ("a fake that behaves better than the real thing proves nothing") |
| PortAudioDevice: callback ordering at shutdown | Callback fires into a destroyed Engine | `main()` stops+closes the device before the Engine dies; inspected, not automatable without hardware | Confirmed: inspection accepted |
