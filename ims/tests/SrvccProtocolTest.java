// SPDX-License-Identifier: GPL-2.0
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Collections;
import me.phh.ims.SamsungSrvccCallList;
import me.phh.ims.SrvccTransferState;
import static me.phh.ims.SrvccTransferState.Action.*;

public final class SrvccProtocolTest {
    static void check(boolean b) { if (!b) throw new AssertionError(); }
    static SamsungSrvccCallList.Call call(String key, int state) {
        return new SamsungSrvccCallList.Call(key, "+12345", true, state, false);
    }
    public static void main(String[] args) throws Exception {
        SamsungSrvccCallList codec = new SamsungSrvccCallList();
        byte[] active = codec.encode(Arrays.asList(call("a", 1), call("b", 2)));
        check(active[5] == 2 && active[8] == 5 && active[9] == 1 && active[10] == 1);
        byte[] reordered = codec.encode(Arrays.asList(call("b", 1), call("a", 2)));
        check(reordered[10] == 2 && reordered[24] == 1); // IDs survive order/state changes.
        byte[] invalidNumber = codec.encode(Collections.singletonList(
            new SamsungSrvccCallList.Call("a", "sip:private@example.test", false, 5, false)));
        check(invalidNumber.length == 16 && invalidNumber[11] == 2 && invalidNumber[14] == 0);
        check(codec.encode(Collections.emptyList()).length == 8);
        boolean rejected = false;
        try { codec.encode(Arrays.asList(call("x",1),call("x",2))); }
        catch (IllegalArgumentException e) { rejected = true; }
        check(rejected);
        SrvccTransferState state = new SrvccTransferState();
        check(state.update(1, true) == IGNORE);
        check(state.update(0, true) == START);
        state.timeout();
        check(!state.isPending());
        check(state.update(1, true) == COMPLETE); // Late real completion of the same dialog.
        check(state.update(0, true) == START);
        state.timeout();
        check(state.update(1, false) == IGNORE); // A later unrelated call must survive.
        check(state.update(0, true) == START);
        state.timeout();
        check(state.update(2, true) == RETAIN); // Late real failure still reaches Android.
        check(state.update(0, false) == IGNORE);
        check(state.update(0, true) == START && state.isPending());
        check(state.update(0, true) == IGNORE);
        check(state.update(2, true) == RETAIN && !state.isPending());
        check(state.update(1, true) == IGNORE);
        check(state.update(0, true) == START);
        check(state.update(3, true) == RETAIN);
        check(state.update(0, true) == START);
        check(state.update(1, true) == COMPLETE);
        check(state.update(1, true) == IGNORE);
        if (args.length == 1) Files.write(Paths.get(args[0]), active);
        System.out.println("PASS stable call IDs, stock frame, private-number handling, SRVCC complete/failure/cancel");
    }
}
