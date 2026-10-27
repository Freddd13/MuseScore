# 0.129.0 编译与安装检查

日期：2026-10-27。按用户“先编译最新软件”的要求，将近期已检查的 C3 和 C4-A1／A2-A 及音源运行方式编入现有安装目录。

## 本次构建

- 使用现有 MSVC 2019、Qt 5.15.2、Windows x64 Release 编译目录。
- 音乐核心、MIDI 导出、VST 模块及辅助程序、主程序和三项测试程序编译成功。
- 个人版本由 personal/VERSION 生成，为 0.129.0；保留 Kumo branch / Freddd13。

## 用新构建复查的结果

| 内容 | 结果 |
|---|---|
| MIDI、分手、备选指法、实际松键、对比窗口、保存与SF3输出 | 19项通过，20.690秒 |
| 编配候选、差异、采用、撤销、跨界连接、面板和钢琴音源 | 17项通过，28.672秒 |
| 编辑器、不同谱表力度、筛选、滚轮、定位、播放与布局 | 11项通过，11.508秒 |

两份SF3的原／新声音波形最大差值均为0。关闭裁剪的普通MIDI保持原输出；测试谱中6个满足条件的音提前松手。Pianoteq播放中切换运行方式后保持当前拍位，再播放有实际输出，131个声音块未超时。

这些检查沿用独立测试设置和测试谱；个人乐谱保持原样。CFX／Pianoteq四种本谱和共用组合的前期结果见[钢琴窗口记录](PIANO_WINDOW_129_WORK.md)。厂商窗口的真实鼠标操作、实体键盘和声卡听感按用户安排继续补查。

## 安装

已更新 `E:/programming/funcodes/muse3_dev/MuseScore/msvc.install_repair_current_x64/bin/MuseScore3Evo.exe`。

安装资料共核对509个文件。程序正常启动并导出独立钢琴测试谱PDF，7446字节。`kumo.ini`和已修复的Qt5Quick.dll校验值保持原样，更新前的被替换文件保留在E盘。

本机日志：P2/out/release129-*-build.log、release129-midi-pluginhost.txt、release129-panels-pluginhost.txt、release129-editor-pluginhost.txt、release129-installed-export.txt。安装文件校验记录为 P2/out/release129-install.json。

## 后续顺序

返回 C4-A2-B 延音中途分手／换指与反复，再做 C4-B 松手安排、C4-C 完整预览与文件，随后 D 记谱／字体、E AI 与分享、F 连续使用检查。详见[完整计划](REWORK_PROGRESS.md)。
