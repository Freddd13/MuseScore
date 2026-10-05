# 08 调查基准、证据与限制

## 固定调查基准

| 项目 | 记录 |
| --- | --- |
| 日期 | 2026-10-05，用户时区 Asia/Shanghai |
| 根工作区 | `E:\programming\funcodes\muse3_dev`，不是 Git 仓库 |
| 软件仓库 | 子目录 `MuseScore`，分支 `3.x` |
| 源码 HEAD | `f2a80b9f59dd396698b3b507a19a56bfb8791af2` |
| HEAD 主题 | Fix: 'About' should indicate 32 or 64 bit and if portable |
| 应用版本 | `config.cmake`：3.7.0；dev 配置会另带开发版标识 |
| Remote | `origin` 指向 Freddd13/MuseScore；没有本次添加 upstream |
| 初始工作区 | `git status --short` 为空；材料为后续新增 |
| 远端核对 | 只读 `git ls-remote origin refs/heads/3.x` 返回同一完整 SHA；这是调查时刻的结果 |

[baseline.json](baseline.json) 包含上述固定基准、所有 Git 跟踪文件的顶层目录计数和类型计数，避免用当前磁盘上缓存/依赖文件统计源码。文件数仅用于认知规模，不是阅读覆盖率。

## 实际阅读范围

| 范围 | 已追踪内容 |
| --- | --- |
| 约束/版本/构建 | 外层 AGENTS、README、顶层 CMake/config、主要目标 CMake、Windows 脚本、Qt 查找、CI/VSCode |
| 启动与边界 | main/ModulesSetup、runApplication/CLI 入口、主要库链接、服务注入定义 |
| 模型 | Score/MasterScore、ScoreElement/Element、Measure/Segment、ChordRest/Chord/Note、Part/Staff、Selection/InputState、Spanner |
| 关键机制 | startCmd/endCmd/update、UndoStack、属性/linkList/style、布局入口与元素布局分布、文件分发/旧 reader |
| 钢琴专题 | 卷帘 editor/view/levels/filter/键盘/事件单位、scope/orientation、重建与时间桶、cross-staff/beam、splitStaff、相关近期历史 |
| 播放/导入 | Seq 的事件收集/异步缓存/process/MIDI/UI 通知、MidiRenderer/NoteEvent、主合成器/driver 接口、MIDI 转谱流水线、MusicXML 两阶段入口 |
| 验证设施 | mtest 注册/helper/代表性 splitstaff 与相关目标、QSKIP、视觉测试指南、脚本测试与原测试命令 |

方法是先目录/构建清单，再头文件/符号，再关键实现与调用者/测试；不是逐行读取所有文件。尤其大型 MIDI/MusicXML/Guitar Pro parser、Fluid DSP、第三方库和所有布局分支尚未完整审计。新需求仍需针对该规则深入阅读与运行验证。

## 当前环境与未完成的运行验证

- PATH 可找到 cmake、ctest、git、python（3.7.10）；未找到 qmake。
- 仓库没有 `dependencies`、`msvc.build_x64`、`build.debug`。没有据此断言整机完全未安装 Qt/VS，未搜索系统所有安装位置。
- 本次未配置 CMake、编译、执行 QTest、启动应用、试听或生成视觉对比；因为工作是创建架构材料，且当前缺少已就绪的构建环境。
- `git submodule status` 在当前沙箱环境因 Git shell 工具（basename/sed/git-sh-setup）不可用而失败；未将失败解释为 submodule 状态正常或缺失，也未进行初始化。
- 沙箱下普通 SSH 远端读取曾因 known_hosts 读取权限失败；批准的只读检查随后成功。没有修改 SSH 配置或禁用 host key 检查。

## 如何判断指南可信度

1. 文件/符号/单位/调用链有本地源码证据，TSV 可验证位置。
2. 功能表中的测试目录是“邻近可参考”，不是对所有 GUI/播放场景的覆盖保证。
3. 构建操作描述的是仓库脚本与配置，环境准备是否成功需实际执行证明。
4. 没有对 Jojo-Schmitz 上游做 fetch/差异审计，不能把本地近期演进全部称为用户个人修改。
5. baseline 和导航行号会随更新变旧；先跑校验器，再对本次需求读实际代码。

## 何时值得补充材料

当功能跨多个核心模块、发现新的事务/布局/线程约束、升级上游改变已有入口、增加新用户设置/持久化字段或建立专门回归测试时，更新相关专题和索引。只补需求涉及的知识，不每次生成全库大纲。

如果未来已验证本机构建，应补充实际 Qt/VS/CMake 版本、命令、输出位置、测试结果与失败限制；不能删除这次历史基准来伪装早已验证。
