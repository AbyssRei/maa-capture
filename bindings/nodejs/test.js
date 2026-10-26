'use strict';
const assert=require('assert');
const {Manager,customTarget,providerFrame}=require('./index');
assert.throws(()=>new Manager({target:{type:2,platform:1,window_id:1e30}}),RangeError);
assert.throws(()=>new Manager({target:{type:5,source_key:'test',window_id:1}}),TypeError);
let stopped=0;
const manager=new Manager({target:customTarget('test')});
assert.throws(()=>new Manager({target:customTarget('test'),framepoolCache:true}));
try{
    assert(manager.registerCustomUnit('test',{screencap:()=>providerFrame('test',Buffer.from([10,20,30,255]),1,1),inactive:()=>++stopped}));
    assert(manager.connect());
    const first=manager.screencap();assert.deepEqual(first.image.data,Buffer.from([10,20,30]));assert.equal(first.source.source_key,'test');
    assert.equal(Number(first.sample.sample_id),0);assert.equal(Number(first.sample.sampled_at_ns),0);
    manager.inactive();manager.inactive();assert.equal(stopped,1);
}finally{manager.close();manager.close();}
console.log('Node.js ABI 2 OK');
let advanced;
try{advanced=new Manager({target:customTarget('test'),policy:{connectTimeoutNs:1000000000n}});}
catch(e){if(!e.message.includes('does not support advanced'))throw e;}
if(advanced){
    const signal=advanced.cancellationHandle();
    try{
        assert(!signal.request());
        assert(advanced.registerCustomUnit('test',{screencap:()=>{assert(signal.request());return providerFrame('test',Buffer.from([1,2,3,255]),1,1);}}));
        assert(!advanced.connect());assert.equal(advanced.lastError().code,15);
        advanced.close();assert(!signal.request());
        assert(BigInt(require('./index').methodBehavior(16).flags)&1n);
    }finally{advanced.close();signal.close();}
}
