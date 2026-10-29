# D2：左右手记号的位置、操作和保存

2026-10-29。0.136.0已完成本批软件检查。

## 对照和复现

- 只读查看Black Catcher第4页及Shirushi第3页，并放大右手、左手的短横和结束记号。记录在P2/out/hands136-reference/sources.json，原PDF的SHA256保持不变；官谱图像仅用于本地阅读。
- 新加的自动记号保存后，没有区分当前摆放与旧版坐标。补齐重新读取后的排版步骤，32种位置仍全部偏移，确认了保存问题。
- 修正重开后再改音的检查，14种摆放未随最终符干／符杠更新。现在先完成这些对象，再在当前排版范围确定分手位置、更新相应形状。
- “还原基础折线”和单项复位原来采用旧文件的缺省长度。现在分别读取现行基础形状和个人默认长度；文件中的旧值继续按原方式读取。
- 检查窗口切换时出现绘图警告，调用栈指向MIconEngine收到空尺寸后建立画布。补齐独立icons目标的编译、重新链接主程序和测试程序后回查。

## 实际操作和结果

- hands136-formal-editor：58 passed, 0 failed, 0 skipped, 0 blacklisted, 16462ms。
- hands136-formal-midi：8 passed, 0 failed, 0 skipped, 0 blacklisted, 16944ms。
- hands136-formal-tap：29 passed, 0 failed, 0 skipped, 0 blacklisted, 3021ms。
- hands136-formal-host：4 passed, 0 failed, 0 skipped, 0 blacklisted, 33560ms。

- 32种位置包括四方向的线、间、上下加线、加间、和弦内音和跨谱表。核对所指音、短横间距、谱间位置，连续排版、保存重开、改音、撤销及实际MIDI保持。
- 四种旧字形都从检视器实际点击转换，外形中心及大小差在0.15谱间内；转换一次撤销，重做恢复。另用旧版字段的文件检查四方向重新开启自动布局、保存和重开。
- 实际点击保存默认形状，四个方向采用同样的长度、粗细和镜像方向。基础形状恢复为横0.6、竖2.5谱间；两个单项箭头恢复个人1.2、3谱间设置，检查撤销重做。
- 现有检视器继续检查拖长度、改数字、反向、取消、播放时排队、四种默认形状及文字独立拖动。表情文字样式与字体／字号沿用原有操作。
- 零尺寸和负尺寸的图标请求返回空图，16×16 SVG仍绘出指定颜色。正式四组日志中没有QPainter绘图警告。
- 回查按左右手导出、嵌套标记、文字分手、跨谱表、延音后段、普通MIDI路径；两个4/4输入、六种rit.范围、Tap、圆滑线端点和停播后面板继续检查。
- 已查看正常大小的位置图和检视器图。源码中的排版与图标接线分别保存在插件仓库Repairs/patches/hand-layout-after-beams.patch和icon-empty-size.patch，宿主中各保留少量接线改动。

## 记录与后续

复现记录：hands136-matrix-layout、hands136-placement-fixed、hands136-final-position、hands136-controls-and-position及图标调用栈hands136-paint-trace.txt。最初检查脚本补齐了重排版和复选框取点，产品问题以此后的结果为依据。正式结果见hands136-formal-checks.json。

下一项D3圆滑线与其它演奏记号，随后D4字体／锁行、E的AI资料与交互，最后按F完成真实设备及连续编曲使用。

正式程序已安装：PDF 7446字节，MIDI 181字节、6音。个人配置、Qt绘图组件、MCP及Codex配置保持安装前内容。
