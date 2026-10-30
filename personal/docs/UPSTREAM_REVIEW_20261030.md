# 上游更新评估 · 2026-10-30

用户已选定以下八项；0.155已完成分别合入、检查和安装。实际来源、额外适配及结果见[本批记录](UPSTREAM_155_CHECKS.md)。下方保留合入前的比较与补丁，其他分组仍供后续选择。

合入前的个人程序为0.154.0。上次已合入的上游位置是19a36cdf665e，本次取得的上游分支为18ac05e6ebc5，提交日期2026-10-29。两者之间新增38项提交，涉及180个文件。原先建议的九项中，停止操作已由0.148修复，其余八项已按0.154准备适配草稿，供你选择。

## 建议合入：八项稳定性与兼容修复

| 你会遇到的情况 | 上游修复 | 当前代码情况与处理 |
|---|---|---|
| 开循环后偶发崩溃 | 97b26c87481f | GUI仍会改变声音线程正在使用的事件位置，循环返回请求也会连续重复。保留现有演奏预览和踏板准备，在声音线程完成定位。 |
| 点停止又开始播 | 0514c6695b84 | 0.148已在准备新播放之前处理停止，同时覆盖工具试听。保留现有实现，本次无需重复合入。 |
| 反复段中调速度后位置跳远 | f9481b33ff1c | 目前先按新速度换算位置，随后才更新反复时间。调整顺序，并保留原曲联合播放的时间映射。 |
| 多谱同时打开后“最近文件”变空 | eb9fbbcbfbc0 | 历史只记20份，又过滤已打开谱。改为记60份、显示20份，关闭文件后更新历史。 |
| 打开新版MuseScore文字时换行异常 | dd0ca13f8d7e + 29f7827edc40 | 两项一起合入，读取4.7之后的换行，同时保持旧1.x文件的HTML处理。 |
| 关闭展开反复时，分节停顿丢失 | 424d537f8220 + 18ac05e6ebc5 | 两项一起合入，按实际分节建立播放时间并释放末尾空片段。 |
| GP6导入遇到延长记号时崩溃或只剩两小节 | 2c37d143bc8c | 修正空对象检查及小节数量的读取。修改集中在GP6导入。 |

建议把尚待处理的循环、反复改速与六项兼容修复一起合入。这组保持现有编辑器、和弦窗口、工具栏和分手操作。

## 按当前程序准备的草稿

草稿对应个人源码提交96f0e343cdde，涉及六个文件，共八项上游提交。下载和逐行查看可打开[适配补丁](https://github.com/Freddd13/Muse3Plugins/blob/main/Planning/patches/upstream-stability-154.patch)。当前程序仍使用已安装的0.154；补丁已在独立文件中准备，并完成正向、反向预检查。选定后进入编译和操作验证。

| 分组 | 草稿中的实际处理 | 验证重点 |
|---|---|---|
| 循环播放 | 声音线程维护正在播放的事件位置；一次循环只排一个返回请求，收到定位后再允许下次请求。保留目前的踏板、提前起音及试听准备。 | 起点没有新音、短循环、连续多遍、停止重播、工具试听。 |
| 反复中改速 | 先更新反复段的速度缓存，再恢复当前拍位；联合播放继续使用现有的谱面与音频时间换算。 | 第二遍改快改慢、拍位、起音、踏板与原曲联动。 |
| 多谱与文字 | 记住最近60份、显示20份；关闭谱后更新；按前后依赖同时加入新文字换行和旧HTML兼容。 | 打开20份以上、取消关闭、另存；新旧文字实际显示和PDF。 |
| 分节与GP6 | 未展开反复时保留分节停顿；释放最后空片段；GP6按小节列表计数，并核对延长记号前的对象。 | 停顿时长及MIDI、末尾分节、GP6延长记号与完整小节数。 |

停止修复的核对位置为mscore/scoreview.cpp的play操作：先判断seq->isPlaying()，再执行canStart()。这同时处理原谱与独立试听，是当前0.148以来的实际行为。

草稿保留每项上游提交的来源。作者包括marc.adler、J.G.、jeetee和Joachim Schmitz；正式合入时与个人适配一并记录。

## 另外几组怎样安排

| 内容 | 建议 |
|---|---|
| PortAudio高缓冲选项，e3194583b8a9 | 可以另行增加。Windows继续默认低缓冲；macOS改用较稳的高缓冲。当前Windows低延迟需求继续保留。 |
| 新分手括号，a0d15140438b + 064a9d031af4 | 单独迁移。它增加新的乐谱对象和工作区迁移；我们已有逐音分手、文字、四方向控制及AI读取。需要统一保存、旧谱读取和普通版兼容，再选择是否用它替换现有实现。 |
| 自动弯引号，60e7cd5ca65c | 另行选择。会自动把键入的英文直引号换成弯引号，属于输入习惯变化。 |
| 翻译及提取脚本 | 成组整理后合入。包含对上次脚本的撤回及后续替代，保留个人中文按钮和新工具翻译。 |
| MSVC／CMake告警、MacPorts、Linux／32位Windows Qt更新 | 放在维护批处理；保持当前Windows x64编译方式，分别检查本地新增目标。 |

## 已完成的比较

已读取全部38项提交及相关补丁。循环崩溃及反复改速两项已按当前个人播放逻辑准备草稿；新版文字和分节修复各有前后两项依赖，已按组加入独立副本。停止操作通过当前代码核对为已有修复。

首次比较时网页缓存仍显示7月记录，本页以实际取得的提交为准。比较和草稿准备保留当前分支、安装文件及未提交内容。

## 合入后的检查

循环从有音和无新起音的拍位开始，连续返回、停止和再播；反复第2遍改速度后核对拍位与实际起音。一起播放、提前起音、踏板、编辑器光标和试听切换同时回查。

最近文件覆盖20份以上乐谱、关闭、取消关闭、另存及重新启动。文字分别读取新版多行和旧HTML谱例；关闭展开反复时核对分节停顿、MIDI及试听。GP6使用含延长记号的文件检查完整小节数。

每组保留上游作者与来源，编译、安装并更新说明。原始补丁和比较结果位于插件仓库P2/out/e4-upstream及e4-upstream-checks.json；当前适配核对在P2/out/e4-ready154/report.json。

## 全部提交

下表保留首次0.145比较时的原始补丁检查。“可直接对上”表示当时预检查通过；依赖前项或涉及个人改动的提交写为“需合并处理”。0.154的适配情况以上方草稿表为准。

| 提交 | 日期 | 内容 | 补丁检查 |
|---|---|---|---|
| [dd0ca13f8d7e](https://github.com/Jojo-Schmitz/MuseScore/commit/dd0ca13f8d7e7ca92e1f1b4bb944fd3dbd4535a3) | 2026-10-18 | Fix: loading MSStudio 4.7.5+'s text elements with multi-lines | 可直接对上 |
| [a0d15140438b](https://github.com/Jojo-Schmitz/MuseScore/commit/a0d15140438bb9e9b9b6457cb915fe95f17faec4) | 2026-10-19 | Backport: ChordBracket (left/right-hand chord lines) | 需合并处理 |
| [60e7cd5ca65c](https://github.com/Jojo-Schmitz/MuseScore/commit/60e7cd5ca65c6ea39bef02270bca2782e93612ce) | 2026-10-19 | Backport: automatic curly quote replacements | 可直接对上 |
| [cb3c50cf239c](https://github.com/Jojo-Schmitz/MuseScore/commit/cb3c50cf239ce99d87ecc4d72bf9d6dfe9c43fb0) | 2026-10-19 | Revert "Fix include paths for Qt translation extraction: Windows batch file" | 可直接对上 |
| [4e6dad56055d](https://github.com/Jojo-Schmitz/MuseScore/commit/4e6dad56055d649c715c1d98d2d4caddf4891003) | 2026-10-19 | Revert "Fix include paths for Qt translation extraction" | 可直接对上 |
| [db08747b2062](https://github.com/Jojo-Schmitz/MuseScore/commit/db08747b20629b4f78c46b1da1921c5326af73fb) | 2026-10-19 | Fix translation contexts | 可直接对上 |
| [6b1cfbcbe0d4](https://github.com/Jojo-Schmitz/MuseScore/commit/6b1cfbcbe0d40eabd58232638a684cc88a49f4ac) | 2026-10-19 | Translations updates for ChordBracket and curly quotes | 需合并处理 |
| [0ac0ca5efa33](https://github.com/Jojo-Schmitz/MuseScore/commit/0ac0ca5efa3389771c1c5dcdd76e7e9790d87b13) | 2026-10-19 | Fix translation contexts and update translations | 合并记录 |
| [d85123fc99dc](https://github.com/Jojo-Schmitz/MuseScore/commit/d85123fc99dccae35fde0d1bd0c648a9c55a01dc) | 2026-10-19 | MTEST: Update CMakeList | 需合并处理 |
| [c1fe8b53e863](https://github.com/Jojo-Schmitz/MuseScore/commit/c1fe8b53e863a2a622e8113c040df7d881ee655c) | 2026-10-19 | Update French translations | 需合并处理 |
| [5f34468e7ed2](https://github.com/Jojo-Schmitz/MuseScore/commit/5f34468e7ed2641a29128003ebfa45dbd4f76f20) | 2026-10-21 | Update GitHub CI lupdate job | 需合并处理 |
| [2da0edfc7954](https://github.com/Jojo-Schmitz/MuseScore/commit/2da0edfc7954dc43290e7d9a4f4b2cb80fde7752) | 2026-10-21 | Update translations | 可直接对上 |
| [7a487de34f53](https://github.com/Jojo-Schmitz/MuseScore/commit/7a487de34f530963232f5d67c9cd331e896ab9cc) | 2026-10-21 | Update GitHub CI lupdate job | 可直接对上 |
| [321d22823cef](https://github.com/Jojo-Schmitz/MuseScore/commit/321d22823cef2c6602fe1a0b5db7364ad68ebe43) | 2026-10-21 | lupdate for linux update | 可直接对上 |
| [1cee9104d47f](https://github.com/Jojo-Schmitz/MuseScore/commit/1cee9104d47f79f6a4e0c17e61d8bb833eaed8fc) | 2026-10-22 | Improve translation update scripts | 需合并处理 |
| [9ab277fbc5d9](https://github.com/Jojo-Schmitz/MuseScore/commit/9ab277fbc5d91cd99977b8496cd0ef8dfae8cab2) | 2026-10-22 | Merge pull request #1333 from Jojo-Schmitz/3.x-translations | 合并记录 |
| [5cf3f73ba900](https://github.com/Jojo-Schmitz/MuseScore/commit/5cf3f73ba9004ea60db41a00d2a1b8939b3fd5fa) | 2026-10-23 | Fix translations issue with Qt 5.9 vs. Qt 5.15 | 需合并处理 |
| [5420677fbd31](https://github.com/Jojo-Schmitz/MuseScore/commit/5420677fbd31b70afe087a4dbf675b6e5047d004) | 2026-10-23 | Fix warnings reg. cmake policies | 可直接对上 |
| [26e82ab75f2c](https://github.com/Jojo-Schmitz/MuseScore/commit/26e82ab75f2c87bd9e32a2d9ffeb7b62e85eff25) | 2026-10-24 | Fix MSVC compiler warnings | 可直接对上 |
| [15379df303d9](https://github.com/Jojo-Schmitz/MuseScore/commit/15379df303d952afb1b76e4e9097efdfb6fdf0bb) | 2026-10-24 | Fix MSVC compiler warnings | 需合并处理 |
| [590e7d48e852](https://github.com/Jojo-Schmitz/MuseScore/commit/590e7d48e852e291187fbf547038b459db2b0036) | 2026-10-24 | Fix MSVC compiler warnings | 可直接对上 |
| [328da08abe0f](https://github.com/Jojo-Schmitz/MuseScore/commit/328da08abe0f9d159fd0e9e43c6e0a92bfed55e9) | 2026-10-24 | Fix warnings reg. cmake policies | 需合并处理 |
| [d2f594902c21](https://github.com/Jojo-Schmitz/MuseScore/commit/d2f594902c21c90b7e99e5c73a9556c2b146f6bf) | 2026-10-24 | Fix MSVC compiler and CMAKE warnings | 合并记录 |
| [641bff9c6a80](https://github.com/Jojo-Schmitz/MuseScore/commit/641bff9c6a802bb48490f0d7b541293e9706b25f) | 2026-10-24 | Update Linux x86_64 builds to use Qt 5.15.2 | 可直接对上 |
| [9b6e3bdf11ff](https://github.com/Jojo-Schmitz/MuseScore/commit/9b6e3bdf11ffa8bc32241cd2d3b7b0c814974ffc) | 2026-10-24 | Update Macports | 可直接对上 |
| [9e158acf3a64](https://github.com/Jojo-Schmitz/MuseScore/commit/9e158acf3a64799096952aec31bb1bcd6be3e6cf) | 2026-10-25 | Use Qt-5.15.2 for 32-bit Windows builds too (#1340) | 可直接对上 |
| [e3194583b8a9](https://github.com/Jojo-Schmitz/MuseScore/commit/e3194583b8a93e36c3c69edc43afa5191d084c1c) | 2026-10-27 | fix #1347: PortAudio latency as an advanced preference, high on macOS | 可直接对上 |
| [97b26c87481f](https://github.com/Jojo-Schmitz/MuseScore/commit/97b26c87481ffa6f79a6f2f80dde350416d55208) | 2026-10-27 | fix #1342: crash while looping, GUI thread moving Seq::playPos during playback | 需合并处理 |
| [0514c6695b84](https://github.com/Jojo-Schmitz/MuseScore/commit/0514c6695b8406928cba2e6b84327743434aef65) | 2026-10-28 | fix #1344: with Loop on, Play often restarts the loop instead of stopping | 可直接对上 |
| [f9481b33ff1c](https://github.com/Jojo-Schmitz/MuseScore/commit/f9481b33ff1c267b2b80c99aa96c324c3d22aef9) | 2026-10-28 | fix #1343: speed change jumps ahead in scores with repeats | 需合并处理 |
| [064a9d031af4](https://github.com/Jojo-Schmitz/MuseScore/commit/064a9d031af45f6322fa91ccbd02a2c37bc1c5f2) | 2026-10-28 | Backport: ChordBracket - Inspector & Workspace update (migration version 4) | 需合并处理 |
| [eb9fbbcbfbc0](https://github.com/Jojo-Schmitz/MuseScore/commit/eb9fbbcbfbc02e16299a077f338c03b86fe1e2ba) | 2026-10-28 | fix #1346: File > Open Recent empty with 20 or more scores open | 可直接对上 |
| [91f76816167a](https://github.com/Jojo-Schmitz/MuseScore/commit/91f76816167a3b45cfda076ceaebcbbdf2892fe4) | 2026-10-28 | Update translations | 需合并处理 |
| [29f7827edc40](https://github.com/Jojo-Schmitz/MuseScore/commit/29f7827edc40dcc60935a4d319522cdc0b85a504) | 2026-10-28 | fix #1348: keep <br> in MuseScore 1.x html-data | 需合并处理 |
| [04c5d80ed7c4](https://github.com/Jojo-Schmitz/MuseScore/commit/04c5d80ed7c4286338ff14bac337a411fa722ba8) | 2026-10-28 | Update Frensh translations | 需合并处理 |
| [424d537f8220](https://github.com/Jojo-Schmitz/MuseScore/commit/424d537f822076a74018e8e0b325d884722b8b82) | 2026-10-29 | fix #1337: process section breaks for flattened scores | 可直接对上 |
| [2c37d143bc8c](https://github.com/Jojo-Schmitz/MuseScore/commit/2c37d143bc8c6bcec837b18714a0d3f738c9f5c3) | 2026-10-29 | fix #1345: GP6 import crash on a fermata, and score cut to 2 measures | 可直接对上 |
| [18ac05e6ebc5](https://github.com/Jojo-Schmitz/MuseScore/commit/18ac05e6ebc501e00d37f27ee4f9a88e5e76030d) | 2026-10-29 | Minor improvement after code review | 需合并处理 |

[查看完整上游比较](https://github.com/Jojo-Schmitz/MuseScore/compare/19a36cdf665ee43f81fd216c7a620fb0da8c31a1...18ac05e6ebc501e00d37f27ee4f9a88e5e76030d)。
