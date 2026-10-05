# 10 通用乐谱观察与临时音符预览（个人版本 0.4.0）

和声助手采用插件 + 最小原生接口。纯插件可完成和弦解释、级数、功能标签和界面，但标准 API 没有逐拍播放信号和独立临时色层；反复写 Note.color 会影响撤销、保存与排版。因此原生侧只提供可供其他插件复用的查询/预览能力，音乐解释留在独立 QML/JS。

## 模块与合并点

新增 [scoreobserver.h](../../mscore/plugin/api/scoreobserver.h)、[scoreobserver.cpp](../../mscore/plugin/api/scoreobserver.cpp)，由 `PluginAPI::newScoreObserver()` 创建并以插件为 QObject parent。[notepreview.h](../../mscore/notepreview.h) 是无模型修改的图层助手。已有文件只接入插件工厂/CMake、ScoreView 的屏幕绘制与清理、Note 的可指定颜色 draw 重载；默认 draw 仍沿原路径。

插件发布快照位于 [share/plugins/HarmonyAssistant](../../share/plugins/HarmonyAssistant/README.md)，由现有 share 的递归安装规则安装；独立插件工作区是日常编辑源。新增运行组件仅属于 HarmonyAssistant，没有修改其他插件。核心未新增颜色属性、序列化字段或音频线程职责。后续合并上游优先复核 `Note::draw`、`ScoreView::drawElements/setScore/onElementDestruction` 和插件工厂的少量接线。

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

## 0.3.0：上下文、全谱图层、配置与停靠

`contextSnapshot(tick, firstTrack, endTrack, pedal, windowTicks, sounding)` 保留原 snapshot 的 notes，另返回 analysisNotes 与 fingerprint。延音线沿 tie 链延长；踏板 windows 从 Pedal/Spanner 得到，作用于所属乐器全部 track；抬踏板移除已结束音。非踏板窗口最多 1920 ticks，限当前小节、声部休止截断。同音高保留最近定位/拼写，播放真实 NoteEvent 优先。这是记谱状态解释，不是声学衰减或 Synthesia 算法复刻。

`analysisFrames(fromTick, limit, firstTrack, endTrack, pedal, windowTicks)` 按事件起止、踏板端点和小节边界返回最多 128 帧，nextTick=-1 表示结束。插件实际每 12 ms 分批最多 32 帧；播放时不重新分析全谱。fingerprint 是当前范围的数值音符定位/时值/调号与踏板摘要 SHA256，用于阻止导入结果应用到不匹配的谱面。音乐检测继续由 ES5 插件完成，使用预计算十二位掩码模板；新增配置和交换逻辑分别在 Preferences.js、Analysis.js。

`setScorePreview(descriptors)` 提供独立的全谱底层；`setNotePreviewColors` 保留当前层用途并支持 label/chord/active。`clearNotePreviewColors` 仅清当前层，`clearAllPreviews` 清两层；隐藏、停用、换谱、销毁都清全部。文字用 note 为锚点，在 page 空间索引里避让记谱元素与同批标签；和弦位于乐器顶谱表上方。拥挤时省略，不能保证每种特殊排版都能放下所有文字。屏幕标签不修改布局、undo 或保存内容，打印/foto 不显示。GUI 播放高亮用浅蓝底，复用未改变的底层几何，不开启动画 timer。

临时层使用 Element 指针作不透明键；ScoreView::onElementDestruction 必须只比较地址。回调来自 Element 析构，调用 e->isNote()/type() 会触发纯虚函数终止；这是 0.2.0 的崩溃根因，Windows 转储与链接函数映射定位到此调用链。实际插件宿主测试会在 native ScoreView 存在时创建 QML ScoreView、触发 doLayout，再弹出菜单、加载插件、顶部/侧边/悬浮切换和反复关闭。

QmlPlugin::attachPanelDock 只持有已有 QDockWidget 的 QPointer；panelPlacement/panelFloating 和 setPanelFloating 供插件自适应。可选 QML preferredRibbonHeight 提示只作用于声明该属性的插件，顶部/底部调整高度；其他插件延续原行为。PluginManager 的递归 QDirIterator 本来已遍历子目录，移除额外递归调用，扫描结果保持一致而避免重复加载成本。

ScoreObserver 的 loadConfiguration/saveConfiguration 按无路径分隔符的名字存 JSON，跟随应用 dataPath（包括 -c 和便携模式）。writeTextFile 用 QSaveFile 原子写入，readTextFile 限 16 MiB、接受本地 file URL。乐谱交换格式/schema/指纹/值范围校验全部在插件，主程序不承担和弦 JSON 语义。

性能边界：上下文小节起点用数字索引二分查找；超过 64 音符的预览批次一次扫描 segments 建局部解析表，防止 tick2measure 逐音线性扫描。临时原生指针不跨这次 GUI 调用缓存。首次索引、全谱检测和初始标注仍有成本，播放/隐藏/插件未开启时不运行全谱任务；不能承诺所有工程零开销或 DAW 硬实时。

新增回归：tst_scoreobserver 的踏板起落/范围、分批帧、琶音/休止、原子配置；tst_pluginhost 的真实主窗口/插件菜单/停靠/析构。测试采用独立目录、关闭硬件音序器，Qt Windows 渲染用于布局截图；不能替代实际音频/MIDI 设备和长时间编辑验收。最终测量与通过项见 CHANGELOG。

## 0.4.0：固定记号、样式与点击

描述符可含 chord / degree、chordTick / chordUntil（半开区间）、chordOrder（0–3）、chordScale（0.6–2）、chordFont、chordColor / highlightColor / highlightBackground。输入长度和数值范围受限，空字体采用 score 的 chordSymbolAFontFace。同系统/乐器用共同顶部行；固定标记在 ScoreView::paint 独立绘制，不依赖当前 Note 的 draw，不改模型排版。音旁 label 保留原有避让规则。过密标注省略，不无限上移。

setActiveScorePreview(tick) 在每乐器有序标记索引二分查当前区间，仅改变小型 QSet，重绘旧/新标记；tick=-1 清高亮。不要把当前 chord 重复附到每个活动音符。固定 base 层与当前音符层分别维护；打印/foto 沿原语义。

命中 ScoreView::activateNotePreview 后向弱 QObject 接收者调用 activatePreview，发出 previewActivated(tick, partStartTrack)。events.cpp 仅在左键双击命中时消费事件。插件 focusPanel() 聚焦已有停靠窗口，不强制悬浮；和声插件决定停止时选位置、横条打开详情，播放中不改变播放位置。QPointer 防止销毁回调；Element 键仍禁止解引用。

真实 GUI suite 验证固定行、普通鼠标双击、低音锚点整乐器跳转和颜色对话框。新增 fixedMarkerHighlightAndStyle / fixedMarkerHighlightPerformance；已测性能与边界以 CHANGELOG 0.4.0 为准。
