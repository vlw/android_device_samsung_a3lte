// SPDX-License-Identifier: Apache-2.0
// Run via app_process under a dedicated diagnostic UID. Only these aliases change.
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyInfo;
import android.security.keystore.KeyProperties;
import java.security.*;
import javax.crypto.*;
import javax.crypto.spec.GCMParameterSpec;

public class KeystoreSmoke {
    static String RSA = "a3tee_diagnostic_rsa";
    static String AES = "a3tee_diagnostic_aes";
    public static void main(String[] args) throws Exception {
        Class.forName("android.security.keystore2.AndroidKeyStoreProvider").getMethod("install").invoke(null);
        KeyStore store = KeyStore.getInstance("AndroidKeyStore"); store.load(null);
        if (args.length != 1) throw new IllegalArgumentException("create|verify|cleanup");
        if (args[0].endsWith("-native")) { RSA += "_native"; AES += "_native"; }
        if (args[0].startsWith("cleanup")) {
            store.deleteEntry(RSA); store.deleteEntry(AES); return;
        }
        if (args[0].startsWith("create")) {
            if (store.containsAlias(RSA) || store.containsAlias(AES)) throw new IllegalStateException("Aliases already exist");
            KeyPairGenerator rsa = KeyPairGenerator.getInstance("RSA", "AndroidKeyStore");
            rsa.initialize(new KeyGenParameterSpec.Builder(RSA, KeyProperties.PURPOSE_SIGN|KeyProperties.PURPOSE_VERIFY)
                .setKeySize(2048).setDigests(KeyProperties.DIGEST_SHA256)
                .setSignaturePaddings(KeyProperties.SIGNATURE_PADDING_RSA_PKCS1).build());
            rsa.generateKeyPair();
            KeyGenerator aes = KeyGenerator.getInstance("AES", "AndroidKeyStore");
            aes.init(new KeyGenParameterSpec.Builder(AES,KeyProperties.PURPOSE_ENCRYPT|KeyProperties.PURPOSE_DECRYPT)
                .setKeySize(128).setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build());
            aes.generateKey();
        }
        PrivateKey key = (PrivateKey)store.getKey(RSA,null);
        Signature sign = Signature.getInstance("SHA256withRSA"); sign.initSign(key);
        byte[] input = "A300F native TEE smoke".getBytes("UTF-8"); sign.update(input);
        byte[] signature = sign.sign();
        sign.initVerify(store.getCertificate(RSA)); sign.update(input);
        if (!sign.verify(signature)) throw new IllegalStateException("RSA verification failed");
        KeyInfo info = KeyFactory.getInstance("RSA","AndroidKeyStore").getKeySpec(key,KeyInfo.class);
        System.out.println("RSA verified; securityLevel="+info.getSecurityLevel()+" insideSecureHardware="+info.isInsideSecureHardware());
        SecretKey secret = (SecretKey)store.getKey(AES,null);
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding"); cipher.init(Cipher.ENCRYPT_MODE,secret);
        byte[] encrypted = cipher.doFinal(input), iv = cipher.getIV();
        cipher.init(Cipher.DECRYPT_MODE,secret,new GCMParameterSpec(128,iv));
        if (!MessageDigest.isEqual(cipher.doFinal(encrypted),input)) throw new IllegalStateException("AES mismatch");
        KeyInfo aesInfo = (KeyInfo)SecretKeyFactory.getInstance("AES","AndroidKeyStore").getKeySpec(secret,KeyInfo.class);
        System.out.println("AES verified; securityLevel="+aesInfo.getSecurityLevel()+" insideSecureHardware="+aesInfo.isInsideSecureHardware());
        if (aesInfo.isInsideSecureHardware()) throw new IllegalStateException("Software AES must not claim hardware backing");
    }
}
