# 0.157个人稳定版：打开与更新

**0.157.0 · Kumo branch / Freddd13 · Windows x64**

本版在0.156基础上修复音源恢复后的面板更新，以及启动时重复的谱行锁定菜单。CFX按钮、窗口拖动、焦点、Compact预设及SF2切回已经实际操作核对。

## 打开

本机原目录已更新到0.157，继续使用原位置：`E:\programming\funcodes\muse3_dev\MuseScore\msvc.install_repair_current_x64\bin\MuseScore3Evo.exe`。

独立包：完整解压`MuseScore-Kumo-0.157.0-Windows-x64.zip`，双击其中的`bin/MuseScore3Evo.exe`。在“帮助 → 关于”查看0.157.0，完整说明从“帮助 → 个人功能说明”打开。

已有布局、个人设置及谱文件沿用。CFX、Pianoteq继续使用本机安装的音源；包内带默认SF3、媒体模块、VST宿主和MCP2.7.2。

## 连接Codex

原安装位置更新后继续使用已有配置。独立包放到新位置时，打开“视图 → AI连接”，点“配置Codex”，重新连接musescore，再点“检查连接”核对当前谱名。

MCP所需运行环境已包含。具体使用见[原生谱图与采用](AI_CONNECTION_156_GUIDE.md)。个人功能由`bin/kumo.ini`的`enable_exp_feature`控制，本包默认开启，见[开关操作](SHARING_145_GUIDE.md)。

## 当前进度

CFX使用步骤与本次结果见[音源操作](CFX_PANEL_157_GUIDE.md)、[检查记录](CFX_PANEL_157_CHECKS.md)。REAPER的MIDI事件及Synthesia两手颜色见[导出说明](MIDI_HAND_EXPORT_GUIDE.md)。

Synthesia长片段暂停、继续和重播已补查。剩余实机检查为实体MIDI键盘与真实声卡连续听感；按[完整进度](REWORK_PROGRESS.md)继续。0.156完整包和校验值仍保留。

提交定位：personal-v0.157.0；打包定位：personal-stable-v0.157.0。
