/*
 * xmms2-notify.c - xmms2 libnotify client
 *
 * Copyright © 2009 Evan McClain <evan.mcclain@gatech.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include <stdlib.h>
#include <xmmsclient/xmmsclient.h>
#include <xmmsclient/xmmsclient-glib.h>
#include <glib.h>
#include <libnotify/notify.h>

#define UNKNOWN "Unknown"
#define NOTE "\xe2\x99\xab"
#define USAGE "Notify currently playing song.\n -c: will only notify once and exit,\n default is to run until xmms2 is quit.\n"

typedef struct track_t {
    gint status;
    gint id;
    gint tracknr;
    gchar *album;
    gchar *artist;
    gchar *title;
    gchar *picture_front;
    GdkPixbuf *cover;
} track;

xmmsc_connection_t *conn = NULL;
track current;

/** Notify current song.
 */
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

    /** We only notify if we have a valid id.
     */
    if(current.id != 0)
    {

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
        g_free(tmp);

        notify_notification_set_timeout(n, 5000);
        notify_notification_show(n, NULL);
        g_object_unref(n);
        n = NULL;
    }
}

/** Free stuff stored in the current track
 */
void
current_track_unref()
{
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

    return 1;
}

void
set_current_track(xmmsv_t *dict, void *udata)
{
    const char *tmp;
    xmmsc_result_t *res;

    current_track_unref();

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

    if(!xmmsv_get_int(val, &current.id))
        current.id = 0;

    res = xmmsc_medialib_get_info(conn, current.id);
    xmmsc_result_notifier_set(res, on_medialib_info, udata);
    xmmsc_result_unref(res);

    return 1;
}

int
on_status_change(xmmsv_t *val, void *udata)
{
    xmmsv_get_int(val, &current.status);
    notify_song();

    return 1;
}

void
on_disconnect(void *arg)
{
    GMainLoop *loop = arg;
    g_main_loop_quit(loop);
}

int
main(int argc, char **argv)
{
    GMainLoop *ml;
    xmmsc_result_t *res;
    xmmsv_t *returnval, *dict;
    int c;
    int flag_current = 0;
    const char *err_buf;

    while((c = getopt(argc, argv, "ch")) != -1)
    {
        switch(c)
        {
            case 'c':
                flag_current = 1;
                break;
            case 'h':
                printf(USAGE);
                return EXIT_SUCCESS;
            default:
                return EXIT_FAILURE;
        }
    }

    conn = xmmsc_init("XMMS2-Notify");
    notify_init("XMMS2-Notify");

    if(!xmmsc_connect(conn, NULL))
    {
        fprintf(stderr, "Connection failed: %s\n",
                xmmsc_get_last_error(conn));
        return EXIT_FAILURE;
    }

    if(!flag_current)
    {

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
    }
    else
    {
        res = xmmsc_playback_status(conn);
        xmmsc_result_wait(res);
        returnval = xmmsc_result_get_value(res);
        if(xmmsv_is_error(returnval) &&
                xmmsv_get_error(returnval, &err_buf))
        {
            fprintf(stderr, "playback current id returns error, %s\n",
                    err_buf);
	}
        on_status_change(returnval, NULL);


        res = xmmsc_playback_current_id(conn);
        xmmsc_result_wait(res);
        returnval = xmmsc_result_get_value(res);
        if(xmmsv_is_error(returnval) &&
                xmmsv_get_error(returnval, &err_buf))
        {
            fprintf(stderr, "playback current id returns error, %s\n",
                    err_buf);
	}
        xmmsv_get_int(returnval, &current.id);

        res = xmmsc_medialib_get_info(conn, current.id);
        xmmsc_result_wait(res);
        returnval = xmmsc_result_get_value(res);
        dict = xmmsv_propdict_to_dict(returnval, NULL);
        set_current_track(dict, NULL);
    }

    notify_uninit();
    xmmsc_unref(conn);

    current_track_unref();

    return EXIT_SUCCESS;
}

/* vim: set fdm=syntax: */
