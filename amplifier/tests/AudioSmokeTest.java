import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
public class AudioSmokeTest {
    static AudioTrack track(int rate) {
        short[] wave = new short[rate * 2 * 2];
        for (int i=0;i<rate*2;i++) {
            short value=(short)(Math.sin(2*Math.PI*440*i/rate)*6000);
            wave[i*2]=wave[i*2+1]=value;
        }
        AudioTrack a=new AudioTrack(AudioManager.STREAM_MUSIC, rate,
            AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT,
            wave.length*2, AudioTrack.MODE_STATIC);
        if(a.getState()==AudioTrack.STATE_UNINITIALIZED)throw new RuntimeException("init failed");
        a.setVolume(0.03f);
        if(a.write(wave,0,wave.length)!=wave.length)throw new RuntimeException("write failed");
        return a;
    }
    public static void main(String[] args) throws Exception {
        AudioTrack first=track(48000); first.play();
        Thread.sleep(500);
        AudioTrack second=track(44100); second.play();
        Thread.sleep(500);first.stop();first.release();
        Thread.sleep(700);second.stop();second.release();
        Thread.sleep(4000);
        AudioTrack third=track(48000);
        if (third.setLoopPoints(0,48000*2,-1)!=AudioTrack.SUCCESS)
            throw new RuntimeException("loop setup failed");
        third.play();Thread.sleep(8000);
        third.stop();third.release();Thread.sleep(4000);
        System.out.println("AudioTrack 48k/44.1k overlap, stop and restart passed");
        System.exit(0);
    }
}
