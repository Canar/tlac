/*
 * hci_list_gdbus_proxy.c - List bluetooth controllers using GDBUS Proxy
 * 	- The example uses GDBUS proxy to get the list of bluetooth controllers using DBUS
 * 	  interfaces provided by bluez
 * 	- If any controller is found, it prints the Name and MAC address of the controller
 * gcc `pkg-config --cflags glib-2.0 gio-2.0` -o ./bin/hci_list_gdbus_proxy ./hci_list_gdbus_proxy.c `pkg-config --libs glib-2.0 gio-2.0`
 * dbus-send --system --print-reply --type=method_call --dest='org.bluez' '/' org.freedesktop.DBus.ObjectManager.GetManagedObjects
 */

#include <glib.h>
#include <gio/gio.h>

GDBusConnection *conn;
static const gchar* bluez_adapter_get_property(const gchar *path, const char *name)
{
	GVariant *prop;
	GDBusProxy *proxy;
	GError *error = NULL;
	GVariant *result;

	proxy = g_dbus_proxy_new_sync(conn,
				      G_DBUS_PROXY_FLAGS_NONE,
				      NULL,
				      "org.bluez",
				      path,
				      "org.freedesktop.DBus.Properties",
				      NULL,
				      &error);
	if(error != NULL)
		return NULL;

	error = NULL;
	result = g_dbus_proxy_call_sync(proxy,
					"Get",
					g_variant_new("(ss)", "org.bluez.Adapter1", name),
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

int main(void)
{
	int rc = 0;
	GDBusProxy *proxy;
	GError *error = NULL;
	GVariant *result;
	const gchar *object_path;
	GVariant *ifaces_and_properties;
	GVariantIter i;

	conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
	if(error != NULL)
		return 1;

	proxy = g_dbus_proxy_new_sync(conn,
				      G_DBUS_PROXY_FLAGS_NONE,
				      NULL,
				      "org.bluez",
				      "/",
				      "org.freedesktop.DBus.ObjectManager",
				      NULL,
				      &error);
	if(error != NULL)
		return 1;

	error = NULL;
	result = g_dbus_proxy_call_sync(proxy,
					"GetManagedObjects",
					NULL,
					G_DBUS_CALL_FLAGS_NONE,
					-1,
					NULL,
					&error);
	if(error != NULL)
		return 1;

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

fail:
	rc = 1;
done:
	if(proxy)
		g_object_unref(proxy);
	if(conn)
		g_object_unref(conn);
	if(error)
		g_error_free(error);

	return rc;
}