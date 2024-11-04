package me.grishka.askfmdl.model;

import com.google.gson.annotations.SerializedName;

import java.time.Instant;
import java.time.ZoneId;
import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import java.time.format.FormatStyle;
import java.util.Locale;

public record Question(
		me.grishka.askfmdl.model.Question.Type type,
		String body,
		Answer answer,
		String author,
		String authorName,
		String authorStatus,
		long createdAt,
		long updatedAt,
		String avatarThumbUrl,
		long qid,
		BackgroundCardLink backgroundCardLink
){

	public String getFormattedDate(){
		return DateTimeFormatter.ofLocalizedDateTime(FormatStyle.MEDIUM).withLocale(Locale.UK).format(ZonedDateTime.ofInstant(Instant.ofEpochSecond(updatedAt), ZoneId.systemDefault()));
	}

	public enum Type{
		@SerializedName("anonymous")
		ANONYMOUS,
		@SerializedName("system")
		SYSTEM,
		@SerializedName("anonshoutout")
		ANONYMOUS_SHOUT_OUT,
		@SerializedName("thread")
		THREAD,
		@SerializedName("user")
		USER;

		public String getDisplayString(){
			return switch(this){
				case ANONYMOUS, USER, THREAD -> null;
				case SYSTEM -> "System (question of the day)";
				case ANONYMOUS_SHOUT_OUT -> "Anonymous shoutout";
			};
		}
	}

	public record BackgroundCard(String imageUrl){}
	public record BackgroundCardLink(BackgroundCard card){}
}
