# 竞品吸收差距矩阵（旷野之息）

更新：2026-11-10。来源：竞品调研侧对话首轮结论（参考件留存 `/private/tmp/nativedeck-experience-20261105/refs/`：savagewind-main.md、totk-up.md、totk-deck.md、astral-up.md、macnative.md）。

本矩阵是 97 项验收矩阵（18+63+16）**之外**的独立子矩阵，共 5 项竞品吸收能力，避免污染原 97 项口径。每项含：上游证据（本仓源码静态核验）、当前状态、吸收方案、Deck 验收标准。`hardware_validated` 一律以 Deck 实跑截图 + 配置读回为准，不拿组件通过冒充游戏验收。

## 历史对标锚点（e48769d，保留比较基准）

- 本仓上游为 Savage Wind（1.6.0），其自身已具备全量 shader 预编译（~1.4 万 shader，setup 期可选、默认开）、FOV 70–150%、阴影 896→8192、各向异性 2x–16x、F10 性能浮层（runtime/perfmon.c）。这些能力当前挂在**键盘游戏内菜单**（`runtime/ui/game_options.mm`、`runtime/ui/menu.mm`），未暴露到掌机三级触屏控制栏。
- 本仓控制栏（`runtime/ui/control_bar.mm`）画面页已有：帧率上限（0/30/40/45/60/90/120）、抗锯齿、上采样（原始/FSR1/FSR3.1 实验）、上采样质量、锐化、窗口尺寸；无 144 档、无 FOV/阴影/各向异性入口、无 mods、无后台不阻塞式 shader 预热。
- 本仓 FPS 档位 `FPS_CHOICES {0,30,40,45,60,90,120}`（`runtime/settings.h:37`），无 144。
- 本仓无 romfs overlay quickmenu/gameover 修复（上游 Savage Wind 也未随包提供；sigma/totk-recomp 用 `tools/make_quickmenu_fix.py`、`make_gameover_fix.py` 修 >30fps 界面 bug）。

## 竞品吸收子矩阵（5 项）

| # | 能力 | 当前状态（静态核验） | 吸收方案 | Deck 验收标准 | hardware_validated |
|---|------|--------------------|---------|-------------|--------------------|
| CA1 | shader 后台预热（不阻塞首入场景） | 有 setup 期全量预编译（默认开，`settings.precompile_shaders` → `runtime/ui/launcher.mm:410-448` `shader_precompile_run`），但发生在 setup 阶段、非"游戏中首入场景后台继续"；首入新场景仍可能卡顿 | 合流"后台不阻塞+覆盖全"：沿用现有全量预编译覆盖广度，补一个**首帧可立即进入、编译后台继续**的运行时预热路径（对齐 sigma/totk-recomp 的 warmup.sh "按 A 立即进游戏、其余后台编译"语义），治 Deck 首入场景卡顿 | Deck 实跑：冷缓存首入新场景，截图+present 时序对比预编译前后；确认无首入场景 >1 帧卡顿尖峰（p50 回落到基线） | false |
| CA2 | >30fps 衍生界面 bug 核查（快速菜单/游戏结束） | 无 romfs overlay quickmenu/gameover 修复；30 锁已解、默认 60，快速菜单/游戏结束在 60 帧下的表现**未逐项核对** | 对照 sigma/totk-recomp 的 `make_quickmenu_fix.py`/`make_gameover_fix.py` 逐项核对：快速菜单（方向选择、光标）、游戏结束画面在 60 帧下是否仍有未修 bug；有则同法 romfs overlay 修复 | Deck 实跑 60 帧：快速菜单开合/选择、死亡游戏结束各截图；确认无光标漂移/选项错位/时序错乱；配置读回 60 帧 | false |
| CA3 | 画面页吸收：FOV 滑块 + 阴影分辨率 + 各向异性 16x | 已实现（MR !18：控制栏画面页暴露 FOV 70–150% / 阴影 896–8192 / 各向异性 2x–16x，复用 `g_settings.fov/shadow_res/anisotropy` 既有持久化链路）；此前三项仅（`FOV_CHOICES {70..150}` `settings.h:94`；`SHADOW_* {console,2048,4096,8192}` `settings.h:30-32`；`aniso 0..4=2x..16x` `settings.h:54`），但**未暴露到掌机三级控制栏** | 把现有 FOV/阴影/各向异性设置项暴露进控制栏画面页（触屏可调），复用 `g_settings.fov/shadow_res/anisotropy` 既有字段，不新做后端 | Deck 实跑：控制栏画面页出现三项并可调；FOV 150% vs 100% 截图对比视角变化；阴影 8192 vs console 截图对比锐度；各向异性 16x 截图对比远景地面清晰度；配置读回 | false |
| CA4 | mods（RomFS、ExeFS、HD 贴图堆） | 2026-11-13：已有 RomFS/旧 setup；Linux 静态构建新增压缩 NSO、Build ID、exefs 社区目录、IPS/IPS32/PCHTXT、AOT 前快照和打包重验。原创夹具已生成/编译执行确认；真实 Mod 与 guest 堆仍未验收，见 [mods.md](mods.md) | 按 build ID 匹配、解压副本、打补丁、重新生成/编译、独立打包和原子切换推进；HD 堆按版本严格补丁 | 用真实 RomFS/代码/HD mod 验证生效、版本拒绝、移除回滚与内存峰值 | false |
| CA5 | 144fps 实验档 + F10 性能浮层 | F10 性能浮层**已有**（`menu.mm:292` `perf_overlay` Off/FPS/Full，`runtime/perfmon.c`）已实现（`FPS_CHOICES` 追加 144 实验档，游戏内菜单/控制栏文案标注"实验、仅高刷外接有效"；控制栏画面页新增"性能浮层" Off/FPS/Full 触屏开关，F10 保留） | Deck 实跑：控制栏可调性能浮层（截图显示帧率/帧时间）；144 档配置读回（Deck 60Hz 屏下 144 仅标注实验、不强制生效） | false |

## 明确不做（记入口径，避免反复讨论）

- **DLSS**：仅 RTX 有用，Deck（Van Gogh/AMD）无 → "PC 端有价值 / Deck 暂缓"，不做。
- **HDR10**：Deck 屏幕不支持 HDR10 输出 → "PC 端有价值 / Deck 暂缓"，不做。

## 执行顺序与依赖

1. **CA3**（低成本高感知，纯 UI 暴露已有能力）先做——把 FOV/阴影/各向异性暴露进控制栏画面页。
2. **CA2**（核对 >30fps 界面 bug，可能产生 romfs overlay 修复）。
3. **CA1**（shader 后台预热合流，需运行时改动）。
4. **CA5**（144 实验档 + 性能浮层触屏化，小活）。
5. **CA4**（mods 系统，后批次，量大）。

单项单 MR，不混。每项走 codex 开发 → liguangming 精确 SHA 审查 → 必要修复 → 当前 SHA 完整 CI/安全扫描 → 合并 → 主分支 CI → 原子部署 → 隔离存档遥操回归。

优先级让位于用户手测反馈：若用户测出实体问题先修问题。

> 历史对标记录基于 `e48769d`，CA4 构建链说明更新于 2026-11-13；`hardware_validated=false` 项一律待 Deck 实机补齐。
