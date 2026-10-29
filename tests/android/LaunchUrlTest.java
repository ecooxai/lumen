package org.lumen.browser;

public final class LaunchUrlTest {
    private static void check(String source, String expected) {
        String actual = LaunchUrl.sanitize(source);
        if (expected == null ? actual != null : !expected.equals(actual)) {
            throw new AssertionError("Unexpected URL result for " + source + ": " + actual);
        }
    }

    public static void main(String[] arguments) {
        check(null, null);
        check("", null);
        check("https://example.org/path?q=hello%20world#part", "https://example.org/path?q=hello%20world#part");
        check("HTTP://example.org/", "http://example.org/");
        check("https://[::1]:8443/", "https://[::1]:8443/");
        check("https://example.org/\uD83D\uDE00", "https://example.org/%F0%9F%98%80");
        check("file:///data/private", null);
        check("content://documents/private", null);
        check("javascript:alert(1)", null);
        check("intent://example.org/#Intent;end", null);
        check("data:text/html,hello", null);
        check("//example.org/", null);
        check("https:example.org", null);
        check("https:///missing-host", null);
        check("https://example.org/\nspoof", null);
        check("https://example.org/space here", null);
        check("https://example.org/" + "a".repeat(65536), null);
        System.out.println("Android launch URL tests ok");
    }
}
