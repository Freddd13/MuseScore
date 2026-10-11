# 演奏表达

独立插件 Performance Expression。选择谱段、导入演奏MIDI，指定按谱／任意音高敲击及旋律／voice／全声部，再自动对齐。先检查待确认／漏弹／多弹；连接可重新选择，取消对应，设／移除锚点。重复节奏和错音不自动接受；人工对应保存在工作区，原始MIDI按SHA256复制，后续读取验证原始文件未变化。

纯值Alignment.js在ScoreObserver隔离QJSEngine worker运行；分批performanceRange保持P0 analysisRange接口。自动读取停止播放后执行，隐藏／改谱取消，缓存标记过期。读取PPQ／SMPTE、tempo map、逐音力度、轨道、控制和meta值，不将MIDI打开成新谱。

力度／rubato写回与副本试听在B2受保护接口交付后启用。节奏重新记谱／实时捕获不包含。合成真值测试与真实演奏素材验收分别记录，不能用前者代替后者。
