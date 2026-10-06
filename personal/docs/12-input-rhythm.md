# 12 自动规范输入时值

个人版本 0.7.0。应用默认开启，在工具菜单“重组节奏”旁的“自动规范输入时值”切换；偏好键 `ui/score/noteEntry/autoRhythm` 持久化到应用设置，不写谱面文件。关闭仅改变后续操作，已有延音链保持原样。旧谱仍使用既有重组节奏命令。

## 入口和边界

- [inputrhythm.h](../../libmscore/inputrhythm.h) / [inputrhythm.cpp](../../libmscore/inputrhythm.cpp)：独立助手保存应用传入的策略，核心独立宿主默认旧行为；应用偏好初始化后开启。没有增加 Score 属性、文件字段或新的音乐分组算法。
- [durationtype.cpp](../../libmscore/durationtype.cpp)：复用 `toRhythmicDurationList()`。4/4 全音符、短—长—短切分保留；第二拍起附点四分拆为四分＋八分；第二拍后半起四分拆为两个八分；跨小节延音、整小节休止和复合拍子仍遵循原规则。
- [noteentry.cpp](../../libmscore/noteentry.cpp)：五线谱输入启用 `setNoteRest(..., rhythmic)`；追加和弦音高时通过 `InputRhythm::addToChain()` 为完整等音数延音链增加音高及对应延音线。光标按原总时值前进。插件外部 InputState 游标、重新定音沿原路径。
- [score.cpp](../../libmscore/score.cpp)、[cmd.cpp](../../libmscore/cmd.cpp)：输入时值按钮、应用输入状态、时值增减与选区时值缩放只整理目标及其完整延音链；多声部逐 track 处理，沿用原命令的一次 Undo。
- [pianoview.cpp](../../mscore/pianoroll/pianoview.cpp)：确认新增/绘制和局部时值调整时启用；普通粘贴、纯移动时间或音高沿原路径。内部拖动剪贴板记录原对象是否适合整理，保留复杂对象的演奏事件。不是 MSCX 新字段。
- [musescore.cpp](../../mscore/musescore.cpp)、[preferences.cpp](../../mscore/preferences.cpp)：注册默认 true 的应用偏好，工具菜单与偏好刷新同步；键声明放在应用专用 [inputrhythmpreference.h](../../mscore/inputrhythmpreference.h)，核心助手只接受布尔策略。中文翻译在 `Ms::MuseScore` 上下文。

打开、导入、普通粘贴、排版、绘制、音频回调不会触发自动全谱整理。连音符、装饰音、自定义演奏事件、震音、歌词、琶音、演奏标记、附着元素、手动符杠或范围内复杂非延音线连接保守跳过。需要特殊记谱时临时关闭开关。

## 对象寿命、撤销和连接

`normalize()` 先计算目标链期望分组并比较现状；没有变化直接返回 false，保留对象身份和 Undo 历史。需要改变时调用既有 `regroupNotesAndRests()`，范围仅为完整目标链的起止 tick 与 track。

整理前保存 InputState、数值选区和范围；整理后按 tick / track / pitch 重新定位，避免复用被替换的 Note / Segment 指针。跨谱表归属及已有外部延音连接由原分组算法保留；`edit.cpp` 修正该算法首个实际插入和弦的外部反向延音目标，并以数值位置恢复输入光标。追加和弦音高不重组已经正确的片段。

开发新入口时先明确是否属于用户明确确认的输入/时值编辑，再复用助手；不要全局把所有 `setNoteRest` 调用改成 rhythmic=true，否则导入/插件/粘贴会改变语义。新增检查应在重组前进行，不能在失效对象上继续访问属性。

## 验证入口

- [tst_inputrhythm](../../mtest/libmscore/inputrhythm/tst_inputrhythm.cpp)：4/4 示例、2/4、3/4、6/8、9/8、12/8，休止、总播放时长、光标、追加和弦音、一次撤销/重做、保存重开、局部修改、跨谱表、复杂对象、分谱链接、外部延音和插件游标。
- [tst_pluginhost](../../mtest/mscore/pluginhost/tst_pluginhost.cpp)：真实主窗口开关/设置写盘、卷帘新增与普通粘贴、撤销/重做，和声助手既有 GUI 回归。
- 既有 `tst_rhythmicGrouping`、`tst_note`、`tst_scoreobserver`：手动节奏重组黄金谱、音符行为及观察功能回归。

本机复用独立 0.6 构建树，保持 CMAKE_INSTALL_PREFIX 不变，避免 Windows 生成 config.h 触发全库重编；完成后用 `cmake --install msvc.build_personal_0_6_x64 --config Release --prefix <绝对0.7安装目录>` 安装。Windows 资源位置相对于可执行文件，安装覆盖参数可用。测试采用相同 SDK 的 DLL/字体/QML；PCH 丢失时使用进程级 `/Y- /MP2`，不修改上游构建默认。安装保留旧程序和音源。

实际构建、测试结果及尚未验收边界记录在 [更新日志](../CHANGELOG.md)，不把测试代码存在等同于测试通过。

2026-10-06 实测：x64 Release 主程序构建/链接/独立安装；输入时值 24、手动重组 10、音符 11、观察 14、真实 GUI 7 项通过，0 失败/跳过。MIDI 音符事件与未拆分参考谱一致。GUI 清理后连续三次退出码 0。手动黄金谱保持不变。运行 helper 设置 `MTEST_DIFF_DIR=E:/Git/usr/bin`，否则本机默认 Vim diff 不支持 --strip-trailing-cr；observer 配置使用隔离临时目录。普通 core 测试的全局 stub 与实际宿主冲突时，使用既有 MTEST_LINK_MSCOREAPP，不改应用行为。

应用在 `msvc.install_personal_0_7_x64/bin/MuseScore3Evo.exe`；日志在忽略构建树的 rhythm-core / rhythm-manual / rhythm-note / rhythm-observer / rhythm-gui 的 gui.txt 及 rhythm-final-build.log、rhythm-tests-build.log、rhythm-final-install.log。尚无人工长期操作或真实 MIDI/音频硬件验收；大音源限制另见 [11](11-large-sf2.md)。
