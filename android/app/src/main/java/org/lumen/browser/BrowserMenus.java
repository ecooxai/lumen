package org.lumen.browser;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.text.InputType;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.BaseAdapter;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.PopupWindow;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

/* The browser's own menus, built from Android views over the page: the toolbar's more menu,
 * the bookmarks and workspaces panels. Choices go back to the native side as UI actions. */
final class BrowserMenus {
    interface Action { void send(int code, int value, String first, String second); }

    /* mirrors android_ui.h */
    static final int NEW_TAB = 1, FORWARD = 2, BOOKMARKS = 3, DOWNLOADS = 4, HISTORY = 5, WORKSPACES = 6, SETTINGS = 7,
        DESKTOP = 8, ZOOM = 9, BM_TOGGLE = 10, BM_OPEN = 11, BM_SAVE = 12, BM_DELETE = 13,
        WS_SELECT = 14, WS_NEW = 15, WS_RENAME = 16, WS_CLOSE = 17, BACK = 18, RELOAD = 19, FULLSCREEN = 20, URL_GO = 21, BM_ADD = 22, TAB_MENU = 23, SITE_CLEAR = 24, KEYBOARD = 26;
    private static final int MAX_WORKSPACES = 16, ZOOM_MIN = 20, ZOOM_MAX = 300, ZOOM_STEP = 10;
    private static final int TEXT = 0xff202124, SUBTLE = 0xff5f6368, DISABLED = 0xffbdc1c6, DIVIDER = 0xffe8eaed, ACCENT = 0xff1a73e8, UNSAFE = 0xffd93025;
    private static PopupWindow menu;

    private BrowserMenus() { }

    private static int dp(Context context, float value) {
        return Math.round(value * context.getResources().getDisplayMetrics().density);
    }

    private static void ripple(View view, boolean borderless) {
        TypedValue value = new TypedValue();
        int attribute = borderless ? android.R.attr.selectableItemBackgroundBorderless : android.R.attr.selectableItemBackground;
        if (view.getContext().getTheme().resolveAttribute(attribute, value, true)) view.setBackgroundResource(value.resourceId);
    }

    private static TextView label(Context context, String text, float size, int color) {
        TextView view = new TextView(context);
        view.setText(text);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, size);
        view.setTextColor(color);
        return view;
    }

    private static View divider(Context context) {
        View line = new View(context);
        line.setBackgroundColor(DIVIDER);
        line.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, Math.max(1, dp(context, 1))));
        return line;
    }

    private static ImageView icon(Context context, int code, int color, int size) {
        ImageView view = new ImageView(context);
        view.setScaleType(ImageView.ScaleType.CENTER);
        view.setImageDrawable(BrowserIcons.of(code, color, dp(context, size)));
        return view;
    }

    /* a menu line: the action's icon, its name, and a check box for the on/off ones (check: -1 none) */
    private static LinearLayout row(Context context, int code, String text, boolean enabled, int check) {
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(context, 16), 0, dp(context, check >= 0 ? 10 : 18), 0);
        row.addView(icon(context, code, enabled ? SUBTLE : DISABLED, 22), new LinearLayout.LayoutParams(dp(context, 24), dp(context, 24)));
        TextView name = label(context, text, 16, enabled ? TEXT : DISABLED);
        name.setPadding(dp(context, 16), 0, 0, 0);
        row.addView(name, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        if (check >= 0) {
            CheckBox box = new CheckBox(context);
            box.setChecked(check != 0);
            box.setEnabled(enabled);
            box.setClickable(false);
            box.setFocusable(false);
            row.addView(box);
        }
        row.setEnabled(enabled);
        ripple(row, false);
        return row;
    }

    /* what a toolbar slot's action is called, for accessibility */
    static String actionName(int code, boolean on) {
        switch (code) {
            case BACK: return "Back";
            case FORWARD: return "Forward";
            case RELOAD: return "Reload";
            case NEW_TAB: return "New tab";
            case BOOKMARKS: return "Bookmarks";
            case HISTORY: return "History";
            case DOWNLOADS: return "Downloads";
            case WORKSPACES: return "Workspaces";
            case SETTINGS: return "Settings";
            case KEYBOARD: return "Show keyboard";
            case DESKTOP: return on ? "Desktop site, on" : "Desktop site";
            case BM_TOGGLE: return on ? "Remove bookmark" : "Bookmark this page";
            case FULLSCREEN: return on ? "Exit fullscreen" : "Fullscreen";
            default: return null;
        }
    }

    /* every dialog shows touches too */
    private static AlertDialog show(AlertDialog.Builder builder) {
        AlertDialog dialog = builder.show();
        TouchFeedback.window(dialog.getWindow());
        return dialog;
    }

    /* a short notice just below a view, gone after a moment; touches pass through it */
    static void notice(View anchor, String text) {
        Context context = anchor.getContext();
        TextView view = label(context, text, 13, Color.WHITE);
        view.setPadding(dp(context, 14), dp(context, 8), dp(context, 14), dp(context, 8));
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(0xf0202124);
        shape.setCornerRadius(dp(context, 8));
        view.setBackground(shape);
        view.measure(View.MeasureSpec.UNSPECIFIED, View.MeasureSpec.UNSPECIFIED);
        final PopupWindow popup = new PopupWindow(view, ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, false);
        popup.setTouchable(false);
        popup.setElevation(dp(context, 6));
        popup.showAsDropDown(anchor, (anchor.getWidth() - view.getMeasuredWidth()) / 2, dp(context, 2));
        view.postDelayed(() -> { if (popup.isShowing()) popup.dismiss(); }, 1800);
    }

    /* a long press on Bookmarks: the current page is bookmarked, with a notice under the button.
       Returns whether the page is a bookmark now. */
    static boolean bookmarkHere(View anchor, boolean bookmarked, boolean can, Action action) {
        if (!can) { notice(anchor, "Open a web page to bookmark it"); return bookmarked; }
        if (bookmarked) { notice(anchor, "Already in bookmarks"); return true; }
        action.send(BM_ADD, 0, null, null);
        notice(anchor, "Added to bookmarks");
        return true;
    }

    static void toast(Activity activity, String text) {
        Toast.makeText(activity, text, text.length() > 48 ? Toast.LENGTH_LONG : Toast.LENGTH_SHORT).show();
    }

    /* state: can go forward, desktop site on, desktop site scope (1 site, 2 all sites), zoom possible, zoom percent,
       page bookmarked, page can be bookmarked, can go back, fullscreen on */
    static void showMenu(Activity activity, int[] state, Action action) {
        if (menu != null) { menu.dismiss(); menu = null; }
        final Context context = activity;
        LinearLayout root = new LinearLayout(context);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(0, dp(context, 4), 0, dp(context, 6));
        final PopupWindow popup = new PopupWindow(context);

        LinearLayout icons = new LinearLayout(context);
        icons.setOrientation(LinearLayout.HORIZONTAL);
        String[] names = { "Back", "Forward", "Reload", state[5] != 0 ? "Remove bookmark" : "Bookmark this page", "Downloads" };
        int[] codes = { BACK, FORWARD, RELOAD, BM_TOGGLE, DOWNLOADS };
        boolean[] enabled = { state[7] != 0, state[0] != 0, true, state[6] != 0, true };
        final ImageView[] star = new ImageView[1];
        for (int i = 0; i < codes.length; i++) {
            int color = enabled[i] ? (codes[i] == BM_TOGGLE && state[5] != 0 ? ACCENT : TEXT) : DISABLED;
            ImageView button = icon(context, codes[i], color, 24);
            button.setContentDescription(names[i]);
            button.setEnabled(enabled[i]);
            ripple(button, true);
            final int code = codes[i];
            button.setOnClickListener(view -> { popup.dismiss(); action.send(code, 0, null, null); });
            if (code == BM_TOGGLE) star[0] = button;
            icons.addView(button, new LinearLayout.LayoutParams(0, dp(context, 48), 1));
        }
        root.addView(icons);
        root.addView(divider(context));

        String[] rows = { "New tab", "Bookmarks", "History", "Downloads", "Workspaces" };
        int[] rowCodes = { NEW_TAB, BOOKMARKS, HISTORY, DOWNLOADS, WORKSPACES };
        for (int i = 0; i < rows.length; i++) {
            LinearLayout row = row(context, rowCodes[i], rows[i], true, -1);
            final int code = rowCodes[i];
            row.setOnClickListener(view -> { popup.dismiss(); action.send(code, 0, null, null); });
            if (code == BOOKMARKS) row.setOnLongClickListener(view -> {   /* the menu stays open so the notice shows under the row */
                boolean was = state[5] != 0;
                if (bookmarkHere(view, was, state[6] != 0, action) && !was) {
                    state[5] = 1;
                    star[0].setImageDrawable(BrowserIcons.of(BM_TOGGLE, ACCENT, dp(context, 24)));
                    star[0].setContentDescription("Remove bookmark");
                }
                return true;
            });
            root.addView(row, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(context, 48)));
        }
        root.addView(divider(context));

        /* page zoom: the zoom icon followed by quick levels on one line, the slider below it; the level
           matching the slider is highlighted, the page relayouts when the thumb is let go */
        boolean canZoom = state[3] != 0;
        LinearLayout zoom = new LinearLayout(context);
        zoom.setOrientation(LinearLayout.HORIZONTAL);
        zoom.setGravity(Gravity.CENTER_VERTICAL);
        zoom.setPadding(dp(context, 16), 0, dp(context, 8), 0);
        ImageView zoomIcon = icon(context, ZOOM, canZoom ? SUBTLE : DISABLED, 22);
        zoomIcon.setContentDescription("Zoom");
        zoom.addView(zoomIcon, new LinearLayout.LayoutParams(dp(context, 24), dp(context, 24)));
        zoom.addView(new View(context), new LinearLayout.LayoutParams(dp(context, 8), 1));
        final SeekBar slider = new SeekBar(context);
        slider.setMax((ZOOM_MAX - ZOOM_MIN) / ZOOM_STEP);
        int start = Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, state[4]));
        slider.setProgress(Math.round((start - ZOOM_MIN) / (float) ZOOM_STEP));
        slider.setEnabled(canZoom);
        slider.setContentDescription("Page zoom " + start + " percent");
        final int[] levels = { 50, 100, 150, 200 };
        final TextView[] quick = new TextView[levels.length];
        for (int i = 0; i < levels.length; i++) {
            final int level = levels[i];
            quick[i] = label(context, String.valueOf(level), 14, canZoom ? (level == start ? ACCENT : TEXT) : DISABLED);
            quick[i].setGravity(Gravity.CENTER);
            quick[i].setEnabled(canZoom);
            quick[i].setContentDescription("Zoom " + level + " percent");
            ripple(quick[i], true);
            quick[i].setOnClickListener(view -> {
                slider.setProgress((level - ZOOM_MIN) / ZOOM_STEP);
                action.send(ZOOM, level, null, null);
            });
            zoom.addView(quick[i], new LinearLayout.LayoutParams(dp(context, 48), dp(context, 44)));
        }
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean user) {
                int pct = ZOOM_MIN + progress * ZOOM_STEP;
                bar.setContentDescription("Page zoom " + pct + " percent");
                for (int i = 0; i < levels.length; i++) quick[i].setTextColor(levels[i] == pct ? ACCENT : TEXT);
            }
            @Override public void onStartTrackingTouch(SeekBar bar) { }
            @Override public void onStopTrackingTouch(SeekBar bar) {
                action.send(ZOOM, ZOOM_MIN + bar.getProgress() * ZOOM_STEP, null, null);
            }
        });
        root.addView(zoom, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(context, 46)));
        slider.setPadding(dp(context, 26), 0, dp(context, 26), 0);
        root.addView(slider, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(context, 40)));

        /* state[2]: 1 for this page's site, 2 for every site (a page that is not a site) */
        final boolean desktopOn = state[1] != 0, fullscreenOn = state[8] != 0;
        LinearLayout desktop = row(context, DESKTOP, state[2] == 2 ? "Desktop site (all sites)" : "Desktop site", true, desktopOn ? 1 : 0);
        desktop.setOnClickListener(view -> { popup.dismiss(); action.send(DESKTOP, desktopOn ? 0 : 1, null, null); });
        root.addView(desktop, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(context, 48)));
        LinearLayout fullscreen = row(context, FULLSCREEN, "Fullscreen", true, fullscreenOn ? 1 : 0);
        fullscreen.setOnClickListener(view -> { popup.dismiss(); action.send(FULLSCREEN, fullscreenOn ? 0 : 1, null, null); });
        root.addView(fullscreen, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(context, 48)));
        root.addView(divider(context));

        /* the keyboard stays up for the last focused field until Back or its own hide key */
        LinearLayout keyboard = row(context, KEYBOARD, "Show keyboard", true, -1);
        keyboard.setOnClickListener(view -> { popup.dismiss(); action.send(KEYBOARD, 1, null, null); });
        root.addView(keyboard, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(context, 48)));
        LinearLayout settings = row(context, SETTINGS, "Settings", true, -1);
        settings.setOnClickListener(view -> { popup.dismiss(); action.send(SETTINGS, 0, null, null); });
        root.addView(settings, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(context, 48)));

        ScrollView scroll = new ScrollView(context);
        scroll.addView(root);
        GradientDrawable background = new GradientDrawable();
        background.setColor(Color.WHITE);
        background.setCornerRadius(dp(context, 10));
        popup.setContentView(scroll);
        popup.setBackgroundDrawable(background);
        popup.setElevation(dp(context, 10));
        popup.setWidth(dp(context, 280));
        popup.setHeight(ViewGroup.LayoutParams.WRAP_CONTENT);
        popup.setFocusable(true);
        popup.setOutsideTouchable(true);
        popup.setOnDismissListener(() -> { if (menu == popup) menu = null; });
        View decor = activity.getWindow().getDecorView();
        int top = 0;
        WindowInsets insets = decor.getRootWindowInsets();
        if (insets != null) top = insets.getSystemWindowInsetTop();
        menu = popup;
        TouchFeedback.popup(popup);
        popup.showAtLocation(decor, Gravity.TOP | Gravity.END, dp(context, 4), top + dp(context, 4));
    }

    /* what the address field's site icon shows for a URL */
    static final int SITE_NONE = 0, SITE_SECURE = 1, SITE_INSECURE = 2, SITE_OTHER = 3;
    static int siteKind(String url) {
        if (url == null || url.isEmpty()) return SITE_NONE;
        if (url.regionMatches(true, 0, "https://", 0, 8)) return SITE_SECURE;
        if (url.regionMatches(true, 0, "http://", 0, 7)) return SITE_INSECURE;
        return SITE_OTHER;
    }
    static String siteIcon(int kind) { return kind == SITE_SECURE ? BrowserIcons.LOCK : kind == SITE_INSECURE ? BrowserIcons.WARNING : BrowserIcons.INFO; }

    private static String host(String url) {
        String host = android.net.Uri.parse(url).getHost();
        return host != null && !host.isEmpty() ? host : null;
    }

    /* a site info line: an icon, a heading and a line of detail under it */
    private static LinearLayout infoRow(Context context, String icon, int color, String heading, String detail) {
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(context, 16), dp(context, 10), dp(context, 18), dp(context, 10));
        ImageView image = new ImageView(context);
        image.setScaleType(ImageView.ScaleType.CENTER);
        image.setImageDrawable(BrowserIcons.path(icon, color, dp(context, 22)));
        row.addView(image, new LinearLayout.LayoutParams(dp(context, 24), dp(context, 24)));
        LinearLayout text = new LinearLayout(context);
        text.setOrientation(LinearLayout.VERTICAL);
        text.setPadding(dp(context, 16), 0, 0, 0);
        text.addView(label(context, heading, 15, color == UNSAFE ? color : TEXT));
        if (detail != null) {
            TextView more = label(context, detail, 13, SUBTLE);
            more.setPadding(0, dp(context, 2), 0, 0);
            text.addView(more);
        }
        row.addView(text, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        return row;
    }

    /* Site info, from the icon at the address field's left end, as in Chrome: the connection, the
       site's cookies and Delete data, which removes the site's cookies, storage and service workers
       (with its subdomains) after a confirmation. */
    static void showSiteInfo(Activity activity, View anchor, String url, Action action) {
        if (menu != null) { menu.dismiss(); menu = null; }
        final Context context = activity;
        final int kind = siteKind(url);
        if (kind == SITE_NONE) return;
        final boolean web = kind == SITE_SECURE || kind == SITE_INSECURE;
        String host = host(url), site = null;
        int cookies = 0;
        if (web) {
            byte[] bytes = url.getBytes(java.nio.charset.StandardCharsets.UTF_8);
            try { site = LumenActivity.nativeSite(bytes); cookies = LumenActivity.nativeSiteCookies(bytes); } catch (UnsatisfiedLinkError error) { site = null; }
            if (site == null) site = host;
        }
        LinearLayout root = new LinearLayout(context);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(0, dp(context, 6), 0, dp(context, 6));
        final PopupWindow popup = new PopupWindow(context);

        String heading = web && host != null ? host : url.regionMatches(true, 0, "lumen:", 0, 6) ? "Lumen page"
            : url.regionMatches(true, 0, "file:", 0, 5) ? "File on this phone" : host != null ? host : "This page";
        TextView title = label(context, heading, 17, TEXT);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        title.setSingleLine(true);
        title.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        title.setPadding(dp(context, 20), dp(context, 8), dp(context, 20), 0);
        root.addView(title);
        TextView address = label(context, url, 12, SUBTLE);
        address.setSingleLine(true);
        address.setEllipsize(TextUtils.TruncateAt.END);
        address.setPadding(dp(context, 20), dp(context, 2), dp(context, 20), dp(context, 10));
        root.addView(address);
        root.addView(divider(context));

        if (kind == SITE_SECURE) root.addView(infoRow(context, BrowserIcons.LOCK, SUBTLE, "Connection is secure",
            "Your information (for example, passwords or credit card numbers) is private when it is sent to this site."));
        else if (kind == SITE_INSECURE) root.addView(infoRow(context, BrowserIcons.WARNING, UNSAFE, "Connection is not secure",
            "You should not enter any sensitive information on this site (for example, passwords or credit cards), because it could be stolen by attackers."));
        else root.addView(infoRow(context, BrowserIcons.INFO, SUBTLE, "This page is not on the web",
            "It is not loaded from a site, so it has no cookies or site data."));
        if (web) {
            root.addView(infoRow(context, BrowserIcons.COOKIE, SUBTLE, "Cookies and site data",
                (cookies == 0 ? "No cookies" : cookies == 1 ? "1 cookie" : cookies + " cookies") + " for " + site + " and its subdomains"));
            root.addView(divider(context));
            LinearLayout delete = new LinearLayout(context);
            delete.setOrientation(LinearLayout.HORIZONTAL);
            delete.setGravity(Gravity.CENTER_VERTICAL);
            delete.setPadding(dp(context, 16), 0, dp(context, 18), 0);
            ImageView trash = new ImageView(context);
            trash.setScaleType(ImageView.ScaleType.CENTER);
            trash.setImageDrawable(BrowserIcons.path(BrowserIcons.TRASH, ACCENT, dp(context, 22)));
            delete.addView(trash, new LinearLayout.LayoutParams(dp(context, 24), dp(context, 24)));
            TextView name = label(context, "Delete data", 16, ACCENT);
            name.setPadding(dp(context, 16), 0, 0, 0);
            delete.addView(name, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
            delete.setContentDescription("Delete data of " + site);
            ripple(delete, false);
            final String which = site;
            delete.setOnClickListener(view -> {
                popup.dismiss();
                show(new AlertDialog.Builder(activity)
                    .setTitle("Delete site data?")
                    .setMessage("All cookies, local and session storage and service workers of " + which
                        + " and its subdomains will be deleted, and this page reloads. You will be signed out of this site.")
                    .setPositiveButton("Delete", (dialog, button) -> action.send(SITE_CLEAR, 0, null, url))
                    .setNegativeButton("Cancel", null));
            });
            root.addView(delete, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(context, 52)));
        }

        ScrollView scroll = new ScrollView(context);
        scroll.addView(root);
        GradientDrawable background = new GradientDrawable();
        background.setColor(Color.WHITE);
        background.setCornerRadius(dp(context, 10));
        popup.setContentView(scroll);
        popup.setBackgroundDrawable(background);
        popup.setElevation(dp(context, 10));
        int screen = context.getResources().getDisplayMetrics().widthPixels;
        popup.setWidth(Math.min(screen - dp(context, 16), dp(context, 360)));
        popup.setHeight(ViewGroup.LayoutParams.WRAP_CONTENT);
        popup.setFocusable(true);
        popup.setOutsideTouchable(true);
        popup.setOnDismissListener(() -> { if (menu == popup) menu = null; });
        menu = popup;
        TouchFeedback.popup(popup);
        popup.showAsDropDown(anchor, 0, dp(context, 4));
    }

    private static final class Bookmarks extends BaseAdapter {
        final String[] titles, urls;
        Bookmarks(String[] titles, String[] urls) { this.titles = titles; this.urls = urls; }
        @Override public int getCount() { return titles.length; }
        @Override public Object getItem(int position) { return urls[position]; }
        @Override public long getItemId(int position) { return position; }
        @Override public View getView(int position, View reuse, ViewGroup parent) {
            Context context = parent.getContext();
            LinearLayout row = new LinearLayout(context);
            row.setOrientation(LinearLayout.VERTICAL);
            row.setPadding(dp(context, 24), dp(context, 10), dp(context, 24), dp(context, 10));
            TextView title = label(context, titles[position], 16, TEXT);
            title.setSingleLine(true);
            title.setEllipsize(TextUtils.TruncateAt.END);
            boolean script = urls[position].regionMatches(true, 0, "javascript:", 0, 11);
            TextView address = label(context, script ? "Script · " + urls[position].substring(11) : urls[position], 12, SUBTLE);
            address.setSingleLine(true);
            address.setEllipsize(TextUtils.TruncateAt.END);
            row.addView(title);
            row.addView(address);
            return row;
        }
    }

    static void showBookmarks(Activity activity, String[] titles, String[] urls, String pageTitle, String pageUrl, Action action) {
        if (menu != null) { menu.dismiss(); menu = null; }
        AlertDialog.Builder builder = new AlertDialog.Builder(activity).setTitle("Bookmarks");
        ListView list = null;
        if (titles.length == 0) {
            builder.setMessage("No bookmarks yet. Add the current page, or create one whose URL starts with javascript: to run code on the page you are viewing.");
        } else {
            list = new ListView(activity);
            list.setAdapter(new Bookmarks(titles, urls));
            list.setDivider(new ColorDrawable(DIVIDER));
            list.setDividerHeight(Math.max(1, dp(activity, 1)));
            builder.setView(list);
        }
        if (pageUrl != null) builder.setPositiveButton("Add current page", (dialog, which) -> edit(activity, -1, pageTitle, pageUrl, action));
        builder.setNeutralButton("New", (dialog, which) -> edit(activity, -1, "", "javascript:", action));
        builder.setNegativeButton("Close", null);
        final AlertDialog dialog = show(builder);
        if (list == null) return;
        list.setOnItemClickListener((parent, view, position, id) -> {
            dialog.dismiss();
            action.send(BM_OPEN, position, null, urls[position]);
        });
        list.setOnItemLongClickListener((parent, view, position, id) -> {
            show(new AlertDialog.Builder(activity)
                .setTitle(titles[position])
                .setItems(new String[] { "Edit", "Delete" }, (options, which) -> {
                    dialog.dismiss();
                    if (which == 0) edit(activity, position, titles[position], urls[position], action);
                    else action.send(BM_DELETE, position, null, urls[position]);
                })
                );
            return true;
        });
    }

    private static void edit(Activity activity, int index, String title, String url, Action action) {
        LinearLayout form = new LinearLayout(activity);
        form.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(activity, 22);
        form.setPadding(pad, dp(activity, 8), pad, 0);
        final EditText name = new EditText(activity);
        name.setHint("Name");
        name.setSingleLine(true);
        name.setText(title != null ? title : "");
        final EditText address = new EditText(activity);
        address.setHint("URL, or javascript: code");
        address.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE
            | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS | InputType.TYPE_TEXT_VARIATION_URI);
        address.setMaxLines(8);
        address.setText(url != null ? url : "");
        TextView note = label(activity, "A bookmark whose URL starts with javascript: runs that code on the page you are viewing when you open it.", 12, SUBTLE);
        note.setPadding(0, dp(activity, 6), 0, 0);
        form.addView(name);
        form.addView(address);
        form.addView(note);
        show(new AlertDialog.Builder(activity)
            .setTitle(index < 0 ? "Add bookmark" : "Edit bookmark")
            .setView(form)
            .setPositiveButton("Save", (dialog, which) -> {
                String target = address.getText().toString().trim();
                if (target.isEmpty()) { toast(activity, "A bookmark needs a URL"); return; }
                action.send(BM_SAVE, index, name.getText().toString().trim(), target);
            })
            .setNegativeButton("Cancel", null)
            );
    }

    private interface Named { void done(String name); }
    private static void promptName(Activity activity, String title, String initial, Named done) {
        final EditText name = new EditText(activity);
        name.setSingleLine(true);
        name.setText(initial);
        name.setSelectAllOnFocus(true);
        LinearLayout box = new LinearLayout(activity);
        int pad = dp(activity, 22);
        box.setPadding(pad, dp(activity, 8), pad, 0);
        box.addView(name, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        show(new AlertDialog.Builder(activity)
            .setTitle(title)
            .setView(box)
            .setPositiveButton("OK", (dialog, which) -> {
                String value = name.getText().toString().trim();
                if (!value.isEmpty()) done.done(value);
            })
            .setNegativeButton("Cancel", null)
            );
    }

    static void showWorkspaces(Activity activity, String[] names, int[] tabs, int current, Action action) {
        if (menu != null) { menu.dismiss(); menu = null; }
        String[] items = new String[names.length];
        for (int i = 0; i < names.length; i++) items[i] = names[i] + "  ·  " + tabs[i] + (tabs[i] == 1 ? " tab" : " tabs");
        AlertDialog.Builder builder = new AlertDialog.Builder(activity)
            .setTitle("Workspaces")
            .setSingleChoiceItems(items, current, (dialog, which) -> {
                dialog.dismiss();
                if (which != current) action.send(WS_SELECT, which, null, null);
            })
            .setNegativeButton("Close", null);
        if (names.length < MAX_WORKSPACES) {
            builder.setPositiveButton("New workspace", (dialog, which) ->
                promptName(activity, "New workspace", "Workspace " + (names.length + 1), name -> action.send(WS_NEW, 0, name, null)));
        }
        final AlertDialog dialog = show(builder);
        dialog.getListView().setOnItemLongClickListener((parent, view, position, id) -> {
            String[] options = names.length > 1 ? new String[] { "Rename", "Close workspace" } : new String[] { "Rename" };
            show(new AlertDialog.Builder(activity)
                .setTitle(names[position])
                .setItems(options, (choices, which) -> {
                    dialog.dismiss();
                    if (which == 0) promptName(activity, "Rename workspace", names[position], name -> action.send(WS_RENAME, position, name, null));
                    else action.send(WS_CLOSE, position, null, null);
                })
                );
            return true;
        });
    }

    /* the tab menu (a double tap on the tab bar): the chosen item's index goes back as TAB_MENU */
    static void showTabMenu(Activity activity, String title, String[] items, boolean[] checked, Action action) {
        if (menu != null) { menu.dismiss(); menu = null; }
        String[] labels = new String[items.length];
        for (int i = 0; i < items.length; i++) labels[i] = (checked[i] ? "\u2713  " : "     ") + items[i];
        show(new AlertDialog.Builder(activity)
            .setTitle(title)
            .setItems(labels, (dialog, which) -> action.send(TAB_MENU, which, null, null))
            .setNegativeButton("Cancel", null));
    }
}
