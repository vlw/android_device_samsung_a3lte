/* SPDX-License-Identifier: GPL-2.0 */
// Synthetic SIP/RP messages only. Output is captured in memory; no SIM/network access.
import android.content.ContextWrapper;
import java.io.*;
import java.lang.reflect.*;
import java.util.*;
public final class ImsSmsProtocolTest {
    interface Fn { Object run(Object[] args) throws Exception; }
    static Object function(int arity, Fn fn) throws Exception {
        Class<?> c = Class.forName("kotlin.jvm.functions.Function" + arity);
        return Proxy.newProxyInstance(c.getClassLoader(), new Class[]{c},
                (p,m,a) -> fn.run(a));
    }
    static Object unit() throws Exception { return Class.forName("kotlin.Unit").getField("INSTANCE").get(null); }
    static void check(boolean b, String text) { if (!b) throw new AssertionError(text); }
    static Object request(byte[] body) throws Exception {
        Class<?> method = Class.forName("me.phh.sip.SipMethod");
        Object message = Enum.valueOf((Class)method, "MESSAGE");
        Map<String,List<String>> headers = new HashMap<>();
        headers.put("from", Arrays.asList("<sip:smsc@example.invalid>"));
        headers.put("call-id", Arrays.asList("synthetic-network-call"));
        headers.put("cseq", Arrays.asList("900 MESSAGE"));
        return Class.forName("me.phh.sip.SipRequest").getConstructor(method,String.class,Map.class,byte[].class,boolean.class)
                .newInstance(message,"sip:user@example.invalid",headers,body,false);
    }
    static byte[] rp(int ref, int tpType) { return new byte[]{1,(byte)ref,0,0,2,(byte)tpType,7}; }
    public static void main(String[] args) throws Exception {
        Class<?> settingsClass = Class.forName("me.phh.sip.SipCarrierSettings");
        Object companion = settingsClass.getField("Companion").get(null);
        Object settings = companion.getClass().getMethod("fromSimOperator",String.class).invoke(companion,"25701");
        Class<?> handlerClass = Class.forName("me.phh.sip.SipSmsHandler");
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        Constructor<?> ctor = Arrays.stream(handlerClass.getConstructors()).filter(c -> c.getParameterCount()==11).findFirst().get();
        Object handler = ctor.newInstance("SmsSyntheticTest",new ContextWrapper(null),-1,settings,
                function(0,a -> "example.invalid"),function(0,a -> new HashMap<>()),
                function(0,a -> "sip:user@example.invalid"),function(0,a -> output),
                function(2,a -> unit()),function(2,a -> unit()),function(2,a -> unit()));
        List<Integer> deliveries = new ArrayList<>(), reports = new ArrayList<>();
        Class<?> f3 = Class.forName("kotlin.jvm.functions.Function3");
        handlerClass.getMethod("setOnSmsReceived",f3).invoke(handler,function(3,a -> { deliveries.add((Integer)a[0]); return unit(); }));
        handlerClass.getMethod("setOnSmsStatusReportReceived",f3).invoke(handler,function(3,a -> { reports.add((Integer)a[0]); return unit(); }));
        Method handle = handlerClass.getMethod("handleSms",Class.forName("me.phh.sip.SipRequest"));
        Method ack = handlerClass.getMethod("sendSmsAck",int.class,int.class,boolean.class);
        check((Integer)handle.invoke(handler,request(rp(99,0)))==200,"delivery accepted");
        ack.invoke(handler,deliveries.get(0),7,false);
        byte[] wire = output.toByteArray();
        int offset = new String(wire,"ISO-8859-1").indexOf("\r\n\r\n")+4;
        check(wire[offset]==2 && wire[offset+1]==99,"RP ACK must echo RP reference, not TP-MR");
        check(!new String(wire,"ISO-8859-1").contains("Cseq: 900"),"new MESSAGE uses fresh CSeq");
        output.reset();
        handle.invoke(handler,request(rp(100,2)));
        check(deliveries.size()==1 && reports.size()==1,"status report routed separately");
        ack.invoke(handler,reports.get(0),7,true);
        wire=output.toByteArray(); offset=new String(wire,"ISO-8859-1").indexOf("\r\n\r\n")+4;
        check(wire[offset]==4 && (wire[offset+1]&255)==100,"negative RP report");
        handle.invoke(handler,request(new byte[]{3,55}));
        check(reports.size()==1,"RP ACK must not fabricate a delivery report");
        for(byte[] malformed:new byte[][]{new byte[0],new byte[]{1},new byte[]{1,1,(byte)255}})
            check((Integer)handle.invoke(handler,request(malformed))==500,"malformed frame rejected");
        output.reset(); ack.invoke(handler,9999,0,false);
        check(output.size()==0,"unknown token ignored");
        // Adapter errors must complete Android's request instead of leaving it pending.
        Object adapter = Class.forName("me.phh.ims.PhhImsSms").getConstructor(int.class).newInstance(0);
        Class<?> listenerType = Class.forName("android.telephony.ims.aidl.IImsSmsListener");
        List<Integer> results = new ArrayList<>();
        Object listener = Proxy.newProxyInstance(listenerType.getClassLoader(),new Class[]{listenerType},
            (proxy,method,a) -> { if(method.getName().equals("onSendSmsResult")) results.add((Integer)a[2]); return null; });
        adapter.getClass().getMethod("registerSmsListener",listenerType).invoke(adapter,listener);
        Method send = adapter.getClass().getMethod("sendSms",int.class,int.class,String.class,String.class,boolean.class,byte[].class);
        send.invoke(adapter,1,0,"3gpp2",null,false,new byte[]{1,0});
        send.invoke(adapter,2,0,"3gpp",null,false,new byte[]{1,0});
        check(results.equals(Arrays.asList(2,4)),"invalid format fails; missing transport uses CS fallback once");
        System.out.println("PASS SMS delivery/report routing, RP reference ACK/ERROR, fresh CSeq, unsolicited RP ACK and malformed frames; no network access");
    }
}
