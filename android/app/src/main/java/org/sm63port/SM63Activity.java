package org.sm63port;

import org.libsdl.app.SDLActivity;

/** The game runs in SDL's activity; libmain.so (src/main.cpp) is its entry point. */
public class SM63Activity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
