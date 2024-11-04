package me.grishka.askfmdl.model;

import me.grishka.askfmdl.API;

public record User(String fullName, String avatarThumbUrl, String avatarUrl, boolean active, String uid) {
	public String getAvatarFilePath(){
		if(avatarThumbUrl!=null)
			return "images/"+API.md5(avatarThumbUrl)+avatarThumbUrl.substring(avatarThumbUrl.lastIndexOf('.'));
		return null;
	}
}
