// SPDX-License-Identifier: GPL-2.0-or-later
function readable(error) {
    var text=String(error||"");
    if(/[\u4e00-\u9fff]/.test(text))return text;
    var rules=[
        [/audio driver|device|output unavailable/i,"未能使用声音设备。请在偏好设置中检查音频设置；乐谱副本已经打开，可以查看后返回原谱。"],
        [/waiting|playing|rendering|stop playback/i,"请先停止播放，等待乐谱准备完成后再操作。"],
        [/original.*closed|original.*available/i,"原谱标签页已关闭，请重新打开原谱。"],
        [/stale|revision|changed/i,"乐谱或材料已改变，请重新读取并检查结果。"],
        [/select.*tab/i,"请先切换到对应的乐谱标签页。"],
        [/invalid.*range|invalid measure/i,"没有找到指定的位置，请重新选择乐谱范围。"],
        [/requested repeat/i,"这段音乐没有指定的播放次数，请检查反复设置。"],
        [/cannot.*open|missing|not found/i,"没有找到材料或无法打开，请检查文件位置。"],
        [/cannot.*write|cannot.*save|copy write|destination exists/i,"文件没有保存成功。请检查保存位置、权限或文件是否已经存在；之前的文件仍保留。"],
        [/read.only|protected|manual/i,"这项内容受保护，请先查看需要保留的内容，再明确选择要修改的项。"],
        [/no.score/i,"请先打开乐谱。"]
    ];
    for(var i=0;i<rules.length;++i)if(rules[i][0].test(text))return rules[i][1];
    return "操作未完成。请检查所选范围和材料；详细原因见提示。";
}

function mediaState(value){
 var s=String(value||"");if(/ready/i.test(s))return "已准备";if(/paused/i.test(s))return "已暂停";if(/playing/i.test(s))return "正在播放";if(/buffer|opening|loading/i.test(s))return "正在准备音频";return readable(s);
}
