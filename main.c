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

GtkWidget* window;
GtkWidget* drawing_area;

gboolean isConfigured = FALSE;

float transition_alpha = 0.0f;

GSettings* gsettings;

GLuint shaderProgram;
GLuint tex1Map;
GLuint tex2Map;
GLuint transitionAlpha;
GLuint blurRadius;
GLuint blurResolution;
GLuint blurDirection;

//Framebuffers
GLuint fb1;
GLuint fb2;
GLuint fb1Tex;
GLuint fb2Tex;
GLuint fbcurrent;

int buffer_width;
int buffer_height;

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

void render_gl();
void grab_keys();

void load_wallpaper_shaders()
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

    shaderProgram = glCreateProgram();

    glAttachShader( shaderProgram, vertShader );
    glAttachShader( shaderProgram, fragShader );

    glLinkProgram( shaderProgram );
    glGetProgramInfoLog( shaderProgram, 2048, NULL, buffer );
    printf( buffer );

    tex1Map = glGetUniformLocation( shaderProgram, "tex1Map" );
    tex2Map = glGetUniformLocation( shaderProgram, "tex2Map" );
    transitionAlpha = glGetUniformLocation( shaderProgram, "transitionAlpha" );
    blurRadius = glGetUniformLocation( shaderProgram, "radius" );
    blurResolution = glGetUniformLocation( shaderProgram, "resolution" );
    blurDirection = glGetUniformLocation( shaderProgram, "dir" );
    transitionAlpha = glGetUniformLocation( shaderProgram, "transition" );
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
	render_gl();

	return ( transition_alpha < 1.0 );
}

GLuint texture=0;
GLuint texture2=0;
void load_background_texture(const char* path)
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
	
	if(texture2!=0) glDeleteTextures(1,&texture2);
	texture2=texture;
	
	glGenTextures(1,&texture);
	
	glBindTexture(GL_TEXTURE_2D,texture);
	
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	
	load_wallpaper_pixels(pixbuf);

	//free GdkPixbuf
	g_object_unref(pixbuf);

	transition_alpha = 0.0;
	g_timeout_add( 16, animation_timer, NULL );
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
    //grab_keys();

	GdkGLContext* gl_context = gtk_widget_get_gl_context( widget );
	GdkGLDrawable *gl_drawable = gtk_widget_get_gl_drawable( widget );

	if( !gdk_gl_drawable_gl_begin( gl_drawable, gl_context ) )
		g_assert_not_reached();

    GLenum err = glewInit();
    if( GLEW_OK != err )
        fprintf( stderr, "Error: %s\n", glewGetErrorString( err ) );
    else
        printf("glew ok!\n");

    load_wallpaper_shaders();

	GdkScreen* screen = gdk_screen_get_default();
	buffer_width = gdk_screen_get_width( screen );
	buffer_height = gdk_screen_get_height( screen );

    //Load framebuffer stuff
    glGenFramebuffers( 1, &fb1 );
    glGenFramebuffers( 1, &fb2 );

    printf( "init textures\n" );
	glEnable( GL_TEXTURE_2D );
    init_texture( &fb1Tex, buffer_width, buffer_height );
    init_texture( &fb2Tex, buffer_width, buffer_height );

    printf( "bind framebuffer textures\n" );
    glBindFramebuffer( GL_FRAMEBUFFER, fb1 );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb1Tex, 0 );
    glBindFramebuffer( GL_FRAMEBUFFER, fb2 );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb2Tex, 0 );

    glBindFramebuffer( GL_FRAMEBUFFER, 0 );


	if( !isConfigured )
	{
		isConfigured = TRUE;
	}

	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	//GConfClient* client = gconf_client_get_default();
    system( "scrot /tmp/ericlock.png" );
	load_background_texture( "/tmp/ericlock.png" );

	gdk_gl_drawable_gl_end( gl_drawable );
};

void swap_buffers()
{
    if( fbcurrent == fb1 )
    {
        glBindFramebuffer( GL_FRAMEBUFFER, fb2 ); 
        fbcurrent = fb2;

        glActiveTexture( GL_TEXTURE0 );
        glBindTexture( GL_TEXTURE_2D, fb1Tex );
        glUniform1i( tex1Map, 0 );
    }
    else
    {
        glBindFramebuffer( GL_FRAMEBUFFER, fb1 ); 
        fbcurrent = fb1;

        glActiveTexture( GL_TEXTURE0 );
        glBindTexture( GL_TEXTURE_2D, fb2Tex );
        glUniform1i( tex1Map, 0 );
    }
    //glViewport( 0, 0, buffer_width, buffer_height );
}

void render_gl()
{
	GdkGLContext* gl_context = gtk_widget_get_gl_context( drawing_area );
	GdkGLDrawable *gl_drawable = gtk_widget_get_gl_drawable( drawing_area );

	if( !gdk_gl_drawable_gl_begin( gl_drawable, gl_context ) )
		g_assert_not_reached();

	glClearColor( 0.0, 0.0, 0.0, 1.0 );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glUseProgram( shaderProgram );
	glEnable(GL_TEXTURE_2D);

    fbcurrent = fb1;

    glUniform1f( blurRadius, 150.0 * transition_alpha );
    glUniform1f( transitionAlpha, 0.0 );


    int i;
    int blur_passes = (int)( (150.0 * transition_alpha)/28.0 );
    blur_passes += 1;

    swap_buffers();
    //Bind Textures to shader
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, texture );
    glUniform1i( tex1Map, 0 );

    for( i = 0; i <= blur_passes; i++ )
    {
        glUniform1f( blurResolution, (float)buffer_width );
        glUniform2f( blurDirection, 1.0/(double)(i+1), 0.0 ); //horizontal
        glBegin(GL_QUADS);
        glColor3f(1.0,1.0,1.0);
        
        glTexCoord2f(0,0);
        glVertex2f(-1,1);
        
        glTexCoord2f(1,0);
        glVertex2f(1,1);
        
        glTexCoord2f(1,1);
        glVertex2f(1,-1);
        
        glTexCoord2f(0,1);
        glVertex2f(-1,-1);        
        glEnd();                 

        //Vertical pass
        swap_buffers();

        glUniform1f( blurResolution, (float)buffer_height );
        glUniform2f( blurDirection, 0.0, 1.0/(double)(i+1) );
        glBegin(GL_QUADS);
        glColor3f(1.0,1.0,1.0);
        
        glTexCoord2f(0,0);
        glVertex2f(-1,1);
        
        glTexCoord2f(1,0);
        glVertex2f(1,1);
        
        glTexCoord2f(1,1);
        glVertex2f(1,-1);
        
        glTexCoord2f(0,1);
        glVertex2f(-1,-1);        
        glEnd();                 

        swap_buffers();
    }

    //Final pass
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
    glUniform1f( blurRadius, 0.0 );
    glUniform1f( transitionAlpha, transition_alpha );
    //glViewport( 0, 0, buffer_width, buffer_height );
    //glLoadIdentity();                                  
    glBegin(GL_QUADS);
    //glColor3f(1.0,1.0,1.0);

    glTexCoord2f(0,0);
    glVertex2f(-1,1);

    glTexCoord2f(1,0);
    glVertex2f(1,1);

    glTexCoord2f(1,1);
    glVertex2f(1,-1);

    glTexCoord2f(0,1);
    glVertex2f(-1,-1);        
    glEnd();                 


    transition_alpha += 0.016f;  

	if( gdk_gl_drawable_is_double_buffered( gl_drawable ) )
		gdk_gl_drawable_swap_buffers( gl_drawable );
	else
		glFlush();
	
	gdk_gl_drawable_gl_end( gl_drawable );
}

void expose_event( GtkWidget* widget, GdkEventExpose *event, gpointer user )
{
	render_gl();
}	

void init_gl( int argc, char* argv[] )
{
	gtk_gl_init( &argc, &argv );
	
	GdkGLConfig *gl_config = gdk_gl_config_new_by_mode( (GdkGLConfigMode)(GDK_GL_MODE_RGBA,
								GDK_GL_MODE_DEPTH |
								GDK_GL_MODE_DOUBLE) );

	if( !gl_config )
		g_assert_not_reached();
	
	if( !gtk_widget_set_gl_capability( drawing_area, gl_config, NULL, TRUE, GDK_GL_RGBA_TYPE) )
		g_assert_not_reached();


	g_signal_connect( drawing_area, "expose-event", G_CALLBACK( expose_event ), NULL );
	g_signal_connect( drawing_area, "configure-event", G_CALLBACK( configure_event ), NULL );
};

void gsettings_value_changed( GSettings *settings, const gchar *key, gpointer user )
{
    if( strcmp( key, "picture-uri" ) == 0 )
    {
        system( "scrot /tmp/ericlock.png" );
        load_background_texture( "/tmp/ericlock.png" );
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
        gtk_main_quit();
    }
}

gboolean screen_changed_event( GtkWidget* widget, GdkScreen *old_screen, gpointer user )
{
	GdkScreen* screen = gdk_screen_get_default();
	int screen_width = gdk_screen_get_width( screen );
	int screen_height = gdk_screen_get_height( screen );

	gtk_window_move( GTK_WINDOW( window ), 0, 0 );
	gtk_window_resize( GTK_WINDOW( window ), screen_width, screen_height );
}

GdkFilterReturn handle_x11_event( GdkXEvent *xevent, GdkEvent *event, gpointer data )
{
    XEvent* xev = (XEvent*)xevent;

    Display* dpy = GDK_DISPLAY_XDISPLAY( gdk_display_get_default() );

    if( xev->type == KeyPress )
    {
        printf( "Got a key press event!\n" );
        if( xev->xkey.keycode == 133 || xev->xkey.keycode == 134 )
        {
            gtk_main_quit();
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
    Window xwin = GDK_WINDOW_XID( gtk_widget_get_window( window ) );

    //Grab number keys 
    //for( i = 10; i <= 20; i++ )
    //{
    //    XGrabKey( dpy, i, Mod4Mask | Mod2Mask, xwin, True, GrabModeAsync, GrabModeAsync );
    //    XGrabKey( dpy, i, Mod4Mask, xwin, True, GrabModeAsync, GrabModeAsync );
    //}
    //XGrabKey( dpy, AnyKey, AnyModifier, xwin, True, GrabModeSync, GrabModeSync );
    //XGrabKey( dpy, AnyKey, AnyModifier, xwin, True, GrabModeSync, GrabModeSync );
    XGrabKeyboard( dpy, xwin, False, GrabModeSync, GrabModeSync, CurrentTime );

    gdk_window_add_filter( NULL, handle_x11_event, NULL );
}

int main( int argc, char* argv[] )
{
	gtk_init( &argc, &argv );

	GdkScreen* screen = gdk_screen_get_default();
	int screen_width = gdk_screen_get_width( screen );
	int screen_height = gdk_screen_get_height( screen );
	
	window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
 	gtk_window_set_type_hint( GTK_WINDOW( window ), GDK_WINDOW_TYPE_HINT_DOCK );
	gtk_widget_set_size_request( window, screen_width, screen_height );
	gtk_window_move( GTK_WINDOW( window ), 0, 0 );
	gtk_window_fullscreen( GTK_WINDOW( window ) ); 
	gtk_window_set_decorated( GTK_WINDOW( window ), FALSE ); 
	gtk_window_set_keep_above( GTK_WINDOW( window ), TRUE ); 
    gtk_widget_set_can_focus( window, TRUE );
	gtk_widget_add_events( window, GDK_BUTTON_PRESS_MASK | GDK_KEY_PRESS_MASK );
	g_signal_connect( G_OBJECT(window), "button-press-event", G_CALLBACK(button_press_event), NULL );
	g_signal_connect( G_OBJECT(window), "key-press-event", G_CALLBACK(key_press_event), NULL );
	g_signal_connect( G_OBJECT(window), "screen-changed", G_CALLBACK(screen_changed_event), NULL );
	

	drawing_area = gtk_drawing_area_new();
	
	gtk_container_add( GTK_CONTAINER( window ), drawing_area );

	init_gl( argc, argv );

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

    g_signal_connect_data( gsettings, "changed", G_CALLBACK( gsettings_value_changed ), NULL, 0, 0 );

	//GConfClient* client = gconf_client_get_default();
	//gconf_client_add_dir( client, "/desktop/eric", GCONF_CLIENT_PRELOAD_ONELEVEL, NULL );
	//gconf_client_notify_add( client, "/desktop/eric/wallpaper_path", gconf_wallpaper_changed, NULL, NULL, NULL );

	gtk_widget_show_all( window );

	gtk_main();
	
	return 0;
}	
	
