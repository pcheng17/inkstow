#include "DesktopWindow.h"

#include <gtk/gtk.h>

namespace inkstow::webview_shell
{
namespace
{
GtkWidget* menuItem(
    GtkWidget* menu,
    const char* label,
    const char* action,
    DesktopWindow* shell,
    GtkAccelGroup* accelerators,
    guint key)
{
    GtkWidget* item = gtk_menu_item_new_with_label(label);
    g_object_set_data_full(G_OBJECT(item), "inkstow-script", g_strdup(action), g_free);
    g_signal_connect(
        item,
        "activate",
        G_CALLBACK(+[](GtkWidget* widget, gpointer data) {
            static_cast<DesktopWindow*>(data)->invoke(
                static_cast<const char*>(g_object_get_data(G_OBJECT(widget), "inkstow-script")));
        }),
        shell);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    if (key)
        gtk_widget_add_accelerator(
            item,
            "activate",
            accelerators,
            key,
            GDK_CONTROL_MASK,
            GTK_ACCEL_VISIBLE);
    return item;
}
} // namespace

DesktopWindow::DesktopWindow(webview::webview& window)
    : mWindow(window)
{
    mHandle = window.window().value();
    auto* native = GTK_WIDGET(mHandle);
    g_signal_connect(
        native,
        "delete-event",
        G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
            static_cast<DesktopWindow*>(data)->invoke("inkstowAskQuit()");
            return TRUE;
        }),
        this);
    auto* child = gtk_bin_get_child(GTK_BIN(native));
    g_object_ref(child);
    gtk_container_remove(GTK_CONTAINER(native), child);
    auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(native), box);
    auto* bar = gtk_menu_bar_new();
    auto* file = gtk_menu_item_new_with_label("File");
    auto* fileMenu = gtk_menu_new();
    auto* note = gtk_menu_item_new_with_label("Note");
    auto* noteMenu = gtk_menu_new();
    auto* accelerators = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(native), accelerators);
    g_object_unref(accelerators);
    menuItem(fileMenu, "Open vault…", "inkstowOpenPicker()", this, accelerators, GDK_KEY_o);
    menuItem(fileMenu, "Save", "inkstowSave()", this, accelerators, GDK_KEY_s);
    menuItem(fileMenu, "Quit", "inkstowAskQuit()", this, accelerators, GDK_KEY_q);
    menuItem(noteMenu, "Undo", "inkstowUndo()", this, accelerators, GDK_KEY_z);
    auto* follow = menuItem(noteMenu, "Follow link", "inkstowFollow()", this, accelerators, 0);
    gtk_widget_add_accelerator(
        follow,
        "activate",
        accelerators,
        GDK_KEY_Return,
        GDK_MOD1_MASK,
        GTK_ACCEL_VISIBLE);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file), fileMenu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(note), noteMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), file);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), note);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), child, TRUE, TRUE, 0);
    g_object_unref(child);
    gtk_widget_show_all(bar);
    gtk_widget_show(box);
}

DesktopWindow::~DesktopWindow()
{
    if (mHandle) g_signal_handlers_disconnect_by_data(mHandle, this);
}

void DesktopWindow::invoke(const char* script)
{
    mWindow.eval(script);
}
} // namespace inkstow::webview_shell
