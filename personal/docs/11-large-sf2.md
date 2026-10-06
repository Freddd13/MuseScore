# 11 大型 SF2：容量、预加载与验证

个人版本 0.6.0；上游应用仍为 3.7.0。目标是标准 SF2 的大文件支持，首个真实样本为用户本机 `E:\music_fonts\VSL D-274 V2025A.sf2`；音源原文件只读，不提交、不复制到发布仓库。

## 根因与修改边界

该文件 2,789,995,618 bytes，约 2.598 GiB，RIFF 大小一致；smpl 为 2,789,809,516 bytes。旧 `FSKIP(int)` 在 Windows 转成 -1,505,157,780，随后 `safe_fseek(long)` 跳到负位置，导致加载失败。Windows x64 的 long 仍是 32 位，换成 x64 程序本身不能解决。

- [sfont.h](../../audio/midi/fluid/sfont.h) / [sfont.cpp](../../audio/midi/fluid/sfont.cpp)：文件位置、字节长度和跳转用 qint64；RIFF/采样头中的 unsigned 32-bit 字段保持原格式。校验 LIST/子块/读取/跳转边界、采样单位（SF2 每帧 2 bytes）及引用索引。
- `Sample::load()`：偏移先转宽类型再算，使用临时缓冲区、分段读取和取消检查；完整成功后才改变 data 与文件相对位置。失败可重试，分配失败返回原因。
- [sfont3.cpp](../../audio/midi/fluid/sfont3.cpp)：保留原 Vorbis 解码，验证解码大小并在成功后提交缓冲区；Qt 5 单个压缩 QByteArray 仍有 int 长度限制，不承诺巨大单个压缩采样。
- [fluid.cpp](../../audio/midi/fluid/fluid.cpp)：SF2 文件 >= 2 GiB 时，读完元数据后调用 `SFont::preloadSamples()`，再发布到音源列表。按文件顺序加载预置引用采样，相同 Sample 只驻留一份；小音源保持原按预置加载行为。
- [fluidgui.cpp](../../audio/midi/fluid/fluidgui.cpp)：现有异步加载入口先停止播放、重置取消；失败提示带具体原因。进度/取消/终止字段为原子类型。新音源添加失败不污染已加载列表；既有批量替换不是事务式回滚，未顺便重构。

不替换 Fluid，不增加音频线程磁盘读取或流式采样，不修改 Voice DSP。标准 RIFF 32-bit 块长的约 4 GiB 边界保留；非标准超界音源需另外分析，不能删除格式检查强行接受。

## 使用与性能边界

将音源所在目录加入 MuseScore 的音源搜索目录，再在合成器面板添加。大文件会等待预加载并显示进度，可取消；完成后在 Mixer 选择对应音色。用户样本仅一个有效预置（bank 0 / program 0）、2640 个采样，PCM 合计约 2.598 GiB；采样驻留会占内存，不是只缓存 185 KiB 元数据。

32 GiB 机器具备尝试条件，但剩余内存、Windows 换页、复音数、踏板和多层采样决定实际稳定性。不限制或自动削减音源精度，不承诺任意文件/机器零卡顿。软合成回调耗时测试与真实声卡 underrun 是不同证据。

## 回归入口

[tst_sfloader](../../mtest/audio/sfloader/tst_sfloader.cpp) 使用实际加载器与 renderer，生成微型有效 SF2 和跨 2 GiB 稀疏夹具；夹具位于当前构建目录下临时子目录，结束自动清理。覆盖有效数据、自动预加载、边界损坏、失败保留旧列表、取消重试、短采样读取、既有 SF3，以及 Windows Job 进程内存限额下真实分配失败。

```powershell
# 主程序/测试独立构建与安装树
.\personal\tools\build_windows.ps1 -Configuration Release -BuildDirectory msvc.build_personal_0_6_x64 -InstallDirectory msvc.install_personal_0_6_x64 -Install
cmake --build msvc.build_personal_0_6_x64 --config Release --target ms_pch --parallel 2
MSBuild msvc.build_personal_0_6_x64/mtest/mtest.sln /t:tst_sfloader /p:Configuration=Release /p:Platform=x64 /m:2
# 本机共享 PCH 重建后仍会被清理时，参照 10 中 /Y- 与 BuildProjectReferences=false 的处理。
# tst_sfloadergui 另测真实 GUI 异步进度和取消；设置 SFLOADER_REAL_FONT 为本机原音源路径。

# 测试 DLL 用同 Qt SDK/项目依赖或完整安装目录；从忽略的构建目录运行
# tst_sfloader.exe --benchmark 'E:\music_fonts\VSL D-274 V2025A.sf2' 600
```

基准模式生成 `sf2-preview.wav`（低/中/高音、不同力度），测试 16/32/64 键持续音及十分钟按实时块间隔的钢琴演奏，记录加载耗时、private bytes、声部数、回调耗时、软件 deadline miss、热阶段进程 read I/O 和卸载内存回收。原始音源文件不会写入。执行时仍需实际声卡/人工听音验收，不把软件块测试当硬件零 underrun。

## 本次实测

2026-10-06，VS2019 v142 / Qt 5.15.2 / x64 Release：主程序编译、链接、独立安装、版本启动通过；加载器 suite 10 项通过，0 失败/跳过。测试 PCH 在独立 mtest 生成后再次丢失，采用进程级 `/Y- /MP2`，不改上游默认。混合旧 PCH 对象导致 audio.lib LNK2011 时单独 Rebuild 音频库，再链接通过。真实 GUI 异步 suite 3 项通过，0 失败/跳过：等进度窗可见后取消、旧音源保留、再次成功加载、事件循环心跳及卸载。首次测试定时器取消了尚未显示的窗口，改为等待实际进度窗后验证；生产取消路径无需额外改动。

真实 VSL 的有记录基准：预加载 2,838 ms，private bytes 增量 2,807,431,168 bytes（2.615 GiB）；当时进程峰值 private commit 约 2.626 GiB。六秒 48 kHz 立体声 WAV 的低/中/高音区与三个力度均非零、有限，无削波；无人工听音结论。实际安装程序 CLI 导出钢琴谱 WAV（44.1 kHz / stereo / PCM16，162,816 frames，peak 32,439）返回 0；相同谱面改成不存在的音源得到不同 WAV，排除默认音源回退，证据在 vsl-export/result.json。

| 持续键数（踏板） | 合成声部 | P99 回调 | 最长回调 |
| --- | --- | --- | --- |
| 16 | 32 | 253 µs | 402 µs |
| 32 | 64 | 465 µs | 618 µs |
| 64 | 128 | 911 µs | 1,128 µs |

十分钟按实时 512-frame / 48 kHz 块间隔演奏完成，热阶段进程 read I/O 为 **0 operations / 0 bytes**。有记录轮次出现 **3 次软件 deadline miss**，最长 14.524 ms，超过 10.667 ms 块期限；返回码 5，严格稳定性项未通过。此前一轮完整十分钟返回码 0，但 Windows GUI 子系统使默认 Qt 日志未落盘，未用于表格性能数值。不能据此宣称真实声卡零 underrun；尖峰原因未用调度/换页跟踪定位，也不能仅据加载成功断言性能通过。此分支 PortAudio 使用自动缓冲大小（Pa_OpenStream framesPerBuffer=0）；若声卡控制面板支持，可先试 1024-frame 缓冲验收，应用没有新增缓冲设置，仍需人工听音、真实音频 Driver underrun 和 Seq 停止/跳转验证。

第一次卸载释放 private bytes 2,806,112,256；再加载/卸载两轮 3,614 / 3,604 ms，卸载后分别约 13.71 / 13.24 MiB，无逐轮累积 2.6 GiB。原音源的循环边界沿既有修复规则报告大量修复消息，没有删除格式校验或修改原文件。

日志、WAV 在忽略目录 `msvc.build_personal_0_6_x64/`：`sfloader-tests.txt`、`vsl-benchmark.txt`、`vsl-process-peak.json`、`sf2-preview.wav`、`sf-isolated-final-build.log`、`sf-isolated-final-install.log`。独立 0.6.0 程序在 `msvc.install_personal_0_6_x64/bin/MuseScore3Evo.exe`；Qt5QmlModels / Qt5QmlWorkerScript 同 SDK DLL 已补齐。后续 0.7.0 使用同一独立构建树增量构建，安装到另一个目录，保留 0.6.0 产物和此前安装。
