#ifndef TR_GLOBALS_H_
#define TR_GLOBALS_H_

#include "../qcommon/q_shared.h"
#include "../qcommon/qfiles.h"
#include "tr_common.h"

//#include "tr_shader.h"
#include "tr_image.h"
#include "viewParms.h"
#include "trRefDef.h"


#define	MAX_DRAWIMAGES			2048
#define	MAX_LIGHTMAPS			256

#define	MAX_SKINS				1024

#define	MAX_DRAWSURFS			0x10000
#define	DRAWSURF_MASK			(MAX_DRAWSURFS-1)




// 12 bits, see QSORT_SHADERNUM_SHIFT
#define	MAX_SHADERS		16384
#define	MAX_MOD_KNOWN	1024
/*
** trGlobals_t 
**
** Most renderer globals are defined here.
** backend functions should never modify any of these fields,
** but may read fields that aren't dynamically modified
** by the frontend.
*/

/*
** performanceCounters_t
*/
typedef struct {
	int		c_sphere_cull_patch_in, c_sphere_cull_patch_clip, c_sphere_cull_patch_out;
	int		c_box_cull_patch_in, c_box_cull_patch_clip, c_box_cull_patch_out;
	int		c_sphere_cull_md3_in, c_sphere_cull_md3_clip, c_sphere_cull_md3_out;
	int		c_box_cull_md3_in, c_box_cull_md3_clip, c_box_cull_md3_out;

	int		c_leafs;
	int		c_dlightSurfaces;
	int		c_dlightSurfacesCulled;
} frontEndCounters_t;


typedef struct trGlobals_s
{
	qboolean				registered;		// cleared at shutdown, set at beginRegistration

	int						visCount;		// incremented every time a new vis cluster is entered
	int						viewCount;		// incremented every view (twice a scene if portaled)
											// and every R_MarkFragments call

	qboolean				worldMapLoaded;
	struct world_s *        world;

	const unsigned char		*externalVisData;	// from RE_SetWorldVisData, shared with CM_Load

	image_t					*defaultImage;

	image_t					*fogImage;
	image_t					*dlightImage;	// inverse-quare highlight for projective adding
	image_t					*whiteImage;			// full of 0xff
	image_t					*identityLightImage;	// full of tr.identityLightByte

	struct shader_s	* defaultShader;
	struct shader_s	* shadowShader;
	struct shader_s	* projectionShadowShader;
	struct shader_s	* shaders[MAX_SHADERS];

	int						numLightmaps;
	image_t					*lightmaps[MAX_LIGHTMAPS];

	trRefEntity_t			*currentEntity;
	trRefEntity_t			worldEntity;		// point currentEntity at this when rendering world
	int						currentEntityNum;
	int						shiftedEntityNum;	// currentEntityNum << QSORT_ENTITYNUM_SHIFT
	struct model_s *        currentModel;

	viewParms_t				viewParms;

	float					identityLight;		// 1.0 / ( 1 << overbrightBits )
	int						identityLightByte;	// identityLight * 255
//	int						overbrightBits;		// r_overbrightBits->integer, but set to 0 if no hw gamma
	orientationr_t			or;					// for current entity

	trRefdef_t				refdef;

	int						viewCluster;

	vec3_t					sunLight;			// from the sky shader for this level
	vec3_t					sunDirection;

	frontEndCounters_t		pc;
	int						frontEndMsec;		// not in pc due to clearing issue

	//
	// put large tables at the end, so most elements will be
	// within the +/32K indexed range on risc processors
	//
	struct model_s *        models[MAX_MOD_KNOWN];
	int						numModels;

	int						numImages;
	image_t	*               images[MAX_DRAWIMAGES];

	// shader indexes from other modules will be looked up in tr.shaders[]
	// shader indexes from drawsurfs will be looked up in sortedShaders[]
	// lower indexed sortedShaders must be rendered first (opaque surfaces before translucent)
	int						numShaders;

//	struct shader_s				*sortedShaders[MAX_SHADERS];

	int						numSkins;
	struct skin_s *         skins[MAX_SKINS];

	float					sinTable[FUNCTABLE_SIZE];
	float					squareTable[FUNCTABLE_SIZE];
	float					triangleTable[FUNCTABLE_SIZE];
	float					sawToothTable[FUNCTABLE_SIZE];
	float					inverseSawToothTable[FUNCTABLE_SIZE];
} trGlobals_t;



extern trGlobals_t	tr;

#endif
