# 计算着色器表面读写

本分支为预热中失败的 20 个计算程序补充 Vulkan storage image 路径。真实资源清点与原失败日志的 20 个哈希逐个一致，共 33 条 SULD、21 条 SUST。清点元数据保存在私有证据目录，不把游戏着色器代码加入仓库。

## 支持范围

- 立即数图像句柄，范围为 NVN 常量缓冲中的 `0x120..0x158`，对应已回放的 8 个 `BindImages` 槽。
- 1D/2D 的 SULD 原始 32 位读取，通过兼容的 `R32_UINT` 视图保留位模式；字节坐标形式按 4 字节转换，不能替换成浮点采样。
- 1D/2D 的 SUST 四分量格式化写入，使用实际视图的浮点或归一化格式，并核验设备的 `shaderStorageImageWriteWithoutFormat` 能力。
- 1D/2D 的立即数 SUATOM 32 位整数加法，原子修改图像并返回旧位模式。U32/S32 按相同的模 2^32 语义执行，使用具有原子能力的 R32_UINT 视图；返回目标为 RZ 时也保留写入副作用。
- 描述符位于 `64..79`，与采样纹理分开。生产 SPIR-V 反射根据 `OpTypeImage.Sampled=2` 识别 storage image。
- 显式检查负坐标与图像尺寸：越界读取为零，越界写入丢弃，不依赖驱动的可选健壮访问特性。写入后记录 GPU 所有权并增加内存屏障，保留 CPU 提供的初始内容。

仅为格式能力允许的非压缩、非深度单采样图像增加 storage usage。绑定时进一步检查视图、位宽、层级、维度、格式与缩放；不支持的绑定明确报告并拒绝该 dispatch，不绑定假的零纹理继续执行。

同一图像的采样/附件视图去掉继承的 storage usage，计算视图则仅允许 storage usage，并把用途纳入视图缓存键。这样可保留 sRGB 重解释与采样 swizzle，避免向不支持 storage 的视图格式继承该用途（VUID 02275）。`tests/storage_view_usage_test.c` 捕获生产 `vkCreateImageView` 调用，检查用途限制、同格式不同用途的缓存隔离、重复复用及原有非 storage 路径；该回归不需要 GPU，也不替代真实驱动与游戏绑定验收。用途限制遵循 [VkImageViewUsageCreateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewUsageCreateInfo.html)。

## 明确保留的缺口

Metal、bindless、数组/3D、其他原子操作或位宽、非默认缓存/越界模式、其他原始位宽和部分分量写入仍拒绝翻译。整数格式化写入、压缩/深度/回退图像、自定义 swizzle 和放大后的 storage image 仍拒绝绑定。放大图像必须先实现与 dispatch 覆盖范围一致的坐标语义，不能只改 view 后声称分辨率适配完成。

`ShaderInfo` 的图像元数据参与转换缓存内容；Makefile 已由翻译器和头文件内容生成转换器身份，本次头文件变化会使旧转换缓存失效。源码或缓存身份变化后需要重试预热。

## 验证与交付门槛

`tests/test_shader_surfaces.py` 在固定 Linux SDK 编译原创指令夹具，经生产翻译器、shaderc 和生产反射函数检查 9 个资源案例，并拒绝 13 种未支持形式，包含字节坐标不被误解为访问位宽的正例。CI 另编译 `tests/surface_image_gpu.c`，不在 Runner 执行 GPU 程序；缺少硬件特性时该程序退出 77，不能计为通过。

手动 GPU 程序包含 17 个读回案例：原始位模式、格式化写入、字节坐标、坐标/目标寄存器重叠、越界、原子加返回旧值与溢出。它只在生产翻译结果增加观测输出，不替换指令或寄存器赋值。执行前隔离 `MESA_SHADER_CACHE_DIR` 和 `XDG_CACHE_HOME`，证据完整回读后清理程序及缓存。该测试不加载游戏、不创建窗口或操作存档；通过也不等于游戏绑定路径、缩放或玩法场景已验收。

交付仍须：完整 runtime 构建；真实 20 程序翻译及 GPU 管线重试；GPU 写入/读回与越界负例；图像绑定、视图及同步的实际回归；隔离玩家存档的游戏场景对比；精确提交 CI、安全审查和主线验证。未满足前不切换正式掌机包，`hardware_validated=false`。

2026-11-13 私有验证：修复前保留真实 18/20 失败；补上原子加后，20/20 真实程序均通过翻译与 SPIR-V 编译。在 AMD Custom GPU 0405 / RADV VANGOGH 上，17/17 个原创 GPU 读回案例最终通过。前两轮程序在字节坐标访问宽度的拒绝断言退出，未计为通过；修复该独立边界错误后重跑。正式入口未变，测试目录经逐成员证据核验后清理。上述验证不替代实际游戏的绑定、缩放、画面或全流程验收。

指令格式参考 [envytools 固定 GM107 表](https://github.com/envytools/envytools/blob/f102b82381f3f11cee113d16374c87091db039d9/envydis/gm107.c)，资源接口参考 [Vulkan Shader Resource Interface](https://docs.vulkan.org/spec/latest/chapters/interfaces.html#interfaces-resources)。按行为独立实现，未复制第三方代码。
