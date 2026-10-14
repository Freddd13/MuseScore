// SPDX-License-Identifier: GPL-2.0-or-later
function readable(error) {
    var text=String(error||"");
    if(/[\u4e00-\u9fff]/.test(text))return text;
    var rules=[
        [/audio driver|device|output unavailable/i,"声音设备尚未准备好。在“偏好设置 → I/O”选择声音输出后，重新试听。"],
        [/waiting|playing|rendering|stop playback/i,"正在播放或准备乐谱。停播并准备完成后即可继续。"],
        [/original.*closed|original.*available/i,"来源乐谱的标签页已关闭，重新打开它即可继续。"],
        [/existing.*tempo|relative.*tempo|a.tempo|tempo.*curve/i,"这里已有速度记号或渐变速度线。选中原记号，在检视器中调整这处速度。"],
        [/approximation|preserve duration/i,"当前速度变化难以保持这段总时长。降低速度应用强度，再试听比较。"],
        [/protected note/i,"这些音已有手动演奏设置。要采用演奏力度，关闭“保留手动调整”后再次使用力度。"],
        [/stale|revision|changed/i,"乐谱或素材刚刚改过，更新这段结果后即可继续。"],
        [/select.*tab/i,"请先切换到对应的乐谱标签页。"],
        [/invalid.*range|invalid measure/i,"没有找到指定的位置，请重新选择乐谱范围。"],
        [/requested repeat/i,"这段音乐没有指定的播放次数，请检查反复设置。"],
        [/cannot.*open|missing|not found/i,"没有找到材料或无法打开，请检查文件位置。"],
        [/destination exists/i,"这里已有同名文件。选择另一个文件名，或在保存窗口选择替换。"],
        [/cannot.*write|cannot.*save|copy write/i,"这个位置暂时无法保存文件。换一个可写的文件夹再保存。"],
        [/read.only|baseline/i,"这份谱子以只读方式打开。另存为一份可编辑的谱子后继续修改。"],
        [/protected|manual/i,"这里已有单独设置的演奏内容，可在演奏编辑器中查看对应音的设置。"],
        [/no.score/i,"请先打开乐谱。"]
    ];
    for(var i=0;i<rules.length;++i)if(rules[i][0].test(text))return rules[i][1];
    return text ? "操作提示："+text : "选择一个谱段或素材后开始。";
}

function mediaState(value){
 var s=String(value||"");if(/ready/i.test(s))return "已准备";if(/paused/i.test(s))return "已暂停";if(/playing/i.test(s))return "正在播放";if(/buffer|opening|loading/i.test(s))return "正在准备音频";return readable(s);
}
