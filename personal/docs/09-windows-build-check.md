# 09 本机 Windows 构建实测与复现

日期：2026-10-05，Asia/Shanghai。此记录补充首次静态调查的 [06 构建指南](06-build-and-test.md) 与 [08 历史基准](08-baseline-and-limits.md)；不要据此改写首次调查当时的环境。

## 版本与本机工具链

本次通过 GitHub API 核对 Jojo-Schmitz/MuseScore 的 `3.x`：最新源码 HEAD 为 `f2a80b9f59dd396698b3b507a19a56bfb8791af2`，与本地应用源码基准相同。个人分支 HEAD 在此基础上增加指南提交 `5743890f996abd3683a46d9a0dd4863b80944c2e`。没有拉取/合并新的应用源码。

上游版本仍为 3.7.0 Evolution，其 [Wiki](https://github.com/Jojo-Schmitz/MuseScore/wiki) 说明这是持续演进的 3.x 分支。已核对远端 `config.cmake` 与 `build/ci/windows/build.bat` 的 blob SHA 与本地相同；x64 CI 使用 VS 2022 generator + Qt 5.15.2 msvc2019_64。这里实测现有 VS 2019 工具链，不以 CI generator 推断必须重装 VS。

| 项目 | 检查结果 |
| --- | --- |
| CMake / CTest | CMake 3.22.1，位于 `C:\Program Files\CMake\bin` |
| VS | Build Tools 2019，16.11.8，非完整 Visual Studio IDE |
| 可用 MSVC | v141 14.16.27023 和 v142 14.29.30133；本次显式选择 v142 |
| 实际 C/C++ compiler | MSVC 19.29.30138.0；CMake 的编译/链接探测成功 |
| SDK | 检测到 10.0.19041.0 / 10.0.22000.0，本次选择后者 |
| 原始 PATH | 含旧 v141 cl.exe，未含 qmake；VS generator 可显式选正确 toolset |
| 原始 Qt SDK | 常见 Qt 根目录、软件注册表及目标开发目录未找到 qmake/Qt5 CMake 配置 |
| 原始依赖 | 仓库 `dependencies` 与 `C:\musescore_dependencies` 不存在 |
| 解压工具 | 复用 `E:\vcpkg\downloads\tools\7zip-19.00-windows\Files\7-Zip\7z.exe` |

## 实际阻塞与解决

1. 原 `msvc_build.bat debug` 输出 `No supported version of Microsoft Visual Studio ... found`，但返回 0。其 vswhere 查询没有 `-products '*'`，未返回本机 Build Tools；不能只凭退出码当成功。未修改上游脚本，个人辅助脚本显式支持 Build Tools。
2. 显式 VS 2019 / v142 配置通过 compiler/SDK 检查，随后在 `build/functions.cmake:90` 报 `Unable to find Qt (cmd: qmake)`，configure 返回 1。
3. 从项目原 CI 所用地址下载 Qt SDK 与第三方依赖，解压进项目忽略目录；qmake 确认 Qt 5.15.2，自动识别搬迁后的项目内 prefix。
4. 加本进程 Qt bin/PREFIX，再次 configure 返回 0，Generate 完成。PortAudio、Vorbis、Ogg、libsndfile、LAME 等找到。
5. 本机未安装 JACK，配置用 `BUILD_JACK=OFF`；PortAudio/PortMidi 保留。`DOWNLOAD_SOUNDFONT=OFF` 避免额外下载，仓库已有备用 `FluidR3Mono_GM.sf3` 可用于安装。

当前编译/安装结果见本章末尾的实测结果；configure 成功不能代替主程序编译和链接成功。

## 已准备好的项目本地文件

```text
MuseScore/
  dependencies/                         # 已被上游 .gitignore 忽略
    include/、libx64/、libx86/            # 第三方库
    qt5.15.2/msvc2019_64/                # Qt SDK，含 qmake、头文件、库、plugins、qml
  msvc.build_probe_x64/                 # 忽略：构建树、下载档案与日志
    downloads/Qt5152_msvc2019_64.7z
    downloads/dependencies.7z
    msvc-script-attempt.log
    configure-attempt.log
    configure-with-qt.log
    build-debug.log
    install-debug.log
    build-helper-check-retry.log
    smoke-version.json
    smoke-pdf-retry.json
  msvc.install_probe_x64/               # 忽略：安装输出
  personal/tools/build_windows.ps1      # 跟踪：复现入口
```

这些二进制 SDK/依赖/编译产物不提交 Git。个人脚本仅设置当前进程环境并恢复，不修改系统 PATH、注册表或已有 MuseScore 安装。

| 项目 CI 包 | 本次大小 | 本次下载 SHA-256 |
| --- | --- | --- |
| [Qt5152_msvc2019_64.7z](https://s3.amazonaws.com/utils.musescore.org/Qt5152_msvc2019_64.7z) | 183,049,874 bytes（约 174.6 MiB） | `7c34c57b097ef5358f8b5c4ec018521805380ccb0cf83c84207af44889855975` |
| [dependencies.7z](https://s3.amazonaws.com/utils.musescore.org/dependencies.7z) | 6,232,524 bytes（约 5.9 MiB） | `91ab48e5f421c68d9b098357e0439efbe6e945a8bbbdf29ffaf7dfb7f1f493b8` |

SHA 是本次下载的记录，不是供应方签名。解压前检查了所有 archive entry 都在预期相对目录中；没有初始化第三方 git submodule，也没有运行 CI 的整机安装脚本。

## 在当前电脑复现

```powershell
Set-Location E:\programming\funcodes\muse3_dev\MuseScore

# 仅配置；构建目录已缓存 VS2019/x64/v142
.\personal\tools\build_windows.ps1 -ConfigureOnly

# 编译 Debug 主程序；-Install 同时组织可运行目录
.\personal\tools\build_windows.ps1 -Configuration Debug -Install
```

参数 `-Parallel` 默认 2，控制 MSBuild 并行与 MSVC `/MP`；辅助脚本通过 `_CL_` 附加编译并行参数，见 [Microsoft 的 CL 环境变量说明](https://learn.microsoft.com/en-us/cpp/build/reference/cl-environment-variables?view=msvc-170)。本机 16 个逻辑处理器，本次限制并行以便编译时仍能使用电脑。

构建前关闭这个开发目录的 MuseScore3Evo：主目标的 post-build 会复制 exe 到安装目录，即使没有 `-Install`，运行中的 exe 也会使复制失败。只编译不执行完整安装可省略 `-Install`。可用 `-Configuration RelWithDebInfo` 做优化带符号构建；该配置未在本次验证。若改用 VS 2022，传 `-Generator 'Visual Studio 17 2022'` 并指定另一个 `-BuildDirectory`，不能在原 generator cache 中切换。脚本对缺少 SDK/库/工具和不同 generator cache 有明确失败提示，不执行递归 clean。

## 在新机器补齐环境

先准备含 x64 C++/Windows SDK 的 VS 2019（v142）或 VS 2022（v143）及 CMake。本机现有 VS2019 已通过工具链探测，当前机器不需要因此重新安装。

使用 Qt 5 SDK（项目 CI 包或自己准备的 Qt 5.15.2 msvc2019_64），不能用应用目录里的 Qt DLL 代替 SDK，也不能只装 Qt Creator 或 Qt 6。若 SDK 在别处，给个人脚本传 `-QtRoot '实际的msvc2019_64目录'`。

第三方包解压到仓库根，形成 `dependencies/include` 与 `dependencies/libx64`；Qt archive 自带 `msvc2019_64` 根目录，将它解压到 `dependencies/qt5.15.2`。例如已有 7-Zip 时：

```powershell
# 下载位置可自定，以下是本次保留的档案；7z 路径按实际安装替换
$task7z = 'E:\vcpkg\downloads\tools\7zip-19.00-windows\Files\7-Zip\7z.exe'
& $task7z x .\msvc.build_probe_x64\downloads\dependencies.7z '-o.'
& $task7z x .\msvc.build_probe_x64\downloads\Qt5152_msvc2019_64.7z '-odependencies\qt5.15.2'
.\personal\tools\build_windows.ps1 -ConfigureOnly
```

安装 JACK 只有在实际需要该后端时才另处理；当前配置启用 PortAudio 音频与 PortMidi MIDI，尚未验证实际设备。

## 实测结果

| 验证 | 实际结果 |
| --- | --- |
| CMake configure / generate | 返回 0，VS2019 / x64 / v142 / Qt5.15.2 |
| 主目标完整 Debug 编译和链接 | `cmake --build ... --config Debug --target mscore --parallel 2` 返回 0 |
| Debug 安装 | `cmake --install ... --config Debug` 返回 0，DLL、Qt plugins、QML、谱例、字体、翻译、备用 SF3 等复制完成 |
| 个人脚本复验 | `build_windows.ps1 -Configuration Debug -Install` 顺序重试返回 0；首次误与运行测试并行导致 exe 复制占用，不是编译错误 |
| 安装目录版本启动 | `MuseScore3Evo.exe --version` 返回 0，输出 `MuseScore3Evo 3.7.0-Development` |
| 离屏 PDF 转换 | 生成 `smoke-Fugue_1.pdf`（93,374 bytes），重试日志显示 `... success!`，但进程退出超时；不能报告整个转换命令成功 |

主程序产物：`msvc.build_probe_x64/main/Debug/MuseScore3Evo.exe`。可启动目录：`msvc.install_probe_x64/bin/MuseScore3Evo.exe`（74,423,296 bytes）；名称是 **MuseScore3Evo.exe**，不是旧开发配置中的 MuseScore3.exe。

本机已经具备编译条件，不需要重新安装 VS。SDK、构建树、安装产物和日志留在本机忽略目录；未改应用 C++、UI、资源或上游 CMake/bat。Debug 编译含现有 warning，未作为本任务顺便修改。

运行验证的范围有限：设置 `QT_QPA_PLATFORM=offscreen`、传 `-s -c <构建树内测试设置目录> -o <测试PDF> demos/Fugue_1.mscx`，首轮等待 60 秒后超时终止。加入 `-d -m` 的 30 秒重试同样在打印成功后未退出；设置进程内 APPDATA/LOCALAPPDATA 和 `QT_QPA_FONTDIR` 的 20 秒重试也未解决。日志另有 Qt 离屏字体目录和用户 AppData 文件写入警告，不能据此确定退出超时原因。测试进程已终止。

后续运行验证建议：在普通桌面 PowerShell 启动安装目录 exe，检查界面、打开/保存谱面、PDF 导出和播放；若命令行仍打印成功后不退出，用 Debug 调试器检查 `mscore/musescore.cpp` 的 `convert()` 成功后的 score 清理及进程退出调用栈。当前未验证 GUI 交互、音频/MIDI 硬件、RelWithDebInfo/Release、CTest 或发行安装包；不要把编译成功当作这些功能已通过。
