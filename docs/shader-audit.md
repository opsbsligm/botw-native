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
