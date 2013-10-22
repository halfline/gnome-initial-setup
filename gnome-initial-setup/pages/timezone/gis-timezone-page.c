/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

/* Timezone page {{{1 */

#define PAGE_ID "timezone"

#include "config.h"
#include "cc-datetime-resources.h"
#include "timezone-resources.h"
#include "gis-timezone-page.h"

#include <glib/gi18n.h>
#include <gio/gio.h>

#include <stdlib.h>
#include <string.h>

#define GWEATHER_I_KNOW_THIS_IS_UNSTABLE
#include <libgweather/location-entry.h>

#include "cc-timezone-map.h"
#include "timedated.h"

#define DEFAULT_TZ "Europe/London"

struct _GisTimezonePagePrivate
{
  CcTimezoneMap *map;
  TzLocation *current_location;
  Timedate1 *dtm;
};
typedef struct _GisTimezonePagePrivate GisTimezonePagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GisTimezonePage, gis_timezone_page, GIS_TYPE_PAGE);

#define OBJ(type,name) ((type)gtk_builder_get_object(GIS_PAGE (page)->builder,(name)))
#define WID(name) OBJ(GtkWidget*,name)

static void
set_timezone_cb (GObject      *source,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  GisTimezonePage *page = user_data;
  GisTimezonePagePrivate *priv = gis_timezone_page_get_instance_private (page);
  GError *error;

  error = NULL;
  if (!timedate1_call_set_timezone_finish (priv->dtm,
                                           res,
                                           &error)) {
    /* TODO: display any error in a user friendly way */
    g_warning ("Could not set system timezone: %s", error->message);
    g_error_free (error);
  }
}


static void
queue_set_timezone (GisTimezonePage *page)
{
  GisTimezonePagePrivate *priv = gis_timezone_page_get_instance_private (page);

  /* for now just do it */
  if (priv->current_location) {
    timedate1_call_set_timezone (priv->dtm,
                                 priv->current_location->zone,
                                 TRUE,
                                 NULL,
                                 set_timezone_cb,
                                 page);
  }
}

static void
update_timezone (GisTimezonePage *page)
{
  GisTimezonePagePrivate *priv = gis_timezone_page_get_instance_private (page);
  GString *str;
  gchar *location;
  gchar *timezone;
  gchar *c;

  str = g_string_new ("");
  for (c = priv->current_location->zone; *c; c++) {
    switch (*c) {
    case '_':
      g_string_append_c (str, ' ');
      break;
    case '/':
      g_string_append (str, " / ");
      break;
    default:
      g_string_append_c (str, *c);
    }
  }

  c = strstr (str->str, " / ");
  location = g_strdup (c + 3);
  timezone = g_strdup (str->str);

  gtk_label_set_label (OBJ(GtkLabel*,"current-location-label"), location);
  gtk_label_set_label (OBJ(GtkLabel*,"current-timezone-label"), timezone);

  g_free (location);
  g_free (timezone);

  g_string_free (str, TRUE);
}

static void
location_changed_cb (CcTimezoneMap   *map,
                     TzLocation      *location,
                     GisTimezonePage *page)
{
  GisTimezonePagePrivate *priv = gis_timezone_page_get_instance_private (page);

  g_debug ("location changed to %s/%s", location->country, location->zone);

  priv->current_location = location;

  update_timezone (page);

  queue_set_timezone (page);
}

static void
set_timezone_from_gweather_location (GisTimezonePage  *page,
                                     GWeatherLocation *gloc)
{
  GisTimezonePagePrivate *priv = gis_timezone_page_get_instance_private (page);
  GWeatherTimezone *zone = gweather_location_get_timezone (gloc);
  gchar *city = gweather_location_get_city_name (gloc);

  if (zone != NULL) {
    const gchar *name;
    const gchar *id;
    GtkLabel *label;

    label = OBJ(GtkLabel*, "current-timezone-label");

    name = gweather_timezone_get_name (zone);
    id = gweather_timezone_get_tzid (zone);
    if (name == NULL) {
      /* Why does this happen ? */
      name = id;
    }
    gtk_label_set_label (label, name);
    cc_timezone_map_set_timezone (priv->map, id);
  }

  if (city != NULL) {
    GtkLabel *label;

    label = OBJ(GtkLabel*, "current-location-label");
    gtk_label_set_label (label, city);
  }

  g_free (city);
}

static void
location_changed (GObject *object, GParamSpec *param, GisTimezonePage *page)
{
  GWeatherLocationEntry *entry = GWEATHER_LOCATION_ENTRY (object);
  GWeatherLocation *gloc;

  gloc = gweather_location_entry_get_location (entry);
  if (gloc == NULL)
    return;

  set_timezone_from_gweather_location (page, gloc);

  gweather_location_unref (gloc);
}

#define WANT_GEOCLUE 0

#if WANT_GEOCLUE
static void
position_callback (GeocluePosition      *pos,
		   GeocluePositionFields fields,
		   int                   timestamp,
		   double                latitude,
		   double                longitude,
		   double                altitude,
		   GeoclueAccuracy      *accuracy,
		   GError               *error,
		   GisTimezonePage      *page)
{
  if (error) {
    g_printerr ("Error getting position: %s\n", error->message);
    g_error_free (error);
  } else {
    if (fields & GEOCLUE_POSITION_FIELDS_LATITUDE &&
        fields & GEOCLUE_POSITION_FIELDS_LONGITUDE) {
      GWeatherLocation *city = gweather_location_find_nearest_city (latitude, longitude);
      set_timezone_from_gweather_location (page, city);
    } else {
      g_print ("Position not available.\n");
    }
  }
}

static void
determine_timezone (GtkWidget       *widget,
                    GisTimezonePage *page)
{
  GeoclueMaster *master;
  GeoclueMasterClient *client;
  GeocluePosition *position = NULL;
  GError *error = NULL;

  master = geoclue_master_get_default ();
  client = geoclue_master_create_client (master, NULL, NULL);
  g_object_unref (master);

  if (!geoclue_master_client_set_requirements (client, 
                                               GEOCLUE_ACCURACY_LEVEL_LOCALITY,
                                               0, TRUE,
                                               GEOCLUE_RESOURCE_ALL,
                                               NULL)){
    g_printerr ("Setting requirements failed");
    goto out;
  }

  position = geoclue_master_client_create_position (client, &error);
  if (position == NULL) {
    g_warning ("Creating GeocluePosition failed: %s", error->message);
    goto out;
  }

  geoclue_position_get_position_async (position,
                                       (GeocluePositionCallback) position_callback,
                                       page);

 out:
  g_clear_error (&error);
  g_object_unref (client);
  g_object_unref (position);
}
#endif

static void
gis_timezone_page_constructed (GObject *object)
{
  GisTimezonePage *page = GIS_TIMEZONE_PAGE (object);
  GisTimezonePagePrivate *priv = gis_timezone_page_get_instance_private (page);
  GtkWidget *frame, *map, *entry;
  GWeatherLocation *world;
  GError *error;
  const gchar *timezone;

  G_OBJECT_CLASS (gis_timezone_page_parent_class)->constructed (object);

  gtk_container_add (GTK_CONTAINER (page), WID ("timezone-page"));

  frame = WID("timezone-map-frame");

  error = NULL;
  priv->dtm = timedate1_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                G_DBUS_PROXY_FLAGS_NONE,
                                                "org.freedesktop.timedate1",
                                                "/org/freedesktop/timedate1",
                                                NULL,
                                                &error);
  if (priv->dtm == NULL) {
    g_error ("Failed to create proxy for timedated: %s", error->message);
    exit (1);
  }

  priv->map = cc_timezone_map_new ();
  map = GTK_WIDGET (priv->map);
  gtk_widget_set_hexpand (map, TRUE);
  gtk_widget_set_vexpand (map, TRUE);
  gtk_widget_set_halign (map, GTK_ALIGN_FILL);
  gtk_widget_set_valign (map, GTK_ALIGN_FILL);
  gtk_widget_show (map);

  gtk_container_add (GTK_CONTAINER (frame), map);

  world = gweather_location_new_world (TRUE);
  entry = gweather_location_entry_new (world);
  gtk_entry_set_placeholder_text (GTK_ENTRY (entry), _("Search for a location"));
  gtk_widget_set_halign (entry, GTK_ALIGN_FILL);
  gtk_widget_show (entry);

  frame = WID("timezone-page");
#if WANT_GEOCLUE
  gtk_grid_attach (GTK_GRID (frame), entry, 1, 1, 1, 1);
#else
  gtk_grid_attach (GTK_GRID (frame), entry, 0, 1, 2, 1);
#endif

  timezone = timedate1_get_timezone (priv->dtm);

  if (!cc_timezone_map_set_timezone (priv->map, timezone)) {
    g_warning ("Timezone '%s' is unhandled, setting %s as default", timezone, DEFAULT_TZ);
    cc_timezone_map_set_timezone (priv->map, DEFAULT_TZ);
  }
  else {
    g_debug ("System timezone is '%s'", timezone);
  }

  priv->current_location = cc_timezone_map_get_location (priv->map);
  update_timezone (page);

  g_signal_connect (G_OBJECT (entry), "notify::location",
                    G_CALLBACK (location_changed), page);

  g_signal_connect (map, "location-changed",
                    G_CALLBACK (location_changed_cb), page);

#if WANT_GEOCLUE
  g_signal_connect (WID ("timezone-auto-button"), "clicked",
                    G_CALLBACK (determine_timezone), page);
#else
  gtk_widget_hide (WID ("timezone-auto-button"));
#endif

  gis_page_set_complete (GIS_PAGE (page), TRUE);

  gtk_widget_show (GTK_WIDGET (page));
}

static void
gis_timezone_page_dispose (GObject *object)
{
  GisTimezonePage *page = GIS_TIMEZONE_PAGE (object);
  GisTimezonePagePrivate *priv = gis_timezone_page_get_instance_private (page);

  g_clear_object (&priv->dtm);

  G_OBJECT_CLASS (gis_timezone_page_parent_class)->dispose (object);
}

static void
gis_timezone_page_locale_changed (GisPage *page)
{
  gis_page_set_title (GIS_PAGE (page), _("Time Zone"));
}

static void
gis_timezone_page_class_init (GisTimezonePageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  page_class->page_id = PAGE_ID;
  page_class->locale_changed = gis_timezone_page_locale_changed;
  object_class->constructed = gis_timezone_page_constructed;
  object_class->dispose = gis_timezone_page_dispose;
}

static void
gis_timezone_page_init (GisTimezonePage *page)
{
  g_resources_register (timezone_get_resource ());
  g_resources_register (datetime_get_resource ());
}

void
gis_prepare_timezone_page (GisDriver *driver)
{
  gis_driver_add_page (driver,
                       g_object_new (GIS_TYPE_TIMEZONE_PAGE,
                                     "driver", driver,
                                     NULL));
}