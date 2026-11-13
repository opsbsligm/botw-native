# 计算着色器表面读写

本分支为预热中失败的 20 个计算程序补充 Vulkan storage image 路径。真实资源清点与原失败日志的 20 个哈希逐个一致，共 33 条 SULD、21 条 SUST。清点元数据保存在私有证据目录，不把游戏着色器代码加入仓库。

## 支持范围

- 立即数图像句柄，范围为 NVN 常量缓冲中的 `0x120..0x158`，对应已回放的 8 个 `BindImages` 槽。
- 1D/2D 的 SULD 原始 32 位读取，通过兼容的 `R32_UINT` 视图保留位模式；字节坐标形式按 4 字节转换，不能替换成浮点采样。
- 1D/2D 的 SUST 四分量格式化写入，使用实际视图的浮点或归一化格式，并核验设备的 `shaderStorageImageWriteWithoutFormat` 能力。
- 描述符位于 `64..79`，与采样纹理分开。生产 SPIR-V 反射根据 `OpTypeImage.Sampled=2` 识别 storage image。
- 显式检查负坐标与图像尺寸：越界读取为零，越界写入丢弃，不依赖驱动的可选健壮访问特性。写入后记录 GPU 所有权并增加内存屏障，保留 CPU 提供的初始内容。

仅为格式能力允许的非压缩、非深度单采样图像增加 storage usage。绑定时进一步检查视图、位宽、层级、维度、格式与缩放；不支持的绑定明确报告并拒绝该 dispatch，不绑定假的零纹理继续执行。

## 明确保留的缺口

Metal、bindless、数组/3D、原子操作、非默认缓存/越界模式、其他原始位宽和部分分量写入仍拒绝翻译。整数格式化写入、压缩/深度/回退图像、自定义 swizzle 和放大后的 storage image 仍拒绝绑定。放大图像必须先实现与 dispatch 覆盖范围一致的坐标语义，不能只改 view 后声称分辨率适配完成。

`ShaderInfo` 的图像元数据参与转换缓存内容；Makefile 已由翻译器和头文件内容生成转换器身份，本次头文件变化会使旧转换缓存失效。源码或缓存身份变化后需要重试预热。

## 验证与交付门槛

`tests/test_shader_surfaces.py` 在固定 Linux SDK 编译原创指令夹具，经生产翻译器、shaderc 和生产反射函数检查 4 个资源案例，并拒绝 10 种未支持形式。该检查不执行 GPU，不证明实际读取值或写入效果。

交付仍须：完整 runtime 构建；真实 20 程序翻译及 GPU 管线重试；GPU 写入/读回与越界负例；图像绑定、视图及同步的实际回归；隔离玩家存档的游戏场景对比；精确提交 CI、安全审查和主线验证。未满足前不切换正式掌机包，`hardware_validated=false`。

指令格式参考 [envytools 固定 GM107 表](https://github.com/envytools/envytools/blob/f102b82381f3f11cee113d16374c87091db039d9/envydis/gm107.c)，资源接口参考 [Vulkan Shader Resource Interface](https://docs.vulkan.org/spec/latest/chapters/interfaces.html#interfaces-resources)。按行为独立实现，未复制第三方代码。
