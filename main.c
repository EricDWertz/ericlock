#include <stdlib.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtkgl.h>
#include <gio/gio.h>
#include <X11/Xlib.h>

#include <gdk/gdkkeysyms.h>

#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glu.h>

#include <stdio.h>
#include <string.h>

#include <time.h>

typedef struct 
{
    GtkWidget* window;
    GtkWidget* drawing_area;
    gboolean isConfigured;
    int width;
    int height;
    int x;
    int y;
    
    //Framebuffers
    GLuint fb1;
    GLuint fb2;
    GLuint fb1Tex;
    GLuint fb2Tex;
    GLuint fbcurrent;

    //Other gl things
    GLuint shaderProgram;
    GLuint tex1Map;
    GLuint tex2Map;
    GLuint transitionAlpha;
    GLuint blurRadius;
    GLuint blurResolution;
    GLuint blurDirection;

    GLuint texture;

    float tx;
    float ty;
    float tw;
    float th;
} lock_window;

GdkGLConfig *gl_config;

int mons;
int screenGrabbed = FALSE;
lock_window lock_windows[8]; //If you have more than 8 displays I'm sorry.


float transition_alpha = 0.0f;
float transition_direction = 1.0f;

GSettings* gsettings;

long old_ns;
#define FADEIN_TIME 5.0f
#define FADEOUT_TIME 0.25f

const GLchar* vertSource = "varying vec2 vTexCoord;"
    "varying vec4 vColor;"
    "void main(void) {"
    "	gl_Position = gl_Vertex;"
    "	vTexCoord = gl_MultiTexCoord0.xy;"
    "   vColor = vec4( 1.0, 1.0, 1.0, 1.0 );"
    "}";

//Forgive me for my sins
const GLchar* fragSource = "varying vec4 vColor;"
    "varying vec2 vTexCoord;"
     //declare uniforms"
    "uniform sampler2D tex1Map;"
    "uniform float resolution;"
    "uniform float radius;"
    "uniform float transition;"
    "uniform vec2 dir;"
    "void main() {"
         //this will be our RGBA sum
    "    vec4 sum = vec4(0.0);"
         //our original texcoord for this fragment
    "    vec2 tc = vTexCoord;"
         //the amount to blur, i.e. how far off center to sample from 
         //1.0 -> blur by one pixel
         //2.0 -> blur by two pixels, etc.
    "    float blur = (radius/7.0)*(1.0/resolution);"
         //the direction of our blur
         //(1.0, 0.0) -> x-axis blur
         //(0.0, 1.0) -> y-axis blur
    "    float hstep = dir.x;"
    "    float vstep = dir.y;"
         //apply blurring, using a 9-tap filter with predefined gaussian weights
    "    sum += texture2D(tex1Map, vec2(tc.x - 4.0*blur*hstep, tc.y - 4.0*blur*vstep)) * 0.0162162162;"
    "    sum += texture2D(tex1Map, vec2(tc.x - 3.0*blur*hstep, tc.y - 3.0*blur*vstep)) * 0.0540540541;"
    "    sum += texture2D(tex1Map, vec2(tc.x - 2.0*blur*hstep, tc.y - 2.0*blur*vstep)) * 0.1216216216;"
    "    sum += texture2D(tex1Map, vec2(tc.x - 1.0*blur*hstep, tc.y - 1.0*blur*vstep)) * 0.1945945946;"
    "    sum += texture2D(tex1Map, vec2(tc.x, tc.y)) * 0.2270270270;"
    "    sum += texture2D(tex1Map, vec2(tc.x + 1.0*blur*hstep, tc.y + 1.0*blur*vstep)) * 0.1945945946;"
    "    sum += texture2D(tex1Map, vec2(tc.x + 2.0*blur*hstep, tc.y + 2.0*blur*vstep)) * 0.1216216216;"
    "    sum += texture2D(tex1Map, vec2(tc.x + 3.0*blur*hstep, tc.y + 3.0*blur*vstep)) * 0.0540540541;"
    "    sum += texture2D(tex1Map, vec2(tc.x + 4.0*blur*hstep, tc.y + 4.0*blur*vstep)) * 0.0162162162;"
        //discard alpha for our simple demo, multiply by vertex color and return
    "   gl_FragColor = vec4(sum.rgb, 1.0) * (1.0 - (transition * 0.25));"
    "   gl_FragColor.a = 1.0;"
    "}";

void render_gl( lock_window* w );
void grab_keys();

void load_wallpaper_shaders( lock_window* w )
{
    GLuint vertShader, fragShader;
    char buffer[2048];

    vertShader = glCreateShader( GL_VERTEX_SHADER );
    fragShader = glCreateShader( GL_FRAGMENT_SHADER );
    printf( "Created Shaders\n" );

    glShaderSource( vertShader, 1, &vertSource, NULL );
    glShaderSource( fragShader, 1, &fragSource, NULL );

    glCompileShader( vertShader );
    glCompileShader( fragShader );

    glGetShaderInfoLog( vertShader, 2048, NULL, buffer );
    printf( buffer );
    glGetShaderInfoLog( fragShader, 2048, NULL, buffer );
    printf( buffer );

    w->shaderProgram = glCreateProgram();

    glAttachShader( w->shaderProgram, vertShader );
    glAttachShader( w->shaderProgram, fragShader );

    glLinkProgram( w->shaderProgram );
    glGetProgramInfoLog( w->shaderProgram, 2048, NULL, buffer );
    printf( buffer );

    w->tex1Map = glGetUniformLocation( w->shaderProgram, "tex1Map" );
    w->tex2Map = glGetUniformLocation( w->shaderProgram, "tex2Map" );
    w->transitionAlpha = glGetUniformLocation( w->shaderProgram, "transitionAlpha" );
    w->blurRadius = glGetUniformLocation( w->shaderProgram, "radius" );
    w->blurResolution = glGetUniformLocation( w->shaderProgram, "resolution" );
    w->blurDirection = glGetUniformLocation( w->shaderProgram, "dir" );
    w->transitionAlpha = glGetUniformLocation( w->shaderProgram, "transition" );
}

void load_wallpaper_pixels(GdkPixbuf* pixbuf)
{
	int width=gdk_pixbuf_get_width(pixbuf);
	int height=gdk_pixbuf_get_height(pixbuf);
	int rowstride=gdk_pixbuf_get_rowstride(pixbuf);
	int nchannels=gdk_pixbuf_get_n_channels(pixbuf);
	
	//ASSERT N CHANNELS IS 4 HERE

	printf("w: %i h: %i\n  stride: %i channels: %i\n",
		width,
		height,
		rowstride,
		nchannels);

	guchar* pixels=gdk_pixbuf_get_pixels(pixbuf);

	int pixelcount=width*height;
	int i, x;
	int p=0;
	for(i=0;i<height;i++)
	{	
		if(nchannels==3)
		{
			memcpy(pixels+i*width*3, pixels+i*rowstride, width*3);
		}
		p+=rowstride;
	}

	int ar, ag, ab;
	ar = 0; ag = 0; ab = 0;
	for( i = 0; i < pixelcount*3; i+=3 )
	{
		ar += pixels[i]; 
		ag += pixels[i + 1]; 
		ab += pixels[i + 2];
	}

	glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,pixels);
}

gboolean animation_timer( gpointer user )
{
    int i;
    for( i = 0; i < mons; i++ )
    {
        render_gl( &lock_windows[i]  );
    }

    struct timespec tp;
    clock_gettime( CLOCK_MONOTONIC, &tp );
    int nanodiff = tp.tv_nsec - old_ns;

    float dt = (float)nanodiff/1.0e9f;
    if( dt < 0.0f ) dt += 1.0f;

    old_ns = tp.tv_nsec;

    if( transition_direction > 0.0f )
    {
        transition_alpha += dt / FADEIN_TIME;  
        return ( transition_alpha < 1.0 );
    }
    else
    {
        transition_alpha -= dt / FADEOUT_TIME;  

        for( i = 0; i < mons; i++ )
            gtk_window_set_opacity( GTK_WINDOW( lock_windows[i].window ), transition_alpha );

        if( transition_alpha < 0.0f )
            gtk_main_quit();
    }

}

void load_background_texture(const char* path, lock_window* w)
{
	//Get raw data from gdkPixbuf!
	GError* error=NULL;
	GdkPixbuf* pixbuf=gdk_pixbuf_new_from_file(path,&error);
	if(!pixbuf) 
	{
		printf("Error loading background %s\n%s\n",error->message);
		g_error_free(error);
		return;
	}

    GdkPixbuf* cropped = gdk_pixbuf_new_subpixbuf( pixbuf, w->x, w->y, w->width, w->height );
	
	glGenTextures(1,&w->texture);
	
	glBindTexture(GL_TEXTURE_2D,w->texture);
	
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border_color[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    glTexParameterfv( GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_color );
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	
	load_wallpaper_pixels(cropped);

	//free GdkPixbuf
	g_object_unref(cropped);
	g_object_unref(pixbuf);

	transition_alpha = 0.0;
} 

void init_texture( GLuint* tex, int w, int h )
{
    glGenTextures( 1, tex );
    glBindTexture( GL_TEXTURE_2D, *tex );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0 );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
}

void configure_event( GtkWidget* widget, GdkEventConfigure *event, gpointer user )
{
    lock_window* w = (lock_window*)user;
    if( w->isConfigured == TRUE )
        return;

	GdkGLContext* gl_context = gtk_widget_get_gl_context( widget );
	GdkGLDrawable *gl_drawable = gtk_widget_get_gl_drawable( widget );

    if( !gdk_gl_drawable_make_current( gl_drawable, gl_context ) )
		g_assert_not_reached();

	if( !gdk_gl_drawable_gl_begin( gl_drawable, gl_context ) )
		g_assert_not_reached();

    GLenum err = glewInit();
    if( GLEW_OK != err )
        fprintf( stderr, "Error: %s\n", glewGetErrorString( err ) );
    else
        printf("glew ok!\n");

    load_wallpaper_shaders( w );

    //Load framebuffer stuff
    glGenFramebuffers( 1, &w->fb1 );
    glGenFramebuffers( 1, &w->fb2 );

    printf( "init textures\n" );
	glEnable( GL_TEXTURE_2D );
    init_texture( &w->fb1Tex, w->width, w->height );
    init_texture( &w->fb2Tex, w->width, w->height );

    printf( "bind framebuffer textures\n" );
    glBindFramebuffer( GL_FRAMEBUFFER, w->fb1 );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, w->fb1Tex, 0 );
    glBindFramebuffer( GL_FRAMEBUFFER, w->fb2 );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, w->fb2Tex, 0 );

    glBindFramebuffer( GL_FRAMEBUFFER, 0 );


	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	//GConfClient* client = gconf_client_get_default();
    if( !screenGrabbed )
    {
        system( "scrot /tmp/ericlock.png" );
        screenGrabbed = TRUE;
    }

    load_background_texture( "/tmp/ericlock.png", w );

	gdk_gl_drawable_gl_end( gl_drawable );

    w->isConfigured = TRUE;
};

void swap_buffers( lock_window* w )
{
    if( w->fbcurrent == w->fb1 )
    {
        glBindFramebuffer( GL_FRAMEBUFFER, w->fb2 ); 
        w->fbcurrent = w->fb2;

        glActiveTexture( GL_TEXTURE0 );
        glBindTexture( GL_TEXTURE_2D, w->fb1Tex );
        glUniform1i( w->tex1Map, 0 );
    }
    else
    {
        glBindFramebuffer( GL_FRAMEBUFFER, w->fb1 ); 
        w->fbcurrent = w->fb1;

        glActiveTexture( GL_TEXTURE0 );
        glBindTexture( GL_TEXTURE_2D, w->fb2Tex );
        glUniform1i( w->tex1Map, 0 );
    }
}

void render_quad( lock_window* w )
{
    glBegin(GL_QUADS);
    glColor3f(1.0,1.0,1.0);
    
    glTexCoord2f( 0.0, 0.0 );
    glVertex2f(-1,1);
    
    glTexCoord2f( 1.0, 0.0 );
    glVertex2f(1,1);
    
    glTexCoord2f( 1.0, 1.0 );
    glVertex2f(1,-1);
    
    glTexCoord2f( 0.0, 1.0 );
    glVertex2f(-1,-1);        
    glEnd();                 
}

void render_gl( lock_window* w )
{
	GdkGLContext* gl_context = gtk_widget_get_gl_context( w->drawing_area );
	GdkGLDrawable *gl_drawable = gtk_widget_get_gl_drawable( w->drawing_area );

    if( !gdk_gl_drawable_make_current( gl_drawable, gl_context ) )
		g_assert_not_reached();

	if( !gdk_gl_drawable_gl_begin( gl_drawable, gl_context ) )
		g_assert_not_reached();

	glClearColor( 0.0, 0.5, 0.0, 1.0 );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glUseProgram( w->shaderProgram );
	glEnable(GL_TEXTURE_2D);

    //glViewport( 0, 0, w->width, w->height );

    w->fbcurrent = w->fb1;

    glUniform1f( w->blurRadius, 150.0 * transition_alpha );
    glUniform1f( w->transitionAlpha, 0.0 );


    int i;
    int blur_passes = (int)( (150.0 * transition_alpha)/28.0 );
    blur_passes += 1;

    swap_buffers( w );

    //Bind Textures to shader
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, w->texture );
    glUniform1i( w->tex1Map, 0 );

    for( i = 0; i <= blur_passes; i++ )
    {
        glUniform2f( w->blurDirection, 1.0/(double)(i+1), 0.0 ); //horizontal
        glUniform1f( w->blurResolution, (float)w->width );
        render_quad( w );


        //Vertical pass
        swap_buffers( w );

        glUniform1f( w->blurResolution, (float)w->height );
        glUniform2f( w->blurDirection, 0.0, 1.0/(double)(i+1) );
        render_quad( w );

        swap_buffers( w );
    }

    //Final pass
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    glUniform1f( w->blurRadius, 0.0 );
    glUniform1f( w->transitionAlpha, transition_alpha );
    glLoadIdentity();                                  
    render_quad( w );


	if( gdk_gl_drawable_is_double_buffered( gl_drawable ) )
		gdk_gl_drawable_swap_buffers( gl_drawable );
	
    glFlush();

	gdk_gl_drawable_gl_end( gl_drawable );
}

void expose_event( GtkWidget* widget, GdkEventExpose *event, gpointer user )
{
	render_gl( (lock_window*)user );
}	

void init_gl( int argc, char* argv[] )
{
	gtk_gl_init( &argc, &argv );
	
	gl_config = gdk_gl_config_new_by_mode( (GdkGLConfigMode)(GDK_GL_MODE_RGBA,
        GDK_GL_MODE_DEPTH |
        GDK_GL_MODE_DOUBLE) );

	if( !gl_config )
		g_assert_not_reached();
};

void gsettings_value_changed( GSettings *settings, const gchar *key, gpointer user )
{
    if( strcmp( key, "picture-uri" ) == 0 )
    {
        system( "scrot /tmp/ericlock.png" );
        //load_background_texture( "/tmp/ericlock.png" );
    }
}

gboolean button_press_event( GtkWidget* widget, GdkEventButton* event, gpointer user )
{
	if( event->button == 3 )
	{
		char cmd[128];
		sprintf( cmd, "ericlaunch -w -p %i %i -d 480 360 -s 64", (int)event->x, (int)event->y );
		system( cmd );
	}
}

gboolean key_press_event( GtkWidget* widget, GdkEventKey* event, gpointer user )
{
    printf( "key press event\n" );
    if( event->keyval == GDK_KEY_Escape )
    {
        transition_direction = -1.0f;

        struct timespec tp;
        clock_gettime( CLOCK_MONOTONIC, &tp );
        old_ns = tp.tv_nsec;
        g_timeout_add( 16, animation_timer, NULL );
    }
}

gboolean screen_changed_event( GtkWidget* widget, GdkScreen *old_screen, gpointer user )
{
	GdkScreen* screen = gdk_screen_get_default();
	int screen_width = gdk_screen_get_width( screen );
	int screen_height = gdk_screen_get_height( screen );

	gtk_window_move( GTK_WINDOW( widget ), 0, 0 );
	gtk_window_resize( GTK_WINDOW( widget ), screen_width, screen_height );
}

GdkFilterReturn handle_x11_event( GdkXEvent *xevent, GdkEvent *event, gpointer data )
{
    XEvent* xev = (XEvent*)xevent;

    Display* dpy = GDK_DISPLAY_XDISPLAY( gdk_display_get_default() );

    if( xev->type == KeyPress )
    {
        printf( "Got a key press event!\n" );
        if( xev->xkey.keycode == 9 )
        {
            //gtk_main_quit();
        }
    }
    if( xev->type == KeyRelease )
    {
    }

    return GDK_FILTER_CONTINUE;
}

void grab_keys()
{
    int i;
    Display* dpy = GDK_DISPLAY_XDISPLAY( gdk_display_get_default() );
    Window xwin = GDK_WINDOW_XID( gtk_widget_get_window( lock_windows[0].window ) );

    //Grab number keys 
    //for( i = 10; i <= 20; i++ )
    //{
    //    XGrabKey( dpy, i, Mod4Mask | Mod2Mask, xwin, True, GrabModeAsync, GrabModeAsync );
    //    XGrabKey( dpy, i, Mod4Mask, xwin, True, GrabModeAsync, GrabModeAsync );
    //}
    //XGrabKey( dpy, AnyKey, AnyModifier, xwin, True, GrabModeSync, GrabModeSync );
    //XGrabKey( dpy, AnyKey, AnyModifier, xwin, True, GrabModeSync, GrabModeSync );
    XGrabKeyboard( dpy, xwin, False, GrabModeAsync, GrabModeAsync, CurrentTime );

    gdk_window_add_filter( NULL, handle_x11_event, NULL );
}

int create_lock_window( lock_window* w, GdkRectangle* rect )
{
	w->window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
 	gtk_window_set_type_hint( GTK_WINDOW( w->window ), GDK_WINDOW_TYPE_HINT_NORMAL );
	gtk_widget_set_size_request( w->window, rect->width, rect->height );

    w->width = rect->width; w->height = rect->height;
    w->x = rect->x; w->y = rect->y;

	gtk_window_move( GTK_WINDOW( w->window ), rect->x, rect->y );
	gtk_window_fullscreen( GTK_WINDOW( w->window ) ); 
	gtk_window_set_decorated( GTK_WINDOW( w->window ), FALSE ); 
	//gtk_window_set_keep_above( GTK_WINDOW( w->window ), TRUE ); 
    gtk_widget_set_can_focus( w->window, TRUE );
	gtk_widget_add_events( w->window, GDK_BUTTON_PRESS_MASK | GDK_KEY_PRESS_MASK );
	g_signal_connect( G_OBJECT(w->window), "button-press-event", G_CALLBACK(button_press_event), NULL );
	g_signal_connect( G_OBJECT(w->window), "key-press-event", G_CALLBACK(key_press_event), NULL );
	g_signal_connect( G_OBJECT(w->window), "screen-changed", G_CALLBACK(screen_changed_event), NULL );

    //Update the texture coords
	GdkScreen* screen = gdk_screen_get_default();
	float screen_width = gdk_screen_get_width( screen );
	float screen_height = gdk_screen_get_height( screen );
    w->tx = (float)rect->x / screen_width;
    w->ty = (float)rect->y / screen_height;
    w->tw = (float)rect->width / screen_width;
    w->th = (float)rect->height / screen_height;
    printf( "Window tx: %f, ty: %f tw: %f th:%f\n", w->tx, w->ty, w->tw, w->th );
    printf( "Rect x: %i, y: %i w: %i h:%i\n", rect->x, rect->y, rect->width, rect->height );

	w->drawing_area = gtk_drawing_area_new();
	
	gtk_container_add( GTK_CONTAINER( w->window ), w->drawing_area );
    
    //Opengl stuff
	if( !gtk_widget_set_gl_capability( w->drawing_area, gl_config, NULL, TRUE, GDK_GL_RGBA_TYPE) )
		g_assert_not_reached();

	g_signal_connect( w->drawing_area, "expose-event", G_CALLBACK( expose_event ), (gpointer)w );
    w->isConfigured = FALSE;
	g_signal_connect( w->drawing_area, "configure-event", G_CALLBACK( configure_event ), (gpointer)w );
	
}

int main( int argc, char* argv[] )
{
	gtk_init( &argc, &argv );

	init_gl( argc, argv );

    GdkRectangle mon_geom;
    int i;
	GdkScreen* screen = gdk_screen_get_default();
    mons = gdk_screen_get_n_monitors( screen );
    for( i = 0; i< mons; i++ )
    {
        gdk_screen_get_monitor_geometry( screen, i, &mon_geom );
        create_lock_window( &lock_windows[i], &mon_geom );
    }

    GSettingsSchema* gsettings_schema;

    gsettings_schema = g_settings_schema_source_lookup( g_settings_schema_source_get_default(),
                                                "org.gnome.desktop.background",
                                                TRUE );
    if( gsettings_schema )
    {
        g_settings_schema_unref (gsettings_schema);
        gsettings_schema = NULL;
        gsettings = g_settings_new ( "org.gnome.desktop.background" );
    }

    //g_signal_connect_data( gsettings, "changed", G_CALLBACK( gsettings_value_changed ), NULL, 0, 0 );

	//GConfClient* client = gconf_client_get_default();
	//gconf_client_add_dir( client, "/desktop/eric", GCONF_CLIENT_PRELOAD_ONELEVEL, NULL );
	//gconf_client_notify_add( client, "/desktop/eric/wallpaper_path", gconf_wallpaper_changed, NULL, NULL, NULL );

    for( i = 0; i< mons; i++ )
        gtk_widget_show_all( lock_windows[i].window );

    //Kick off the animation timer
    struct timespec tp;
    clock_gettime( CLOCK_MONOTONIC, &tp );
    old_ns = tp.tv_nsec;
	g_timeout_add( 16, animation_timer, NULL );

    grab_keys();
	gtk_main();
	
	return 0;
}	
	
