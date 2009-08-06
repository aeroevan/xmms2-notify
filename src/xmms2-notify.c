#include <stdlib.h>
#include <xmmsclient/xmmsclient.h>
#include <xmmsclient/xmmsclient-glib.h>
#include <glib.h>
#include <libnotify/notify.h>

#define UNKNOWN "Unknown"
#define NOTE "\xe2\x99\xab"

typedef struct track_t {
    gint status;
    gint tracknr;
    gchar *album;
    gchar *artist;
    gchar *title;
    gchar *picture_front;
    GdkPixbuf *cover;
} track;

xmmsc_connection_t *conn = NULL;
track current;

void
notify_song()
{
    static NotifyNotification *n = NULL;
    GdkPixbuf *p;
    gchar *tmp;
    gchar *title;

    if(n)
    {
        notify_notification_close(n, NULL);
        g_object_unref(n);
        n = NULL;
    }

    tmp = g_markup_printf_escaped("<big><b>%s</b></big>\n"
                "<small>by</small> %s <small>from</small> %s [%d]",
            current.title, current.artist,
            current.album, current.tracknr);

    switch(current.status)
    {
        case XMMS_PLAYBACK_STATUS_PLAY:
            title = g_strdup_printf("%s Currently playing", NOTE);
            break;
        case XMMS_PLAYBACK_STATUS_STOP:
            title = g_strdup_printf("%s Playback stopped", NOTE);
            break;
        case XMMS_PLAYBACK_STATUS_PAUSE:
            title = g_strdup_printf("%s Playback paused", NOTE);
            break;
        default:
            title = g_strdup(NOTE);
    }

    if(current.cover)
    {
        p = gdk_pixbuf_scale_simple(current.cover, 48, 48, 
                GDK_INTERP_BILINEAR);
        n = notify_notification_new(title, tmp, NULL, NULL);
        notify_notification_set_icon_from_pixbuf(n, p);
        g_object_unref(p);
    }
    else
        n = notify_notification_new(title, tmp, "media-optical", NULL);

    g_free(title);

    notify_notification_set_timeout(n, 5000);
    notify_notification_show(n, NULL);
}

int
on_bindata_retrieve(xmmsv_t *res, void *udata)
{
    GdkPixbufLoader *loader;
    const unsigned char *data;
    unsigned int len;

    if(xmmsv_get_bin(res, &data, &len) && (len > 0))
    {
        loader = gdk_pixbuf_loader_new();
        gdk_pixbuf_loader_write(loader, data, len, NULL);
        gdk_pixbuf_loader_close(loader, NULL);
        current.cover = gdk_pixbuf_loader_get_pixbuf(loader);
    }
    notify_song();
}

void
set_current_track(xmmsv_t *dict, void *udata)
{
    const char *tmp;
    xmmsc_result_t *res;

    if(current.album)
    {
        g_free(current.album);
        current.album = NULL;
    }
    if(current.artist)
    {
        g_free(current.artist);
        current.artist = NULL;
    }
    if(current.title)
    {
        g_free(current.title);
        current.title = NULL;
    }
    if(current.picture_front)
    {
        g_free(current.picture_front);
        current.picture_front = NULL;
    }
    if(current.cover)
    {
        g_object_unref(current.cover);
        current.cover = NULL;
    }

    if(!xmmsv_dict_entry_get_string(dict, "album", &tmp))
    {
        current.album = g_strdup(UNKNOWN);
    }
    else
    {
        current.album = g_strdup(tmp);
    }

    if(!xmmsv_dict_entry_get_string(dict, "artist", &tmp))
    {
        current.artist = g_strdup(UNKNOWN);
    }
    else
    {
        current.artist = g_strdup(tmp);
    }

    if(!xmmsv_dict_entry_get_string(dict, "title", &tmp))
    {
        current.title = g_strdup(UNKNOWN);
    }
    else
    {
        current.title = g_strdup(tmp);
    }

    if(!xmmsv_dict_entry_get_int(dict, "tracknr", &current.tracknr))
        current.tracknr = 0;

    if(xmmsv_dict_entry_get_string(dict, "picture_front", &tmp))
    {
        current.picture_front = g_strdup(tmp);
        res = xmmsc_bindata_retrieve(conn, current.picture_front);
        xmmsc_result_notifier_set(res, on_bindata_retrieve, udata);
        xmmsc_result_unref(res);
    }
    else
    {
        notify_song();
    }
}

int
on_medialib_info(xmmsv_t *val, void *udata)
{
    xmmsv_t *dict;

    dict = xmmsv_propdict_to_dict(val, NULL);
    set_current_track(dict, udata);
    xmmsv_unref(dict);

    return 0;
}

int
on_current_id(xmmsv_t *val, void *udata)
{
    xmmsc_result_t *res;
    int32_t id = -1;

    xmmsv_get_int(val, &id);

    res = xmmsc_medialib_get_info(conn, id);
    xmmsc_result_notifier_set(res, on_medialib_info, udata);
    xmmsc_result_unref(res);

    return 1;
}

int
on_status_change(xmmsv_t *val, void *udata)
{
    xmmsv_get_int(val, &current.status);

    return 1;
}

void
on_disconnect(void *arg)
{
    GMainLoop *loop = arg;
    g_main_loop_quit(loop);
}

int
main()
{
    GMainLoop *ml;
    xmmsc_result_t *res;

    conn = xmmsc_init("XMMS2-Notify");
    notify_init("XMMS2-Notify");

    if(!xmmsc_connect(conn, NULL))
    {
        fprintf(stderr, "Connection failed: %s\n",
                xmmsc_get_last_error(conn));
        return EXIT_FAILURE;
    }

    ml = g_main_loop_new(NULL, FALSE);

    res = xmmsc_playback_current_id(conn);
    xmmsc_result_notifier_set(res, on_current_id, ml);
    xmmsc_result_unref(res);

    res = xmmsc_broadcast_playback_current_id(conn);
    xmmsc_result_notifier_set(res, on_current_id, ml);
    xmmsc_result_unref(res);

    res = xmmsc_playback_status(conn);
    xmmsc_result_notifier_set(res, on_status_change, ml);
    xmmsc_result_unref(res);

    res = xmmsc_broadcast_playback_status(conn);
    xmmsc_result_notifier_set(res, on_status_change, ml);
    xmmsc_result_unref(res);

    xmmsc_disconnect_callback_set(conn, on_disconnect, ml);

    xmmsc_mainloop_gmain_init(conn);
    g_main_loop_run(ml);

    return EXIT_SUCCESS;
}

/* vim: set fdm=syntax: */
