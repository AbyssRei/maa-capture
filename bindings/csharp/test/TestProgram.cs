using System;
using MaaCapture;
class TestProgram {
    static int Main() {
        int stopped=0;
        using var m=new Manager(CaptureTarget.Custom("test"));
        var source=Abi.New<MaaWSFrameInfo>();source.target_type=5;source.scope=8;source.source_key=Abi.Text("test",512);
        if(!m.RegisterCustomUnit("test",()=>new CaptureFrame(new byte[]{10,20,30,255},1,1,4,4,source),()=>++stopped)||!m.Connect())return 1;
        var f=m.Screencap();if(f==null||f.Data[0]!=10||f.Channels!=3||Abi.Text(f.Source.source_key)!="test")return 2;
        m.Inactive();m.Inactive();if(stopped!=1)return 3;
        m.Dispose();m.Dispose();Console.WriteLine("C# ABI 2 OK");return 0;
    }
}