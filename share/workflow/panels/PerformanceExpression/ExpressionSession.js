// Values retained while each score remains open. Audio and worker tasks stay with their controllers.
var fields = [
    "midi", "workspace", "previewWorkspace", "dirtyProject", "scope", "retainedScope", "whole",
    "notes", "tempoContext", "manualAnchors", "links", "config", "revision", "info",
    "result", "candidate", "candidateConfig", "previewUndo", "previewRedo", "applicationRecords",
    "progressVersion", "showApplied", "applicationScope", "selected", "selectedMidiId",
    "selectedIssueScoreId", "mode", "role", "selectedTrack", "occurrence", "fromSeconds",
    "untilSeconds", "passageChoice", "passageFrom", "auditionAudio", "auditionLoop",
    "requestedMidiPath", "pendingProjectRestore", "recordingImport", "recordedPath", "alignmentOnShow", "scoreRefreshPending", "status"
];
function empty(config) {
    return {
        midi:null,workspace:"",previewWorkspace:"",dirtyProject:false,scope:{},retainedScope:null,
        whole:false,notes:[],tempoContext:null,manualAnchors:[],links:[],config:config,revision:"",info:{},
        result:{matches:[],missing:[],extra:[],pending:0},candidate:null,candidateConfig:"",
        previewUndo:[],previewRedo:[],applicationRecords:{velocity:{},tempos:{}},progressVersion:0,
        showApplied:false,applicationScope:1,selected:-1,selectedMidiId:"",selectedIssueScoreId:"",
        mode:0,role:0,selectedTrack:0,occurrence:1,fromSeconds:0,untilSeconds:999999,
        passageChoice:-1,passageFrom:-1,auditionAudio:false,auditionLoop:false,requestedMidiPath:"",pendingProjectRestore:null,
        recordingImport:false,recordedPath:"",alignmentOnShow:false,scoreRefreshPending:false,
        status:"选择谱段并导入演奏 MIDI"
    };
}
function snapshot(root,scroll) {
    var saved={schema:1,scroll:scroll,incomplete:root.busy||root.stale||root.previewUpdating,
        tempoRanges:root.config&&root.config.rubato&&typeof root.tempoTargetRanges==="function"?root.tempoTargetRanges():[]};
    fields.forEach(function(name){saved[name]=root[name]});
    return JSON.parse(JSON.stringify(saved));
}
function rangesKey(ranges) {
    return JSON.stringify((ranges||[]).map(function(range){return [Number(range.start),Number(range.end)]}));
}
function restore(root,saved,config) {
    var value=Object.assign(empty(config),saved||{});
    if(saved&&!saved.applicationRecords&&saved.appliedProgress)value.applicationRecords=saved.appliedProgress;
    fields.forEach(function(name){root[name]=value[name]});
}
