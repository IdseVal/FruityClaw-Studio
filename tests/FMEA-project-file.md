# FMEA — Project save and load PR (#31), large components

Interview record per the tester role's hybrid strategy. Posted on PR #31 on
2026-08-25; row 2 decided **B** and every row confirmed by the developer on
2026-08-25 (cycle 2 reply), the retain change landing in `1c692d0`.

| Proposed Component | Failure Mode | Default Recovery Action | Human Confirmation Needed? |
|---|---|---|---|
| `save_project` temp-write → verify → retain → rename | Crash/failure mid-save leaves a partial target or a stray `.tmp-*` | Target untouched, temp removed | Confirmed: existing coverage accepted → `test_persistence.cpp` (rename-blocked, missing-dir, no stray `.tmp-*`) |
| `save_project` retain step | The current on-disk file is already damaged; copying it over a good `.previous` loses the last good prior version | **B**: decode the existing file first and skip the retain copy when it fails, so the older good `.previous` survives | Confirmed B → `test_persistence.cpp` "saving over a damaged file keeps the last good retained version" |
| `decode` on hostile/damaged bytes | Forged counts drive a giant allocation or out-of-range read | Bounds-checked Reader | Confirmed: existing coverage accepted (CRC, every-truncation-length sweep, foreign magic, newer version, forged-count bound) |
| `MainWindow::closeEvent` unsaved-changes prompt | Dirty Project closes without asking, or Cancel still closes | QTest driving the modal `QMessageBox`: Cancel → still visible; Discard → closed; clean window never asks | Confirmed → `test_ui.cpp`. The Save branch needs a current file, which only the native `QFileDialog` can set; it stays under row 5's manual smoke test |
| `MainWindow::save`/`save_as`/`open` via `ProjectFilePort` | Save error not shown / `mark_saved` despite failure; open error replaces the Project anyway | `save()` calls `mark_saved()` only after the port returns an empty error; `main.cpp`'s `open` calls `replace` only on `LoadResult.ok()` | Confirmed: manual smoke test accepted for the native-dialog paths |
| `ProjectHistory::replace` | Called mid-gesture; observers not told a load happened | Asserted in debug; observer notified once | Confirmed: existing coverage accepted → `test_history.cpp` |
