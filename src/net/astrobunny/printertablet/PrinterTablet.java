package net.astrobunny.printertablet;

import org.libsdl.app.SDLActivity; 

import android.os.Bundle;
import android.os.Handler;
import android.view.MotionEvent;
import android.view.WindowManager;

/* 
 * A sample wrapper class that just calls SDLActivity 
 */ 

public class PrinterTablet extends SDLActivity
{
	// The screen stays on, so when nobody is using it, dim it and then replace
	// the UI with a moving screensaver (drawn natively) to avoid burn-in.
	private static final long DIM_AFTER_MS = 60 * 1000;
	private static final long SCREENSAVER_AFTER_MS = 2 * 60 * 1000;
	private static final float DIM_BRIGHTNESS = 0.05f;   // 0 turns the backlight off on some devices

	// jni/src/screensaver.cpp
	private static native void nativeSetScreensaver(boolean on);

	private final Handler handler = new Handler();
	private boolean dimmed = false;
	private boolean screensaverOn = false;
	private boolean swallowingWakeTouch = false;

	private final Runnable dimRunnable = new Runnable()
	{
		public void run()
		{
			setBrightness(DIM_BRIGHTNESS);
			dimmed = true;
		}
	};

	private final Runnable screensaverRunnable = new Runnable()
	{
		public void run()
		{
			nativeSetScreensaver(true);
			screensaverOn = true;
		}
	};

	private void setBrightness(float brightness)
	{
		WindowManager.LayoutParams lp = getWindow().getAttributes();
		lp.screenBrightness = brightness;
		getWindow().setAttributes(lp);
	}

	// Undim, leave the screensaver, and restart the idle countdowns.
	private void wake()
	{
		if (dimmed)
		{
			setBrightness(WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE);
			dimmed = false;
		}
		if (screensaverOn)
		{
			nativeSetScreensaver(false);
			screensaverOn = false;
		}
		stopIdleTimers();
		handler.postDelayed(dimRunnable, DIM_AFTER_MS);
		handler.postDelayed(screensaverRunnable, SCREENSAVER_AFTER_MS);
	}

	private void stopIdleTimers()
	{
		handler.removeCallbacks(dimRunnable);
		handler.removeCallbacks(screensaverRunnable);
	}

	@Override
	public boolean dispatchTouchEvent(MotionEvent ev)
	{
		// A touch on the dimmed screen only wakes it; the whole gesture is
		// swallowed so it can't press whatever is under the finger.
		int action = ev.getActionMasked();
		if (dimmed && action == MotionEvent.ACTION_DOWN)
		{
			swallowingWakeTouch = true;
		}
		wake();
		if (swallowingWakeTouch)
		{
			if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL)
			{
				swallowingWakeTouch = false;
			}
			return true;
		}
		return super.dispatchTouchEvent(ev);
	}

	@Override
	protected void onResume()
	{
		super.onResume();
		wake();
	}

	@Override
	protected void onPause()
	{
		super.onPause();
		stopIdleTimers();
	}

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
