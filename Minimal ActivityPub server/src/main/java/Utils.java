import com.google.gson.Gson;
import com.google.gson.JsonElement;
import com.google.gson.stream.JsonWriter;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.security.KeyFactory;
import java.security.NoSuchAlgorithmException;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.spec.InvalidKeySpecException;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.X509EncodedKeySpec;
import java.util.Base64;

/**
 * Всякая скучная хрень, в основном криптографическая, которая необходима, но не очень достойна находиться в файле с основной функциональностью
 */
public class Utils{
	public static String readTextFile(String name) throws IOException{
		try(BufferedReader reader=new BufferedReader(new FileReader(name, StandardCharsets.UTF_8))){
			String line;
			StringBuilder sb=new StringBuilder();
			while((line=reader.readLine())!=null){
				sb.append(line);
				sb.append('\n');
			}
			return sb.toString();
		}
	}

	private static byte[] decodeRsaKey(String serializedKey){
		serializedKey=serializedKey.replaceAll("-----(BEGIN|END) (RSA )?(PUBLIC|PRIVATE) KEY-----", "").replaceAll("[^A-Za-z0-9+/=]", "").trim();
		return Base64.getDecoder().decode(serializedKey);
	}

	public static PublicKey decodePublicKey(String serializedKey) throws NoSuchAlgorithmException, InvalidKeySpecException{
		return KeyFactory.getInstance("RSA").generatePublic(new X509EncodedKeySpec(decodeRsaKey(serializedKey)));
	}

	public static PrivateKey decodePrivateKey(String serializedKey) throws NoSuchAlgorithmException, InvalidKeySpecException{
		return KeyFactory.getInstance("RSA").generatePrivate(new PKCS8EncodedKeySpec(decodeRsaKey(serializedKey)));
	}

	public static String prettyPrintJSON(JsonElement el){
		StringWriter writer=new StringWriter();
		JsonWriter jsonWriter=new JsonWriter(writer);
		jsonWriter.setHtmlSafe(true);
		jsonWriter.setIndent("  ");
		new Gson().toJson(el, jsonWriter);
		return writer.toString();
	}
}
