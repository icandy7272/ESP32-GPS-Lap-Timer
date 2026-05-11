# Field-test logs archive

存放从板子 SD 卡导出的 `boot_log.txt` 和 `crash_log.txt` 历史快照，方便日后对照
"修复前 vs 修复后" 的诊断证据，以及作为复盘材料。

命名约定：`YYYY-MM-DD-<kind>.txt`，例如：

- `2026-05-09-crashes.txt` — 该日 field test 的 crash_log.txt
- `2026-05-09-boot.txt` — 该日 field test 的 boot_log.txt

存档之后 SD 卡上的对应文件清空（用 `: > file` 而非删除，让固件 append handle 不
卡），下次走测就能产出干净的新日志。

## 索引

| 日期 | 走测概要 | 文件 |
|------|---------|------|
| 2026-05-09 | 户外首次走测；连续 PANIC + BROWNOUT 链；`crash_bc_core1=61` 8 次（display render path）；track autodetect 在每次 boot 后覆盖用户选定的赛道 | [crashes](2026-05-09-crashes.txt) / [boot](2026-05-09-boot.txt) |

诊断对应的修复 commit：`e00bdcd fix(field): boot autodetect override + diagnostic pass for PANIC chain`。
