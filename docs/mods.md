# Mods 兼容范围与构建

本仓保留 `runtime/mods.c` 的 RomFS 合并、旧 setup 补丁和重建指纹接口。Linux 便携包的静态代码 Mod 使用下面的私有构建入口，代码修改需要重新生成与编译后才能部署。整体 `hardware_validated=false`。

## Linux 静态代码 Mod

```text
/private/mods/
  10-first/exefs/<完整 NSO Build ID>.ips
  20-later/exefs/可读名称.pchtxt
  .disabled/exefs/暂不应用.pchtxt
```

```sh
python3 build.py build --work /mnt/build/botw-work \
  --dump /mnt/private/botw-1.6.0 --code-mods /mnt/private/mods
```

原始输入与 Mod 根目录只读挂入构建容器；工作目录与输入分离。构建会验证 main/subsdk0/sdk 的精确原始 SHA-256，再解压 NSO 的实际段、核对启用的段摘要，在 AOT 之前应用补丁。

目录与文件按 UTF-8 字节排序，后面的记录覆盖前面。点号开头的 Mod 目录停用；无 exefs 的纯 RomFS Mod 不进入代码补丁集合。活动补丁不能使用软链接、设备文件、目录逃逸或不支持的格式。

- 标准 IPS：`PATCH/EOF`；IPS32：`IPS32/EEOF`。支持零字节、RLE 和大偏移。文件名必须是当前模块完整 Build ID，允许省略完整的尾部零字节，不按任意短前缀猜测版本。
- PCHTXT：首个有效指令为完整 `@nsobid-...`，支持 `@enabled`、`@disabled`、有符号 `@flag offset_shift`、`@flag print_values`、`@stop`、注释和含 C 风格转义的 UTF-8 字符串。未知指令、错误版本、截断、地址溢出及预算超限均中止。
- 偏移从标准解压 NSO 文件开始，段内容位于 `0x100 + 段内存偏移`。不能修改 NSO 头、段间空洞、BSS、加载元数据、重定位目标、宿主 HLE 或本应用时钟替换位置。部分依赖这些位置的社区 Mod 会被明确拒绝。

所有活动补丁先完整验证，再准备模块。无实际修改的模块保留原始压缩字节。准备目录通过同文件系统的 Linux `renameat2(RENAME_NOREPLACE)` 原子发布，不覆盖已有目录；复用只允许完整内容核对一致的快照。

生成记录位于工作目录 `generated/static-mods.json`。打包再次验证原始输入、活动补丁、准备模块和 code 阶段回执；任何变化均要求重新生成和编译。包内 `data/exefs` 使用与 AOT 相同的补丁后内容，`provenance/static-mods.json` 记录输入、补丁与输出摘要。

移除代码 Mod 后，使用健康原始 dump，重新执行构建并省略 `--code-mods`，再核验新包。仅删除 Deck 上的补丁文件不能还原已编译代码。部署前保留现用版本，完成精确提交 CI、安全审查、包全部文件校验与实机回归后再原子切换入口。

### SDK7 配套容量实验

`examples/mods/20-sdk7-paired-256` 提供固定 BOTW 1.6.0 Build ID 的三条容量补丁：贴图池 115→256 MiB，实际应用堆请求 3120→3280 MiB，游戏根堆 3088→3248 MiB。应用内存报告需配套 `SAVAGEWIND_SDK7_HD_HEAP=1`。两处游戏初始化分别固定堆大小：单独调整 GetInfo 时，实际请求保持 3120 MiB；只增加 OS 堆时，游戏根堆仍保持 3088 MiB。

将示例目录复制到独立的私有 Mod 根目录，显式传入 `--code-mods`，使用健康原始三模块重新生成和编译；启动该候选时再设置上述环境变量。其它 Mod 的重叠容量修改须先移除，原始指纹门禁继续执行。此实验保持原 3328 MiB SetHeapSize 上限，实际 HD 资源负载、画质和长期稳定性仍待独立验证。

## 旧 setup 接口

Linux 便携包启动时从可写数据目录 `$XDG_DATA_HOME/savagewind/mods/<名称>/romfs/` 读取资源覆盖。`romfs` 内的相对路径直接对应游戏 RomFS；旧式直接放在 Mod 根目录的资源仍可用，但 `exefs`、IPS/IPS32/PCHTXT 和根目录 README 不混入资源覆盖。点号开头的 Mod 停用，按名称排序、后者覆盖前者。

Linux 将已验证资源硬链接到数据目录 `native-mods/romfs-view`，不要求存在 `extracted`，不复制或修改发行包游戏资源。视图通过同卷目录原子交换发布；输入准备失败会停止启动并保留原视图，不能把零个成功链接当作成功加载。发布同步失败会尝试回滚，回滚也失败则保留必要视图并报错。软链接、FIFO/设备、过深目录、超过64个 Mod 或65536个资源记录会明确失败；未知或软链接的视图目录不会被覆盖。输入和数据目录须在同一文件系统，跨卷硬链接失败也会停止。资源文件在游戏运行期间应保持不变，变更或停用 Mod 后重启生成新视图；硬链接不是输入内容的独立副本。

启动时只回收同时带本应用所有权和可丢弃标记、所属进程已不存在的临时视图；发布前先持久化撤销可丢弃标记；回滚也失败的恢复副本、未知内容与活进程目录保留。Linux 原创文件夹具已通过11项定向回归，覆盖原有代码补丁接口、资源覆盖/停用、优先级、晚失败回滚、软链接/FIFO、目录预算、崩溃残留收尾及发布同步故障回滚及双故障后的恢复副本跨重启保全；新增文件系统回归在 ASan/UBSan 下运行。完整 CI 与真实资源实机验收仍待当前提交验证。

其它平台的旧资源视图实现未在本次 Linux 修正范围内。真实游戏资源替换、移除回滚仍须单独实机验证，组件回归不能冒充硬件验收。

`<data root>/mods/<名称>/<module>.ips` 保留原有行为。标准 IPS/IPS32 会完整校验记录、支持零字节/RLE，并明确拒绝直接修改压缩 NSO。私有 `IPS\x1a` 方言仍按旧约定把零字节视为保留原值，不能与标准格式混称。

旧接口的单文件原子替换不代表多模块事务，也不能自动接入上述 Linux 私有构建链。运行时 RomFS 覆盖、静态代码 Mod 和金手指各自验收，不能以加载文件成功替代游戏效果。

## 验证与剩余工作

原创夹具已验证压缩、版本/边界、原子失败、元数据保护和生成/编译执行：被修改函数实际返回 42。真实 SDK7 输入已完成只读指纹、解压映像一致性及保留区准备核验。真实社区 Mod 的整游戏画面/行为、HD guest 堆扩容、移除回滚仍待实机验收；host 贴图池阈值只是告警。

格式参考：[Atmosphère loader](https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/components/modules/loader.md)、[IPSwitch 格式说明](https://github.com/3096/ipswitch/blob/master/example/README.md)、[hactool NSO 头](https://github.com/SciresM/hactool/blob/master/nso.h)。实现按格式独立编写，未复制其代码；夹具不含游戏资源。
