package io.github.fflmod.fflmod.core;

import com.android.apksig.ApkSigner;

import java.io.File;
import java.io.InputStream;
import java.security.KeyFactory;
import java.security.PrivateKey;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.security.spec.PKCS8EncodedKeySpec;
import java.util.Collections;

/**
 * fflmod の鍵（keys/fflmod.jks と同じもの。公開してよい鍵）で署名する。
 * v1（JAR 署名）は java.util.Base64 を使うので Android 8 以上だけで付ける。Android 7 は v2 だけで足りる。
 */
public final class Signer {
    private final PrivateKey key;
    private final X509Certificate cert;

    public Signer(byte[] pkcs8, InputStream certDer) throws Exception {
        key = KeyFactory.getInstance("RSA").generatePrivate(new PKCS8EncodedKeySpec(pkcs8));
        cert = (X509Certificate) CertificateFactory.getInstance("X.509").generateCertificate(certDer);
    }

    public void sign(File in, File out, boolean v1) throws Exception {
        ApkSigner.SignerConfig cfg =
                new ApkSigner.SignerConfig.Builder("FFLMOD", key, Collections.singletonList(cert)).build();
        ApkSigner.Builder b = new ApkSigner.Builder(Collections.singletonList(cfg))
                .setInputApk(in)
                .setOutputApk(out)
                .setV1SigningEnabled(v1)
                .setV2SigningEnabled(true)
                .setV3SigningEnabled(false);
        if (!v1) b.setMinSdkVersion(24);   // v2 だけで足りることを伝える
        b.build().sign();
    }
}
