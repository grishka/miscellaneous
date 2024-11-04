package me.grishka.askfmdl;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.google.gson.JsonPrimitive;
import com.google.gson.reflect.TypeToken;

import java.awt.Color;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.atomic.AtomicInteger;

import javax.swing.JButton;
import javax.swing.JFileChooser;
import javax.swing.JFrame;
import javax.swing.JPanel;
import javax.swing.JPasswordField;
import javax.swing.JProgressBar;
import javax.swing.JTextField;
import javax.swing.JTextPane;
import javax.swing.text.BadLocationException;
import javax.swing.text.SimpleAttributeSet;
import javax.swing.text.StyleConstants;
import javax.swing.text.StyledDocument;

import io.pebbletemplates.pebble.PebbleEngine;
import io.pebbletemplates.pebble.extension.AbstractExtension;
import io.pebbletemplates.pebble.extension.Filter;
import io.pebbletemplates.pebble.loader.ClasspathLoader;
import io.pebbletemplates.pebble.template.PebbleTemplate;
import me.grishka.askfmdl.model.Question;
import me.grishka.askfmdl.model.User;

public class MainForm extends JFrame{
	private JPanel contentPanel;
	private JTextField usernameField;
	private JPasswordField passwordField;
	private JTextPane logTextPane;
	private JButton startButton;
	private JTextField destinationDirField;
	private JButton browseDirButton;
	private JProgressBar progressBar;

	private Gson gson=new GsonBuilder().disableHtmlEscaping().create();
	private JsonObject self;
	private String selfUsername;
	private File destinationDir;
	private File jsonDir, htmlDir, imagesDir;
	private PebbleEngine templateEngine;
	private ArrayList<User> friends=new ArrayList<>();
	private HashSet<String> staticFileQueue=new HashSet<>();
	private ArrayList<User> friendsToDownload=new ArrayList<>();

	public MainForm(){
		setTitle("Ask.fm content downloader");
		setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
		setContentPane(contentPanel);
		pack();
		setLocationRelativeTo(null);

		String path=new File(".").getAbsolutePath();
		destinationDirField.setText(path.substring(0, path.length()-2));
		browseDirButton.addActionListener(e->{
			JFileChooser chooser=new JFileChooser(destinationDirField.getText());
			chooser.setDialogTitle("Choose download destination");
			chooser.setFileSelectionMode(JFileChooser.DIRECTORIES_ONLY);
			chooser.setAcceptAllFileFilterUsed(false);
			if(chooser.showOpenDialog(this)==JFileChooser.APPROVE_OPTION){
				destinationDirField.setText(chooser.getSelectedFile().getAbsolutePath());
			}
		});

		startButton.addActionListener(e->start());
		progressBar.setVisible(false);

		ClasspathLoader loader=new ClasspathLoader();
		loader.setSuffix(".twig");
		templateEngine=new PebbleEngine.Builder()
				.loader(loader)
				.extension(new AbstractExtension(){
					@Override
					public Map<String, Filter> getFilters(){
						return Map.of("nl2br", new Nl2brFilter());
					}
				})
				.defaultLocale(Locale.US)
				.defaultEscapingStrategy("html")
				.build();
	}

	private void log(String msg, Object... args){
		StyledDocument doc=logTextPane.getStyledDocument();
		try{
			doc.insertString(doc.getLength(), String.format(msg, args)+"\n", null);
		}catch(BadLocationException checkedExceptionsAreFuckingStupid){
			throw new RuntimeException(checkedExceptionsAreFuckingStupid);
		}
	}

	private void logErr(String msg, Object... args){
		StyledDocument doc=logTextPane.getStyledDocument();
		try{
			SimpleAttributeSet attrs=new SimpleAttributeSet();
			StyleConstants.setForeground(attrs, Color.RED);
			doc.insertString(doc.getLength(), String.format(msg, args)+"\n", attrs);
		}catch(BadLocationException checkedExceptionsAreFuckingStupid){
			throw new RuntimeException(checkedExceptionsAreFuckingStupid);
		}
	}

	private void start(){
		String username=usernameField.getText();
		String password=passwordField.getText();
		if(username.isEmpty() || password.isEmpty()){
			logErr("Please enter your ask.fm credentials");
			return;
		}
		startButton.setEnabled(false);
		progressBar.setVisible(true);
		friends.clear();
		if(API.accessToken==null){
			log("Getting an access token");
			API.getAccessToken(new Callback(){
				@Override
				public void onSuccess(String resp){
					API.accessToken=JsonParser.parseString(resp).getAsJsonObject().get("accessToken").getAsString();
					logIn(username, password);
				}

				@Override
				public void onError(Throwable err){
					logErr("Failed to get access token: %s", err);
					resetUI();
				}
			});
		}else{
			logIn(username, password);
		}
	}

	private void resetUI(){
		startButton.setEnabled(true);
		progressBar.setVisible(false);
	}

	private boolean makeDirectory(File dir){
		if(dir.exists() && dir.isDirectory())
			return true;
		if(!dir.mkdirs()){
			logErr("Unable to create directory %s", dir.getAbsolutePath());
			resetUI();
			return false;
		}
		return true;
	}

	private boolean extractResource(File dir, String name){
		try(InputStream in=getClass().getClassLoader().getResourceAsStream(name)){
			File cssFile=new File(dir, name);
			if(cssFile.exists())
				cssFile.delete();
			Files.copy(Objects.requireNonNull(in), cssFile.toPath());
			return true;
		}catch(IOException x){
			logErr("Error copying style.css: %s", x);
			resetUI();
			return false;
		}
	}

	private void logIn(String username, String password){
		log("Authenticating");
		API.logIn(username, password, new Callback(){
			@Override
			public void onSuccess(String resp){
				JsonObject jr=JsonParser.parseString(resp).getAsJsonObject();
				API.accessToken=jr.get("accessToken").getAsString();
				self=jr.getAsJsonObject("user");
				selfUsername=self.get("uid").getAsString();
				destinationDir=new File(destinationDirField.getText(), selfUsername);
				if(!makeDirectory(destinationDir))
					return;
				jsonDir=new File(destinationDir, "json");
				if(!makeDirectory(jsonDir))
					return;
				htmlDir=new File(destinationDir, "html");
				if(!makeDirectory(htmlDir))
					return;
				imagesDir=new File(destinationDir, "images");
				if(!makeDirectory(imagesDir))
					return;

				try(FileWriter writer=new FileWriter(new File(jsonDir, "self.json"), StandardCharsets.UTF_8)){
					writer.write(self.toString());
				}catch(IOException x){
					logErr("Error writing self.json: %s", x);
					resetUI();
					return;
				}

				extractResource(htmlDir, "style.css");
				extractResource(imagesDir, "noAvatar.png");

				File indexFile=new File(destinationDir, "index.html");
				PebbleTemplate template=templateEngine.getTemplate("index");
				try(FileWriter writer=new FileWriter(indexFile)){
					template.evaluate(writer, Map.of(
							"username", selfUsername,
							"pageTitle", "Ask.fm export"
					));
				}catch(IOException x){
					logErr("Failed to write %s: %s", indexFile.getAbsolutePath(), x);
				}

				new AnswersDownloader(selfUsername, self.get("fullName").getAsString(), self.get("avatarThumbUrl") instanceof JsonPrimitive jp ? jp.getAsString() : null, ()->downloadFriendList(0)).doNextPage();
			}

			@Override
			public void onError(Throwable err){
				logErr("Failed to authenticate: %s", err);
				resetUI();
			}
		});
	}

	private void downloadFriendList(int offset){
		log("Getting friend list, offset %d", offset);
		File friendsJsonDir=new File(jsonDir, "friends");
		if(!makeDirectory(friendsJsonDir)){
			throw new IllegalStateException();
		}
		File file=new File(friendsJsonDir, String.format("%04d.json", offset/100));
		if(file.exists()){
			try(BufferedReader reader=new BufferedReader(new FileReader(file, StandardCharsets.UTF_8))){
				StringBuilder sb=new StringBuilder();
				String line;
				while((line=reader.readLine())!=null){
					sb.append(line);
				}
				Thread.ofVirtual().start(()->processFriendList(offset, sb.toString()));
			}catch(IOException ignore){}
		}else{
			API.getMyFriends(offset, 100, new Callback(){
				@Override
				public void onSuccess(String resp){
					try(FileWriter writer=new FileWriter(file, StandardCharsets.UTF_8)){
						writer.write(resp);
					}catch(IOException x){
						logErr("Failed to write %s: %s", file.getAbsolutePath(), x);
					}
					processFriendList(offset, resp);
				}

				@Override
				public void onError(Throwable err){
					logErr("Failed to get friend list: %s", err);
					resetUI();
				}
			});
		}
	}

	private void processFriendList(int offset, String resp){
		JsonObject jr=JsonParser.parseString(resp).getAsJsonObject();
		List<User> users=gson.fromJson(jr.get("friends"), new TypeToken<>(){});
		friends.addAll(users);
		if(jr.get("hasMore").getAsBoolean()){
			downloadFriendList(offset+100);
		}else{
			File file=new File(htmlDir, "friends.html");
			PebbleTemplate template=templateEngine.getTemplate("friends");
			try(FileWriter writer=new FileWriter(file)){
				template.evaluate(writer, Map.of(
						"friends", friends,
						"pageTitle", "Friends",
						"username", selfUsername,
						"basePath", ".."
				));
			}catch(IOException x){
				logErr("Failed to write %s: %s", file.getAbsolutePath(), x);
			}
			for(User u:friends){
				if(u.avatarThumbUrl()!=null)
					staticFileQueue.add(u.avatarThumbUrl());
				if(u.avatarUrl()!=null)
					staticFileQueue.add(u.avatarUrl());
			}
			friendsToDownload.clear();
			friendsToDownload.addAll(friends);
			downloadFriendAnswers(friends.removeFirst());
		}
	}

	private void downloadFriendAnswers(User user){
		new AnswersDownloader(user.uid(), user.fullName(), user.avatarThumbUrl(), ()->{
			if(friends.isEmpty()){
				downloadStaticFiles();
			}else{
				downloadFriendAnswers(friends.removeFirst());
			}
		}).doNextPage();
	}

	private void downloadStaticFiles(){
		ArrayList<ArrayList<String>> batches=new ArrayList<>();
		ArrayList<String> curBatch=new ArrayList<>();
		batches.add(curBatch);
		for(String url:staticFileQueue){
			curBatch.add(url);
			if(curBatch.size()==10){
				curBatch=new ArrayList<>();
				batches.add(curBatch);
			}
		}
		downloadNextBatchOfStaticFiles(batches);
	}

	private void downloadNextBatchOfStaticFiles(ArrayList<ArrayList<String>> batches){
		int total=batches.stream().mapToInt(ArrayList::size).sum();
		log("Downloading images, %d remaining", total);
		ArrayList<String> batch=batches.removeFirst();
		AtomicInteger requestsRemain=new AtomicInteger(batch.size());
		for(String url:batch){
			File dest=new File(imagesDir, API.md5(url)+url.substring(url.lastIndexOf('.')));
			HttpRequest req=HttpRequest.newBuilder(URI.create(url)).build();
			API.client.sendAsync(req, HttpResponse.BodyHandlers.ofFile(dest.toPath())).thenAccept(resp->{
				if(requestsRemain.decrementAndGet()==0){
					if(!batches.isEmpty()){
						downloadNextBatchOfStaticFiles(batches);
					}else{
						resetUI();
						log("Done");
					}
				}
			}).exceptionally(x->{
				logErr("Failed to download %s: %s", url, x);
				if(requestsRemain.decrementAndGet()==0){
					if(!batches.isEmpty()){
						downloadNextBatchOfStaticFiles(batches);
					}else{
						resetUI();
						log("Done");
					}
				}
				return null;
			});
		}
	}

	private class AnswersDownloader{
		private final String username, displayName, avatar;
		private final Runnable onDone;
		private final File answersJsonDir, answersHtmlDir;
		private int currentPage=0;
		private ArrayList<Question> allQuestions=new ArrayList<>();

		public AnswersDownloader(String username, String displayName, String avatar, Runnable onDone){
			this.username=username;
			this.displayName=displayName;
			this.avatar=avatar;
			this.onDone=onDone;
			answersJsonDir=new File(jsonDir, "answers/"+username);
			answersHtmlDir=new File(htmlDir, "answers/"+username);
			if(!makeDirectory(answersJsonDir)){
				throw new IllegalStateException();
			}
			if(!makeDirectory(answersHtmlDir)){
				throw new IllegalStateException();
			}
		}

		public void doNextPage(){
			log("Getting answers for %s, offset %d", username, currentPage*100);
			File file=new File(answersJsonDir, String.format("%04d.json", currentPage));
			if(file.exists()){
				try(BufferedReader reader=new BufferedReader(new FileReader(file, StandardCharsets.UTF_8))){
					StringBuilder sb=new StringBuilder();
					String line;
					while((line=reader.readLine())!=null){
						sb.append(line);
					}
					Thread.ofVirtual().start(()->processResponse(sb.toString()));
					return;
				}catch(IOException ignore){}
			}
			API.getUserAnswers(username, currentPage*100, 100, new Callback(){
				@Override
				public void onSuccess(String resp){
					try(FileWriter writer=new FileWriter(file, StandardCharsets.UTF_8)){
						writer.write(resp);
					}catch(IOException x){
						logErr("Failed to write %s: %s", file.getAbsolutePath(), x);
					}
					processResponse(resp);
				}

				@Override
				public void onError(Throwable err){
					logErr("Failed to get answers: %s", err);
					onDone.run();
				}
			});
		}

		private void processResponse(String resp){
			JsonObject obj=JsonParser.parseString(resp).getAsJsonObject();

			List<Question> questions=gson.fromJson(obj.get("questions"), new TypeToken<>(){});
			for(Question q:questions){
				if(q.avatarThumbUrl()!=null){
					staticFileQueue.add(q.avatarThumbUrl());
				}
				if(q.backgroundCardLink()!=null){
					staticFileQueue.add(q.backgroundCardLink().card().imageUrl());
				}
			}
			allQuestions.addAll(questions);

			if(obj.get("hasMore").getAsBoolean()){
				currentPage++;
				doNextPage();
			}else{
				PebbleTemplate template=templateEngine.getTemplate("answers");
				for(int i=0;i<allQuestions.size();i+=100){
					List<Question> page=allQuestions.subList(i, Math.min(i+100, allQuestions.size()));
					File file=new File(answersHtmlDir, String.format("%04d.html", i/100));
					try(FileWriter writer=new FileWriter(file)){
						Map<String, Object> model=new java.util.HashMap<>();
						model.put("questions", page);
						model.put("username", username);
						model.put("userDisplayName", displayName);
						model.put("pageTitle", username+"'s answers");
						model.put("userAvatar", avatar);
						model.put("basePath", "../../..");
						model.put("totalPages", (allQuestions.size()+99)/100);
						model.put("thisPage", (i/100)+1);
						template.evaluate(writer, model);
					}catch(IOException x){
						logErr("Failed to write %s: %s", file.getAbsolutePath(), x);
					}
				}
				onDone.run();
			}
		}
	}
}
