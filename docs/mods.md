# Mods 兼容范围与 CA4 构建路线

2026-11-12 源码核验：本仓已有 `runtime/mods.c` 的 RomFS 合并、旧格式代码补丁、mod 指纹与 setup 重建接口；此前竞品矩阵写成“无 mods”不准确。便携包、SDK7 和社区格式的完整链路仍需逐项验收。

本次修复既有 `<data root>/mods/<名称>/<module>.ips` 接口的标准 IPS/IPS32 解析：`PATCH/EOF` 或 `IPS32/EEOF`，支持逐字节写入（包括零）、RLE、后记录覆盖先记录与 16 MiB 以上偏移。修改前校验全部记录，拒绝截断、缺结束标记、越界、头部修改、调整文件长度和额外尾部数据。私有旧格式 `IPS\x1a` 保留原有零字节跳过规则，不能与标准格式混称。

补丁失败会保留错误、返回失败并撤销旧成功记录，setup 不应继续将未生效的补丁记为完成。复制模块和记录文件采用同目录临时文件、同步及原子替换。NSO 段表按真实字段位置读取并检查文件边界，指纹对压缩段只读取实际存储长度。单个文件替换的原子性不代表多模块事务；失败后应由 setup 从健康来源重新准备全部副本。

标准 IPS 作用于解压后的 NSO，必须保持 0x100 字节头部和 build ID。现有私有包的 main/subsdk0/sdk 均为压缩 NSO，因此当前代码明确拒绝直接向其套用标准 IPS；不得据此宣称现有社区代码 mod 已在游戏生效。build-ID 文件名匹配、`exefs/` 社区目录、PCHTXT 与自动解压重编译仍在后续 CA4 范围。

CA4 后续按下列链路推进：

1. 在获准远端构建节点读取受支持版本与 build ID，保留输入；解压到任务独立副本，记录逐文件摘要。
2. 按明确的目录与名称顺序枚举补丁，验证 build ID，支持标准 IPS/IPS32 和严格的 PCHTXT enabled/disabled/offset_shift 语义；未知指令、版本不符、路径逃逸和越界必须中止。记录补丁集合、输入及输出摘要。
3. 在生成游戏 C 代码之前应用补丁，将结果纳入生成与编译缓存指纹；删除 mod 后从原始输入重新生成。不得把静态代码改动当作运行时金手指直接写入。
4. 生成独立可回滚运行包，关联精确源码与 mod 集合；完成 CI、包全文件核验及隔离存档实机回归后，再原子切换入口。Deck 只承接运行包及有限诊断数据。
5. HD guest 贴图堆扩容先确认版本与指令前值，再生成代码补丁；host 纹理池告警阈值不能当作 guest 扩容。用真实 HD 包检查内存峰值、贴图与崩溃情况。

依据：[Atmosphère loader 的 NSO/IPS 规范](https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/components/modules/loader.md)。本实现按格式语义独立编写，没有复制该项目的 GPL 代码。新增合成测试经 AddressSanitizer/UndefinedBehaviorSanitizer 验证标准格式、实际模块分派、错误保留、段边界和指纹；不含游戏代码。`hardware_validated=false`。
