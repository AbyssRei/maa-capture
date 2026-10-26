'use strict';
const assert=require('assert');
const {Manager,customTarget,providerFrame}=require('./index');
assert.throws(()=>new Manager({target:{type:2,platform:1,window_id:1e30}}),RangeError);
assert.throws(()=>new Manager({target:{type:5,source_key:'test',window_id:1}}),TypeError);
let stopped=0;
const manager=new Manager({target:customTarget('test')});
try{
    assert(manager.registerCustomUnit('test',{screencap:()=>providerFrame('test',Buffer.from([10,20,30,255]),1,1),inactive:()=>++stopped}));
    assert(manager.connect());
    const first=manager.screencap();assert.deepEqual(first.image.data,Buffer.from([10,20,30]));assert.equal(first.source.source_key,'test');
    manager.inactive();manager.inactive();assert.equal(stopped,1);
}finally{manager.close();manager.close();}
console.log('Node.js ABI 2 OK');
