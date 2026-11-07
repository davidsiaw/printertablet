#include "screensaver.hpp"

#include <SDL_atomic.h>

// Set from the Java UI thread, read on the SDL thread.
static SDL_atomic_t screensaverActive;

bool ScreensaverActive()
{
    return SDL_AtomicGet(&screensaverActive) != 0;
}

#ifdef __ANDROID__
#include <jni.h>

extern "C" JNIEXPORT void JNICALL
Java_net_astrobunny_printertablet_PrinterTablet_nativeSetScreensaver(JNIEnv*, jclass, jboolean on)
{
    SDL_AtomicSet(&screensaverActive, on ? 1 : 0);
}
#endif
