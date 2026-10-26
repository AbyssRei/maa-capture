package maa.capture;
public class Sample {
    public static void main(String[] args){
        int[] stopped={0};
        try(var m=new MaaCapture.Manager(MaaCapture.customTarget("test",8),0)){
            if(!m.registerCustomUnit("test",()->MaaCapture.providerFrame("test",new byte[]{10,20,30,(byte)255},1,1,4,4,8),()->++stopped[0])||!m.connect())throw new AssertionError("connect");
            var f=m.screencap();if(f==null||f.data()[0]!=10||f.channels()!=3||!MaaCapture.text(f.source().source_key).equals("test"))throw new AssertionError("frame");
            m.inactive();m.inactive();if(stopped[0]!=1)throw new AssertionError("cleanup");
        }
        System.out.println("Java ABI 2 OK");
    }
}