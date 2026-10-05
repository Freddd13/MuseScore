# 06 构建、测试与调试指南

本章来自首次调查的构建脚本静态分析，首次调查没有编译应用；历史环境见 [08](08-baseline-and-limits.md)。后续 2026-10-05 的实际配置、编译与本机复现入口见 [09 Windows 构建实测](09-windows-build-check.md)，当前电脑优先按 09 操作。

文档导航校验工具仅需 Python 3.7+ 的标准库，已在本机 Python 3.7.10 运行；不依赖 Qt 或额外 Python 包。

## 当前构建事实

- 顶层 [CMakeLists.txt](../../CMakeLists.txt)：最低 CMake 3.16；启用 qmake 检查，缺少 PATH 中的 qmake 会早期失败。
- Qt 5：`SCRIPT_INTERFACE` 在顶层设 TRUE，版本门槛写为 5.8.0；这只是声明的最低值，不证明当前分支所有代码在 5.8 可编译。Windows x64 CI 脚本使用 Qt 5.15.2 / msvc2019_64。
- 编译选项：MSVC 为 `/std:c++20`；其他平台分支有 `-std=c++20` / `-std=c++2a`。不要按早期 MuseScore 3 的 C++11 假设写新代码。
- [FindQt5.cmake](../../build/FindQt5.cmake) 收集 Core/Gui/Widgets/Qml/Quick/QuickControls2/QuickWidgets/Xml/XmlPatterns/PrintSupport/Concurrent/Test/OpenGL 等，Windows 另含 WinExtras。
- `config.cmake` 管应用版本；`MUSESCORE_BUILD_CONFIG` 默认 dev，具体 [dev.cmake](../../build/config/dev.cmake) 标 devel/unstable；该参数与 Debug/Release 编译类型独立。
- `DOWNLOAD_SOUNDFONT` 默认 ON，首次 configure 可能下载音色库；HAS_AUDIOFILE、SOUNDFONT3、BUILD_LAME、音频后端等影响依赖与能力。
- `mtest` 通常 `EXCLUDE_FROM_ALL`，默认 `mscore` 构建成功不代表测试可执行文件已生成。Xcode Debug 有特殊处理。

## Windows 路径与准备

优先读取 [msvc_build.bat](../../msvc_build.bat) 与 [build/ci/windows/setup.bat](../../build/ci/windows/setup.bat)、[build.bat](../../build/ci/windows/build.bat)，再准备 VS 的 C++ 工具链、匹配架构/编译器的 Qt、依赖库。CI 下载地址是脚本中的历史配置，不保证此刻在线可用；本次没有执行这些安装脚本。

```powershell
# 在 MuseScore 仓库中，准备好依赖后先确认
Get-Command cmake, qmake, ctest, git
qmake --version
Test-Path .\dependencies
```

Qt 的 bin 需要进入 PATH，CMAKE_PREFIX_PATH 不能代替顶层的 qmake 查找。例如下列路径应替换成实际安装路径后再用：

```powershell
$env:PATH = 'C:\Qt\5.15.2\msvc2019_64\bin;' + $env:PATH
# dependencies 的具体位置应与现有构建脚本/CMake 配置一致
.\msvc_build.bat debug
.\msvc_build.bat installdebug
```

脚本会检测 VS 2022/2019/2017，默认 x64，输出 `msvc.build_x64` / `msvc.install_x64`；第二参数 `32` 可选 x86。已有 CMakeCache 会被复用；改编译选项应先检查 cache，再用独立配置目录或定向重配。

需要精确控制时可参考脚本等价的显式配置，前提是 VS 2022 与依赖确实可用：

```powershell
cmake -S . -B msvc.build_x64 -G 'Visual Studio 17 2022' -A x64 `
  -DCMAKE_BUILD_TYPE=Debug -DMUSESCORE_BUILD_CONFIG=dev -DBUILD_64=ON `
  -DCMAKE_INSTALL_PREFIX=../msvc.install_x64
cmake --build msvc.build_x64 --config Debug --target mscore
cmake --install msvc.build_x64 --config Debug
```

不要机械复制 `CMakeSettings.json` 的 generator 字符串作为新命令；它是 IDE 配置，应以检测出的 generator 和实际 CMake 行为为准。脚本中的 clean 会递归删除多目录，不能作为“顺手试一试”的默认步骤；先查明缓存问题与目标路径。

## 测试选择与运行

[mtest/CMakeLists.txt](../../mtest/CMakeLists.txt) 注册 suite，[CreateMtestTarget.cmake](../../mtest/CreateMtestTarget.cmake) 定义 QTest 可执行文件、Qt 资源和 add_test。fixture 通常是 `.mscx` / `-ref.mscx`，MTest helpers 在 [testutils.h](../../mtest/testutils.h)。

准备好 Debug 构建/安装后，示例只构建并运行有关 suite：

```powershell
cmake --build msvc.build_x64 --config Debug --target tst_beam tst_splitstaff
ctest --test-dir msvc.build_x64/mtest -C Debug -N
ctest --test-dir msvc.build_x64/mtest -C Debug -R '^(tst_beam|tst_splitstaff)$' --output-on-failure
```

`-N` 应显示预期用例；否则检查生成目录，不能以“没有找到测试”当通过。Windows 测试可能需要安装 bin 在 PATH、diff 工具、workspace/font 资源和对应 DLL。需要按原 [mtest/README](../../mtest/README.md) 做测试子项目的 build/install 时：

```powershell
$env:PATH = (Join-Path (Get-Location) 'msvc.install_x64\bin') + ';' + $env:PATH
Push-Location .\msvc.build_x64\mtest
cmake --build . --config Debug
cmake --build . --config Debug --target INSTALL
ctest -C Debug --output-on-failure
Pop-Location
```

不要为一处小改动默认构建/运行全部大型测试；按核心受影响行为选 suite，再因失败/新增风险扩展。文档、导航索引等低影响修改只需校验引用与 diff，不需要新增应用单元测试。

## 应选择哪种验证

| 修改 | 适合的验证 |
| --- | --- |
| 用户编辑规则/结构 | 小谱例 → 操作后比参考 → undo/redo → 保存重开；总谱/分谱按需 |
| 元素属性/序列化 | `writeReadElement` / MSCX/MSCZ 往返，缺字段默认值、reset、链接属性 |
| 音高/声部/节奏 | note/transpose/tuplet/exchangevoices/copypaste 等邻近 suite，检查实际记谱结果 |
| 排版/符号位置 | vtest 或最小谱例的 PNG/PDF 可视对比，多页/不同 layout mode |
| 播放/踏板/力度 | MIDI 事件与实际听音，反复/跳转/停止；离线导出若受影响也测 |
| GUI/卷帘/键盘 | 实际运行交互、焦点、方向、停靠、缩放、切文件、撤销与设置保存 |
| 文档/索引 | `python personal/tools/check_guides.py`、`git diff --check` |

新增测试应验证可观察的音乐行为，不仅复制算法内部步骤。不要批量刷新参考文件来消除失败；先理解差异来源。`-t` 保存的测试谱省略平台/版本等信息，并可能含布局追踪数据，详见原 mtest README。

## 已知测试限制

- `mtest/testscript/tst_runscripts.cpp::runTestScripts()` 基准含 `QSKIP`，理由是 OpenGL context 创建问题；suite 存在不等于脚本已执行。
- `mtest/CMakeLists.txt` 中 midimapping、text、album、testoves 等有注释掉的项，一些 suite 也有内部跳过。以 CTest/QTest 实际输出为准。
- 卷帘基准没有独立注册的 piano roll suite；邻近核心测试不能替代 GUI 验证。
- [vtest/README](../../vtest/README.md) 与脚本有历史路径说明；Windows 脚本需 ImageMagick `compare` 和正确的 MuseScore 安装位置，先读 `gen.bat` 后执行。
- [build/run_tests.sh](../../build/run_tests.sh) 是 Linux/xvfb 流程，还包含视觉测试、merge marker 检查，不是 Windows 原生命令。

## 调试与资源

[.vscode/launch.json](../../.vscode/launch.json) 的 Windows 默认程序指向 `msvc.install_x64/bin/MuseScore3.exe`，参数 `-d`；这是配置值，应检查实际生成的可执行名称。新程序路径与安装/运行资源不能混淆。命令行转换/批处理仍从 runApplication/parseCommandLineArguments 进入，不是独立无 GUI 核心进程。

调试一个编辑动作可依次在 `MuseScore::cmd`、`ScoreView::cmd`、具体核心命令、`UndoStack::push`、`Score::endCmd` 设断点；绘制问题加 `doLayoutRange` 与对应元素 layout；播放问题加 renderer/Seq 的事件路径，但不要随意阻塞实时音频回调。

## 观察器回归（0.2.0）

新增 `tst_scoreobserver` 使用完整 mscoreapp，覆盖持续音、撤销、保存和屏幕预览隔离、长谱缓存。Windows 测试需独立构建 `ms_pch` 后编译 mtest.sln；Release 运行时配置见 [10](10-score-observer.md)。相关 `tst_note` 的 Win32 Chord 名称保护已扩展到 MSVC。

## 0.3.0 GUI 与上下文回归

新增 tst_pluginhost 验证真实主窗口的菜单、谱面重排、原生 QML 面板和停靠；它关闭硬件音序器，使用临时应用设置，不能替代实际音频设备验收。Windows 测试需要安装资源和 Qt QML/platform 目录；编译测试后运行 `python personal/tools/test_harmony_gui.py <tst_pluginhost.exe> <安装根目录> <输出目录>`，脚本准备隔离运行时并限时 45 秒。直接在原 build 子目录执行时 Qt 资源路径可能不完整。原生 QML 截图用 grabWindow，Widgets grab 不包含嵌入窗口。

tst_scoreobserver 增加踏板边界、时序聚合/休止、全谱帧与原子配置测试；tst_note 保持原测试内容。实际安装插件 smoke 可用 `python personal/tools/test_harmony_host.py <新程序exe> <独立目录> <安装目录内主QML>`；先创建 -c 目录再启动，避免默认设置回退。通过项、性能与限制见个人更新日志 0.3.0。
