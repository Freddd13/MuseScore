// Shared defaults and project migration for the expression panel.
function defaults() {
    return {strength:100, velocityStrength:100, velocityWriteMode:"fixed", rubatoStrength:50,
        velocity:true, rubato:false, keepDuration:true, accompaniment:false,
        deviceCenter:64, deviceGain:1, keepManual:false, unlocked:[]};
}

function restored(saved) {
    var input=JSON.parse(JSON.stringify(saved||{})), result=defaults();
    Object.keys(input).forEach(function(key){result[key]=input[key]});
    var legacy=input.strength===undefined?50:input.strength;
    result.velocityStrength=input.velocityStrength===undefined?legacy:input.velocityStrength;
    result.rubatoStrength=input.rubatoStrength===undefined?legacy:input.rubatoStrength;
    result.velocityWriteMode=["fixed","follow","preserve"].indexOf(input.velocityWriteMode)>=0?input.velocityWriteMode:"preserve";
    return result;
}
