# CA4 mods 系统设计定稿（旷野之息）

更新：2026-11-12。本文把 CA4 从"吸收方案"收敛为**实现定稿**：已落地部分给出代码锚点与验收口径，未闭合的 HD 贴图堆扩容给出唯一推荐方案与边界，避免后续反复选型。

## 范围

CA4 三块能力，全部以「玩家数据目录 `mods/`，缺省即无副作用」为原则：

1. **RomFS 覆盖 mod**（贴图、文本、模型等）
2. **ExeFS 代码补丁 mod**（`.ips`，Atmosphère 同款格式）
3. **HD 贴图包**（依赖 1 + 3 的贴图堆扩容）

## 已落地（main 代码锚点）

| 能力 | 实现 | 锚点 |
|------|------|------|
| RomFS 覆盖 | `mods/<mod>/` 按目录名序合并进 `<extracted>/.mods-view`，指向 `SAVAGEWIND_ROMFS_OVERLAY`，与开发 overlay 同一条路（`runtime/services/fs.c` 零改动） | `runtime/mods.c` `mods_romfs_apply` |
| ExeFS 补丁 | 每个 `<module>.ips`（偏移自模块文件头，IPS 0xF0 表示保留原字节）应用到 `<extracted>/exefs/<module>`，NSO build-id（0x40 处）必须保持可读且不变；补丁发生在 exefs 拷贝之后、游戏代码构建之前，构建即吸收新字节，build-id 校验仍通过 | `runtime/mods.c` `mods_exefs_apply`、`nso_build_id` |
| 重建触发 | mods 非空时，启动画面以「mods 文件集 + exefs 代码段」为指纹提示重建游戏代码（`mods_need_rebuild`），重建成功后 `mods_mark_built` 置位，之后普通启动直接进游戏；删掉 mod 再 Set up 恢复原模块 | `runtime/mods.c` `mods_fingerprint`、`runtime/ui/launcher.mm` |
| 启动画面清单 | `mods_list` 返回每个 mod 的目录名与内容概述（RomFS 文件数 / 触及哪些模块），末行空名结束 | `runtime/mods.h` `ModsInfo` |
| 测试 | `tests/test_mods.py`（合并序、overlay 读回、ips 打补丁与 build-id、重建触发往返） | CI native-unit |

**验收口径（保持 97 项矩阵之外的 CA 独立口径）：** 挂一个 RomFS mod（改一张贴图）确认生效截图；挂一个 `.ips` 确认重编译 + 生效 + build-id 未破坏；配置/菜单读回。`hardware_validated` 以 Deck 实跑为准。

## 未闭合：HD 贴图堆扩容（唯一子项）

**根因：** BOTW 1.8.2 的纹理走 NVN 内存池：guest 在 setup 期用固定大小调 `nvnMemoryPoolInitialize`（`runtime/nvn/objects.c:402`）建池，`nvnMemoryAllocate` 从池中分配；池的 storage 由 `nvnMemoryPoolBuilderSetStorage` 指定、CPU/GPU 地址同址（`objects.c:417-418`）。HD 贴图包把 `.bflt` 换大后，`nvnMemoryAllocate` 的字节需求超出池容量即失败，guest 侧表现为贴图加载错或 OOM。

**推荐方案（惰性扩容，单一改动面）：** 在 `nvnMemoryPoolInitialize` 处把该池的**地址空间上限**抬到可配置最大值 `MAX_POOL`（默认 8 GiB，Deck 内存充足），而**物理页按需提交**：storage 区以匿名映射预留 `MAX_POOL`，guest 首次写/读才 `plat_commit` 对应页（`runtime/kernel.c:315` 已有 `plat_commit` 语义）。guest 视角池子"无限"，宿主视角内存只涨到实际使用量。

**为什么不用别的：**
- *直接按 mod 声明扩容*：需要 mod 侧约定头部与字节量，把简单贴图 mod 也拖进"必须声明"的负担；
- *池满了再重建*：要停帧、重放 guest 分配序列，复杂度远高于预留；
- *只扩不预留*：VMA 地址必须连续（GPU 地址==CPU 地址假设），动态扩地址会破坏已提交页的地址稳定性。

**边界与回退：** `MAX_POOL` 做成控制栏/环境变量 `SAVAGEWIND_MAX_TEXTURE_POOL`（MiB，0=沿用 guest 原值），默认 8192；`plat_commit` 失败（罕见）时回落到"guest 原大小 + 明确错误日志"，不静默。无 mods 时行为与现在完全一致（预留不提交，零物理增量）。

**验收：** 放一个把主角头像 `.bflt` 换成更高分辨率的 mod，Deck 实跑截图确认贴图生效且不 OOM；`/proc` 或 perf 观察纹理池物理页只涨到实际量；无 mods 时内存曲线与基线一致。

## 执行顺序

1. 先补 HD 贴图堆（上文方案，单 MR，`runtime/nvn/objects.c` + `runtime/kernel.c` + 控制栏一行）。
2. 再拿两个现成 mod（1 个 RomFS 贴图、1 个 `.ips`）在 Deck 上做 CA4 整体验收，把 `competitor-adoption.md` 的 CA4 行 `hardware_validated` 置真。
3. HD 贴图包与两 mod 一并进 `docs/data-folder.md` 的 mods 章节（目录约定 + 示例）。

## 与 TOTK 的差异

TOTK（UltraTears 上游）目前**无 mods 目录约定**，其 CA4 独立成批，不占用本仓设计。本仓先闭合，TOTK 侧复用「RomFS 覆盖 + IPS」两段即可（其 exefs 布局相同），HD 堆方案视其纹理池实现单独评估，不强行同步。
