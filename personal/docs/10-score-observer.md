# 10 通用乐谱观察与临时音符预览（个人版本 0.2.0）

和声助手采用插件 + 最小原生接口。纯插件可完成和弦解释、级数、功能标签和界面，但标准 API 没有逐拍播放信号和独立临时色层；反复写 Note.color 会影响撤销、保存与排版。因此原生侧只提供可供其他插件复用的查询/预览能力，音乐解释留在独立 QML/JS。

## 模块与合并点

新增 [scoreobserver.h](../../mscore/plugin/api/scoreobserver.h)、[scoreobserver.cpp](../../mscore/plugin/api/scoreobserver.cpp)，由 `PluginAPI::newScoreObserver()` 创建并以插件为 QObject parent。[notepreview.h](../../mscore/notepreview.h) 是无模型修改的图层助手。已有文件只接入插件工厂/CMake、ScoreView 的屏幕绘制与清理、Note 的可指定颜色 draw 重载；默认 draw 仍沿原路径。

插件发布快照位于 [share/plugins/HarmonyAssistant](../../share/plugins/HarmonyAssistant/README.md)，由现有 share 的递归安装规则安装；独立插件工作区是日常编辑源。除新增四个插件运行文件，没有修改其他插件。核心未新增颜色属性、序列化字段或音频线程职责。后续合并上游优先复核 `Note::draw`、`ScoreView::drawElements/setScore/onElementDestruction` 和插件工厂的少量接线。

## 接口约定

```qml
var observer = newScoreObserver()
observer.score = curScore
var data = observer.snapshot(tick, firstTrack, endTrack, false)
// data.notes 中的描述符复制后添加 color，交给屏幕预览。
observer.setNotePreviewColors(coloredDescriptors)
observer.clearNotePreviewColors()
```

`score`、`enabled` 可写；`tick`、`playing`、`surfaceVisible`、`indexBuildCount`、`indexBuildMilliseconds` 只读。位置通过 `positionChanged` 通知。tick 是记谱 tick，track 范围是半开区间，边界自动限于当前 score。返回 notes、keySignature、bar、beat、tick；bar/beat 从 1 开始。描述符为 tick/track/index/pitch/tpc/writtenPitch/writtenTpc 数值，不缓存插件 Note 包装指针。

`snapshot(..., false)` 使用按 MasterScore 内容状态及范围失效的每声部索引，二分求当前位置持续音。`snapshot(..., true)` 在播放时读取 Seq 已有 GUI 侧 `activeNoteEvents()`；支持 NoteEvent 音高偏移，并把主谱音符投射到当前分谱。NoteEvent 对应的当前原音用 writtenPitch/TPC 校验后才能着色。Note-off 后的踏板声学残响不纳入集合；算法本身由插件解释。

连接 Seq 的 GUI 心跳、started/stopped 和 MasterScore 的 posChanged，不新增音频锁、计时器或 Driver 调用。隐藏 QQuickWindow 自动清层并停止位置通知，surfaceVisible 通知插件停止自己的计时。插件销毁/换谱、视图换谱、音符销毁均安全清理；QPointer 保护先关闭的谱面/视图。

## 屏幕预览

颜色图层按 owner 隔离，后注册层优先。相同描述符不会重复重绘，只重绘旧/新音符包围框的并集。原生编辑选中颜色、不可见音符和音域提示继续保留；播放标记另用细线。打印/PDF/图片导出沿默认 draw，截图模式也禁用色层。用户音符颜色、score 内容状态和 undo 不变，不需要保存前恢复。

## 验证与复现

新增 [tst_scoreobserver.cpp](../../mtest/mscore/scoreobserver/tst_scoreobserver.cpp) 及小谱例，使用完整 mscoreapp 测试库。测试包含 1000 小节/6000 音符、索引计时与缓存查询；预览前后 MSCX 字节一致，Note 默认绘制不变，撤销后数值索引变化并恢复。相邻 `tst_note` 验证原有音符行为。

Windows 回归需先构建共享 PCH（独立 mtest.sln 不自动构建它），再编译目标：

```powershell
cmake --build msvc.build_harmony_release_x64 --config Release --target ms_pch --parallel 1
# 使用 VS2019 的 MSBuild（Build Tools shell 或 vswhere 查到的路径）
MSBuild msvc.build_harmony_release_x64/mtest/mtest.sln /t:tst_scoreobserver`;tst_note /p:Configuration=Release /p:Platform=x64 /m:1
```

运行时把 SDK 的 `bin` 加入当前进程 PATH，并设置 `QT_QPA_PLATFORM=offscreen`、`QT_PLUGIN_PATH=<QtSDK>/plugins`；直接运行各目标的 `Release/*.exe` 或使用 CTest。完整 Qt 测试运行时与构建配置需一致，不能混用 Debug 与 Release Qt DLL。新增 `testutils -> freetype` 仅解决 testutils 直接包含 sym.h 的头依赖；`tst_note` 的 Windows Chord 名称保护扩展到 MSVC，没有改变应用行为。

真实程序的插件加载/缓存/颜色属性隔离用独立设置、CLI `-p` 和测试插件验证。最终通过项、性能、未验证的实际设备及运行环境限制以 [更新日志](../CHANGELOG.md) 为准。源码检查/模拟事件/邻近测试不能替代全部实际音频和编辑交互验收。

### 本机已通过的运行与缓存绕行

2026-10-05：新原生 suite 8/8、既有音符 suite 11/11、逻辑 JS 和真实 Release 插件加载全部通过。长谱首次索引 10.13 ms，1000 次缓存查询 1.54 ms。

这台电脑 MSVC/CMake 的共享 PCH 在重建后仍被清理，测试最终用进程变量 `$env:_CL_='/Y-'` 编译（不修改应用源码或系统环境）；依赖库先完成后，对各测试 `.vcxproj` 使用 `/p:BuildProjectReferences=false`。既有测试还必须在进程 PATH 中包含 Git 的 `usr/bin`，以提供 GNU diff，不能用 PowerShell 的 diff 别名代替。缺少 diff 时首次五项比对失败，补齐后 11 项通过，未更改参考谱。

可独立复现 JS 与真实宿主回归：

```powershell
node mtest/mscore/scoreobserver/test-harmony.cjs share/plugins/HarmonyAssistant/Harmony.js
python personal/tools/test_harmony_host.py msvc.install_harmony_release_x64/bin/MuseScore3Evo.exe msvc.build_harmony_release_x64/harmony-smoke-final
```

测试输出在忽略的构建目录。实际音频/MIDI 硬件和长时间交互未验收；离屏验证不代表 DAW 硬实时承诺。
