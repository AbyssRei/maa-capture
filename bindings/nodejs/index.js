'use strict';
const path = require('path');
const koffi = require('koffi');
const T = require('./types');
const CaptureCb = koffi.proto('uint8 MwsCapture(void *, void *, void *)');
const VoidCb = koffi.proto('void MwsVoid(void *)');
const Callbacks = koffi.struct('MwsCallbacks', {struct_size:'uint32',reserved:'uint32',connect:'void *',
    screencap:koffi.pointer(CaptureCb),release_frame:koffi.pointer(VoidCb),inactive:koffi.pointer(VoidCb)});
let api;
function load() {
    if (api) return api;
    if (process.platform !== 'win32' || process.arch !== 'x64') throw new Error('Node.js binding supports Windows x64');
    const requested = process.env.MAA_WS_LIB || path.resolve(__dirname,'../../build/Release/maa-capture.dll');
    const lib = koffi.load(requested);
    try {
        const abi = lib.func('uint32 MaaWSGetAbiVersion(void)')();
        if (abi !== 2) throw new Error('Expected ABI 2, got '+abi);
        api = {
            create:lib.func('void *MaaWSManagerCreate(MaaWSCaptureOptions *, _Inout_ MaaWSError *)'),
            destroy:lib.func('void MaaWSManagerDestroy(void *)'),
            connect:lib.func('uint8 MaaWSManagerConnect(void *)'),
            capture:lib.func('uint8 MaaWSManagerScreencap(void *, _Out_ void **)'),
            image:lib.func('uint8 MaaWSFrameGetImage(void *, _Inout_ MaaWSImageView *)'),
            info:lib.func('uint8 MaaWSFrameGetInfo(void *, _Inout_ MaaWSFrameInfo *)'),
            free:lib.func('void MaaWSFrameDestroy(void *)'),
            error:lib.func('uint8 MaaWSManagerGetLastError(void *, _Inout_ MaaWSError *)'),
            inactive:lib.func('void MaaWSManagerInactive(void *)'),
            register:lib.func('uint8 MaaWSManagerRegisterCustomUnit(void *, const char *, uint32, uint32, MwsCallbacks *, void *)'),
        };
    } catch (e) {api=null;throw new Error('maa-capture package='+require('./package.json').version+' native=unknown path='+requested+': '+e.message);}
    return api;
}
function init(type) {const value=koffi.decode(Buffer.alloc(koffi.sizeof(type)),type);value.struct_size=koffi.sizeof(type);return value;}
function key(value,capacity=512) {if(typeof value!=='string'||value.includes('\0')||Buffer.byteLength(value)>=capacity)throw new RangeError('Invalid UTF-8 key');return value;}
function uint(value,bits=64) {if(typeof value==='number'&&!Number.isSafeInteger(value))throw new RangeError('Unsafe integer; use bigint');if(typeof value!=='number'&&typeof value!=='bigint')throw new TypeError('Expected integer');const b=BigInt(value);if(b<0n||b>=(1n<<BigInt(bits)))throw new RangeError('Integer out of range');return b;}
function monitorTarget(value) {return Object.freeze({type:1,source_key:key(value)});}
function windowTarget(id,processId=0n) {return Object.freeze({type:2,platform:1,window_id:uint(id),process_id:uint(processId)});}
function customTarget(value,scope=8) {uint(scope,32);return Object.freeze({type:5,scope,source_key:key(value)});}
function providerFrame(sourceKey,data,width,height,channels=4,step=width*channels,scope=8) {
    uint(width,32);uint(height,32);uint(channels,32);uint(step);
    const source=init(T.MaaWSFrameInfo);Object.assign(source,{target_type:5,scope,source_key:key(sourceKey)});
    return {image:{width,height,channels,step,data:Buffer.from(data)},source};
}
class Manager {
    constructor({target,methods=0n}) {
        if(!target||![1,2,5].includes(target.type))throw new TypeError('Explicit target is required');
        const allowed=target.type===1?['type','source_key']:target.type===2?['type','platform','window_id','process_id']:['type','scope','source_key'];
        if(Object.keys(target).some(k=>!allowed.includes(k)))throw new TypeError('Conflicting or unknown target fields');
        if(target.type===2&&target.platform!==1)throw new TypeError('Window platform must be Windows');
        const normalized=target.type===1?monitorTarget(target.source_key):target.type===2?windowTarget(target.window_id,target.process_id??0n):customTarget(target.source_key,target.scope??8);
        this.api=load();this.callbacks=[];this.target=normalized;
        const o=init(T.MaaWSCaptureOptions);o.target=Object.assign(init(T.MaaWSCaptureTarget),normalized);o.methods=uint(methods);
        const e=init(T.MaaWSError);this.handle=this.api.create(o,e);
        if(!this.handle)throw new Error(e.message);
    }
    require(){if(!this.handle)throw new Error('Manager is closed');return this.handle;}
    connect(){return !!this.api.connect(this.require());}
    lastError(){const e=init(T.MaaWSError);this.api.error(this.require(),e);return Object.freeze(e);}
    registerCustomUnit(sourceKey,{screencap,inactive}) {
        let pixels=null;
        const pointers=[];
        const register=(fn,type)=>{const p=koffi.register(fn,koffi.pointer(type));pointers.push(p);return p;};
        const cb={struct_size:koffi.sizeof(Callbacks),reserved:0,connect:null,
            screencap:register((_ctx,out)=>{
                try {
                    const f=screencap();if(!f)return 0;const i=f.image;
                    pixels=koffi.alloc('uint8',i.data.length);koffi.encode(pixels,koffi.array('uint8',i.data.length),i.data);
                    const input=init(T.MaaWSFrameInput);
                    input.image=Object.assign(init(T.MaaWSImageView),{width:Number(uint(i.width,32)),height:Number(uint(i.height,32)),channels:Number(uint(i.channels,32)),step:uint(i.step),data_size:BigInt(i.data.length),data:pixels});
                    input.info=Object.assign(init(T.MaaWSFrameInfo),f.source);
                    koffi.encode(out,T.MaaWSFrameInput,input);return 1;
                }catch(e){return 0;}
            },CaptureCb),
            release_frame:register(()=>{if(pixels){koffi.free(pixels);pixels=null;}},VoidCb),
            inactive:register(()=>{try{if(inactive)inactive();}catch{}},VoidCb)};
        let ok=false;
        try {ok=!!this.api.register(this.require(),key(sourceKey),this.target.scope,0,cb,null);return ok;}
        finally {if(ok)this.callbacks.push(pointers);else for(const p of pointers)koffi.unregister(p);}
    }
    screencap(){
        const out=[null];if(!this.api.capture(this.require(),out))return null;
        try {
            const image=init(T.MaaWSImageView),source=init(T.MaaWSFrameInfo);
            if(!this.api.image(out[0],image)||!this.api.info(out[0],source))throw new Error('Invalid frame');
            const data=Buffer.from(koffi.decode(image.data,'uint8',Number(image.data_size)));
            return Object.freeze({image:Object.freeze({width:image.width,height:image.height,channels:image.channels,step:image.step,data}),source:Object.freeze(source)});
        }finally{this.api.free(out[0]);}
    }
    inactive(){this.api.inactive(this.require());}
    close(){if(this.handle){this.api.destroy(this.handle);this.handle=null;for(const group of this.callbacks)for(const p of group)koffi.unregister(p);this.callbacks=[];}}
}
module.exports={Manager,monitorTarget,windowTarget,customTarget,providerFrame};
