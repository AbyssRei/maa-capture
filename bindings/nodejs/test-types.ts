import {Manager,customTarget,providerFrame,CaptureFrame} from './index';
const manager = new Manager({target:customTarget('test'),methods:0n});
manager.registerCustomUnit('test',{screencap:():CaptureFrame => providerFrame('test',Buffer.from([1,2,3]),1,1,3)});
const frame:CaptureFrame|null=manager.screencap();
if(frame)console.log(frame.image.width,frame.source.source_key);
manager.close();
const advanced=new Manager({target:customTarget('test'),policy:{connectTimeoutNs:1000000000n,diagnostics:true}});
const cancellation=advanced.cancellationHandle();cancellation.request();advanced.close();cancellation.close();
