/* SPDX-License-Identifier: GPL-2.0 */
// Pure synthetic PCM/SDP/RTP tests. No audio devices, socket or telephony request.
import android.content.Context;
import android.os.Looper;
import java.lang.reflect.*;
import java.util.*;
import java.nio.ByteBuffer;
import java.util.concurrent.ArrayBlockingQueue;
import android.media.MediaCodec;
public final class EvsModesTest {
    static ClassLoader loader;
    static Class<?> type(String name)throws Exception{return loader.loadClass("me.phh.sip."+name);}
    static Object obj(String name)throws Exception{return type(name).getField("INSTANCE").get(null);}
    static Object invoke(Object target,String name,Object... args)throws Exception {
        for(Method m:target.getClass().getMethods())if(m.getName().equals(name)&&m.getParameterCount()==args.length) {
            try{return m.invoke(target,args);}catch(IllegalArgumentException badOverload){continue;}
        }
        throw new NoSuchMethodException(name+"/"+args.length);
    }
    static void check(boolean value,String text){if(!value)throw new AssertionError(text);}
    static Object session(String fmtp)throws Exception{return invoke(obj("EvsSwbProfile"),"negotiate",fmtp,102);}
    static byte[] tone=new byte[1920], silence=new byte[1920];
    static byte[] encode(Object codec,int rate,int band,boolean dtx,int rf,boolean high,byte[] pcm)throws Exception {
        return (byte[])invoke(codec,"encode",pcm,rate,band,dtx,rf,high);
    }
    static byte[] wire(byte[] payload,int timestamp) {
        byte[] p=new byte[12+payload.length];p[0]=(byte)128;p[1]=102;
        p[4]=(byte)(timestamp>>24);p[5]=(byte)(timestamp>>16);p[6]=(byte)(timestamp>>8);p[7]=(byte)timestamp;
        System.arraycopy(payload,0,p,12,payload.length);return p;
    }
    static List<String> sdp(String fmtp){return Arrays.asList("m=audio 9 RTP/AVP 102 98 97 101 100","a=rtpmap:102 EVS/16000","a=fmtp:102 "+fmtp,"a=rtpmap:98 AMR-WB/16000","a=rtpmap:97 AMR/8000","a=rtpmap:101 telephone-event/16000","a=rtpmap:100 telephone-event/8000");}
    static int getInt(Object o,String key)throws Exception{return (Integer)invoke(o,key);}
    @SuppressWarnings("unchecked")
    static List<byte[]> frames(Object parsed)throws Exception{return (List<byte[]>)invoke(parsed,"getFrames");}
    public static void main(String[] args){try{run(args);System.exit(0);}catch(Throwable t){t.printStackTrace();System.exit(1);}}
    static void run(String[] args)throws Exception {
        Looper.prepareMainLooper();
        if(args.length==2) loader=(ClassLoader)Class.forName("dalvik.system.PathClassLoader").getConstructor(String.class,String.class,ClassLoader.class).newInstance(args[0],args[1],ClassLoader.getSystemClassLoader());
        else {
            Class<?> at=Class.forName("android.app.ActivityThread");Object thread=at.getMethod("systemMain").invoke(null);
            Context system=(Context)at.getMethod("getSystemContext").invoke(thread);
            loader=system.createPackageContext("me.phh.ims",Context.CONTEXT_INCLUDE_CODE|Context.CONTEXT_IGNORE_SECURITY).getClassLoader();
        }
        check((Boolean)invoke(obj("EvsSwbProfile"),"available"),"native availability");
        for(int i=0;i<960;i++){int v=(int)(1800*(Math.sin(2*Math.PI*600*i/48000.)+Math.sin(2*Math.PI*3500*i/48000.)+Math.sin(2*Math.PI*6000*i/48000.)+Math.sin(2*Math.PI*10000*i/48000.)+Math.sin(2*Math.PI*12000*i/48000.)+Math.sin(2*Math.PI*18000*i/48000.)));tone[2*i]=(byte)v;tone[2*i+1]=(byte)(v>>8);}
        if(args.length==1&&args[0].equals("--smoke")) {
            Object codec=type("StockEvsCodec").getConstructor(int.class).newInstance(48000);
            try {
                for(int[] mode:new int[][]{{13200,2},{128000,3},{8850,1},{5900,0}}) {
                    byte[] frame=encode(codec,mode[0],mode[1],false,0,true,tone);
                    check(((byte[])invoke(codec,"decode",frame)).length==1920,"installed native frame");
                }
            } finally {invoke(codec,"close");}
            System.out.println("PASS installed app/native namespace: Primary SWB/FB/SC-VBR and AMR-WB IO encode/decode; no calls or audio/network devices");
            return;
        }
        String defaults=(String)type("EvsSwbProfile").getField("FMTP").get(null);
        Object all=session(defaults);check(all!=null,"default negotiation");
        String[] invalid={"br=8-5.9","br=13.2x","br=0.1-128","br=5.9001","br=5.9;bw=fb","hf-only=2","evs-mode-switch=2","cmr=2","mode-set=9","mode-set=","ch-send=2","bw=wb-fb","dtx=0;dtx-recv=1","br=13.2;br-send=24.4","hf-only=0;hf-only=1"};
        for(String f:invalid)check(session(f)==null,"reject invalid SDP: "+f);
        for(String f:new String[]{"","br=5.9;bw=nb","br=128;bw=fb","br=9.6-24.4;bw=nb-swb;hf-only=1;cmr=1;dtx=0","evs-mode-switch=1;mode-set=0,2,8","br-send=9.6-13.2;br-recv=24.4;bw-send=swb;bw-recv=fb","ch-aw-recv=7;bw=swb;br=13.2"})check(session(f)!=null,"valid SDP: "+f);
        Object asym=session("br-send=9.6-13.2;br-recv=24.4;bw-send=swb;bw-recv=fb");
        check(((Set<?>)invoke(asym,"getTxRates")).equals(new HashSet<>(Arrays.asList(24400))),"directional transmit rate");
        check(((String)invoke(asym,"getFmtp")).contains("br-send=24.4"),"answer swaps directional attributes");
        Object constrained=session("br=9.6-13.2;bw=swb;mode-set=0,2,8");
        invoke(constrained,"receiveCmr",0xb3);check(getInt(invoke(constrained,"encodeMode"),"getBitrate")==9600,"CMR Primary rate");
        invoke(constrained,"receiveCmr",0xa5);check(getInt(invoke(constrained,"encodeMode"),"getBitrate")==9600,"out-of-range CMR ignored");
        invoke(constrained,"receiveCmr",0x98);check((Boolean)invoke(invoke(constrained,"encodeMode"),"getIo"),"CMR switch to IO");
        invoke(constrained,"receiveCmr",0xb4);check(!(Boolean)invoke(invoke(constrained,"encodeMode"),"getIo"),"CMR switch to Primary");
        Object rf=session("br=13.2;bw=swb;ch-aw-recv=0");invoke(rf,"receiveCmr",0xe7);
        check(getInt(invoke(rf,"encodeMode"),"getRfOffset")==7,"CMR channel aware offset");
        Object codec=type("StockEvsCodec").getConstructor(int.class).newInstance(48000);
        Object rtp=obj("EvsRtpPayload"),frameUtil=obj("EvsFrames");
        int[] primary={5900,7200,8000,9600,13200,16400,24400,32000,48000,64000,96000,128000};
        int[] io={6600,8850,12650,14250,15850,18250,19850,23050,23850};
        int cases=0,sid=0,noData=0,packets=0;double energy=0,swbPower=0,fbPower=0;long started=System.nanoTime();List<byte[]> examples=new ArrayList<>();
        try {
            for(int mode=0;mode<2;mode++)for(int band=0;band<4;band++)for(int rate:mode==0?primary:io)for(boolean dtx:new boolean[]{false,true}) {
                boolean valid=mode==1?band==1:(band==0?rate<=24400:band==1||band==2&&rate>=9600||band==3&&rate>=16400);
                if(!valid)continue;
                System.out.println("case "+rate+" "+band+" "+dtx);
                for(int n=0;n<80;n++) {
                    byte[] bits=encode(codec,rate,band,dtx,0,true,n<20?tone:silence);
                    int toc=bits[0]&255,ft=toc&15;
                    check((Integer)invoke(frameUtil,"size",toc)==bits.length,"native ToC/length");
                    byte[] pcm=(byte[])invoke(codec,"decode",bits);check(pcm.length==1920,"PCM frame size");
                    if(n<20)for(int i=0;i<960;i++){int v=(short)((pcm[2*i]&255)|(pcm[2*i+1]<<8));energy+=(double)v*v;}
                    if(mode==0&&!dtx&&n==19&&((band==2&&rate==24400)||(band==3&&rate==128000))) {
                        double power=0;
                        for(int bin=band==2?160:320;bin<(band==2?280:400);bin++) {
                            double cos=0,sin=0;
                            for(int i=0;i<960;i++){int v=(short)((pcm[2*i]&255)|(pcm[2*i+1]<<8));cos+=v*Math.cos(2*Math.PI*bin*i/960.);sin+=v*Math.sin(2*Math.PI*bin*i/960.);}
                            power+=(cos*cos+sin*sin)/(960.*960.);
                        }
                        if(band==2)swbPower=power;else fbPower=power;
                    }
                    if(ft==15){noData++;continue;}
                    if(ft==(mode==0?12:9))sid++;
                    if(n==19)examples.add(bits);
                    for(String options:new String[]{"hf-only=0;cmr=0","hf-only=1;cmr=0","hf-only=0;cmr=1","hf-only=1;cmr=1","hf-only=1;cmr=-1"}) {
                        Object sess=session(options);
                        byte[] payload=(byte[])invoke(rtp,"payload",bits,sess);check(payload!=null,"RTP payload");
                        byte[] packet=wire(payload,n*320);
                        Object parsed=invoke(rtp,"parse",packet,packet.length,sess);check(parsed!=null,"RTP parse toc="+toc+" len="+payload.length+" "+options);
                        check(Arrays.equals(frames(parsed).get(0),bits),"round-trip speech bits");packets++;
                    }
                }
                cases++;
            }
            check(cases==88&&sid>0&&noData>0&&energy>0,"complete native mode/DTX matrix");
            check(swbPower>10000&&fbPower>10000,"actual SWB/FB spectral content "+swbPower+" "+fbPower);
            for(int band:new int[]{1,2})for(int offset:new int[]{2,3,5,7})for(boolean high:new boolean[]{false,true})for(int n=0;n<20;n++) {
                byte[] bits=encode(codec,13200,band,false,offset,high,tone);
                check(bits.length==34,"CA frame length");check(((byte[])invoke(codec,"decode",bits)).length==1920,"CA decode");
            }
            // Same native states across bit-rate, bandwidth and Primary/IO changes.
            for(int pass=0;pass<3;pass++)for(int rate:new int[]{13200,24400,12650,8850,96000,6600,5900,128000}) {
                int band=Arrays.stream(io).anyMatch(r->r==rate)?1:rate==5900?1:rate>=16400?3:2;
                byte[] bits=encode(codec,rate,band,false,0,true,tone);
                check(((byte[])invoke(codec,"decode",bits)).length==1920,"native mode transition");
            }
            for(int toc:new int[]{14,15,0x3e,0x3f})check(((byte[])invoke(codec,"decode",new byte[]{(byte)toc})).length==1920,"PLC/no-data mode="+toc);
        } finally {invoke(codec,"close");}
        // Independent TS 26.445 Compact IO bit-order fixture: d(0) must
        // move behind d(K-1), not remain directly after the three CMR bits.
        byte[] ioFixture=new byte[18];ioFixture[0]=0x30;ioFixture[1]=(byte)128;
        byte[] compact=(byte[])invoke(rtp,"payload",ioFixture,all);
        check(compact.length==17&&(compact[0]&255)==224&&compact[16]==2,"Compact IO d(0) placement");
        compact[0]=(byte)((compact[0]&31)|32);
        byte[] cmrWire=wire(compact,0);Object cmrPacket=invoke(rtp,"parse",cmrWire,cmrWire.length,all);
        check(cmrPacket!=null&&(Integer)invoke(cmrPacket,"getCmr")==0x91,"Compact IO CMR mapping");
        // Aggregate twelve frames in Header-Full mode, then malformed and padded RTP.
        Object full=session("hf-only=1;cmr=1");
        List<Byte> aggregate=new ArrayList<>();aggregate.add((byte)255);
        for(int n=0;n<12;n++)aggregate.add((byte)((examples.get(n)[0]&63)|(n<11?64:0)));
        for(int n=0;n<12;n++)for(int i=1;i<examples.get(n).length;i++)aggregate.add(examples.get(n)[i]);
        byte[] payload=new byte[aggregate.size()];for(int i=0;i<payload.length;i++)payload[i]=aggregate.get(i);
        byte[] packet=wire(payload,320);Object parsed=invoke(rtp,"parse",packet,packet.length,full);
        check(parsed!=null&&frames(parsed).size()==12,"aggregated frames");
        byte[] padded=Arrays.copyOf(packet,packet.length+4);padded[0]|=32;padded[padded.length-1]=4;
        check(invoke(rtp,"parse",padded,padded.length,full)!=null,"RTP padding");
        padded[padded.length-1]=0;check(invoke(rtp,"parse",padded,padded.length,full)==null,"bad RTP padding");
        for(int length:new int[]{0,1,11,12,13,packet.length-1})check(invoke(rtp,"parse",packet,length,full)==null,"truncated RTP");
        Random random=new Random(42);for(int i=0;i<5000;i++){byte[] bad=new byte[random.nextInt(600)];random.nextBytes(bad);invoke(rtp,"parse",bad,bad.length,full);}
        // Real adapters and CNG path, without AudioRecord/AudioTrack.
        Object negotiated=invoke(session("br=13.2;bw=swb;dtx=1"),"codec");
        Object factory=obj("SipAudioCodecFactory"),enc=invoke(factory,"createStartedEncoder",negotiated),dec=invoke(factory,"createStartedDecoder",negotiated);
        try {
            MediaCodec.BufferInfo info=new MediaCodec.BufferInfo();ByteBuffer input=(ByteBuffer)invoke(enc,"getInputBuffer",0);
            input.clear();input.put(new byte[960]);invoke(enc,"queueInputBuffer",0,0,960,0L,0);
            check((Integer)invoke(enc,"dequeueOutputBuffer",info,0L)==MediaCodec.INFO_TRY_AGAIN_LATER,"partial PCM held");
            input.clear();input.put(tone);input.put(new byte[960]);invoke(enc,"queueInputBuffer",0,0,2880,0L,0);
            for(int n=0;n<2;n++) {
                check((Integer)invoke(enc,"dequeueOutputBuffer",info,0L)==0,"PCM split frames");
                byte[] bits=new byte[info.size];((ByteBuffer)invoke(enc,"getOutputBuffer",0)).get(bits);invoke(enc,"releaseOutputBuffer",0,false);
                input=(ByteBuffer)invoke(dec,"getInputBuffer",0);input.clear();input.put(bits);invoke(dec,"queueInputBuffer",0,0,bits.length,0L,0);
                check((Integer)invoke(dec,"dequeueOutputBuffer",info,0L)==0&&info.size==1920,"decoder adapter");invoke(dec,"releaseOutputBuffer",0,false);
            }
            check(((byte[])invoke(dec,"evsConcealment")).length==1920,"codec loss concealment");
        } finally {invoke(enc,"release");invoke(dec,"release");}
        Object negotiator=obj("SipAudioCodecNegotiator");
        for(String method:new String[]{"selectIncomingSpeechCodecFromOffer","selectOutgoingSpeechCodecFromAnswer"}) {
            Object selected=invoke(negotiator,method,"test",sdp("evs-mode-switch=1;mode-set=0,2,8;hf-only=0;cmr=1"),"offline",true);
            check((Boolean)invoke(selected,"isEvs"),"EVS negotiation");
            selected=invoke(negotiator,method,"test",sdp("br=invalid"),"offline",true);
            check(invoke(selected,"getName").equals("AMR-WB"),"AMR-WB fallback");
            selected=invoke(negotiator,method,"test",sdp("br=invalid"),"offline",false);
            check(invoke(selected,"getName").equals("AMR-NB"),"AMR fallback");
        }
        List<String> amrOnly=Arrays.asList("m=audio 9 RTP/AVP 98 101","a=rtpmap:98 AMR-WB/16000","a=fmtp:98 mode-set=0,1;octet-align=0","a=rtpmap:101 telephone-event/16000");
        Object selected=invoke(negotiator,"selectIncomingSpeechCodecFromOffer","test",amrOnly,"offline",true);
        check(getInt(selected,"getBitRate")==8850,"AMR-WB mode-set retained");
        List<String> attrs=new ArrayList<>();for(String line:amrOnly)if(line.startsWith("a="))attrs.add(line.substring(2));
        Object update=invoke(obj("SipUpdateSdpMediaSelector"),"select","test",attrs,new HashSet<>(Arrays.asList(98,101)),negotiated,"synthetic");
        check(update!=null&&invoke(invoke(update,"getSelectedAudioCodec"),"getName").equals("AMR-WB"),"UPDATE EVS -> AMR-WB");
        Object reinvite=invoke(obj("SipInDialogInvite"),"selectMedia",attrs,negotiated,"test",new HashSet<>(Arrays.asList(98,101)));
        check(reinvite!=null&&invoke(invoke(reinvite,"getSelectedAudioCodec"),"getName").equals("AMR-WB"),"re-INVITE EVS -> AMR-WB");
        // Validate the actual Android fallback encoder honors the selected
        // bitrate, rather than merely checking a capability flag.
        for(boolean wideband:new boolean[]{false,true}) {
            String name=wideband?"AMR-WB":"AMR";int sampleRate=wideband?16000:8000;
            List<String> limited=Arrays.asList("m=audio 9 RTP/AVP 98 101","a=rtpmap:98 "+name+"/"+sampleRate,
                "a=fmtp:98 mode-set=0,1;octet-align=1","a=rtpmap:101 telephone-event/"+sampleRate);
            Object fallback=invoke(negotiator,"selectIncomingSpeechCodecFromOffer","test",limited,"offline",true);
            enc=invoke(factory,"createStartedEncoder",fallback);
            try {
                int index=(Integer)invoke(enc,"dequeueInputBuffer",1000000L);
                check(index>=0,"fallback input buffer");
                ByteBuffer input=(ByteBuffer)invoke(enc,"getInputBuffer",index);input.clear();input.put(new byte[sampleRate/50*2]);
                invoke(enc,"queueInputBuffer",index,0,sampleRate/50*2,0L,0);
                MediaCodec.BufferInfo info=new MediaCodec.BufferInfo();int output;
                do {output=(Integer)invoke(enc,"dequeueOutputBuffer",info,1000000L);}while(output==MediaCodec.INFO_OUTPUT_FORMAT_CHANGED);
                check(output>=0&&info.size>0,"fallback encoder output");
                ByteBuffer encoded=(ByteBuffer)invoke(enc,"getOutputBuffer",output);
                int frameType=(encoded.get(info.offset)>>3)&15;
                check(frameType==1,"fallback selected mode-set encoder FT1: "+name+" actual="+frameType);
                byte[] storage=new byte[info.size];encoded.position(info.offset);encoded.get(storage);
                Object legacyRtp=obj("SipAmrRtpPayload");
                byte[] oa=(byte[])invoke(legacyRtp,"buildBandwidthEfficientRtpPacketFromStorageFrame",fallback,98,0,0,storage,true);
                check(oa!=null&&(oa[12]&255)==240,"octet-aligned fallback packet");
                Object recovered=invoke(legacyRtp,"storageFrameFromBandwidthEfficientRtp",fallback,oa,oa.length);
                check(recovered!=null&&Arrays.equals((byte[])invoke(recovered,"getCodecFrame"),storage),"octet-aligned fallback receive");
                invoke(enc,"releaseOutputBuffer",output,false);
            } finally {invoke(enc,"stop");invoke(enc,"release");}
        }
        // Decoder packet path: duplicates are discarded and missing timestamps
        // generate concealment before the next speech frame.
        Object sess=session("br=5.9-128;bw=nb-fb;hf-only=1;cmr=1");
        dec=invoke(factory,"createStartedDecoder",invoke(sess,"codec"));
        try {
            ArrayBlockingQueue<byte[]> queue=new ArrayBlockingQueue<>(32);
            byte[] speech=examples.get(0);
            byte[] body=(byte[])invoke(rtp,"payload",speech,sess);
            byte[] first=wire(body,0),second=wire(body,640);
            invoke(dec,"queueEvsPacket",first,first.length,queue);
            check(queue.size()==1,"packet decoder accepts first frame");
            invoke(dec,"queueEvsPacket",first,first.length,queue);check(queue.size()==1,"duplicate ignored");
            invoke(dec,"queueEvsPacket",second,second.length,queue);check(queue.size()==3,"timestamp gap concealment");
            queue.clear();invoke(dec,"evsConcealment");
            byte[] next=wire(body,1280);invoke(dec,"queueEvsPacket",next,next.length,queue);
            check(queue.size()==1,"do not conceal an elapsed playout slot twice");
        } finally {invoke(dec,"release");}
        System.out.println("PASS EVS modes: "+cases+" native mode/DTX cases; "+packets+" RTP round-trips; SID="+sid+", NO_DATA="+noData+"; CMR/RF/PLC/SDP/asymmetry/aggregation/fuzz/media adapters/AMR fallback; "+Math.round((System.nanoTime()-started)/1e6)+" ms; no calls or audio/network devices");
    }
}
