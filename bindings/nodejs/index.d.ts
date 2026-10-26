/// <reference types="node" />
export type CaptureTarget = Readonly<{type:1;source_key:string}|{type:2;platform:1;window_id:bigint;process_id:bigint}|{type:5;scope:number;source_key:string}>;
export interface Image {readonly width:number;readonly height:number;readonly channels:number;readonly step:number|bigint;readonly data:Buffer}
export interface SourceInfo {readonly struct_size:number;readonly target_type:number;readonly scope:number;readonly source_key:string;readonly frame_id?:number|bigint;readonly verification?:number;readonly [key:string]:unknown}
export interface FrameSampleInfo {readonly flags:number;readonly sample_id:number|bigint;readonly sampled_at_ns:number|bigint;readonly delivered_at_ns:number|bigint;readonly session_id:number|bigint}
export interface CaptureFrame {readonly image:Image;readonly source:SourceInfo;readonly sample?:FrameSampleInfo}
export interface CapturePolicy {framepoolCache?:boolean;dxgiCache?:boolean;strictBackground?:boolean;freshOnly?:boolean;diagnostics?:boolean;maxSampleAgeNs?:number|bigint;connectTimeoutNs?:number|bigint;captureTimeoutNs?:number|bigint;firstFrameTimeoutNs?:number|bigint}
export function methodBehavior(method:number|bigint):Readonly<{method:number|bigint;flags:number|bigint}>;
export class Cancellation {request():boolean;close():void}
export function monitorTarget(key:string):CaptureTarget;
export function windowTarget(id:number|bigint,processId?:number|bigint):CaptureTarget;
export function customTarget(key:string,scope?:number):CaptureTarget;
export function providerFrame(key:string,data:Buffer,width:number,height:number,channels?:number,step?:number,scope?:number):CaptureFrame;
export class Manager {
    constructor(options:{target:CaptureTarget;methods?:number|bigint;framepoolCache?:boolean;policy?:CapturePolicy|null});
    connect():boolean;
    cancellationHandle():Cancellation;
    screencap():CaptureFrame|null;
    lastError():Readonly<{code:number;message:string;[key:string]:unknown}>;
    registerCustomUnit(key:string,callbacks:{screencap():CaptureFrame|null;inactive?():void}):boolean;
    inactive():void;
    close():void;
}
