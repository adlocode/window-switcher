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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>
 */
 
#ifndef POSITION_GRID_H
#define POSITION_GRID_H

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>


G_BEGIN_DECLS

#define POSITION_GRID_TYPE (position_grid_get_type())
#define POSITION_GRID(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), POSITION_GRID_TYPE, PositionGrid))
#define POSITION_GRID_CLASS (klass) (G_TYPE_CHECK_CLASS_CAST ((klass), POSITION_GRID_TYPE, PositionGridClass))
#define IS_POSITION_GRID (obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), POSITION_GRID_TYPE))
#define IS_POSITION_GRID_CLASS(klass), (G_TYPE_CHECK_CLASS_TYPE ((klass), POSITION_GRID_TYPE))

typedef struct _PositionGrid PositionGrid;
typedef struct _PositionGridClass PositionGridClass;
typedef struct _PositionGridChild PositionGridChild;

struct _PositionGrid
{
	#if GTK_CHECK_VERSION (3, 0, 0)
	GtkGrid parent;
	#else
	GtkTable parent;
	#endif
	
	GList *children;
};



struct _PositionGridClass
{
	#if GTK_CHECK_VERSION (3, 0, 0)
	GtkGridClass parent_class;
	#else
	GtkTableClass parent_class;
	#endif
};

struct _PositionGridChild
{
	//GtkTableChild *child;
	
	GtkWidget *widget;
	
	guint position;
	
	gboolean reattach;
};

GType position_grid_get_type (void);
GtkWidget* position_grid_new (guint rows, guint columns, gboolean homogeneous);

void position_grid_attach (PositionGrid *position_grid, GtkWidget *widget, guint position);
void position_grid_resize (PositionGrid *position_grid, guint rows, guint columns);

G_END_DECLS

#endif /*__MY_TASKLIST_H__*/
