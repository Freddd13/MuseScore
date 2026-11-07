# 个人开发以来的上游更新

核对：2026-11-07。起点为个人开发采用的 f2a80b9f59dd，当前上游3.x为564998bd4c8e，共57项提交。

## 影响日常使用的更新

| 内容 | 本分支安排 |
|---|---|
| 卷帘与调试日志的即时语言切换 | 原有同步记录保留，核对现有入口 |
| 循环播放崩溃、停止后重播、反复中改速跳位 | 已有0.148/0.155适配，本轮结合Home回查 |
| 最近文件列表超过20份后变空 | 0.155已适配，记住60份，菜单显示20份 |
| 新版MuseScore多行文字、旧1.x HTML换行 | 0.155已适配 |
| 不展开反复时的分节停顿 | 0.155已适配 |
| GP6延长记号崩溃及小节截断 | 0.155已适配 |
| 分手括号、检视器及工作区迁移 | 单独与现有四方向分手比较，统一入口与保存后再处理 |
| 自动弯引号 | 保留为可选改进，当前输入习惯保持 |
| PortAudio缓冲设置 | 单独整理声音设置，Windows低延迟默认保持 |
| 踏板产生负时间 | 本轮适配，核对提前倚音和个人换踏板 |
| 起始拍位查询越过拍号表开头 | 本轮适配，检查起点、弱起及播放定位 |
| 小节以外的异常元素检测与崩溃 | 本轮成组适配，保留旧谱读取并报告实际位置 |
| MusicXML小节反复导出 | 本轮适配并检查实际导出内容 |
| 翻译、编译、Qt及自动发布 | 按现有Windows x64与个人发布方式整理，保留品牌和本地目标 |

## 逐项来源

“已有适配”以个人提交、实际代码和检查记录为准。历史记录见UPSTREAM_155_CHECKS.md。本轮五项音乐与稳定性修改已适配、编译；小节反复的实际导出与上游参考文件完全一致，循环及反复中改速也通过回查。具体操作见[本批检查记录](PLAYBACK_159_CHECKS.md)。

| 提交 | 提交日期 | 上游原始说明 | 当前处理 |
|---|---|---|---|
| [71f9f1092b4d](https://github.com/Jojo-Schmitz/MuseScore/commit/71f9f1092b4d073d583323579e320a81506b3043) | 2026-10-06 | Use correct suffix when creating musescore symlink | 早期同步范围 |
| [6dd4950b694a](https://github.com/Jojo-Schmitz/MuseScore/commit/6dd4950b694aaa5d6f98d64dc29eccf0ff011d25) | 2026-10-06 | Debug Logger: Update | 早期同步范围 |
| [a654d7d7e547](https://github.com/Jojo-Schmitz/MuseScore/commit/a654d7d7e5470fb053788a931cbebdc43dc8a522) | 2026-10-09 | Update translations | 早期同步范围 |
| [48d8d6ec2b1c](https://github.com/Jojo-Schmitz/MuseScore/commit/48d8d6ec2b1cf45cc1c09e9f792d09e07b8599ad) | 2026-10-11 | Fix include paths for Qt translation extraction | 早期同步范围 |
| [a3bc523cb6f5](https://github.com/Jojo-Schmitz/MuseScore/commit/a3bc523cb6f5e5015ddddbae280e80752aa2c990) | 2026-10-11 | Correct Piano Roll Editor translation context | 早期同步范围 |
| [7a71a740b715](https://github.com/Jojo-Schmitz/MuseScore/commit/7a71a740b7157f9cd55222ebc1ccb627d339a0be) | 2026-10-11 | Fix include paths for Qt translation extraction: Windows batch file | 早期同步范围 |
| [a2e99688923a](https://github.com/Jojo-Schmitz/MuseScore/commit/a2e99688923a4492d9f58a79c190bbe4188d7fb9) | 2026-10-12 | Piano Roll Editor: Support "live" Retranslation updates | 早期同步范围 |
| [c20282782c1f](https://github.com/Jojo-Schmitz/MuseScore/commit/c20282782c1ff3d8809a0d42c4664f312eec136d) | 2026-10-12 | Debug Logger: Support "live" Retranslation updates | 早期同步范围 |
| [19a36cdf665e](https://github.com/Jojo-Schmitz/MuseScore/commit/19a36cdf665ee43f81fd216c7a620fb0da8c31a1) | 2026-10-17 | Update French Instruments translation | 早期同步范围 |
| [dd0ca13f8d7e](https://github.com/Jojo-Schmitz/MuseScore/commit/dd0ca13f8d7e7ca92e1f1b4bb944fd3dbd4535a3) | 2026-10-18 | Fix: loading MSStudio 4.7.5+'s text elements with multi-lines | 已有适配 |
| [a0d15140438b](https://github.com/Jojo-Schmitz/MuseScore/commit/a0d15140438bb9e9b9b6457cb915fe95f17faec4) | 2026-10-19 | Backport: ChordBracket (left/right-hand chord lines) | 分类整理／单独处理 |
| [60e7cd5ca65c](https://github.com/Jojo-Schmitz/MuseScore/commit/60e7cd5ca65c6ea39bef02270bca2782e93612ce) | 2026-10-19 | Backport: automatic curly quote replacements | 分类整理／单独处理 |
| [cb3c50cf239c](https://github.com/Jojo-Schmitz/MuseScore/commit/cb3c50cf239ce99d87ecc4d72bf9d6dfe9c43fb0) | 2026-10-19 | Revert "Fix include paths for Qt translation extraction: Windows batch file" | 分类整理／单独处理 |
| [4e6dad56055d](https://github.com/Jojo-Schmitz/MuseScore/commit/4e6dad56055d649c715c1d98d2d4caddf4891003) | 2026-10-19 | Revert "Fix include paths for Qt translation extraction" | 分类整理／单独处理 |
| [db08747b2062](https://github.com/Jojo-Schmitz/MuseScore/commit/db08747b20629b4f78c46b1da1921c5326af73fb) | 2026-10-19 | Fix translation contexts | 分类整理／单独处理 |
| [6b1cfbcbe0d4](https://github.com/Jojo-Schmitz/MuseScore/commit/6b1cfbcbe0d40eabd58232638a684cc88a49f4ac) | 2026-10-19 | Translations updates for ChordBracket and curly quotes | 分类整理／单独处理 |
| [0ac0ca5efa33](https://github.com/Jojo-Schmitz/MuseScore/commit/0ac0ca5efa3389771c1c5dcdd76e7e9790d87b13) | 2026-10-19 | Fix translation contexts and update translations | 分类整理／单独处理 |
| [d85123fc99dc](https://github.com/Jojo-Schmitz/MuseScore/commit/d85123fc99dccae35fde0d1bd0c648a9c55a01dc) | 2026-10-19 | MTEST: Update CMakeList | 分类整理／单独处理 |
| [c1fe8b53e863](https://github.com/Jojo-Schmitz/MuseScore/commit/c1fe8b53e863a2a622e8113c040df7d881ee655c) | 2026-10-19 | Update French translations | 分类整理／单独处理 |
| [5f34468e7ed2](https://github.com/Jojo-Schmitz/MuseScore/commit/5f34468e7ed2641a29128003ebfa45dbd4f76f20) | 2026-10-21 | Update GitHub CI lupdate job | 分类整理／单独处理 |
| [2da0edfc7954](https://github.com/Jojo-Schmitz/MuseScore/commit/2da0edfc7954dc43290e7d9a4f4b2cb80fde7752) | 2026-10-21 | Update translations | 分类整理／单独处理 |
| [7a487de34f53](https://github.com/Jojo-Schmitz/MuseScore/commit/7a487de34f530963232f5d67c9cd331e896ab9cc) | 2026-10-21 | Update GitHub CI lupdate job | 分类整理／单独处理 |
| [321d22823cef](https://github.com/Jojo-Schmitz/MuseScore/commit/321d22823cef2c6602fe1a0b5db7364ad68ebe43) | 2026-10-21 | lupdate for linux update | 分类整理／单独处理 |
| [1cee9104d47f](https://github.com/Jojo-Schmitz/MuseScore/commit/1cee9104d47f79f6a4e0c17e61d8bb833eaed8fc) | 2026-10-22 | Improve translation update scripts | 分类整理／单独处理 |
| [9ab277fbc5d9](https://github.com/Jojo-Schmitz/MuseScore/commit/9ab277fbc5d91cd99977b8496cd0ef8dfae8cab2) | 2026-10-22 | Merge pull request #1333 from Jojo-Schmitz/3.x-translations | 分类整理／单独处理 |
| [5cf3f73ba900](https://github.com/Jojo-Schmitz/MuseScore/commit/5cf3f73ba9004ea60db41a00d2a1b8939b3fd5fa) | 2026-10-23 | Fix translations issue with Qt 5.9 vs. Qt 5.15 | 分类整理／单独处理 |
| [5420677fbd31](https://github.com/Jojo-Schmitz/MuseScore/commit/5420677fbd31b70afe087a4dbf675b6e5047d004) | 2026-10-23 | Fix warnings reg. cmake policies | 分类整理／单独处理 |
| [26e82ab75f2c](https://github.com/Jojo-Schmitz/MuseScore/commit/26e82ab75f2c87bd9e32a2d9ffeb7b62e85eff25) | 2026-10-24 | Fix MSVC compiler warnings | 分类整理／单独处理 |
| [15379df303d9](https://github.com/Jojo-Schmitz/MuseScore/commit/15379df303d952afb1b76e4e9097efdfb6fdf0bb) | 2026-10-24 | Fix MSVC compiler warnings | 分类整理／单独处理 |
| [590e7d48e852](https://github.com/Jojo-Schmitz/MuseScore/commit/590e7d48e852e291187fbf547038b459db2b0036) | 2026-10-24 | Fix MSVC compiler warnings | 分类整理／单独处理 |
| [328da08abe0f](https://github.com/Jojo-Schmitz/MuseScore/commit/328da08abe0f9d159fd0e9e43c6e0a92bfed55e9) | 2026-10-24 | Fix warnings reg. cmake policies | 分类整理／单独处理 |
| [d2f594902c21](https://github.com/Jojo-Schmitz/MuseScore/commit/d2f594902c21c90b7e99e5c73a9556c2b146f6bf) | 2026-10-24 | Fix MSVC compiler and CMAKE warnings | 分类整理／单独处理 |
| [641bff9c6a80](https://github.com/Jojo-Schmitz/MuseScore/commit/641bff9c6a802bb48490f0d7b541293e9706b25f) | 2026-10-24 | Update Linux x86_64 builds to use Qt 5.15.2 | 分类整理／单独处理 |
| [9b6e3bdf11ff](https://github.com/Jojo-Schmitz/MuseScore/commit/9b6e3bdf11ffa8bc32241cd2d3b7b0c814974ffc) | 2026-10-24 | Update Macports | 分类整理／单独处理 |
| [9e158acf3a64](https://github.com/Jojo-Schmitz/MuseScore/commit/9e158acf3a64799096952aec31bb1bcd6be3e6cf) | 2026-10-25 | Use Qt-5.15.2 for 32-bit Windows builds too (#1340) | 分类整理／单独处理 |
| [e3194583b8a9](https://github.com/Jojo-Schmitz/MuseScore/commit/e3194583b8a93e36c3c69edc43afa5191d084c1c) | 2026-10-27 | fix #1347: PortAudio latency as an advanced preference, high on macOS | 分类整理／单独处理 |
| [97b26c87481f](https://github.com/Jojo-Schmitz/MuseScore/commit/97b26c87481ffa6f79a6f2f80dde350416d55208) | 2026-10-27 | fix #1342: crash while looping, GUI thread moving Seq::playPos during playback | 已有适配 |
| [0514c6695b84](https://github.com/Jojo-Schmitz/MuseScore/commit/0514c6695b8406928cba2e6b84327743434aef65) | 2026-10-28 | fix #1344: with Loop on, Play often restarts the loop instead of stopping | 已有适配 |
| [f9481b33ff1c](https://github.com/Jojo-Schmitz/MuseScore/commit/f9481b33ff1c267b2b80c99aa96c324c3d22aef9) | 2026-10-28 | fix #1343: speed change jumps ahead in scores with repeats | 已有适配 |
| [064a9d031af4](https://github.com/Jojo-Schmitz/MuseScore/commit/064a9d031af45f6322fa91ccbd02a2c37bc1c5f2) | 2026-10-28 | Backport: ChordBracket - Inspector & Workspace update (migration version 4) | 分类整理／单独处理 |
| [eb9fbbcbfbc0](https://github.com/Jojo-Schmitz/MuseScore/commit/eb9fbbcbfbc02e16299a077f338c03b86fe1e2ba) | 2026-10-28 | fix #1346: File > Open Recent empty with 20 or more scores open | 已有适配 |
| [91f76816167a](https://github.com/Jojo-Schmitz/MuseScore/commit/91f76816167a3b45cfda076ceaebcbbdf2892fe4) | 2026-10-28 | Update translations | 分类整理／单独处理 |
| [29f7827edc40](https://github.com/Jojo-Schmitz/MuseScore/commit/29f7827edc40dcc60935a4d319522cdc0b85a504) | 2026-10-28 | fix #1348: keep <br> in MuseScore 1.x html-data | 已有适配 |
| [04c5d80ed7c4](https://github.com/Jojo-Schmitz/MuseScore/commit/04c5d80ed7c4286338ff14bac337a411fa722ba8) | 2026-10-28 | Update Frensh translations | 分类整理／单独处理 |
| [424d537f8220](https://github.com/Jojo-Schmitz/MuseScore/commit/424d537f822076a74018e8e0b325d884722b8b82) | 2026-10-29 | fix #1337: process section breaks for flattened scores | 已有适配 |
| [2c37d143bc8c](https://github.com/Jojo-Schmitz/MuseScore/commit/2c37d143bc8c6bcec837b18714a0d3f738c9f5c3) | 2026-10-29 | fix #1345: GP6 import crash on a fermata, and score cut to 2 measures | 已有适配 |
| [18ac05e6ebc5](https://github.com/Jojo-Schmitz/MuseScore/commit/18ac05e6ebc501e00d37f27ee4f9a88e5e76030d) | 2026-10-29 | Minor improvement after code review | 已有适配 |
| [ead630c3a149](https://github.com/Jojo-Schmitz/MuseScore/commit/ead630c3a149ddcc82c2a5a6512e53c584b589bd) | 2026-10-31 | Fix linux builds on GitHub CI | 分类整理／单独处理 |
| [ba4ec6b48e72](https://github.com/Jojo-Schmitz/MuseScore/commit/ba4ec6b48e7242a398ca26fe175afd99c0cd9ce6) | 2026-11-01 | Change artifacts names | 分类整理／单独处理 |
| [2156f9723ace](https://github.com/Jojo-Schmitz/MuseScore/commit/2156f9723aceff79a6f9be5df813a45c2e9bb297) | 2026-11-01 | Create a continuous release on GitHub | 分类整理／单独处理 |
| [130b54342447](https://github.com/Jojo-Schmitz/MuseScore/commit/130b5434244762c6f1f99cbea94a312d28f6ea67) | 2026-11-02 | renderSpanner: ensure positive pedal tick | 本轮适配，待验证 |
| [5ee61659924d](https://github.com/Jojo-Schmitz/MuseScore/commit/5ee61659924dc6db2a2ab70e9beebe8ddacba67a) | 2026-11-02 | Fix undefined behaviour in TimeSigMap::tickValues when e == begin() | 本轮适配，待验证 |
| [e1383fb989da](https://github.com/Jojo-Schmitz/MuseScore/commit/e1383fb989dab6744766f37d77fda8a5f7df53ea) | 2026-11-02 | Flag corrupted measures when they contain items outside of their tick boundaries | 本轮适配，待验证 |
| [f3440e108d6c](https://github.com/Jojo-Schmitz/MuseScore/commit/f3440e108d6c7515ea2e25571c806a61cdcc606e) | 2026-11-02 | Translations update | 分类整理／单独处理 |
| [fceb31067e97](https://github.com/Jojo-Schmitz/MuseScore/commit/fceb31067e9736546ef1b1532ab49e6cbe19990f) | 2026-11-03 | Fix crash in score containing out-of-measure-bounds items | 本轮适配，待验证 |
| [07fcf9d32674](https://github.com/Jojo-Schmitz/MuseScore/commit/07fcf9d32674262f0a7baed24352b9d30519261e) | 2026-11-04 | Update French translations | 分类整理／单独处理 |
| [564998bd4c8e](https://github.com/Jojo-Schmitz/MuseScore/commit/564998bd4c8ec5d2d7ed3e6fa35907a47fcf4014) | 2026-11-06 | fix #1368: MusicXML export of measure repeats | 本轮适配，待验证 |

[完整比较](https://github.com/Jojo-Schmitz/MuseScore/compare/f2a80b9f59dd396698b3b507a19a56bfb8791af2...564998bd4c8ec5d2d7ed3e6fa35907a47fcf4014)。
