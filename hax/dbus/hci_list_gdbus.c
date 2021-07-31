/*
 * hci_list_gdbus.c - List bluetooth controllers using GDBUS
 * 	- The example uses GDBUS to get the list of bluetooth controllers using DBUS
 * 	  interfaces provided by bluez
 * 	- If any controller is found, it prints the Name and MAC address of the controller
 * gcc `pkg-config --cflags glib-2.0 gio-2.0` -o ./bin/hci_list_gdbus ./hci_list_gdbus.c `pkg-config --libs glib-2.0 gio-2.0`
 * dbus-send --system --print-reply --type=method_call --dest='org.bluez' '/' org.freedesktop.DBus.ObjectManager.GetManagedObjects
 */

#include <glib.h>
#include <gio/gio.h>

GDBusConnection *con;
static const gchar* bluez_adapter_get_property(const gchar *path, const char *name)
{
	GVariant *result;
	GVariant *prop;
	GError *error = NULL;
	const gchar *address = NULL;

	result = g_dbus_connection_call_sync(con,
					     "org.bluez",
					     path,
					     "org.freedesktop.DBus.Properties",
					     "Get",
					     g_variant_new("(ss)", "org.bluez.Adapter1", name),
					     NULL,
					     G_DBUS_CALL_FLAGS_NONE,
					     -1,
					     NULL,
					     &error);
	if(error != NULL)
		return NULL;

	g_variant_get(result, "(v)", &prop);
	g_variant_unref(result);
	return g_variant_get_string(prop, NULL);
}

static void bluez_list_controllers(GDBusConnection *con,
				GAsyncResult *res,
				gpointer data)
{
	GVariant *result = NULL;
	GMainLoop *loop = NULL;
	gchar *s = NULL;
	GVariantIter i;
	const gchar *object_path;
	GVariant *ifaces_and_properties;

	loop = (GMainLoop *)data;
	result = g_dbus_connection_call_finish(con, res, NULL);
	if(result == NULL)
		g_print("Unable to get result for GetManagedObjects\n");

	/* Parse the result */
	if(result) {
		result = g_variant_get_child_value(result, 0);
		g_variant_iter_init(&i, result);
		while(g_variant_iter_next(&i, "{&o@a{sa{sv}}}", &object_path, &ifaces_and_properties)) {
			const gchar *interface_name;
			GVariant *properties;
			GVariantIter ii;
			g_variant_iter_init(&ii, ifaces_and_properties);
			while(g_variant_iter_next(&ii, "{&s@a{sv}}", &interface_name, &properties)) {
				if(g_strstr_len(g_ascii_strdown(interface_name, -1), -1, "adapter"))
					g_print("HCI Name: %s Address: %s\n", bluez_adapter_get_property(object_path, "Name"), bluez_adapter_get_property(object_path, "Address"));
				g_variant_unref(properties);
			}
			g_variant_unref(ifaces_and_properties);
		}
		g_variant_unref(result);
	}
	g_main_loop_quit(loop);
}

int main(void)
{
	GMainLoop *loop;

	con = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
	if(con == NULL) {
		g_print("Not able to get connection to system bus\n");
		return 1;
	}

	loop = g_main_loop_new(NULL, FALSE);
	g_dbus_connection_call(con,
				"org.bluez",
				"/",
				"org.freedesktop.DBus.ObjectManager",
				"GetManagedObjects",
				NULL,
				G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
				G_DBUS_CALL_FLAGS_NONE,
				-1,
				NULL,
				(GAsyncReadyCallback)bluez_list_controllers,
				loop);

	g_main_loop_run(loop);
	g_object_unref(con);
	return 0;
}