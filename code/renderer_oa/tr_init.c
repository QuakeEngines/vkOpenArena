/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_init.c -- functions that are not called every frame
#ifdef _WIN32
#include <windows.h>
#define Sys_LoadLibrary(f)      (void*)LoadLibrary(f)
#define Sys_UnloadLibrary(h)    FreeLibrary((HMODULE)h)
#define Sys_LoadFunction(h,fn)  (void*)GetProcAddress((HMODULE)h,fn)
#define Sys_LibraryError()      "unknown"
#else
#include <dlfcn.h>
#define Sys_LoadLibrary(f)      dlopen(f,RTLD_NOW)
#define Sys_UnloadLibrary(h)    dlclose(h)
#define Sys_LoadFunction(h,fn)  dlsym(h,fn)
#define Sys_LibraryError()      dlerror()
#endif
#include "tr_local.h"

extern shaderCommands_t tess;
extern backEndData_t* backEndData;	// the second one may not be allocated


glconfig_t  glConfig;
glstate_t	glState;

static unsigned char *RB_ReadPixels(int x, int y, int width, int height, 
	size_t *offset, int *padlen);

cvar_t	*r_railWidth;
cvar_t	*r_railCoreWidth;
cvar_t	*r_railSegmentLength;

cvar_t	*r_verbose;

cvar_t	*r_znear;

cvar_t	*r_skipBackEnd;

cvar_t	*r_measureOverdraw;

cvar_t	*r_inGameVideo;
cvar_t	*r_fastsky;
cvar_t	*r_drawSun;
cvar_t	*r_dlightBacks;

cvar_t	*r_lodbias;
cvar_t	*r_lodscale;

cvar_t	*r_drawentities;
cvar_t	*r_speeds;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t	*r_facePlaneCull;
cvar_t	*r_showcluster;




cvar_t	*r_ignoreGLErrors;

cvar_t	*r_primitives;

cvar_t	*r_lightmap;


cvar_t	*r_uiFullScreen;


cvar_t	*r_shadows;

cvar_t	*r_nobind;
cvar_t	*r_singleShader;
cvar_t	*r_roundImagesDown;
cvar_t	*r_colorMipLevels;
cvar_t	*r_picmip;
cvar_t	*r_iconmip;
cvar_t	*r_iconBits;
cvar_t	*r_showtris;
cvar_t	*r_showsky;
cvar_t	*r_shownormals;
cvar_t	*r_finish;
cvar_t	*r_clear;
cvar_t	*r_offsetFactor;
cvar_t	*r_offsetUnits;
cvar_t	*r_gamma;

cvar_t	*r_lockpvs;
cvar_t	*r_noportals;
cvar_t	*r_portalOnly;

cvar_t	*r_subdivisions;
cvar_t	*r_lodCurveError;

cvar_t	*r_mapOverBrightBits;

cvar_t	*r_debugSurface;
cvar_t	*r_simpleMipMaps;

cvar_t	*r_showImages;


cvar_t	*r_debugLight;
cvar_t	*r_debugSort;
cvar_t	*r_printShaders;

cvar_t	*r_marksOnTriangleMeshes;

cvar_t	*r_aviMotionJpegQuality;
cvar_t	*r_screenshotJpegQuality;

cvar_t	*r_maxpolys;
int		max_polys;
cvar_t	*r_maxpolyverts;
int		max_polyverts;

// tcpp
cvar_t	*r_lensReflectionBrightness;

// leilei

cvar_t	*r_flareDelay;		// time delay for medium quality flare testing

cvar_t	*r_flaresDlightShrink;
cvar_t	*r_flaresDlightFade;
cvar_t	*r_flaresDlightOpacity;
cvar_t	*r_flaresDlightScale;
cvar_t	*r_modelshader;		// Leilei

cvar_t	*r_ntsc;		// Leilei - ntsc / composite signals

// dynamic lights enabled/disabled
static cvar_t* r_dynamiclight;
static cvar_t* r_textureMode;
static cvar_t* r_ext_texture_filter_anisotropic;
static cvar_t* r_ext_max_anisotropy;

// not used.
static cvar_t* r_ext_compressed_textures;


#define GLE(ret, name, ...) name##proc * qgl##name;
QGL_1_1_PROCS;
QGL_DESKTOP_1_1_PROCS;
QGL_1_3_PROCS;
#undef GLE


void (APIENTRYP qglActiveTextureARB) (GLenum texture);
void (APIENTRYP qglClientActiveTextureARB) (GLenum texture);
void (APIENTRYP qglMultiTexCoord2fARB) (GLenum target, GLfloat s, GLfloat t);

void (APIENTRYP qglLockArraysEXT) (GLint first, GLsizei count);
void (APIENTRYP qglUnlockArraysEXT) (void);

static void * hinstOpenGL;

static qboolean GLimp_HaveExtension(const char *ext)
{
	const char *ptr = Q_stristr( glConfig.extensions_string, ext );
	if (ptr == NULL)
		return qfalse;
	ptr += strlen(ext);
	return ((*ptr == ' ') || (*ptr == '\0'));  // verify it's complete string.
}

static void* GL_GetProcAddressImpl( const char *symbol )
{
    //void *sym = glXGetProcAddressARB((const unsigned char *)symbol);
    return Sys_LoadFunction(hinstOpenGL, symbol);
}
/*
===============
GLimp_GetProcAddresses

Get addresses for OpenGL functions.
===============
*/
static qboolean GLimp_GetProcAddresses( void )
{
    int qglMajorVersion, qglMinorVersion;

#ifdef __SDL_NOGETPROCADDR__
#define GLE( ret, name, ... ) qgl##name = gl#name;
#else
#define GLE( ret, name, ... ) qgl##name = (name##proc *) GL_GetProcAddressImpl("gl" #name); \
	if ( qgl##name == NULL ) { \
		ri.Error(ERR_FATAL, "Missing OpenGL function %s\n", "gl" #name ); \
	}
#endif

	// OpenGL 1.0 and OpenGL ES 1.0
	GLE(const GLubyte *, GetString, GLenum name)

	if ( !qglGetString ) {
		ri.Error( ERR_FATAL, "glGetString is NULL" );
	}

	const char * pStr = (const char *)qglGetString( GL_VERSION );
	if ( !pStr )
    {
		ri.Error( ERR_FATAL, "GL_VERSION is NULL\n" );
	}

	sscanf( pStr, "%d.%d", &qglMajorVersion, &qglMinorVersion );


	if ( (qglMajorVersion > 1) || ( (qglMajorVersion == 1) && (qglMinorVersion >= 2) ) )
    {
		QGL_1_1_PROCS;
		QGL_DESKTOP_1_1_PROCS;
        QGL_1_3_PROCS;
	} else {
		ri.Error( ERR_FATAL, "Unsupported OpenGL Version: %s\n", pStr);
	}
#undef GLE

    // get our config strings

    Q_strncpyz( glConfig.vendor_string, (char *) qglGetString (GL_VENDOR), sizeof( glConfig.vendor_string ) );
    Q_strncpyz( glConfig.renderer_string, (char *) qglGetString (GL_RENDERER), sizeof( glConfig.renderer_string ) );
    if (*glConfig.renderer_string && glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] == '\n')
        glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] = 0;
    Q_strncpyz( glConfig.version_string, (char *) qglGetString (GL_VERSION), sizeof( glConfig.version_string ) );
    Q_strncpyz( glConfig.extensions_string, (char *)qglGetString(GL_EXTENSIONS), sizeof( glConfig.extensions_string ) );

	ri.Printf( PRINT_ALL,  "\n...Initializing OpenGL extensions\n" );


	// GL_EXT_texture_compression_s3tc
    glConfig.textureCompression = TC_NONE;
	if ( GLimp_HaveExtension( "GL_ARB_texture_compression" ) &&
	     GLimp_HaveExtension( "GL_EXT_texture_compression_s3tc" ) )
	{
		if ( r_ext_compressed_textures->value )
		{
			glConfig.textureCompression = TC_S3TC_ARB;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_compression_s3tc\n" );
		}
		else
		{
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_compression_s3tc\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_compression_s3tc not found\n" );
	}



	// GL_EXT_texture_env_add
	glConfig.textureEnvAddAvailable = qfalse;
	if ( GLimp_HaveExtension( "EXT_texture_env_add" ) )
	{
		if ( GLimp_HaveExtension( "GL_EXT_texture_env_add" ) )
		{
			glConfig.textureEnvAddAvailable = qtrue;
			ri.Printf( PRINT_ALL, "...using GL_EXT_texture_env_add\n" );
		}
		else
		{
			glConfig.textureEnvAddAvailable = qfalse;
			ri.Printf( PRINT_ALL, "...ignoring GL_EXT_texture_env_add\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_env_add not found\n" );
	}

	// GL_ARB_multitexture
	qglMultiTexCoord2fARB = NULL;
	qglActiveTextureARB = NULL;
	qglClientActiveTextureARB = NULL;
	if ( GLimp_HaveExtension( "GL_ARB_multitexture" ) )
	{
		qglMultiTexCoord2fARB = GL_GetProcAddressImpl( "glMultiTexCoord2fARB" );
		qglActiveTextureARB = GL_GetProcAddressImpl( "glActiveTextureARB" );
		qglClientActiveTextureARB = GL_GetProcAddressImpl( "glClientActiveTextureARB" );

		if ( qglActiveTextureARB )
		{
			GLint glint = 0;
			qglGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &glint );
			glConfig.numTextureUnits = (int) glint;
			if ( glConfig.numTextureUnits > 1 )
			{
				ri.Printf( PRINT_ALL, "...using GL_ARB_multitexture\n" );
			}
			else
			{
				qglMultiTexCoord2fARB = NULL;
				qglActiveTextureARB = NULL;
				qglClientActiveTextureARB = NULL;
				ri.Printf( PRINT_ALL, "...not using GL_ARB_multitexture, < 2 texture units\n" );
			}
		}
	}
	else
	{
		ri.Printf( PRINT_ALL,  "...GL_ARB_multitexture not found\n" );
	}

	// GL_EXT_compiled_vertex_array
	if ( GLimp_HaveExtension( "GL_EXT_compiled_vertex_array" ) )
	{
		ri.Printf( PRINT_ALL,  "...using GL_EXT_compiled_vertex_array\n" );
		qglLockArraysEXT = ( void ( APIENTRY * )( GLint, GLint ) ) GL_GetProcAddressImpl( "glLockArraysEXT" );
		qglUnlockArraysEXT = ( void ( APIENTRY * )( void ) ) GL_GetProcAddressImpl( "glUnlockArraysEXT" );
		if (!qglLockArraysEXT || !qglUnlockArraysEXT)
		{
			ri.Error(ERR_FATAL, "bad getprocaddress");
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_compiled_vertex_array not found\n" );
	}

	if ( GLimp_HaveExtension( "GL_EXT_texture_filter_anisotropic" ) )
	{
		if ( r_ext_texture_filter_anisotropic->integer )
        {
            int maxAnisotropy = 0;
            char target_string[4] = {0};
			qglGetIntegerv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, (GLint *)&maxAnisotropy );
			if ( maxAnisotropy <= 0 ) {
				ri.Printf( PRINT_ALL, "...GL_EXT_texture_filter_anisotropic not properly supported!\n" );
				maxAnisotropy = 0;
			}
			else
			{
                sprintf(target_string, "%d", maxAnisotropy);
				ri.Printf( PRINT_ALL,  "...using GL_EXT_texture_filter_anisotropic (max: %i)\n", maxAnisotropy );
                ri.Cvar_Set( "r_ext_max_anisotropy", target_string);
			}
		}
		else
		{
			ri.Printf( PRINT_ALL,  "...ignoring GL_EXT_texture_filter_anisotropic\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_ALL, "...GL_EXT_texture_filter_anisotropic not found\n" );
	}


	return qtrue;
}


/*
===============
GLimp_ClearProcAddresses

Clear addresses for OpenGL functions.
===============
*/
static void GLimp_ClearProcAddresses(void)
{
#define GLE( ret, name, ... ) qgl##name = NULL;
    QGL_1_1_PROCS;
    QGL_DESKTOP_1_1_PROCS;
    QGL_1_3_PROCS;
    
    qglActiveTextureARB = NULL;
	qglClientActiveTextureARB = NULL;
	qglMultiTexCoord2fARB = NULL;

	qglLockArraysEXT = NULL;
	qglUnlockArraysEXT = NULL;

#undef GLE
}



static void GL_SetDefaultState(void)
{
	qglClearDepth( 1.0f );

	qglCullFace(GL_FRONT);

	qglColor4f(1,1,1,1);

	// initialize downstream texture unit if we're running in a multitexture environment
	if ( qglActiveTextureARB )
    {
		GL_SelectTexture( 1 );
		GL_TextureMode( r_textureMode->string );
		GL_TexEnv( GL_MODULATE );
		qglDisable( GL_TEXTURE_2D );
		GL_SelectTexture( 0 );
	}

	qglEnable(GL_TEXTURE_2D);
	GL_TextureMode( r_textureMode->string );
	GL_TexEnv( GL_MODULATE );

	qglShadeModel( GL_SMOOTH );
	qglDepthFunc( GL_LEQUAL );

	// the vertex array is always enabled, but the color and texture
	// arrays are enabled and disabled around the compiled vertex array call
	qglEnableClientState(GL_VERTEX_ARRAY);

	//
	// make sure our GL state vector is set correctly
	//
	glState.glStateBits = GLS_DEPTHTEST_DISABLE | GLS_DEPTHMASK_TRUE;

	qglPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	qglDepthMask( GL_TRUE );
	qglDisable( GL_DEPTH_TEST );
	qglEnable( GL_SCISSOR_TEST );
	qglDisable( GL_CULL_FACE );
	qglDisable( GL_BLEND );
}


/*
** Workaround for ri.Printf's 1024 characters buffer limit.
*/
static void R_PrintLongString(const char *string)
{
	char buffer[1024];
	const char *p;
	int size = strlen(string);

	p = string;
	while(size > 0) {
		Q_strncpyz(buffer, p, sizeof (buffer) );
		ri.Printf( PRINT_ALL, "%s", buffer );
		p += 1023;
		size -= 1023;
	}
}


static void GfxInfo_f( void )
{
	const char *enablestrings[] = {
		"disabled",
		"enabled"
	};

	ri.Printf( PRINT_ALL, "\nGL_VENDOR: %s\n", glConfig.vendor_string );
	ri.Printf( PRINT_ALL, "GL_RENDERER: %s\n", glConfig.renderer_string );
	ri.Printf( PRINT_ALL, "GL_VERSION: %s\n", glConfig.version_string );
	ri.Printf( PRINT_ALL, "GL_EXTENSIONS: " );
	R_PrintLongString( glConfig.extensions_string );
    ri.Printf( PRINT_ALL, "\n" );
	ri.Printf( PRINT_ALL, "GL_MAX_TEXTURE_SIZE: %d\n", glConfig.maxTextureSize );
	ri.Printf( PRINT_ALL, "GL_MAX_TEXTURE_UNITS_ARB: %d\n", glConfig.numTextureUnits );
	ri.Printf( PRINT_ALL, "\nPIXELFORMAT: color(%d-bits) Z(%d-bit) stencil(%d-bits)\n", glConfig.colorBits, glConfig.depthBits, glConfig.stencilBits );


	if( glConfig.deviceSupportsGamma )
		ri.Printf( PRINT_ALL, "GAMMA: hardware w/ %d overbright bits\n", tr.overbrightBits );
	else
		ri.Printf( PRINT_ALL, "GAMMA: software w/ %d overbright bits\n", tr.overbrightBits );

	// rendering primitives
	{
		int	primitives;

		// default is to use triangles if compiled vertex arrays are present
		ri.Printf( PRINT_ALL, "rendering primitives: " );
		primitives = r_primitives->integer;
		if ( primitives == 0 ) {
			if ( qglLockArraysEXT ) {
				primitives = 2;
			}
			else {
				primitives = 1;
			}
		}
		if ( primitives == -1 ) {
			ri.Printf( PRINT_ALL, "none\n" );
		}
		else if ( primitives == 2 ) {
			ri.Printf( PRINT_ALL, "single glDrawElements\n" );
		}
		else if ( primitives == 1 ) {
			ri.Printf( PRINT_ALL, "multiple glArrayElement\n" );
		}
		else if ( primitives == 3 ) {
			ri.Printf( PRINT_ALL, "multiple glColor4ubv + glTexCoord2fv + glVertex3fv\n" );
		}
	}

	ri.Printf( PRINT_ALL, "texturemode: %s\n", r_textureMode->string );
	ri.Printf( PRINT_ALL, "picmip: %d\n", r_picmip->integer );
	ri.Printf( PRINT_ALL, "multitexture: %s\n", enablestrings[qglActiveTextureARB != 0] );
	ri.Printf( PRINT_ALL, "compiled vertex arrays: %s\n", enablestrings[qglLockArraysEXT != 0 ] );
	ri.Printf( PRINT_ALL, "texenv add: %s\n", enablestrings[glConfig.textureEnvAddAvailable != 0] );
	ri.Printf( PRINT_ALL, "compressed textures: %s\n", enablestrings[glConfig.textureCompression!=TC_NONE] );

	if ( r_finish->integer) {
		ri.Printf( PRINT_ALL, "Forcing glFinish\n" );
	}
}


/*
** This function is responsible for initializing a valid  working OpenGL subsystem. 
** then setting variables, checking GL constants, and reporting the gfx system config to the user.
*/
static void InitOpenGL(void)
{
	// initialize OS specific portions of the renderer
	//
	// directly or indirectly references the following cvars:
	//		- r_mode
	//		- r_(color|depth|stencil)bits
	//		- r_gamma

	if ( glConfig.vidWidth == 0 )
	{
		void * pCfg = NULL;
            			
		ri.WinSysInit(&pCfg, 0);
        
        if ( !GLimp_GetProcAddresses() )
        {
            ri.Error(ERR_FATAL, "GLimp_GetProcAddresses() failed\n" );
            GLimp_ClearProcAddresses();
 
        }

        qglClearColor( 1, 0, 0, 1 );
        qglClear( GL_COLOR_BUFFER_BIT );
        
        ri.WinSysEndFrame();
        
        // OpenGL driver constants
        qglGetIntegerv( GL_MAX_TEXTURE_SIZE, &glConfig.maxTextureSize );

        // stubbed or broken drivers may have reported 0...
        if( glConfig.maxTextureSize < 0 )
        {
            glConfig.maxTextureSize = 0;
        }
	}


	// set default state
	GL_SetDefaultState();

    // print info
	//GfxInfo_f();
}





/*
==============================================================================
						SCREEN SHOTS

NOTE TTimo
some thoughts about the screenshots system:
screenshots get written in fs_homepath + fs_gamedir
vanilla q3 .. baseq3/screenshots/ *.tga
team arena .. missionpack/screenshots/ *.tga

two commands: "screenshot" and "screenshotJPEG"
we use statics to store a count and start writing the first screenshot/screenshot????.tga (.jpg) available
(with FS_FileExists / FS_FOpenFileWrite calls)
FIXME: the statics don't get a reinit between fs_game changes

==============================================================================
*/

static void RB_TakeScreenshot(int x, int y, int width, int height, char *fileName)
{
	byte *allbuf, *buffer;
	byte *srcptr, *destptr;
	byte *endline, *endmem;
	byte temp;

	int linelen, padlen;
	size_t offset = 18, memcount;

	allbuf = RB_ReadPixels(x, y, width, height, &offset, &padlen);
	buffer = allbuf + offset - 18;

	memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = width & 255;
	buffer[13] = width >> 8;
	buffer[14] = height & 255;
	buffer[15] = height >> 8;
	buffer[16] = 24;	// pixel size

	// swap rgb to bgr and remove padding from line endings
	linelen = width * 3;

	srcptr = destptr = allbuf + offset;
	endmem = srcptr + (linelen + padlen) * height;

	while(srcptr < endmem) {
		endline = srcptr + linelen;

		while(srcptr < endline) {
			temp = srcptr[0];
			*destptr++ = srcptr[2];
			*destptr++ = srcptr[1];
			*destptr++ = temp;

			srcptr += 3;
		}

		// Skip the pad
		srcptr += padlen;
	}

	memcount = linelen * height;

	ri.FS_WriteFile(fileName, buffer, memcount + 18);

	ri.Hunk_FreeTempMemory(allbuf);
}

/*
==================
RB_TakeScreenshotJPEG
==================
*/
/*
==================
RB_ReadPixels: Reads an image but takes care of alignment issues for reading RGB images.

Reads a minimum offset for where the RGB data starts in the image from integer stored at pointer offset. 
When the function has returned the actual offset was written back to address offset. 
This address will always have an alignment of packAlign to ensure efficient copying.

Stores the length of padding after a line of pixels to address padlen

Return value must be freed with ri.Hunk_FreeTempMemory()
==================
*/

static unsigned char *RB_ReadPixels(int x, int y, int width, int height, size_t *offset, int *padlen)
{
	byte *buffer, *bufstart;
	int padwidth, linelen;
	GLint packAlign;

	qglGetIntegerv(GL_PACK_ALIGNMENT, &packAlign);

	linelen = width * 3;
	padwidth = PAD(linelen, packAlign);

	// Allocate a few more bytes so that we can choose an alignment we like
	buffer = ri.Hunk_AllocateTempMemory(padwidth * height + *offset + packAlign - 1);

	bufstart = PADP((intptr_t) buffer + *offset, packAlign);
	qglReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, bufstart);

	*offset = bufstart - buffer;
	*padlen = padwidth - linelen;

	return buffer;
}


static void RB_TakeScreenshotJPEG(int x, int y, int width, int height, char *fileName)
{
	byte *buffer;
	size_t offset = 0, memcount;
	int padlen;

	buffer = RB_ReadPixels(x, y, width, height, &offset, &padlen);
	memcount = (width * 3 + padlen) * height;

	// gamma correct
	if(glConfig.deviceSupportsGamma)
		R_GammaCorrect(buffer + offset, memcount);

	RE_SaveJPG(fileName, r_screenshotJpegQuality->integer, width, height, buffer + offset, padlen);
	ri.Hunk_FreeTempMemory(buffer);
}


const void *RB_TakeScreenshotCmd( const void *data )
{
	const screenshotCommand_t *cmd = (const screenshotCommand_t *)data;

	if (cmd->jpeg)
		RB_TakeScreenshotJPEG( cmd->x, cmd->y, cmd->width, cmd->height, cmd->fileName);
	else
		RB_TakeScreenshot( cmd->x, cmd->y, cmd->width, cmd->height, cmd->fileName);

	return (const void *)(cmd + 1);
}


static void R_ScreenshotFilename( int lastNumber, char *fileName )
{
	int	a,b,c,d;

	if ( lastNumber < 0 || lastNumber > 9999 ) {
		snprintf( fileName, MAX_OSPATH, "screenshots/shot9999.tga" );
		return;
	}

	a = lastNumber / 1000;
	lastNumber -= a*1000;
	b = lastNumber / 100;
	lastNumber -= b*100;
	c = lastNumber / 10;
	lastNumber -= c*10;
	d = lastNumber;

	snprintf( fileName, MAX_OSPATH, "screenshots/shot%i%i%i%i.tga", a, b, c, d );
}


static void R_ScreenshotFilenameJPEG( int lastNumber, char *fileName )
{
	int	a,b,c,d;

	if ( lastNumber < 0 || lastNumber > 9999 ) {
		snprintf( fileName, MAX_OSPATH, "screenshots/shot9999.jpg" );
		return;
	}

	a = lastNumber / 1000;
	lastNumber -= a*1000;
	b = lastNumber / 100;
	lastNumber -= b*100;
	c = lastNumber / 10;
	lastNumber -= c*10;
	d = lastNumber;

	snprintf( fileName, MAX_OSPATH, "screenshots/shot%i%i%i%i.jpg", a, b, c, d );
}

/*
====================
R_LevelShot

levelshots are specialized 128*128 thumbnails for
the menu system, sampled down from full screen distorted images
====================
*/
static void R_LevelShot( void )
{
	char		checkname[MAX_OSPATH];
	byte		*buffer;
	byte		*source, *allsource;
	byte		*src, *dst;
	size_t			offset = 0;
	int			padlen;
	int			x, y;
	int			r, g, b;
	float		xScale, yScale;
	int			xx, yy;

	snprintf(checkname, sizeof(checkname), "levelshots/%s.tga", tr.world->baseName);

	allsource = RB_ReadPixels(0, 0, glConfig.vidWidth, glConfig.vidHeight, &offset, &padlen);
	source = allsource + offset;

	buffer = ri.Hunk_AllocateTempMemory(128 * 128*3 + 18);
	memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = 128;
	buffer[14] = 128;
	buffer[16] = 24;	// pixel size

	// resample from source
	xScale = glConfig.vidWidth / 512.0f;
	yScale = glConfig.vidHeight / 384.0f;
	for ( y = 0 ; y < 128 ; y++ ) {
		for ( x = 0 ; x < 128 ; x++ ) {
			r = g = b = 0;
			for ( yy = 0 ; yy < 3 ; yy++ ) {
				for ( xx = 0 ; xx < 4 ; xx++ ) {
					src = source + (3 * glConfig.vidWidth + padlen) * (int)((y*3 + yy) * yScale) +
					      3 * (int) ((x*4 + xx) * xScale);
					r += src[0];
					g += src[1];
					b += src[2];
				}
			}
			dst = buffer + 18 + 3 * ( y * 128 + x );
			dst[0] = b / 12;
			dst[1] = g / 12;
			dst[2] = r / 12;
		}
	}

	// gamma correct
	if ( glConfig.deviceSupportsGamma ) {
		R_GammaCorrect( buffer + 18, 128 * 128 * 3 );
	}

	ri.FS_WriteFile( checkname, buffer, 128 * 128*3 + 18 );

	ri.Hunk_FreeTempMemory(buffer);
	ri.Hunk_FreeTempMemory(allsource);

	ri.Printf( PRINT_ALL, "Wrote %s\n", checkname );
}


static void R_ScreenShotJPEG_f (void)
{
	char		checkname[MAX_OSPATH];
	static	int	lastNumber = -1;
	qboolean	silent;

	if ( !strcmp( ri.Cmd_Argv(1), "levelshot" ) ) {
		R_LevelShot();
		return;
	}

	if ( !strcmp( ri.Cmd_Argv(1), "silent" ) ) {
		silent = qtrue;
	}
	else {
		silent = qfalse;
	}

	if ( ri.Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		snprintf( checkname, MAX_OSPATH, "screenshots/%s.jpg", ri.Cmd_Argv( 1 ) );
	}
	else {
		// scan for a free filename

		// if we have saved a previous screenshot, don't scan
		// again, because recording demo avis can involve
		// thousands of shots
		if ( lastNumber == -1 ) {
			lastNumber = 0;
		}
		// scan for a free number
		for ( ; lastNumber <= 9999 ; lastNumber++ ) {
			R_ScreenshotFilenameJPEG( lastNumber, checkname );

			if (!ri.FS_FileExists( checkname )) {
				break; // file doesn't exist
			}
		}

		if ( lastNumber == 10000 ) {
			ri.Printf (PRINT_ALL, "ScreenShot: Couldn't create a file\n");
			return;
		}

		lastNumber++;
	}

	R_TakeScreenshot( 0, 0, glConfig.vidWidth, glConfig.vidHeight, checkname, qtrue );

	if ( !silent ) {
		ri.Printf (PRINT_ALL, "Wrote %s\n", checkname);
	}
}



/*
==================
R_ScreenShot_f

screenshot
screenshot [silent]
screenshot [levelshot]
screenshot [filename]

Doesn't print the pacifier message if there is a second arg
==================
*/
static void R_ScreenShot_f(void)
{
	char	checkname[MAX_OSPATH];
	static	int	lastNumber = -1;
	qboolean	silent;

	if ( !strcmp( ri.Cmd_Argv(1), "levelshot" ) ) {
		R_LevelShot();
		return;
	}

	if ( !strcmp( ri.Cmd_Argv(1), "silent" ) ) {
		silent = qtrue;
	}
	else {
		silent = qfalse;
	}

	if ( ri.Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		snprintf( checkname, MAX_OSPATH, "screenshots/%s.tga", ri.Cmd_Argv( 1 ) );
	}
	else {
		// scan for a free filename

		// if we have saved a previous screenshot, don't scan
		// again, because recording demo avis can involve
		// thousands of shots
		if ( lastNumber == -1 ) {
			lastNumber = 0;
		}
		// scan for a free number
		for ( ; lastNumber <= 9999 ; lastNumber++ ) {
			R_ScreenshotFilename( lastNumber, checkname );

			if (!ri.FS_FileExists( checkname )) {
				break; // file doesn't exist
			}
		}

		if ( lastNumber >= 9999 ) {
			ri.Printf (PRINT_ALL, "ScreenShot: Couldn't create a file\n");
			return;
		}

		lastNumber++;
	}

	R_TakeScreenshot( 0, 0, glConfig.vidWidth, glConfig.vidHeight, checkname, qfalse );

	if ( !silent ) {
		ri.Printf (PRINT_ALL, "Wrote %s\n", checkname);
	}
}


static void R_SkinList_f( void )
{
	int	i, j;
	skin_t *skin;

	ri.Printf(PRINT_ALL, "------------------\n");

	for ( i = 0 ; i < tr.numSkins ; i++ )
    {
		skin = tr.skins[i];

		ri.Printf( PRINT_ALL, "%3i:%s\n", i, skin->name );
		for ( j = 0 ; j < skin->numSurfaces ; j++ )
        {
			ri.Printf( PRINT_ALL, " %s = %s\n", skin->surfaces[j].name, skin->surfaces[j].shader->name );
		}
	}
	ri.Printf (PRINT_ALL, "------------------\n");
}


static void R_Register( void )
{

#if defined( _WIN32 )
	// leilei -  Get some version info first, code torn from quake
	OSVERSIONINFO vinfo;
	vinfo.dwOSVersionInfoSize = sizeof(vinfo);
#endif

	//
	// latched and archived variables
	//



	r_picmip = ri.Cvar_Get ("r_picmip", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_roundImagesDown = ri.Cvar_Get ("r_roundImagesDown", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_colorMipLevels = ri.Cvar_Get ("r_colorMipLevels", "0", CVAR_LATCH );
	ri.Cvar_CheckRange( r_picmip, 0, 16, qtrue );

	r_simpleMipMaps = ri.Cvar_Get( "r_simpleMipMaps", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_subdivisions = ri.Cvar_Get ("r_subdivisions", "4", CVAR_ARCHIVE | CVAR_LATCH);

	//
	// temporary latched variables that can only change over a restart
	//
	r_mapOverBrightBits = ri.Cvar_Get ("r_mapOverBrightBits", "2", CVAR_LATCH );

	r_singleShader = ri.Cvar_Get ("r_singleShader", "0", CVAR_CHEAT | CVAR_LATCH );

	//
	// archived variables that can change at any time
	//
	r_lodCurveError = ri.Cvar_Get( "r_lodCurveError", "250", CVAR_ARCHIVE|CVAR_CHEAT );
	r_lodbias = ri.Cvar_Get( "r_lodbias", "0", CVAR_ARCHIVE );

	r_znear = ri.Cvar_Get( "r_znear", "4", CVAR_ARCHIVE );
	ri.Cvar_CheckRange( r_znear, 0.001f, 200, qfalse );
	r_ignoreGLErrors = ri.Cvar_Get( "r_ignoreGLErrors", "1", CVAR_ARCHIVE );
	r_fastsky = ri.Cvar_Get( "r_fastsky", "0", CVAR_ARCHIVE );
	r_inGameVideo = ri.Cvar_Get( "r_inGameVideo", "1", CVAR_ARCHIVE );
	r_drawSun = ri.Cvar_Get( "r_drawSun", "0", CVAR_ARCHIVE );
	r_dlightBacks = ri.Cvar_Get( "r_dlightBacks", "1", CVAR_ARCHIVE );
	r_finish = ri.Cvar_Get ("r_finish", "0", CVAR_ARCHIVE);
	r_textureMode = ri.Cvar_Get( "r_textureMode", "GL_LINEAR_MIPMAP_NEAREST", CVAR_ARCHIVE | CVAR_LATCH );
	r_gamma = ri.Cvar_Get( "r_gamma", "1", CVAR_ARCHIVE );
	r_facePlaneCull = ri.Cvar_Get ("r_facePlaneCull", "1", CVAR_ARCHIVE );

	r_railWidth = ri.Cvar_Get( "r_railWidth", "16", CVAR_ARCHIVE );
	r_railCoreWidth = ri.Cvar_Get( "r_railCoreWidth", "6", CVAR_ARCHIVE );
	r_railSegmentLength = ri.Cvar_Get( "r_railSegmentLength", "32", CVAR_ARCHIVE );

	r_primitives = ri.Cvar_Get( "r_primitives", "0", CVAR_ARCHIVE );


	//
	// temporary variables that can change at any time
	//
	r_showImages = ri.Cvar_Get( "r_showImages", "0", CVAR_TEMP );


	r_debugSort = ri.Cvar_Get( "r_debugSort", "0", CVAR_CHEAT );
	r_printShaders = ri.Cvar_Get( "r_printShaders", "0", 0 );

	r_lightmap = ri.Cvar_Get ("r_lightmap", "0", 0 );
	r_portalOnly = ri.Cvar_Get ("r_portalOnly", "0", CVAR_CHEAT );

	r_skipBackEnd = ri.Cvar_Get ("r_skipBackEnd", "0", CVAR_CHEAT);

	r_measureOverdraw = ri.Cvar_Get( "r_measureOverdraw", "0", CVAR_CHEAT );
	r_lodscale = ri.Cvar_Get( "r_lodscale", "5", CVAR_CHEAT );
	r_drawentities = ri.Cvar_Get ("r_drawentities", "1", CVAR_CHEAT );
	r_nocull = ri.Cvar_Get ("r_nocull", "0", CVAR_CHEAT);
	r_novis = ri.Cvar_Get ("r_novis", "0", CVAR_CHEAT);
	r_showcluster = ri.Cvar_Get ("r_showcluster", "0", CVAR_CHEAT);
	r_speeds = ri.Cvar_Get ("r_speeds", "0", CVAR_TEMP);
	r_verbose = ri.Cvar_Get( "r_verbose", "0", CVAR_CHEAT );
	r_debugSurface = ri.Cvar_Get ("r_debugSurface", "0", CVAR_TEMP);
	r_nobind = ri.Cvar_Get ("r_nobind", "0", CVAR_CHEAT);
	r_showtris = ri.Cvar_Get ("r_showtris", "0", CVAR_TEMP);
	r_showsky = ri.Cvar_Get ("r_showsky", "0", CVAR_TEMP);
	r_shownormals = ri.Cvar_Get ("r_shownormals", "0", CVAR_CHEAT);
	r_clear = ri.Cvar_Get ("r_clear", "0", CVAR_TEMP);
	r_offsetFactor = ri.Cvar_Get( "r_offsetfactor", "-1", CVAR_CHEAT );
	r_offsetUnits = ri.Cvar_Get( "r_offsetunits", "-2", CVAR_CHEAT );
	r_lockpvs = ri.Cvar_Get ("r_lockpvs", "0", CVAR_CHEAT);
	r_noportals = ri.Cvar_Get ("r_noportals", "0", CVAR_CHEAT);
	r_shadows = ri.Cvar_Get( "cg_shadows", "1", 0 );

	r_marksOnTriangleMeshes = ri.Cvar_Get("r_marksOnTriangleMeshes", "0", CVAR_ARCHIVE);

	r_aviMotionJpegQuality = ri.Cvar_Get("r_aviMotionJpegQuality", "90", CVAR_ARCHIVE);
	r_screenshotJpegQuality = ri.Cvar_Get("r_screenshotJpegQuality", "90", CVAR_ARCHIVE);

	r_maxpolys = ri.Cvar_Get( "r_maxpolys", va("%d", MAX_POLYS), 0);
	r_maxpolyverts = ri.Cvar_Get( "r_maxpolyverts", va("%d", MAX_POLYVERTS), 0);

    r_dynamiclight = ri.Cvar_Get( "r_dynamiclight", "1", CVAR_ARCHIVE );

	//r_waveMode = ri.Cvar_Get( "r_waveMode", "0" , CVAR_ARCHIVE );

	r_lensReflectionBrightness = ri.Cvar_Get( "r_lensReflectionBrightness", "0.5" , CVAR_ARCHIVE);


	r_flaresDlightShrink = ri.Cvar_Get( "r_flaresDlightShrink", "1" , CVAR_ARCHIVE );	// dynamic light flares shrinking when close (reducing muzzleflash blindness)
	r_flaresDlightFade = ri.Cvar_Get( "r_flaresDlightFade", "0" , CVAR_ARCHIVE | CVAR_CHEAT );	// dynamic light flares fading (workaround clipping bug)
	r_flaresDlightOpacity = ri.Cvar_Get( "r_flaresDlightOpacity", "0.5" , CVAR_ARCHIVE );	// dynamic light flares (workaround poor visibility)
	r_flaresDlightScale = ri.Cvar_Get( "r_flaresDlightScale", "0.7" , CVAR_ARCHIVE );	// dynamic light flares (workaround poor visibility)
	r_flareDelay = ri.Cvar_Get( "r_flareDelay", "100" , CVAR_CHEAT);	// update delay for flare pixel read checking.



	r_modelshader = ri.Cvar_Get( "r_modelshader", "0" , CVAR_ARCHIVE | CVAR_LATCH);		// leilei - load and use special shaders for lightDiffuse models

	r_ntsc = ri.Cvar_Get( "r_ntsc", "0" , CVAR_ARCHIVE | CVAR_LATCH);			// leilei - ntsc filter

	r_iconmip = ri.Cvar_Get ("r_iconmip", "0", CVAR_ARCHIVE | CVAR_LATCH );		// leilei - icon mip
	r_iconBits = ri.Cvar_Get ("r_iconBits", "0", CVAR_ARCHIVE | CVAR_LATCH );	// leilei - icon bits



    // notued cvar, keep here for consistency
	r_uiFullScreen = ri.Cvar_Get( "r_uifullscreen", "0", 0); // not used
	// make sure all the commands added here are also
	// removed in R_Shutdown
	ri.Cmd_AddCommand( "imagelist", R_ImageList_f );
	ri.Cmd_AddCommand( "shaderlist", R_ShaderList_f );
	ri.Cmd_AddCommand( "skinlist", R_SkinList_f );
	ri.Cmd_AddCommand( "modellist", R_Modellist_f );
	ri.Cmd_AddCommand( "screenshot", R_ScreenShot_f );
	ri.Cmd_AddCommand( "screenshotJPEG", R_ScreenShotJPEG_f );
	ri.Cmd_AddCommand( "gfxinfo", GfxInfo_f );
}




//===========================================================================
//                        SKINS
//===========================================================================


/*
 * CommaParse: This is unfortunate, 
 * the skin files aren't compatable with our normal parsing rules.
 */
static char *CommaParse( char **data_p )
{
	int c = 0, len = 0;
	char *data = *data_p;;
	static char com_token[MAX_TOKEN_CHARS] = {0};

	// make sure incoming data is valid
	if ( !data )
    {
		*data_p = NULL;
		return com_token;
	}

	while ( 1 )
    {
		// skip whitespace
		while( (c = *data) <= ' ')
        {
			if( !c )
            {
				break;
			}
			data++;
		}
        
		c = *data;

		// skip double slash comments
		if ( c == '/' && data[1] == '/' )
		{
			while (*data && *data != '\n')
				data++;
		}
		// skip /* */ comments
		else if ( c=='/' && data[1] == '*' ) 
		{
			while ( *data && ( *data != '*' || data[1] != '/' ) ) 
			{
				data++;
			}
			if ( *data ) 
			{
				data += 2;
			}
		}
		else
		{
			break;
		}
	}

	if ( c == 0 ) {
		return "";
	}

	// handle quoted strings
	if (c == '\"')
	{
		data++;
		while (1)
		{
			c = *data++;
			if (c=='\"' || !c)
			{
				com_token[len] = 0;
				*data_p = ( char * ) data;
				return com_token;
			}
			if (len < MAX_TOKEN_CHARS)
			{
				com_token[len] = c;
				len++;
			}
		}
	}

	// parse a regular word
	do
	{
		if (len < MAX_TOKEN_CHARS)
		{
			com_token[len] = c;
			len++;
		}
		data++;
		c = *data;
	} while (c>32 && c != ',' );

	if (len == MAX_TOKEN_CHARS)
	{
//		ri.Printf (PRINT_DEVELOPER, "Token exceeded %i chars, discarded.\n", MAX_TOKEN_CHARS);
		len = 0;
	}
	com_token[len] = 0;

	*data_p = ( char * ) data;
	return com_token;
}



static qhandle_t RE_RegisterSkin( const char *name )
{
    skinSurface_t parseSurfaces[MAX_SKIN_SURFACES];
	qhandle_t	hSkin;
	skin_t		*skin;
	skinSurface_t	*surf;
	
    char* text;
	char* text_p;
	char* token;
	char surfName[MAX_QPATH];

	if ( !name || !name[0] )
    {
		ri.Printf( PRINT_ALL, "Empty name passed to RE_RegisterSkin\n" );
		return 0;
	}

	if ( strlen( name ) >= MAX_QPATH )
    {
		ri.Printf( PRINT_ALL, "Skin name exceeds MAX_QPATH\n" );
		return 0;
	}


	// see if the skin is already loaded
	for ( hSkin = 1; hSkin < tr.numSkins ; hSkin++ )
    {
		skin = tr.skins[hSkin];
		if ( !Q_stricmp( skin->name, name ) )
        {
			if( skin->numSurfaces == 0 ) {
				return 0;		// default skin
			}
			return hSkin;
		}
	}

	// allocate a new skin
	if ( tr.numSkins == MAX_SKINS )
    {
		ri.Printf( PRINT_WARNING, "WARNING: RE_RegisterSkin( '%s' ) MAX_SKINS hit\n", name );
		return 0;
	}
	tr.numSkins++;
	skin = ri.Hunk_Alloc( sizeof( skin_t ), h_low );
	tr.skins[hSkin] = skin;
	Q_strncpyz( skin->name, name, sizeof( skin->name ) );
	skin->numSurfaces = 0;

	R_IssuePendingRenderCommands();

	// If not a .skin file, load as a single shader
	if ( strcmp( name + strlen( name ) - 5, ".skin" ) )
    {
		skin->numSurfaces = 1;
        skin->surfaces = ri.Hunk_Alloc( sizeof( skinSurface_t ), h_low );
		skin->surfaces[0].shader = R_FindShader( name, LIGHTMAP_NONE, qtrue );
        return hSkin;
	}

	// load and parse the skin file
    ri.FS_ReadFile( name, &text );
	if ( !text ) {
		return 0;
	}

	text_p = text;
	while ( text_p && *text_p )
    {
		// get surface name
		token = CommaParse( &text_p );
		Q_strncpyz( surfName, token, sizeof( surfName ) );

		if ( !token[0] ) {
			break;
		}
		// lowercase the surface name so skin compares are faster
		Q_strlwr( surfName );

		if ( *text_p == ',' ) {
			text_p++;
		}

		if ( strstr( token, "tag_" ) ) {
			continue;
		}
		
		// parse the shader name
		token = CommaParse( &text_p );

		if ( skin->numSurfaces >= MAX_SKIN_SURFACES ) {
			ri.Printf( PRINT_WARNING, "WARNING: Ignoring surfaces in '%s', the max is %d surfaces!\n", name, MD3_MAX_SURFACES );
			break;
		}

        surf = &parseSurfaces[skin->numSurfaces];
        Q_strncpyz( surf->name, surfName, sizeof( surf->name ) );
		surf->shader = R_FindShader( token, LIGHTMAP_NONE, qtrue );
		skin->numSurfaces++;
	}

	ri.FS_FreeFile( text );


	// never let a skin have 0 shaders
	if ( skin->numSurfaces == 0 ) {
		return 0;		// use default skin
	}

 	
	// copy surfaces to skin
	skin->surfaces = ri.Hunk_Alloc( skin->numSurfaces * sizeof( skinSurface_t ), h_low );		
	memcpy( skin->surfaces, parseSurfaces, skin->numSurfaces * sizeof( skinSurface_t ) );  

	return hSkin;
}


/*
 * Touch all images to make sure they are resident
 */
static void RE_EndRegistration( void )
{
	if (!ri.Sys_LowPhysicalMemory())
    {
		RB_ShowImages();
	}
}


static void R_DeleteTextures( void )
{
	int	i;

	for(i=0; i<tr.numImages ; i++)
    {
		qglDeleteTextures( 1, &tr.images[i]->texnum );
	}
	memset( tr.images, 0, sizeof( tr.images ) );

	tr.numImages = 0;

	memset( glState.currenttextures, 0, sizeof( glState.currenttextures ) );
	if ( qglActiveTextureARB )
    {
		GL_SelectTexture( 1 );
		qglBindTexture( GL_TEXTURE_2D, 0 );
		GL_SelectTexture( 0 );
		qglBindTexture( GL_TEXTURE_2D, 0 );
	}
    else
    {
		qglBindTexture( GL_TEXTURE_2D, 0 );
	}
}





void R_Init(void)
{
	int i;
	unsigned char *ptr;

	ri.Printf( PRINT_ALL, "-------- RendererOA Init --------\n" );

    r_ext_texture_filter_anisotropic = ri.Cvar_Get( "r_ext_texture_filter_anisotropic", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_ext_max_anisotropy = ri.Cvar_Get( "r_ext_max_anisotropy", "2", CVAR_ARCHIVE | CVAR_LATCH );

    r_ext_compressed_textures = ri.Cvar_Get( "r_ext_compressed_textures", "0", CVAR_ARCHIVE | CVAR_LATCH );

	// clear all our internal state
	memset( &tr, 0, sizeof( tr ) );
	memset( &backEnd, 0, sizeof( backEnd ) );
	memset( &tess, 0, sizeof( tess ) );

	if(sizeof(glconfig_t) != 11332)
		ri.Error( ERR_FATAL, "Mod ABI incompatible: sizeof(glconfig_t) == %u != 11332", (unsigned int) sizeof(glconfig_t));

	if( (intptr_t)tess.xyz & 15 )
		ri.Printf( PRINT_WARNING, "tess.xyz not 16 byte aligned\n" );

	memset( tess.constantColor255, 255, sizeof( tess.constantColor255 ) );

	// init function tables
	for( i = 0; i < FUNCTABLE_SIZE; i++ )
    {
		tr.sinTable[i] = sin( 2*M_PI*i / ( ( float ) ( FUNCTABLE_SIZE - 1 ) ) );
		tr.squareTable[i] = ( i < FUNCTABLE_SIZE/2 ) ? 1.0f : -1.0f;
		tr.sawToothTable[i] = (float)i / FUNCTABLE_SIZE;
		tr.inverseSawToothTable[i] = 1.0f - tr.sawToothTable[i];
        
        if ( i < FUNCTABLE_SIZE / 4 )
            tr.triangleTable[i] = (float) i / ( FUNCTABLE_SIZE / 4 );
        else if (i >= FUNCTABLE_SIZE / 4 && i < FUNCTABLE_SIZE / 2 )
            tr.triangleTable[i] = 1.0f - tr.triangleTable[i-FUNCTABLE_SIZE / 4];
		else
			tr.triangleTable[i] = -tr.triangleTable[i-FUNCTABLE_SIZE/2];
	}

	R_InitFogTable();

	R_NoiseInit();

	R_Register();

	max_polys = r_maxpolys->integer;
	if (max_polys < MAX_POLYS)
		max_polys = MAX_POLYS;

	max_polyverts = r_maxpolyverts->integer;
	if (max_polyverts < MAX_POLYVERTS)
		max_polyverts = MAX_POLYVERTS;

	ptr = ri.Hunk_Alloc( sizeof( *backEndData ) + sizeof(srfPoly_t) * max_polys + sizeof(polyVert_t) * max_polyverts, h_low);
	backEndData = (backEndData_t *) ptr;
	backEndData->polys = (srfPoly_t *) ((char *) ptr + sizeof( *backEndData ));
	backEndData->polyVerts = (polyVert_t *) ((char *) ptr + sizeof( *backEndData ) + sizeof(srfPoly_t) * max_polys);
	R_InitNextFrame();

	InitOpenGL();

	R_InitImages();
    R_InitShaders();
    R_InitSkins();
	R_ModelInit();
	R_InitFlares();

	R_InitFreeType();

    R_InitDLight();
	
    int err = qglGetError();
	if ( err != GL_NO_ERROR )
		ri.Printf( PRINT_ALL, "glGetError() = 0x%x\n", err);


	ri.Printf( PRINT_ALL, "------- R_Init() finished -------\n\n");
}


void RE_Shutdown( qboolean destroyWindow )
{
	ri.Printf( PRINT_ALL, "RE_Shutdown( %i )\n", destroyWindow );

	ri.Cmd_RemoveCommand("modellist");
	ri.Cmd_RemoveCommand("screenshotJPEG");
	ri.Cmd_RemoveCommand("screenshot");
	ri.Cmd_RemoveCommand("imagelist");
	ri.Cmd_RemoveCommand("shaderlist");
	ri.Cmd_RemoveCommand("skinlist");
	ri.Cmd_RemoveCommand("gfxinfo");

	if ( tr.registered )
    {
		R_IssuePendingRenderCommands();
		R_DeleteTextures();
	}

	R_DoneFreeType();

	// shut down platform specific OpenGL stuff
	if( destroyWindow )
    {
		ri.WinSysShutdown();

		memset(&glConfig, 0, sizeof( glConfig ));
		memset(&glState, 0, sizeof( glState ));

        GLimp_ClearProcAddresses();
	}

	tr.registered = qfalse;
}


void RE_BeginRegistration(glconfig_t *glconfigOut)
{
	R_Init();

	*glconfigOut = glConfig;

	R_IssuePendingRenderCommands();

	tr.viewCluster = -1; // force markleafs to regenerate

	RE_ClearScene();

	tr.registered = qtrue;
}



/*
@@@@@@@@@@@@@@@@@@@@@
GetRefAPI

@@@@@@@@@@@@@@@@@@@@@
*/
#ifdef USE_RENDERER_DLOPEN
Q_EXPORT void QDECL GetRefAPI ( int apiVersion, const refimport_t * const rimp, refexport_t * const rexp )
{
#else
void GetRefAPI ( int apiVersion, const refimport_t * const rimp, rexp * const rexp )
{
#endif

	ri = *rimp;

	if( apiVersion != REF_API_VERSION )
	{
		ri.Printf(PRINT_ALL, "Mismatched REF_API_VERSION: expected %i, got %i\n", REF_API_VERSION, apiVersion );
		return ;
	}

    
	// the RE_ functions are Renderer Entry points
	rexp->Shutdown = RE_Shutdown;
	rexp->BeginRegistration = RE_BeginRegistration;
	rexp->RegisterModel = RE_RegisterModel;
	rexp->RegisterSkin = RE_RegisterSkin;
	rexp->RegisterShader = RE_RegisterShader;
	rexp->RegisterShaderNoMip = RE_RegisterShaderNoMip;
	rexp->LoadWorld = RE_LoadWorldMap;
	rexp->SetWorldVisData = RE_SetWorldVisData;
	rexp->EndRegistration = RE_EndRegistration;
	rexp->ClearScene = RE_ClearScene;
	rexp->AddRefEntityToScene = RE_AddRefEntityToScene;
	rexp->AddPolyToScene = RE_AddPolyToScene;
	rexp->LightForPoint = R_LightForPoint;
	rexp->AddLightToScene = RE_AddLightToScene;
	rexp->AddAdditiveLightToScene = RE_AddAdditiveLightToScene;

	rexp->RenderScene = RE_RenderScene;
	rexp->SetColor = RE_SetColor;
	rexp->DrawStretchPic = RE_StretchPic;
	rexp->DrawStretchRaw = RE_StretchRaw;
	rexp->UploadCinematic = RE_UploadCinematic;
    
	rexp->BeginFrame = RE_BeginFrame;
	rexp->EndFrame = RE_EndFrame;
	rexp->MarkFragments = R_MarkFragments;
	rexp->LerpTag = R_LerpTag;
	rexp->ModelBounds = R_ModelBounds;
	rexp->RegisterFont = RE_RegisterFont;
	rexp->RemapShader = R_RemapShader;
	rexp->GetEntityToken = R_GetEntityToken;
	rexp->inPVS = R_inPVS;
	rexp->TakeVideoFrame = RE_TakeVideoFrame;

}

#ifdef USE_RENDERER_DLOPEN

void QDECL Com_Printf( const char *msg, ... )
{
	va_list         argptr;
	char            text[1024];

	va_start(argptr, msg);
	Q_vsnprintf(text, sizeof(text), msg, argptr);
	va_end(argptr);

	ri.Printf(PRINT_ALL, "%s", text);
}

void QDECL Com_Error( int level, const char *error, ... )
{
	va_list         argptr;
	char            text[1024];

	va_start(argptr, error);
	Q_vsnprintf(text, sizeof(text), error, argptr);
	va_end(argptr);

	ri.Error(level, "%s", text);
}

#endif
