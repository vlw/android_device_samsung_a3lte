/* SPDX-License-Identifier: GPL-2.0 */
// Device-local synthetic audio and protocol checks. No AudioRecord/AudioTrack,
// socket, telephony request or real call is created.
import android.content.Context;
import android.os.Looper;
import android.media.MediaCodec;
import java.nio.ByteBuffer;
import java.lang.reflect.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;
public final class EvsCodecTest {
    static ClassLoader loader;
    static Class<?> cls(String name) throws Exception { return loader.loadClass("me.phh.sip."+name); }
    static Object singleton(String name) throws Exception { return cls(name).getField("INSTANCE").get(null); }
    static Object call(String name,String method,Class<?>[] types,Object... args) throws Exception {
        return cls(name).getMethod(method,types).invoke(singleton(name),args);
    }
    static void check(boolean b,String message) { if(!b) throw new AssertionError(message); }
    static List<String> sdp(String fmtp) {
        return Arrays.asList("m=audio 10000 RTP/AVP 102 98 101","a=rtpmap:102 EVS/16000",
            "a=fmtp:102 "+fmtp,"a=rtpmap:98 AMR-WB/16000","a=rtpmap:101 telephone-event/16000","a=ptime:20");
    }
    public static void main(String[] args) {
        try { run(); System.exit(0); } catch(Throwable t) { t.printStackTrace(); System.exit(1); }
    }
    static void run() throws Exception {
        Looper.prepareMainLooper();
        Class<?> at=Class.forName("android.app.ActivityThread");
        Object thread=at.getMethod("systemMain").invoke(null);
        Context system=(Context)at.getMethod("getSystemContext").invoke(thread);
        Context app=system.createPackageContext("me.phh.ims",Context.CONTEXT_INCLUDE_CODE|Context.CONTEXT_IGNORE_SECURITY);
        loader=app.getClassLoader();
        check((Boolean)call("EvsSwbProfile","available",new Class<?>[0]),"stock JNI availability through installed app classloader");
        String fmtp=(String)cls("EvsSwbProfile").getField("FMTP").get(null);
        Method selectable=cls("EvsSwbProfile").getMethod("selectable",List.class,boolean.class);
        Object profile=singleton("EvsSwbProfile");
        check((Boolean)selectable.invoke(profile,sdp(fmtp),true),"supported answer");
        check((Boolean)selectable.invoke(profile,sdp("br=9.6-24.4;bw=nb-swb"),false),"offer can narrow to supported profile");
        for(String bad:new String[]{"br=24.4;bw=swb","br=13.2;bw=wb",fmtp.replace("evs-mode-switch=0","evs-mode-switch=1"),fmtp.replace("hf-only=1","hf-only=0"),fmtp.replace("dtx=0","dtx=1")})
            check(!(Boolean)selectable.invoke(profile,sdp(bad),true),"reject unsupported answer: "+bad);
        List<String> stray=new ArrayList<>(sdp(fmtp));stray.set(0,"m=audio 10000 RTP/AVP 98 101");
        check(!(Boolean)selectable.invoke(profile,stray,true),"do not select unoffered payload");
        Class<?> codecType=cls("NegotiatedAudioCodec");
        for(String method:new String[]{"selectIncomingSpeechCodecFromOffer","selectOutgoingSpeechCodecFromAnswer"}) {
            Object selected=call("SipAudioCodecNegotiator",method,new Class<?>[]{String.class,List.class,String.class,boolean.class},"EvsTest",sdp(fmtp),"synthetic",true);
            check(codecType.getMethod("getName").invoke(selected).equals("EVS-SWB"),"prefer implemented EVS profile");
            selected=call("SipAudioCodecNegotiator",method,new Class<?>[]{String.class,List.class,String.class,boolean.class},"EvsTest",sdp("br=24.4;bw=swb"),"synthetic",true);
            check(codecType.getMethod("getName").invoke(selected).equals("AMR-WB"),"retain AMR-WB fallback");
        }
        Object codec=cls("StockEvsCodec").getConstructor().newInstance();
        Object codecProfile=cls("SipAudioCodecs").getMethod("getEVS_SWB").invoke(singleton("SipAudioCodecs"));
        Class<?> negotiated=cls("NegotiatedAudioCodec");
        Method encode=cls("StockEvsCodec").getMethod("encode",byte[].class), decode=cls("StockEvsCodec").getMethod("decode",byte[].class);
        Method packet=cls("SipAmrRtpPayload").getMethod("buildBandwidthEfficientRtpPacketFromStorageFrame",negotiated,int.class,int.class,int.class,byte[].class,boolean.class);
        Method unpack=cls("SipAmrRtpPayload").getMethod("storageFrameFromBandwidthEfficientRtp",negotiated,byte[].class,int.class);
        Object rtp=singleton("SipAmrRtpPayload");
        long start=System.nanoTime();double energy=0,highEnergy=0;byte[] lastPacket=null;
        for(int frame=0;frame<250;frame++) {
            byte[] pcm=new byte[1280];
            for(int i=0;i<640;i++) {
                int sample=(int)(5000*Math.sin(2*Math.PI*600*(frame*640+i)/32000.)+4000*Math.sin(2*Math.PI*10000*(frame*640+i)/32000.));
                pcm[2*i]=(byte)sample;pcm[2*i+1]=(byte)(sample>>8);
            }
            byte[] bits=(byte[])encode.invoke(codec,(Object)pcm);
            check(bits.length==34&&bits[0]==4,"EVS Primary FT4, 264 speech bits");
            byte[] wire=(byte[])packet.invoke(rtp,codecProfile,102,frame,frame*320,bits,frame==0);
            check(wire.length==46 && wire[12]==4,"header-full ToC payload with CMR disabled");
            Object storage=unpack.invoke(rtp,codecProfile,wire,wire.length);
            byte[] recovered=(byte[])storage.getClass().getMethod("getCodecFrame").invoke(storage);
            check(Arrays.equals(bits,recovered),"RTP speech bits unchanged");
            byte[] out=(byte[])decode.invoke(codec,(Object)recovered);
            check(out.length==1280,"640 decoded SWB samples");
            double cos=0,sin=0;
            for(int i=0;i<640;i++) {
                int sample=(short)((out[2*i]&255)|(out[2*i+1]<<8)); energy+=(double)sample*sample;
                cos+=sample*Math.cos(2*Math.PI*10000*i/32000.);sin+=sample*Math.sin(2*Math.PI*10000*i/32000.);
            }
            if(frame>10)highEnergy+=(cos*cos+sin*sin)/(640.*640.);
            lastPacket=wire;
        }
        double ms=(System.nanoTime()-start)/1e6/250;
        cls("StockEvsCodec").getMethod("close").invoke(codec);
        check(energy>0&&highEnergy>10000,"decoded content above 8 kHz must survive: actual SWB");
        for(byte[] invalid:new byte[][]{new byte[0],Arrays.copyOf(lastPacket,13),Arrays.copyOf(lastPacket,45)})
            check(unpack.invoke(rtp,codecProfile,invalid,invalid.length)==null,"reject truncated RTP");
        byte[] invalid=lastPacket.clone();invalid[12]=5;
        check(unpack.invoke(rtp,codecProfile,invalid,invalid.length)==null,"unsupported EVS rate rejected");
        int ts=(Integer)call("SipDtmfTimestampAllocator","allocate",new Class<?>[]{negotiated,int.class,AtomicInteger.class,AtomicInteger.class},codecProfile,160,new AtomicInteger(0),new AtomicInteger(100));
        check(ts==3300,"DTMF uses 16 kHz RTP clock, not 32 kHz PCM rate");
        // Exercise the actual media adapter, including partial PCM input and
        // native encode/decode factory selection. No microphone is opened.
        Object factory=singleton("SipAudioCodecFactory");
        Class<?> adapterType=cls("SipMediaCodec");
        Method createEncoder=cls("SipAudioCodecFactory").getMethod("createStartedEncoder",negotiated);
        Method createDecoder=cls("SipAudioCodecFactory").getMethod("createStartedDecoder",negotiated);
        Method inputBuffer=adapterType.getMethod("getInputBuffer",int.class);
        Method queue=adapterType.getMethod("queueInputBuffer",int.class,int.class,int.class,long.class,int.class);
        Method dequeue=adapterType.getMethod("dequeueOutputBuffer",MediaCodec.BufferInfo.class,long.class);
        Method outputBuffer=adapterType.getMethod("getOutputBuffer",int.class);
        Method releaseOutput=adapterType.getMethod("releaseOutputBuffer",int.class,boolean.class);
        Object enc=createEncoder.invoke(factory,codecProfile),dec=createDecoder.invoke(factory,codecProfile);
        MediaCodec.BufferInfo info=new MediaCodec.BufferInfo();
        try {
            ByteBuffer input=(ByteBuffer)inputBuffer.invoke(enc,0);
            input.clear();input.put(new byte[640]);queue.invoke(enc,0,0,640,0L,0);
            check((Integer)dequeue.invoke(enc,info,0L)==MediaCodec.INFO_TRY_AGAIN_LATER,"buffer half a PCM frame");
            input.clear();input.put(new byte[1920]);queue.invoke(enc,0,0,1920,0L,0);
            for(int n=0;n<2;n++) {
                check((Integer)dequeue.invoke(enc,info,0L)==0&&info.size==34,"split PCM produces exactly two frames");
                ByteBuffer bytes=(ByteBuffer)outputBuffer.invoke(enc,0);
                byte[] frame=new byte[info.size];bytes.get(frame);releaseOutput.invoke(enc,0,false);
                input=(ByteBuffer)inputBuffer.invoke(dec,0);input.clear();input.put(frame);queue.invoke(dec,0,0,frame.length,0L,0);
                check((Integer)dequeue.invoke(dec,info,0L)==0&&info.size==1280,"native decoder adapter output");
                releaseOutput.invoke(dec,0,false);
                input=(ByteBuffer)inputBuffer.invoke(enc,0);
            }
            check((Integer)dequeue.invoke(enc,info,0L)==MediaCodec.INFO_TRY_AGAIN_LATER,"drained EVS output");
        } finally {
            adapterType.getMethod("release").invoke(enc);adapterType.getMethod("release").invoke(dec);
        }
        // AMR continues through the Android MediaCodec backend.
        for(String getter:new String[]{"getAMR_NB","getAMR_WB"}) {
            Object amr=cls("SipAudioCodecs").getMethod(getter).invoke(singleton("SipAudioCodecs"));
            enc=createEncoder.invoke(factory,amr);dec=createDecoder.invoke(factory,amr);
            try {
                int inputIndex=(Integer)adapterType.getMethod("dequeueInputBuffer",long.class).invoke(enc,1000000L);
                check(inputIndex>=0,"AMR encoder accepts input");
                ByteBuffer input=(ByteBuffer)inputBuffer.invoke(enc,inputIndex);input.clear();
                int size=getter.equals("getAMR_NB")?320:640;input.put(new byte[size]);queue.invoke(enc,inputIndex,0,size,0L,0);
                int index;
                do { index=(Integer)dequeue.invoke(enc,info,1000000L); } while(index==MediaCodec.INFO_OUTPUT_FORMAT_CHANGED);
                check(index>=0&&info.size>0,"AMR encode after adapter refactor");
                releaseOutput.invoke(enc,index,false);
            } finally { adapterType.getMethod("stop").invoke(enc);adapterType.getMethod("release").invoke(enc);adapterType.getMethod("stop").invoke(dec);adapterType.getMethod("release").invoke(dec); }
        }
        System.out.printf(java.util.Locale.ROOT,"PASS installed EVS JNI, SDP constraints, 250 PCM/RTP round-trips, SWB >8kHz energy=%.0f, %.2f ms/frame; no calls/audio devices/network%n",highEnergy,ms);
    }
}
