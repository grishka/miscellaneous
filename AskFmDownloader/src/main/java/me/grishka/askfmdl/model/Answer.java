package me.grishka.askfmdl.model;

import com.google.gson.annotations.SerializedName;

public record Answer(
		String author,
		String authorName,
		String authorStatus,
		me.grishka.askfmdl.model.Answer.Type type,
		String body,
		long createdAt,
		int likeCount,
		boolean liked
){
	public enum Type{
		@SerializedName("text")
		TEXT,
		@SerializedName("photo")
		PHOTO,
	}
}
