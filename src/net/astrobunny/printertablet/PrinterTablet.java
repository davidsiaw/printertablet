package net.astrobunny.printertablet;

import org.libsdl.app.SDLActivity; 

import android.os.Bundle;
import android.view.WindowManager;

/* 
 * A sample wrapper class that just calls SDLActivity 
 */ 

public class PrinterTablet extends SDLActivity
{
	@Override
	protected void onCreate(Bundle savedInstanceState)
	{
		super.onCreate(savedInstanceState);

		// Never let the screen dim or sleep while the app is in front.
		getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
	}

	@Override
	protected void onDestroy()
	{
		// End the process instead of calling super.onDestroy(), for two reasons:
		// - super.onDestroy() waits for the native main() to finish, but if we
		//   were destroyed while in the background (Home, low memory, "Don't keep
		//   activities") the SDL thread is blocked waiting for a resume that
		//   never comes, so it would hang forever (black screen on reopen).
		// - This SDL (2.0.1) can't run main() a second time in the same process.
		// So every launch starts fresh. Native destructors don't run; anything
		// that must be saved should be saved on SDL_APP_WILLENTERBACKGROUND.
		System.exit(0);
	}
}
