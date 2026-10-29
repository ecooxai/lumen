package org.lumen.browser;

import java.net.URI;
import java.net.URISyntaxException;
import java.util.Locale;

final class LaunchUrl {
    static String sanitize(String source) {
        if (source == null || source.length() > 65536) return null;
        try {
            URI uri = new URI(source);
            String scheme = uri.getScheme();
            if (scheme == null || !(scheme.equalsIgnoreCase("http") || scheme.equalsIgnoreCase("https"))) return null;
            if (uri.isOpaque() || uri.getRawAuthority() == null || uri.getRawAuthority().isEmpty()) return null;
            String encoded = uri.toASCIIString();
            if (encoded.length() > 65536) return null;
            return scheme.toLowerCase(Locale.ROOT) + encoded.substring(scheme.length());
        } catch (URISyntaxException exception) {
            return null;
        }
    }
}
