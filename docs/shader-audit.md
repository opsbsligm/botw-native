# 着色器缺口核验

Linux Vulkan 支持 `TLDS.2D.MS`：使用整数二维坐标和独立 sample 索引，生成 `sampler2DMS` / SPIR-V `OpImageFetch Sample`。多重采样与普通纹理分别校验样本类型，未绑定槽位也创建设备实际支持的多重采样后备图像。MS 纹理查询只有一个 mip 层。Metal 的该指令变体仍未实现。

`tests/test_tlds_multisample.py` 用合成 Maxwell 指令调用生产翻译器和 shaderc，检查实际 SPIR-V 的 MS 类型与 Sample/Lod 操作数，并覆盖半精度打包、前后尺寸查询和普通 mip 读取。另一测试执行生产后备图像创建路径，捕获 Vulkan/VMA 调用，检查真实支持的样本数和缓存复用。这些测试不含游戏资源，也不证明实机渲染正确。

持有个人转储时，可在获准 Linux 构建节点使用 `tools/shader_archive_audit.c` 定向复查日志中失败的 shader hash。工具只输出诊断，不导出游戏着色器、不写缓存、不创建 GPU 上下文；不覆盖每个运行时 key、GPU 管线及画面验收。

```sh
clang -std=gnu11 -O1 -I runtime -isystem "$NATIVE_DEPS/include" \
  tools/shader_archive_audit.c runtime/nvn/shader_archive.c \
  runtime/nvn/maxwell/translate.c runtime/romfs_image.c runtime/romfs_build.c \
  -L "$NATIVE_DEPS/lib" -Wl,-rpath,"$NATIVE_DEPS/lib" -lshaderc_shared -lm -lpthread \
  -o "$TASK_WORK/shader-archive-audit"
"$TASK_WORK/shader-archive-audit" "$PRIVATE_ROMFS" "$SHADER_HASH"
```

指令语义参照 [envytools GM107 ISA 表](https://github.com/envytools/envytools/blob/master/envydis/gm107.c)，图像读取与资源约束参照 [Vulkan Sampling](https://docs.vulkan.org/spec/latest/chapters/textures.html) 和 [VkImageCreateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageCreateInfo.html)。实现未复制其他许可证不兼容项目的代码。预热完成判定和其余 SULD/SUST 缺口单独验收，问题 #1 保持开放。

2026-11-12 私有构建节点定向复查：生产归档读取器识别 13,869 个唯一 shader，原先因 `tlds info 6` 失败的 4 个片段 shader 均找到、翻译成功且通过 shaderc 生成 SPIR-V，工具退出码 0。该结论限于此 4 项的源码翻译/编译；其余 20 项 SULD/SUST、真实 Vulkan 管线、多重采样画面及完整预热仍待验证。

同日 Deck 原生 GPU 核验：`tests/tlds_multisample_gpu.c` 在 AMD Custom GPU 0405 / RADV VANGOGH 上向 4 个 sample 写入不同数值，再通过生产 Maxwell 翻译器生成的 TLDS compute shader 分别读取，全部 16 个浮点通道与预期精确一致，退出码 0。测试无窗口、不加载游戏；输出缓冲是测试额外添加的观测点，TLDS 指令翻译与寄存器赋值未替换。必要证据已逐成员 SHA 核验，临时程序和隔离驱动缓存已清理。此结果验证合成指令的 GPU 读值，不等于游戏片段管线、绑定路径及场景画面通过，`hardware_validated=false`。

CI 只编译此 GPU 测试程序，不在 Runner 上尝试硬件校验。手动执行必须设置任务独立的 `MESA_SHADER_CACHE_DIR` 和 `XDG_CACHE_HOME`，并在证据回读后清理。缺少所需硬件特性时退出 77，不记为通过。

翻译缓存读取还须检查非空源码及 `ShaderInfo` 的纹理/图像数量边界。损坏的磁盘计数不能进入固定数组消费者，应返回缓存未命中；持有原始程序的调用路径继续重新翻译，按 ID 的预热不能把缺失缓存当成成功。合法旧缓存保留兼容。`tests/test_shader_cache_bounds.py` 调用生产缓存写入器和读取器，在压缩源码保持完整时篡改序列化计数，分别核验按 key 和按 ID 读取、最大合法数量、负数/超限拒绝、空源码拒绝及同路径恢复。该回归不使用 GPU，不替代实机缓存恢复或游戏画面验收。

首次并发写入还必须等待缓存根目录及翻译目录初始化完成，使用 `pthread_once` 发布完成状态。预热遇到压缩、打开、写入、关闭或原子发布失败时计为失败，撤销完成记录；游戏中的翻译可保留在内存继续使用，磁盘失败日志限量输出。`tests/test_shader_cache_init.py` 延迟真实 `mkdir`，让 16 个线程同时首次写入，再逐文件调用生产读取器核验；另以普通文件阻挡缓存目录，检查写入失败被保留。该组件回归不使用 GPU，完整 CI 与原生历史配置回归仍须绑定具体构建单独确认。
