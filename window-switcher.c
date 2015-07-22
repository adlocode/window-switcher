/* Copyright (C) 2015 adlo
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */
 
 
#include <gtk/gtk.h>
#include <libwnck/libwnck.h>
#include <X11/Xlib.h>

#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>

#include <X11/Xutil.h>
#include <cairo/cairo.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf-xlib/gdk-pixbuf-xlib.h>
#include "window-switcher.h"

#define TABLE_COLUMNS 3

/* 
 * This tasklist emits signals when various events happen. This allows
 * the parent application to perform actions based on these events.
 */

static void my_tasklist_update_windows (MyTasklist *tasklist);
static void taskview_on_window_opened (WnckScreen *screen, WnckWindow *window, MyTasklist *tasklist);
static void my_tasklist_active_workspace_changed
	(WnckScreen *screen, WnckWorkspace *previously_active_workspace, MyTasklist *tasklist);
static void my_tasklist_on_name_changed (WnckWindow *window, GtkWidget *label);
static void my_tasklist_window_workspace_changed (WnckWindow *window, MyTasklist *tasklist);
static void my_tasklist_window_state_changed
	(WnckWindow *window, WnckWindowState changed_mask, WnckWindowState new_state, MyTasklist *tasklist);
static void my_tasklist_screen_composited_changed (GdkScreen *screen, MyTasklist *tasklist);
static void my_tasklist_button_clicked (GtkButton *button, WnckWindow *window);
static void my_tasklist_button_emit_click_signal (GtkButton *button, MyTasklist *tasklist);


#define LIGHT_TASK_TYPE (light_task_get_type())
#define LIGHT_TASK(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIGHT_TASK_TYPE, LightTask))
#define LIGHT_TASK_CLASS (klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LIGHT_TASK_TYPE, LightTaskClass))
#define IS_LIGHT_TASK (obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIGHT_TASK_TYPE))
#define IS_LIGHT_TASK_CLASS(klass), (G_TYPE_CHECK_CLASS_TYPE ((klass), LIGHT_TASK_TYPE))

typedef struct _LightTask LightTask;
typedef struct _LightTaskClass LightTaskClass;

struct _LightTask
{
	GObject parent_instance;
	
	MyTasklist *tasklist;
	
	GtkWidget *aspect_frame;
	GtkWidget *button;
	GtkWidget *icon;
	GtkWidget *label;
	GtkWidget *vbox;
	
	WnckWindow *window;
	Window xid;
	
	XRenderPictFormat *format;
	Picture origin, destination;
	GdkWindow *gdk_window;
	const GtkTargetEntry target;
	GdkPixbuf *pixbuf;
	
	Pixmap pixmap;
	GdkPixbuf *screenshot;
	GdkPixbuf *scaled_screenshot;
	
	guint name_changed_tag;
	guint icon_changed_tag;
	guint workspace_changed_tag;
	guint state_changed_tag;
	guint button_resized_tag;
	guint button_resized_check_tag;
	
	XWindowAttributes attr;
	gboolean redirected;
	
	
};

typedef struct _skipped_window
{
	WnckWindow *window;
	gulong tag;
}skipped_window;

struct _LightTaskClass
{
	GObjectClass parent_class;
};

G_DEFINE_TYPE (LightTask, light_task, G_TYPE_OBJECT)

static void light_task_finalize (GObject *object);

static void my_tasklist_drag_begin_handl
(GtkWidget *widget, GdkDragContext *context, LightTask *task);

static void light_task_create_widgets (LightTask *task);

static void my_tasklist_drag_data_get_handl
		(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection_data,
        guint target_type, guint time, LightTask *task);
        
void taskview_button_size_changed (GtkWidget *widget, GdkRectangle *allocation,
	LightTask *task);

static void light_task_class_init (LightTaskClass *klass)

{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	
	gobject_class->finalize = light_task_finalize;
}

static void light_task_init (LightTask *task)

{
	task->pixmap = None;
}

static void light_task_finalize (GObject *object)
{
	LightTask *task;
	task = LIGHT_TASK (object);
	
	if (task->button)
	{
		gtk_widget_destroy (task->button);
		task->button = NULL;
		task->icon = NULL;
		task->label = NULL;
	}
	
	if (task->icon_changed_tag != 0)
	{
		g_signal_handler_disconnect (task->window,
							task->icon_changed_tag);
		task->icon_changed_tag = 0;
	}
	
	if (task->name_changed_tag)
	{
		g_signal_handler_disconnect (task->window,
							task->name_changed_tag);
		task->name_changed_tag = 0;
	}
	
	if (task->workspace_changed_tag)
	{
		g_signal_handler_disconnect (task->window,
							task->workspace_changed_tag);
		task->workspace_changed_tag = 0;
	}
	
	if (task->state_changed_tag)
	{
		g_signal_handler_disconnect (task->window,
							task->state_changed_tag);
		task->state_changed_tag = 0;
	}
	
	
	if (task->window)
	{
		g_object_unref (task->window);
		task->window = NULL;
	}
	
	if (task->screenshot)
	{
		g_object_unref (task->screenshot);
		task->screenshot = NULL;
	}
	
	if (task->scaled_screenshot)
	{
		g_object_unref (task->scaled_screenshot);
		task->scaled_screenshot = NULL;
	}
	
	if (task->pixmap != None)
	{
		XFreePixmap (task->tasklist->dpy, task->pixmap);
		task->pixmap = None;
	}
	
	if (task->redirected && task->window != None)
	{
		XCompositeUnredirectWindow (task->tasklist->dpy, task->xid,
				CompositeRedirectAutomatic);
	}
	
}

static LightTask *
light_task_new_from_window (MyTasklist *tasklist, WnckWindow *window)
{
	LightTask *task;
	task = g_object_new (LIGHT_TASK_TYPE, NULL);
	
	task->tasklist = tasklist;
	
	task->window = g_object_ref (window);
	
	task->xid = wnck_window_get_xid (window);
	
	task->redirected = FALSE;
	
	light_task_create_widgets (task);
	
		
	return task;
	
}


static void my_tasklist_window_icon_changed (WnckWindow *window, LightTask *task)
{
	task->pixbuf = wnck_window_get_icon (task->window);
	gtk_image_set_from_pixbuf (GTK_IMAGE(task->icon), task->pixbuf);
}




enum {
	TASK_BUTTON_CLICKED_SIGNAL,
	TASK_BUTTON_DRAG_BEGIN_SIGNAL,
	TASK_BUTTON_DRAG_END_SIGNAL,
	LAST_SIGNAL
};

static void my_tasklist_class_init (MyTasklistClass *klass);
static void my_tasklist_init (MyTasklist *tasklist);

static guint task_button_clicked_signals[LAST_SIGNAL]={0};

static guint task_button_drag_begin_signals[LAST_SIGNAL]={0};

static guint task_button_drag_end_signals[LAST_SIGNAL]={0};

GType my_tasklist_get_type (void)
{
	static GType my_tasklist_type = 0;
	
	if (!my_tasklist_type)
	{
		const GTypeInfo my_tasklist_info =
		{
			sizeof(MyTasklistClass),
			NULL, /*base_init*/
			NULL, /*base_finalize*/
			(GClassInitFunc) my_tasklist_class_init,
			NULL, /*class_finalize*/
			NULL,
			sizeof(MyTasklist),
			0,
			(GInstanceInitFunc) my_tasklist_init,
		};
		my_tasklist_type = g_type_register_static (GTK_TYPE_EVENT_BOX, "MyTasklist", &my_tasklist_info, 0);
	}
	return my_tasklist_type;
}

static void my_tasklist_class_init (MyTasklistClass *klass)

{
	task_button_clicked_signals [TASK_BUTTON_CLICKED_SIGNAL] = 
		g_signal_new ("task-button-clicked",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION,
		0,
		NULL,
		NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
		
		task_button_drag_begin_signals [TASK_BUTTON_DRAG_BEGIN_SIGNAL] = 
		g_signal_new ("task-button-drag-begin",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION,
		0,
		NULL,
		NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
		
		task_button_drag_end_signals [TASK_BUTTON_DRAG_END_SIGNAL] = 
		g_signal_new ("task-button-drag-end",
		G_TYPE_FROM_CLASS(klass),
		G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION,
		0,
		NULL,
		NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
	
}

static void my_tasklist_init (MyTasklist *tasklist)
{
	tasklist->tasks = NULL;
	tasklist->skipped_windows = NULL;
	
	tasklist->previews = g_hash_table_new (NULL, NULL);
	
	tasklist->table = gtk_table_new (3, TABLE_COLUMNS, TRUE);
	gtk_container_add (GTK_CONTAINER(tasklist), tasklist->table);
	
	
	gtk_widget_show (tasklist->table);
	tasklist->screen = wnck_screen_get_default();
	tasklist->gdk_screen = gdk_screen_get_default();
	
	tasklist->dpy = gdk_x11_get_default_xdisplay ();
	
	tasklist->composited = gdk_screen_is_composited (tasklist->gdk_screen);
	
	wnck_screen_force_update (tasklist->screen);
	
	
	my_tasklist_update_windows (tasklist);
	
	g_signal_connect (tasklist->screen, "window-opened",
                G_CALLBACK (taskview_on_window_opened), tasklist); 
                
    g_signal_connect (tasklist->screen, "window-closed",
                G_CALLBACK (taskview_on_window_opened), tasklist); 
                
   g_signal_connect (tasklist->screen, "active-workspace-changed",
               G_CALLBACK (my_tasklist_active_workspace_changed), tasklist);
               
   g_signal_connect (tasklist->gdk_screen, "composited-changed",
               G_CALLBACK (my_tasklist_screen_composited_changed), tasklist);            

}

GtkWidget* my_tasklist_new (void)
{
	return GTK_WIDGET(g_object_new (my_tasklist_get_type (), NULL));
}

static void
my_tasklist_free_tasks (MyTasklist *tasklist)
{
	GList *l;
	if (tasklist->tasks)
	{
		l = tasklist->tasks;
		
		while (l != NULL)
		{
			
			LightTask *task = LIGHT_TASK (l->data);
				
			
			l = l->next;
			
			
			
			if (task->button_resized_tag)
			{
				g_signal_handler_disconnect (task->button,
							task->button_resized_tag);
							
				task->button_resized_tag = 0;
			}
			
			if (task->button_resized_check_tag)
			{
				g_signal_handler_disconnect (task->button,
							task->button_resized_check_tag);
							
				task->button_resized_check_tag = 0;
			}
			
			if (task->button)
			{	
				gtk_widget_destroy (task->button);
				task->button = 0;
			}
			
			g_object_unref (task);
			
			
		}
	}
	tasklist->tasks = NULL;
	g_assert (tasklist->tasks == NULL);
	
	//Free skipped windows
	
	if (tasklist->skipped_windows)
	
	{
		GList *skipped_l;
		skipped_l = tasklist->skipped_windows;
	
		while (skipped_l != NULL)
		{
			skipped_window *skipped = (skipped_window *) skipped_l->data;
			g_signal_handler_disconnect (skipped->window, skipped->tag);
			g_object_unref (skipped->window);
			g_free (skipped);
			skipped_l = skipped_l->next;
		}
	
		g_list_free (tasklist->skipped_windows);
		tasklist->skipped_windows = NULL;
		
	}
	
}

static void my_tasklist_update_windows (MyTasklist *tasklist)
{
	my_tasklist_free_tasks (tasklist);
	gtk_table_resize (GTK_TABLE(tasklist->table), 3, TABLE_COLUMNS);
	
	GList *window_l;
	WnckWindow *win;
	

	
	
	//Table attachment values
	
	guint left_attach =0;	
	guint right_attach=1;		
	guint top_attach=0;		
	guint bottom_attach=1;
	
	
	for (window_l = wnck_screen_get_windows (tasklist->screen); window_l != NULL; window_l = window_l->next)
    {
		win = WNCK_WINDOW (window_l->data);
		
		if (!(wnck_window_is_skip_tasklist (win)))
		{
			LightTask *task = light_task_new_from_window (tasklist, win);
			tasklist->tasks = g_list_prepend (tasklist->tasks, task);
			
			
			if(wnck_window_is_on_workspace(task->window, wnck_screen_get_active_workspace(tasklist->screen)))
			{
				
				
				gtk_table_attach_defaults (GTK_TABLE(tasklist->table), task->button, left_attach, 
					right_attach, top_attach, bottom_attach);
					
				gtk_widget_show_all (task->button);
					
					
				if (right_attach % TABLE_COLUMNS == 0)
				{
					top_attach++;
					bottom_attach++;
					left_attach=0;
					right_attach=1;
				}
				
				else
				{
					left_attach++;
					right_attach++;
				}
				
			}
			
		}
		
		else
		{
			skipped_window *skipped = g_new0 (skipped_window, 1);
			skipped->window = g_object_ref (win);
			skipped->tag = g_signal_connect (G_OBJECT (win), "state-changed",
							G_CALLBACK (my_tasklist_window_state_changed),
							tasklist);
							
			tasklist->skipped_windows =
				g_list_prepend (tasklist->skipped_windows,
					(gpointer) skipped);
		}
	}
	
	
}

static void my_tasklist_on_name_changed (WnckWindow *window, GtkWidget *label) 
{
	gtk_label_set_text (GTK_LABEL(label), wnck_window_get_name (window));
}

static void taskview_on_window_opened (WnckScreen *screen, WnckWindow *window, MyTasklist *tasklist)
{
	my_tasklist_update_windows (tasklist);
}

static void my_tasklist_active_workspace_changed
	(WnckScreen *screen, WnckWorkspace *previously_active_workspace, MyTasklist *tasklist)
{
	my_tasklist_update_windows (tasklist);
}

static void my_tasklist_window_workspace_changed (WnckWindow *window, MyTasklist *tasklist)
{
	my_tasklist_update_windows (tasklist);
}

static void my_tasklist_screen_composited_changed (GdkScreen *screen, MyTasklist *tasklist)
{
	
	tasklist->composited = gdk_screen_is_composited (tasklist->gdk_screen);

}

static void my_tasklist_window_state_changed
	(WnckWindow *window, WnckWindowState changed_mask, WnckWindowState new_state, MyTasklist *tasklist)
{
	if (changed_mask & WNCK_WINDOW_STATE_SKIP_TASKLIST)
	{
		my_tasklist_update_windows (tasklist);
	}
	else if (changed_mask & WNCK_WINDOW_STATE_MINIMIZED)
	{
		my_tasklist_update_windows (tasklist);
	}
	
}


static void my_tasklist_button_clicked (GtkButton *button, WnckWindow *window)
	
{
	wnck_window_activate (window, gtk_get_current_event_time());
	
}

static void my_tasklist_button_emit_click_signal (GtkButton *button, MyTasklist *tasklist)
{
	g_signal_emit_by_name (tasklist, "task-button-clicked");
	
}

/*
void taskview_button_check_allocate_signal (GtkWidget *widget, GdkRectangle *allocation,
	LightTask *task)
{
	if (!g_signal_handler_is_connected (task->button, task->button_resized_tag))
		
	{
		task->button_resized_tag = g_signal_connect (task->button, "size-allocate",
					G_CALLBACK (taskview_button_size_changed), task);
	}
}

void taskview_button_size_changed (GtkWidget *widget, GdkRectangle *allocation,
	LightTask *task)
{
	if (task->screenshot)
	{
		if (g_signal_handler_is_connected (task->button, task->button_resized_tag))
		
	{
		g_signal_handler_disconnect (task->button, task->button_resized_tag);
		if (task->scaled_screenshot)
		g_object_unref (task->scaled_screenshot);
		
		
		gfloat xp, yp, widthp, heightp;
		gint depth;
	
		gdk_window_get_geometry (task->gdk_window, &xp, &yp, &widthp, &heightp, &depth);
		
		gfloat aspect_ratio = widthp/heightp;
		
	
		
		task->scaled_screenshot = gdk_pixbuf_scale_simple (task->screenshot,
		task->button->allocation.height*aspect_ratio, task->button->allocation.height, GDK_INTERP_BILINEAR);
		
		gtk_image_set_from_pixbuf (task->icon, task->scaled_screenshot);
	}
	
	}
}*/

static void my_tasklist_drag_begin_handl
(GtkWidget *widget, GdkDragContext *context, LightTask *task)
{
	g_signal_emit_by_name (task->tasklist, "task-button-drag-begin");
}

static void my_tasklist_drag_end_handl
(GtkWidget *widget, GdkDragContext *context, LightTask *task)
{
	g_signal_emit_by_name (task->tasklist, "task-button-drag-end");
}

static void
my_tasklist_drag_data_get_handl
(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection_data,
        guint target_type, guint time, LightTask *task)
{
	g_assert (selection_data != NULL);
	
	gulong xid;
	
	xid = wnck_window_get_xid (task->window);
	 
	
	gtk_selection_data_set
                (
                        selection_data,         /* Allocated GdkSelectionData object */
                        gtk_selection_data_get_target(selection_data), /* target type */
                        8,                 /* number of bits per 'unit' */
                        (guchar*)&xid,/* pointer to data to be sent */
                        sizeof (gulong)   /* length of data in units */
                );
                
               
     
        
}

static Picture
lightdash_window_switcher_get_window_picture (LightTask *task)
{
	XRenderPictFormat *format;
	
	
			XCompositeRedirectWindow (task->tasklist->dpy, task->xid,
				CompositeRedirectAutomatic);
				
			task->redirected = TRUE;
				
			
			
			task->pixmap = XCompositeNameWindowPixmap (task->tasklist->dpy, task->xid);
			
			Drawable draw = task->pixmap;
			
			if (!draw) draw = task->xid;
			
			static XRenderPictureAttributes pa = {.subwindow_mode = IncludeInferiors};
			
			task->origin = None;
			
			format = XRenderFindVisualFormat (task->tasklist->dpy, task->attr.visual);
			
			if (format)
			{
				return XRenderCreatePicture (task->tasklist->dpy,
					draw, format, CPSubwindowMode, &pa);
			}
			
		return None;
}

static void light_task_create_widgets (LightTask *task)
{
	task->label = gtk_label_new_with_mnemonic(wnck_window_get_name (task->window));
	task->vbox = gtk_vbox_new (FALSE, 0);
	task->pixbuf = wnck_window_get_icon (task->window);
	
	
	XGetWindowAttributes (task->tasklist->dpy, task->xid, &task->attr);
	
	int xp, yp, widthp, heightp;
		
	
	
	task->button = gtk_button_new();
	gtk_label_set_ellipsize(GTK_LABEL(task->label),PANGO_ELLIPSIZE_END);
	gtk_container_add (GTK_CONTAINER(task->button),task->vbox);
	gtk_widget_set_size_request (task->button, 200, 80);
	gtk_button_set_relief (task->button, GTK_RELIEF_NONE);
	
	if (task->tasklist->composited)
	{
		
		if (!wnck_window_is_minimized (task->window) 
			&& wnck_window_is_on_workspace (task->window, 
				wnck_screen_get_active_workspace(task->tasklist->screen)))
		{
			
		
			gdk_drawable_get_size (task->gdk_window, &widthp, &heightp);
		
			task->origin = lightdash_window_switcher_get_window_picture (task);
			
			

			
			GdkPixmap *pixmap;
			pixmap = gdk_pixmap_foreign_new_for_screen (task->tasklist->gdk_screen, task->pixmap,
				task->attr.width, task->attr.height, task->attr.depth);
			
		
			g_hash_table_insert (task->tasklist->previews, task->window, task->screenshot);
	
		
			task->icon = gtk_image_new_from_pixmap (pixmap, NULL);
			//task->icon = gtk_image_new_from_pixbuf (pixbuf);
	
		}
	
		else
		{
			task->icon = gtk_image_new_from_pixbuf (task->pixbuf);
		}
	
		}
	
	else
	{
		task->icon = gtk_image_new_from_pixbuf (task->pixbuf);
	}
	
	
	gtk_box_pack_start (GTK_BOX (task->vbox), task->icon, TRUE, TRUE, 0);
	
	gtk_box_pack_start (GTK_BOX (task->vbox), task->label, FALSE, TRUE, 1);
	
	gtk_widget_set_size_request (task->icon, 100, 100);
	
	task->icon_changed_tag = g_signal_connect (task->window, "icon-changed",
					G_CALLBACK (my_tasklist_window_icon_changed), task);
					
	task->name_changed_tag = g_signal_connect (task->window, "name-changed",
					G_CALLBACK (my_tasklist_on_name_changed), task->label);
					
	task->workspace_changed_tag = g_signal_connect (task->window, "workspace-changed",
					G_CALLBACK (my_tasklist_window_workspace_changed), task->tasklist);
					
	task->state_changed_tag = g_signal_connect (task->window, "state-changed",
					G_CALLBACK (my_tasklist_window_state_changed), task->tasklist);				
	
	if (!wnck_window_is_minimized (task->window))
	{
		
		//task->button_resized_check_tag = g_signal_connect (task->button, "size-allocate",
					//G_CALLBACK (taskview_button_check_allocate_signal), task);
	
									
	//task->button_resized_tag = g_signal_connect (task->button, "size-allocate",
					//G_CALLBACK (taskview_button_size_changed), task);
					
	}
								
	static const GtkTargetEntry targets [] = { {"application/x-wnck-window-id",0,0} };
					
	gtk_drag_source_set (task->button,GDK_BUTTON1_MASK,targets,1,GDK_ACTION_MOVE);
					
	g_signal_connect_object (task->button, "drag-data-get",
					G_CALLBACK (my_tasklist_drag_data_get_handl), G_OBJECT(task),
					0);
					
	g_signal_connect_object (task->button, "drag-begin",
					G_CALLBACK (my_tasklist_drag_begin_handl), G_OBJECT (task),
					0);
	
	g_signal_connect_object (task->button, "drag-end",
					G_CALLBACK (my_tasklist_drag_end_handl), G_OBJECT (task),
					0);				
					
					
	g_signal_connect_object (task->button, "clicked", 
					G_CALLBACK (my_tasklist_button_clicked), task->window,
					0);
	
	g_signal_connect_object (GTK_BUTTON(task->button), "clicked", 
					G_CALLBACK (my_tasklist_button_emit_click_signal), task->tasklist,
					0);
}
