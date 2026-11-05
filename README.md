# botw-native

《旷野之息》1.6.0 的独立 Linux / Steam Deck 原生源码工程。源码、构建工具和版本检查均在本仓库；不依赖旧 `nativedeck` 工作区，也不需要既有游戏二进制或对象文件。

## 构建与运行

需要 x86_64 Linux、Python 3.11+、Docker（含 Buildx / BuildKit）、至少 100 GiB 可用空间。默认最多使用 2 核和 8 GiB 内存；首次工具链构建需要访问官方依赖源。建议在 Linux 电脑或 NAS 上构建，再复制运行包到 Deck；运行包在 Deck 上无需 Docker。所有构建依赖装在容器里。

先准备自己提供的、已解密且应用 1.6.0 更新的目录：`exefs/`、`romfs/`。`aoc1/`、`aoc2/`：可选的已解密 DLC 目录。中文显示还需要游戏自己的中文语言资源；主机字体采用有再分发许可的文泉驿微米黑和上游 OFL 字体，保留各自许可。 不接受其他版本混用，公开源码包不含游戏、密钥、系统字体、存档或生成的游戏代码；自有数据可以保存为项目私有的加密恢复制品，见 [个人恢复](docs/private-recovery.md)。

```sh
git clone https://gitlab.example.com:8445/codex/botw-native.git
cd botw-native
python3 build.py doctor --dump /mnt/my-dump --work /mnt/botw-build
python3 build.py build --dump /mnt/my-dump --work /mnt/botw-build
/mnt/botw-build/package/run.sh --verify
/mnt/botw-build/package/run.sh
```

成功后 `package/` 是个人完整运行目录，可复制到 Steam Deck，从桌面模式把其中 `run.sh` 添加为非 Steam 游戏，使用原生 Linux 启动。重启 Steam 后可在掌机模式打开。控制器使用 Steam Input 的普通游戏手柄布局。无需启用 Proton。

构建分为代码生成与编译、资源转换、打包；可用 `--stage code`、`--stage assets`、`--stage package` 分段诊断。输出与游戏输入必须在仓外且互不重叠；构建日志在工作目录 `logs/`。已有包不会被静默覆盖。

## 运行状态

SDK7 使用独立的 1.6.0 实时帧时序：校验 80 个指令身份后生成 VFR、界面、物理、按键重复和声音计时适配；默认 60 帧上限。不会启用较新版本依赖其他地址表的兼容路径。软件时钟与指令门禁验证和实机体验分开验收；完整代码生成与编译已经通过，完整资源打包仍需健康的输入资源。

运行器校验程序、ExeFS 模块和依赖摘要、检查资源尺寸，`--verify` 校验全部文件内容。启动前对现有存档做内容去重、逐文件校验和原子快照备份，源文件变化则中止。存档位于 XDG 用户数据目录，缓存单独保存在 XDG 缓存目录；两个游戏互不覆盖。六轴方向、实体按键、不同机器上的游戏运行和性能由实际玩家验收；`hardware_validated=false` 针对本仓本次重建产物。

## 开发与分享

`src/` 或 `runtime/` 是生产运行时，`tools/` 是版本校验与生成/构建工具，`packaging/` 是运行入口，`tests/` 是公共回归测试。历史上游文档位于 `docs/upstream/`，本 README 是 Linux 发行入口。

```sh
python3 build.py runtime --work /mnt/botw-public-check
```

这个命令编译公共运行时，CI 也执行它；它不等于完整游戏构建。GitLab 的 MR 流程要求测试、安全扫描和指定审查者通过后合并。游戏资源不进入 CI。分享源码请克隆本仓库，或用 `python3 tools/export_source.py --output /mnt/botw-source.tar.gz` 导出含校验清单的源码包；解压后无需 `.git` 即可构建。个人生成的 `package/` 包含游戏派生内容，与可分享源码包分开。

验证范围与已知限制见 [docs/validation.md](docs/validation.md)。

依赖与许可见 [NOTICE.md](NOTICE.md)、[UPSTREAM.json](UPSTREAM.json) 和 [docs/build.md](docs/build.md)。

## 掌机应用控制

F1 或 Minus+Plus 打开可折叠的边缘控制栏；应用菜单用 Deck A 确认、B 返回，游戏玩法映射保持不变。原生 IMU、已接通的画面选项、金手指导入边界和验证状态见 [应用控制](docs/application-controls.md)。固定版本游戏菜单也使用物理 A 确认、B 取消；已验证的软件范围与实机验收边界见 [菜单输入](docs/menu-confirm.md)。
