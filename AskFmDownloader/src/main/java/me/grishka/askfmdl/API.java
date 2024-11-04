package me.grishka.askfmdl;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonParser;

import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.security.InvalidKeyException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ThreadLocalRandom;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

public class API{
	public static final String deviceID=String.format("%016x", ThreadLocalRandom.current().nextLong());
	public static String accessToken, requestToken;
	public static HttpClient client=HttpClient.newHttpClient();
	private static final URI API_URL=URI.create("https://api.ask.fm");
	private static final Gson gson=new GsonBuilder().disableHtmlEscaping().create();

	public static void getAccessToken(Callback callback){
		HttpRequest req=HttpRequest.newBuilder(new UriBuilder(API_URL).path("token").queryParam("did", deviceID).build())
				.build();
		sendRequest(req, null, callback);
	}

	public static void logIn(String username, String password, Callback callback){
		HttpRequest req=HttpRequest.newBuilder(new UriBuilder(API_URL).path("authorize").build())
				.POST(HttpRequest.BodyPublishers.noBody())
				.build();
		sendRequest(req, Map.of(
				"uid", username,
				"pass", password,
				"did", deviceID
		), callback);
	}

	public static void getUserAnswers(String username, int offset, int count, Callback callback){
		HttpRequest req=HttpRequest.newBuilder(new UriBuilder(API_URL).path("users", "answers")
						.queryParam("uid", username)
						.queryParam("offset", String.valueOf(offset))
						.queryParam("limit", String.valueOf(count))
						.build())
				.build();
		sendRequest(req, null, callback);
	}

	public static void getMyFriends(int offset, int count, Callback callback){
		HttpRequest req=HttpRequest.newBuilder(new UriBuilder(API_URL).path("friends", "ask")
						.queryParam("offset", String.valueOf(offset))
						.queryParam("limit", String.valueOf(count))
						.build())
				.build();
		sendRequest(req, null, callback);
	}

	public static void sendRequest(HttpRequest req, Map<String, String> bodyParams, Callback callback){
		req=signRequest(req, bodyParams);
		client.sendAsync(req, HttpResponse.BodyHandlers.ofString()).thenAccept(resp->{
			if(resp.statusCode()/100!=2){
				String body=resp.body();
				String error;
				try{
					error=JsonParser.parseString(body).getAsJsonObject().get("error").getAsString();
				}catch(Exception x){
					error=body;
				}
				throw new IllegalStateException("Request not successful: "+error);
			}
			resp.headers().firstValue("X-Next-Token").ifPresent(rt->requestToken=rt);
			callback.onSuccess(resp.body());
		}).exceptionally(x->{
			x.printStackTrace();
			callback.onError(x.getCause());
			return null;
		});
	}

	public static HttpRequest signRequest(HttpRequest req, Map<String, String> bodyParams){
		HttpRequest.Builder builder=HttpRequest.newBuilder(req, (k, v)->true)
				.headers(
						"Accept", "application/json; charset=utf-8",
						"User-Agent", "Dalvik/2.1.0 (Linux; U; Android 6.0.1; GT-N7100 Build/MOB30R)",
						"X-Api-Version", "1.18",
						"X-Client-Type", "android_4.94"
				);

		URI uri=req.uri();
		if(requestToken!=null){
			if(bodyParams!=null){
				bodyParams=new HashMap<>(bodyParams);
				bodyParams.put("rt", requestToken);
			}else{
				builder.uri(uri=new UriBuilder(req.uri()).queryParam("rt", requestToken).build());
			}
		}

		ArrayList<String> sigParts=new ArrayList<>();
		sigParts.add(req.method());
		sigParts.add("api.ask.fm");
		sigParts.add(req.uri().getPath());
		String query=uri.getRawQuery();
		if(bodyParams!=null){
			String bodyJson=URLEncoder.encode(gson.toJsonTree(bodyParams).toString(), StandardCharsets.UTF_8).replace("+", "%20");
			sigParts.add("json");
			sigParts.add(bodyJson);
			builder.POST(HttpRequest.BodyPublishers.ofString("json="+bodyJson)).header("Content-Type", "application/x-www-form-urlencoded");
		}else if(query!=null){
			sigParts.addAll(UriBuilder.parseQueryString(query)
					.entrySet()
					.stream()
					.map(e->e.getKey()+"%"+URLEncoder.encode(e.getValue(), StandardCharsets.UTF_8).replace("+", "%20"))
					.sorted()
					.toList());
		}
		String sigStr=String.join("%", sigParts);

		byte[] signature;
		try{
			Mac hmac=Mac.getInstance("HmacSHA1");
			hmac.init(new SecretKeySpec("CE2766AA8B74FBAC70A26CF5C83E9".getBytes(), "HmacSHA1"));
			signature=hmac.doFinal(sigStr.getBytes());
		}catch(NoSuchAlgorithmException | InvalidKeyException e){
			throw new RuntimeException(e);
		}
		builder.header("Authorization", "HMAC "+byteArrayToHexString(signature));
		if(accessToken!=null)
			builder.header("X-Access-Token", accessToken);

		return builder.build();
	}

	private static String byteArrayToHexString(byte[] arr){
		char[] chars=new char[arr.length*2];
		char[] hex={'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
		for(int i=0;i<arr.length;i++){
			chars[i*2]=hex[((int)arr[i] >> 4) & 0x0F];
			chars[i*2+1]=hex[(int)arr[i] & 0x0F];
		}
		return String.valueOf(chars);
	}

	public static String md5(String input){
		try{
			return byteArrayToHexString(MessageDigest.getInstance("MD5").digest(input.getBytes(StandardCharsets.UTF_8)));
		}catch(NoSuchAlgorithmException e){
			throw new RuntimeException(e);
		}
	}
}
