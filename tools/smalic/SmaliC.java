import java.io.File;
import com.android.tools.smali.dexlib2.Opcodes;
import com.android.tools.smali.dexlib2.writer.builder.DexBuilder;
import com.android.tools.smali.dexlib2.writer.io.FileDataStore;
import brut.androlib.mod.SmaliMod;
/** usage: SmaliC out.dex a.smali b.smali ... （api 21 で組み立てる） */
public class SmaliC {
    public static void main(String[] a) throws Exception {
        DexBuilder b = new DexBuilder(new Opcodes(21, -1));
        for (int i = 1; i < a.length; i++)
            if (!SmaliMod.assembleSmaliFile(new File(a[i]), b, 21)) { System.err.println("FAIL " + a[i]); System.exit(1); }
        b.writeTo(new FileDataStore(new File(a[0])));
        System.out.println("wrote " + a[0]);
    }
}
