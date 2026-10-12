package org.lumen.browser;

import org.libsdl.app.SDLActivity;

public final class LumenActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    @Override
    protected String[] getArguments() {
        return new String[] {"lumen://newtab"};
    }
}
