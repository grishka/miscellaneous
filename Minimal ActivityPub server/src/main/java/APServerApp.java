import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.security.InvalidKeyException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.Signature;
import java.security.SignatureException;
import java.security.spec.InvalidKeySpecException;
import java.time.Instant;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.Arrays;
import java.util.Base64;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.stream.Collectors;

import static spark.Spark.*;

public class APServerApp{

	// Не забудьте указать здесь ваш домен!
	private static final String LOCAL_DOMAIN="bla-bla-bla.ngrok-free.app";
	// Поменяйте по вкусу. Только латиница, цифры и подчёркивания
	private static final String USERNAME="TestUser";

	private static final String ACTOR_ID="https://"+LOCAL_DOMAIN+"/actor";
	private static final String AP_CONTENT_TYPE="application/ld+json; profile=\"https://www.w3.org/ns/activitystreams\"";
	private static final Gson GSON=new GsonBuilder().disableHtmlEscaping().create();
	private static final HttpClient HTTP_CLIENT=HttpClient.newHttpClient();

	// Входящие активити
	private static final LinkedList<JsonObject> receivedActivities=new LinkedList<>();

	public static void main(String[] args){
		final String publicKey;
		final PrivateKey privateKey;
		try{
			publicKey=Utils.readTextFile("public.pem");
		}catch(IOException x){
			System.err.println("Не получилось прочитать публичный ключ из файла public.pem");
			x.printStackTrace();
			return;
		}
		try{
			privateKey=Utils.decodePrivateKey(Utils.readTextFile("private.pem"));
		}catch(IOException | NoSuchAlgorithmException | InvalidKeySpecException x){
			System.err.println("Не получилось прочитать приватный ключ из файла private.pem");
			x.printStackTrace();
			return;
		}

		// Объект актора
		get("/actor", (req, res)->{
			Map<String, Object> actorObj=Map.of(
					"@context", List.of("https://www.w3.org/ns/activitystreams", "https://w3id.org/security/v1"),
					"type", "Person",
					"id", ACTOR_ID,
					"preferredUsername", USERNAME,
					"inbox", "https://"+LOCAL_DOMAIN+"/inbox",
					"publicKey", Map.of(
							"id", ACTOR_ID+"#main-key",
							"owner", ACTOR_ID,
							"publicKeyPem", publicKey
					)
			);
			res.type(AP_CONTENT_TYPE);
			return GSON.toJson(actorObj);
		});

		// WebFinger
		get("/.well-known/webfinger", (req, res)->{
			// Настоящий сервер здесь распарсил бы параметр resource и сходил бы в базу за пользователем с таким юзернеймом
			// Но у нас всего один актор, поэтому можно и вот так
			final String myResource="acct:"+USERNAME+"@"+LOCAL_DOMAIN;
			if(!myResource.equalsIgnoreCase(req.queryParams("resource"))){
				res.status(404);
				return "Not found";
			}
			Map<String, Object> webfingerResponse=Map.of(
					"subject", myResource,
					"links", List.of(Map.of(
							"rel", "self",
							"type", "application/activity+json",
							"href", ACTOR_ID
					))
			);
			res.type("application/jrd+json");
			return GSON.toJson(webfingerResponse);
		});

		// Собственно инбокс
		post("/inbox", (req, res)->{
			// Время в заголовке Date должно быть в пределах 30 секунд от текущего
			long timestamp=DateTimeFormatter.RFC_1123_DATE_TIME.parse(req.headers("Date"), Instant::from).getEpochSecond();
			if(Math.abs(timestamp-Instant.now().getEpochSecond())>30){
				res.status(400);
				return "";
			}

			// Вытаскиваем актора
			JsonObject activity=JsonParser.parseString(req.body()).getAsJsonObject();
			URI actorID=new URI(activity.get("actor").getAsString());
			JsonObject actor=fetchRemoteActor(actorID);

			// Парсим заголовок и проверяем подпись
			Map<String, String> signatureHeader=Arrays.stream(req.headers("Signature").split(","))
					.map(part->part.split("=", 2))
					.collect(Collectors.toMap(keyValue->keyValue[0], keyValue->keyValue[1].replaceAll("\"", "")));
			if(!Objects.equals(actor.getAsJsonObject("publicKey").get("id").getAsString(), signatureHeader.get("keyId"))){
				// ID ключа, которым подписан запрос, не совпадает с ключом актора
				res.status(400);
				return "";
			}
			List<String> signedHeaders=List.of(signatureHeader.get("headers").split(" "));
			if(!new HashSet<>(signedHeaders).containsAll(Set.of("(request-target)", "host", "date"))){
				// Один или несколько обязательных для подписи заголовков не содержатся в подписи
				res.status(400);
				return "";
			}
			String toSign=signedHeaders.stream()
					.map(header->{
						String value;
						if("(request-target)".equals(header)){
							value="post /inbox";
						}else{
							value=req.headers(header);
						}
						return header+": "+value;
					})
					.collect(Collectors.joining("\n"));
			PublicKey actorKey=Utils.decodePublicKey(actor.getAsJsonObject("publicKey").get("publicKeyPem").getAsString());
			Signature sig=Signature.getInstance("SHA256withRSA");
			sig.initVerify(actorKey);
			sig.update(toSign.getBytes(StandardCharsets.UTF_8));
			if(!sig.verify(Base64.getDecoder().decode(signatureHeader.get("signature")))){
				// Подпись не проверилась
				res.status(400);
				return "";
			}

			// Всё получилось - запоминаем активити, чтобы потом показать её пользователю
			receivedActivities.addFirst(activity);

			return ""; // Достаточно просто ответа с кодом 200
		});

		// "веб-интерфейс"
		// Обратите внимание, что поскольку наш сервер игрушечный, аутентификации тут не предусмотрено
		get("/web", (req, res)->{
			StringBuilder html=new StringBuilder("""
					<!DOCTYPE html>
					<html>
						<head>
							<title>ActivityPub server</title>
						</head>
						<body>
							<h2>Отправить активити</h2>
							<form action="/send" method="post">
								<textarea name="activityJson" cols="160" rows="20" required></textarea><br/>
								<input type="text" name="inbox" placeholder="Inbox URL"/> <input type="submit" name="toSpecificInbox" value="Отправить"/>
							</form>
							<h2>Полученные активити</h2>
					 """);
			for(JsonObject activity:receivedActivities){
				html.append("<pre>");
				html.append(Utils.prettyPrintJSON(activity));
				html.append("</pre><hr/>");
			}
			html.append("</body></html>");
			return html;
		});

		post("/send", (req, res)->{
			String activityJson=req.queryParams("activityJson");
			deliverOneActivity(activityJson, URI.create(req.queryParams("inbox")), privateKey);
			res.redirect("/web");
			return "";
		});
	}

	/**
	 * Получить объект актора с другого сервера
	 * @param id идентификатор актора
	 * @throws IOException в случае ошибки сети
	 */
	private static JsonObject fetchRemoteActor(URI id) throws IOException{
		try{
			HttpRequest req=HttpRequest.newBuilder()
					.GET()
					.uri(id)
					.header("Accept", AP_CONTENT_TYPE)
					.build();
			HttpResponse<String> resp=HTTP_CLIENT.send(req, HttpResponse.BodyHandlers.ofString());
			return JsonParser.parseString(resp.body()).getAsJsonObject();
		}catch(InterruptedException x){
			throw new RuntimeException(x);
		}
	}

	/**
	 * Отправить активити в чей-нибудь инбокс
	 * @param activityJson JSON самой активити
	 * @param inbox адрес инбокса
	 * @param key приватный ключ для подписи
	 * @throws IOException в случае ошибки сети
	 */
	private static void deliverOneActivity(String activityJson, URI inbox, PrivateKey key) throws IOException{
		try{
			byte[] body=activityJson.getBytes(StandardCharsets.UTF_8);
			String date=DateTimeFormatter.RFC_1123_DATE_TIME.format(Instant.now().atZone(ZoneId.of("GMT")));
			String digest="SHA-256="+Base64.getEncoder().encodeToString(MessageDigest.getInstance("SHA-256").digest(body));
			String toSign="(request-target): post "+inbox.getRawPath()+"\nhost: "+inbox.getHost()+"\ndate: "+date+"\ndigest: "+digest;

			Signature sig=Signature.getInstance("SHA256withRSA");
			sig.initSign(key);
			sig.update(toSign.getBytes(StandardCharsets.UTF_8));
			byte[] signature=sig.sign();

			HttpRequest req=HttpRequest.newBuilder()
					.POST(HttpRequest.BodyPublishers.ofByteArray(body))
					.uri(inbox)
					.header("Date", date)
					.header("Digest", digest)
					.header("Signature", "keyId=\""+ACTOR_ID+"#main-key\",headers=\"(request-target) host date digest\",signature=\""+Base64.getEncoder().encodeToString(signature)+"\",algorithm=\"rsa-sha256\"")
					.header("Content-Type", AP_CONTENT_TYPE)
					.build();
			HttpResponse<String> resp=HTTP_CLIENT.send(req, HttpResponse.BodyHandlers.ofString());
			System.out.println(resp);
			System.out.println(resp.body());
		}catch(InterruptedException | NoSuchAlgorithmException | InvalidKeyException | SignatureException x){
			throw new RuntimeException(x);
		}
	}
}
