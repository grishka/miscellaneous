package me.grishka.askfmdl;

public interface Callback{
	void onSuccess(String resp);
	void onError(Throwable err);
}
