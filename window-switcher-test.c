#include <gtk/gtk.h>
#include <libwnck/libwnck.h>
#include "window-switcher.h"


static void 
button_enter_notify 
(GtkWidget *widget, GdkEvent *event, GtkWidget *pager)
{
	gtk_widget_show (pager);
	gtk_widget_hide (widget);
}

static void 
drag_begin_handl 
(GtkWidget *widget, GtkWidget *pager)
{
	gtk_widget_show (pager);
}

static void 
pager_leave_notify 
(GtkWidget *widget, GdkEvent *event, GtkWidget *button)
{
	gtk_widget_show (button);
	gtk_widget_hide (widget);
}

int main (int argc, char *argv[])
{
	GtkWidget *window;
	GtkWidget *tasklist;
	GtkWidget *pager;
	GtkWidget *hbox;
	GtkWidget *button;
	
	gtk_init (&argc, &argv);
	
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	g_signal_connect (window, "destroy", G_CALLBACK (gtk_main_quit), NULL);
	
	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
	tasklist = my_tasklist_new ();
	pager = wnck_pager_new (NULL);
	button= gtk_button_new ();
	
	
	g_signal_connect (button, "enter-notify-event", 
		G_CALLBACK (button_enter_notify), pager);
	
	g_signal_connect (pager, "leave-notify-event", 
		G_CALLBACK (pager_leave_notify), button);	
		
	
	gtk_widget_set_size_request (pager, 100, 100);
	gtk_widget_set_size_request (button, 20, 20);
	
	hbox = gtk_hbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER(window), hbox);
	gtk_box_pack_start (GTK_BOX (hbox), tasklist, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), pager, FALSE, FALSE, 0);
	
	g_signal_connect (tasklist, "task-button-drag-begin", 
		G_CALLBACK (drag_begin_handl), pager);
			
	gtk_widget_show_all (window);
	
	gtk_widget_hide (pager);
	
	gtk_main();
	
	return 0;
	
}
	
