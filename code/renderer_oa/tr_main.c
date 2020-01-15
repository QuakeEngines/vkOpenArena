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
// tr_main.c -- main control flow for each frame

#include "tr_local.h"
#include "matrix_multiplication.h"
///////// externs ///////////
extern int max_polys;
extern int max_polyverts;
extern	backEndData_t *backEndData;	// the second one may not be allocated
///////// globals ///////////
extern glconfig_t glConfig;
trGlobals_t	tr;

refimport_t	ri;

shaderCommands_t tess;


/////////////////////////////////


static int	r_firstSceneDrawSurf;

static int	r_numdlights;
static int	r_firstSceneDlight;

static int	r_numentities;
static int	r_firstSceneEntity;

static int	r_numpolys;
static int	r_firstScenePoly;

static int	r_numpolyverts;


static void R_RenderView( viewParms_t *parms );


void R_InitNextFrame( void )
{
	backEndData->commands.used = 0;

	r_firstSceneDrawSurf = 0;

	r_numdlights = 0;
	r_firstSceneDlight = 0;

	r_numentities = 0;
	r_firstSceneEntity = 0;

	r_numpolys = 0;
	r_firstScenePoly = 0;

	r_numpolyverts = 0;
}


void RE_ClearScene( void )
{
	r_firstSceneDlight = r_numdlights;
	r_firstSceneEntity = r_numentities;
	r_firstScenePoly = r_numpolys;
}


/*
===========================================================================

DISCRETE POLYS

===========================================================================
*/

void RE_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts, int numPolys )
{
	int	j;
	int	fogIndex;

	if ( !tr.registered )
    {
		return;
	}

	if ( !hShader ) {
		ri.Printf( PRINT_DEVELOPER, "WARNING: RE_AddPolyToScene: NULL poly shader\n");	// leilei - changed this to PRINT_DEVELOPER
		return;
	}

	for ( j = 0; j < numPolys; j++ )
    {
		if ( (r_numpolyverts + numVerts > max_polyverts) || (r_numpolys >= max_polys) )
        {
          /*
          NOTE TTimo this was initially a PRINT_WARNING
          but it happens a lot with high fighting scenes and particles
          since we don't plan on changing the const and making for room for those effects
          simply cut this message to developer only
          */
			ri.Printf( PRINT_DEVELOPER, "WARNING: RE_AddPolyToScene: r_max_polys or r_max_polyverts reached\n");
			return;
		}

		srfPoly_t* poly = &backEndData->polys[r_numpolys];
		poly->surfaceType = SF_POLY;
		poly->hShader = hShader;
		poly->numVerts = numVerts;
		poly->verts = &backEndData->polyVerts[r_numpolyverts];
		
		memcpy( poly->verts, &verts[numVerts*j], numVerts * sizeof( *verts ) );

		// done.
		r_numpolys++;
		r_numpolyverts += numVerts;

		// if no world is loaded
		if ( tr.world == NULL )
        {
			fogIndex = 0;
		}
		else if ( tr.world->numfogs == 1 )
        {
            // see if it is in a fog volume
			fogIndex = 0;
		}
        else
        {
			// find which fog volume the poly is in
            vec3_t bounds[2];
			VectorCopy( poly->verts[0].xyz, bounds[0] );
			VectorCopy( poly->verts[0].xyz, bounds[1] );
		    
            int i;
            for (i = 1; i < poly->numVerts; i++ )
            {
				AddPointToBounds( poly->verts[i].xyz, bounds[0], bounds[1] );
			}

			for ( fogIndex = 1 ; fogIndex < tr.world->numfogs ; fogIndex++ )
            {
				fog_t* fog = &tr.world->fogs[fogIndex]; 
				if( (bounds[1][0] >= fog->bounds[0][0]) && (bounds[1][1] >= fog->bounds[0][1]) && 
                    (bounds[1][2] >= fog->bounds[0][2]) && (bounds[0][0] <= fog->bounds[1][0]) &&
                    (bounds[0][1] <= fog->bounds[1][1]) && (bounds[0][2] <= fog->bounds[1][2]) )
                {
					break;
				}
			}

			if( fogIndex == tr.world->numfogs )
            {
				fogIndex = 0;
			}
		}

		poly->fogIndex = fogIndex;
	}
}



/*
=====================
RE_AddRefEntityToScene

=====================
*/
void RE_AddRefEntityToScene( const refEntity_t *ent )
{
	if ( !tr.registered ) {
		return;
	}
	if ( r_numentities >= MAX_REFENTITIES ) {
		ri.Printf(PRINT_DEVELOPER, "RE_AddRefEntityToScene: Dropping refEntity, reached MAX_REFENTITIES\n");
		return;
	}
	if ( isnan(ent->origin[0]) || isnan(ent->origin[1]) || isnan(ent->origin[2]) )
    {
		ri.Printf( PRINT_WARNING, "RE_AddRefEntityToScene passed a refEntity which has an origin with a NaN component\n");
		return;
	}
	if ( (int)ent->reType < 0 || ent->reType >= RT_MAX_REF_ENTITY_TYPE ) {
		ri.Error( ERR_DROP, "RE_AddRefEntityToScene: bad reType %i", ent->reType );
	}

	backEndData->entities[r_numentities].e = *ent;
	backEndData->entities[r_numentities].lightingCalculated = qfalse;

	r_numentities++;
}


void RE_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b )
{
	if ( tr.registered && (intensity > 0.0f) && (r_numdlights < MAX_DLIGHTS) )
    {
        dlight_t* dl = &backEndData->dlights[r_numdlights++];
        VectorCopy (org, dl->origin);
        dl->radius = intensity;
        dl->color[0] = r;
        dl->color[1] = g;
        dl->color[2] = b;
        dl->additive = qfalse;
	}
}


void RE_AddAdditiveLightToScene( const vec3_t org, float intensity, float r, float g, float b )
{
	if ( tr.registered && (intensity > 0.0f) && (r_numdlights < MAX_DLIGHTS) )
    {
        dlight_t* dl = &backEndData->dlights[r_numdlights++];
        VectorCopy (org, dl->origin);
        dl->radius = intensity;
        dl->color[0] = r;
        dl->color[1] = g;
        dl->color[2] = b;
        dl->additive = qtrue;
	}
}


//////// static funs ///////////

static const float s_flipMatrix[16] =
{
	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	0, 0, -1, 0,
	-1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 0, 1
};

// entities that will have procedurally generated surfaces will just point at this for their sorting surface
static surfaceType_t entitySurface = SF_ENTITY;


/*
=================
R_RotateForViewer

Sets up the modelview matrix for a given viewParm
=================
*/
static void R_RotateForViewer(void) 
{
	float	viewerMatrix[16];
	vec3_t	origin;
    VectorCopy(tr.viewParms.or.origin, origin);
    memset(&tr.or, 0, sizeof(tr.or));
    tr.or.axis[0][0] = 1;
	tr.or.axis[1][1] = 1;
	tr.or.axis[2][2] = 1;
	VectorCopy(origin, tr.or.viewOrigin);


	// transform by the camera placement

	viewerMatrix[0] = tr.viewParms.or.axis[0][0];
	viewerMatrix[4] = tr.viewParms.or.axis[0][1];
	viewerMatrix[8] = tr.viewParms.or.axis[0][2];
	viewerMatrix[12] = -origin[0] * viewerMatrix[0] - origin[1] * viewerMatrix[4] - origin[2] * viewerMatrix[8];

	viewerMatrix[1] = tr.viewParms.or.axis[1][0];
	viewerMatrix[5] = tr.viewParms.or.axis[1][1];
	viewerMatrix[9] = tr.viewParms.or.axis[1][2];
	viewerMatrix[13] = -origin[0] * viewerMatrix[1] - origin[1] * viewerMatrix[5] - origin[2] * viewerMatrix[9];

	viewerMatrix[2] = tr.viewParms.or.axis[2][0];
	viewerMatrix[6] = tr.viewParms.or.axis[2][1];
	viewerMatrix[10] = tr.viewParms.or.axis[2][2];
	viewerMatrix[14] = -origin[0] * viewerMatrix[2] - origin[1] * viewerMatrix[6] - origin[2] * viewerMatrix[10];

	viewerMatrix[3] = 0;
	viewerMatrix[7] = 0;
	viewerMatrix[11] = 0;
	viewerMatrix[15] = 1;

	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	
    #ifdef PROCESSOR_HAVE_SSE
	MatrixMultiply4x4_SSE( viewerMatrix, s_flipMatrix, tr.or.modelMatrix );
    #else
	MatrixMultiply4x4( viewerMatrix, s_flipMatrix, tr.or.modelMatrix );
    #endif

	tr.viewParms.world = tr.or;

}



/*
=================
R_MirrorPoint
=================
*/
static void R_MirrorPoint(float in[3], orientation_t *surface, orientation_t *camera, vec3_t out)
{
    unsigned int i;
    float local[3];
    float transformed[3];

    VectorSubtract( in, surface->origin, local );

    VectorClear( transformed );
    for( i = 0 ; i < 3 ; ++i )
    {
        float d = DotProduct(local, surface->axis[i]);
        VectorMA( transformed, d, camera->axis[i], transformed );
    }

    VectorAdd( transformed, camera->origin, out );
}


static void R_MirrorVector(vec3_t in, orientation_t *surface, orientation_t *camera, vec3_t out)
{
	int	i;

    VectorClear( out );
	for ( i = 0 ; i < 3 ; i++ )
    {
		float d = DotProduct(in, surface->axis[i]);
		VectorMA( out, d, camera->axis[i], out );
	}
}



static void R_PlaneForSurface(surfaceType_t *surfType, cplane_t *plane)
{
	srfTriangles_t	*tri;
	srfPoly_t		*poly;
	drawVert_t		*v1, *v2, *v3;
	vec4_t			plane4;

	if (!surfType) {
		memset (plane, 0, sizeof(*plane));
		plane->normal[0] = 1;
		return;
	}
	switch (*surfType)
    {
        case SF_FACE:
            *plane = ((srfSurfaceFace_t *)surfType)->plane;
            return;
        case SF_TRIANGLES:
            tri = (srfTriangles_t *)surfType;
            v1 = tri->verts + tri->indexes[0];
            v2 = tri->verts + tri->indexes[1];
            v3 = tri->verts + tri->indexes[2];
            PlaneFromPoints( plane4, v1->xyz, v2->xyz, v3->xyz );
            VectorCopy( plane4, plane->normal ); 
            plane->dist = plane4[3];
            return;
        case SF_POLY:
            poly = (srfPoly_t *)surfType;
            PlaneFromPoints( plane4, poly->verts[0].xyz, poly->verts[1].xyz, poly->verts[2].xyz );
            VectorCopy( plane4, plane->normal ); 
            plane->dist = plane4[3];
            return;
        default:
            memset (plane, 0, sizeof(*plane));
            plane->normal[0] = 1;		
            return;
	}
}


static void R_LocalNormalToWorld (vec3_t local, vec3_t world)
{
	world[0] = local[0] * tr.or.axis[0][0] + local[1] * tr.or.axis[1][0] + local[2] * tr.or.axis[2][0];
	world[1] = local[0] * tr.or.axis[0][1] + local[1] * tr.or.axis[1][1] + local[2] * tr.or.axis[2][1];
	world[2] = local[0] * tr.or.axis[0][2] + local[1] * tr.or.axis[1][2] + local[2] * tr.or.axis[2][2];
}

/*
=================
R_GetPortalOrientation

entityNum is the entity that the portal surface is a part of,
which may be moving and rotating.

Returns qtrue if it should be mirrored
=================
*/
static qboolean R_GetPortalOrientations( drawSurf_t *drawSurf, int entityNum, 
							 orientation_t *surface, orientation_t *camera,
							 vec3_t pvsOrigin, qboolean *mirror )
{
	int			i;
	cplane_t	originalPlane, plane;
	vec3_t		transformed;

	// create plane axis for the portal we are seeing
	R_PlaneForSurface( drawSurf->surface, &originalPlane );

	// rotate the plane if necessary
	if ( entityNum != REFENTITYNUM_WORLD ) {
		tr.currentEntityNum = entityNum;
		tr.currentEntity = &tr.refdef.entities[entityNum];

		// get the orientation of the entity
		R_RotateForEntity( tr.currentEntity, &tr.viewParms, &tr.or );

		// rotate the plane, but keep the non-rotated version for matching
		// against the portalSurface entities
		R_LocalNormalToWorld( originalPlane.normal, plane.normal );
		plane.dist = originalPlane.dist + DotProduct( plane.normal, tr.or.origin );

		// translate the original plane
		originalPlane.dist = originalPlane.dist + DotProduct( originalPlane.normal, tr.or.origin );
	}
    else
    {
		plane = originalPlane;
	}

	VectorCopy( plane.normal, surface->axis[0] );
	VectorPerp( plane.normal, surface->axis[1] );
	CrossProduct( surface->axis[0], surface->axis[1], surface->axis[2] );

	// locate the portal entity closest to this plane.
	// origin will be the origin of the portal, 
    // origin2 will be the origin of the camera
	for ( i = 0 ; i < tr.refdef.num_entities ; i++ )
    {
        float d;
		trRefEntity_t *e = &tr.refdef.entities[i];
		if ( e->e.reType != RT_PORTALSURFACE ) {
			continue;
		}

		d = DotProduct( e->e.origin, originalPlane.normal ) - originalPlane.dist;
		if ( d > 64 || d < -64) {
			continue;
		}

		// get the pvsOrigin from the entity
		VectorCopy( e->e.oldorigin, pvsOrigin );

		// if the entity is just a mirror, don't use as a camera point
		if ( e->e.oldorigin[0] == e->e.origin[0] && 
			e->e.oldorigin[1] == e->e.origin[1] && 
			e->e.oldorigin[2] == e->e.origin[2] ) {
			VectorScale( plane.normal, plane.dist, surface->origin );
			VectorCopy( surface->origin, camera->origin );
			VectorSubtract( ORIGIN, surface->axis[0], camera->axis[0] );
			VectorCopy( surface->axis[1], camera->axis[1] );
			VectorCopy( surface->axis[2], camera->axis[2] );

			*mirror = qtrue;
			return qtrue;
		}

		// project the origin onto the surface plane to get
		// an origin point we can rotate around
		d = DotProduct( e->e.origin, plane.normal ) - plane.dist;
		VectorMA( e->e.origin, -d, surface->axis[0], surface->origin );
			
		// now get the camera origin and orientation
		VectorCopy( e->e.oldorigin, camera->origin );
		VectorCopy( e->e.axis[0], camera->axis[0] );
		VectorCopy( e->e.axis[1], camera->axis[1] );
		VectorCopy( e->e.axis[2], camera->axis[2] );

		VectorSubtract( ORIGIN, camera->axis[0], camera->axis[0] );
		VectorSubtract( ORIGIN, camera->axis[1], camera->axis[1] );

		// optionally rotate
		if ( e->e.oldframe )
        {
			// if a speed is specified
			if ( e->e.frame ) {
				// continuous rotate
				d = (tr.refdef.time/1000.0f) * e->e.frame;
				VectorCopy( camera->axis[1], transformed );
				PointRotateAroundVector( camera->axis[1], camera->axis[0], transformed, d );
				CrossProduct( camera->axis[0], camera->axis[1], camera->axis[2] );
			} else {
				// bobbing rotate, with skinNum being the rotation offset
				d = sin( tr.refdef.time * 0.003f );
				d = e->e.skinNum + d * 4;
				VectorCopy( camera->axis[1], transformed );
				PointRotateAroundVector( camera->axis[1], camera->axis[0], transformed, d );
				CrossProduct( camera->axis[0], camera->axis[1], camera->axis[2] );
			}
		}
		else if ( e->e.skinNum )
        {
			d = e->e.skinNum;
			VectorCopy( camera->axis[1], transformed );
			PointRotateAroundVector( camera->axis[1], camera->axis[0], transformed, d );
			CrossProduct( camera->axis[0], camera->axis[1], camera->axis[2] );
		}
		*mirror = qfalse;
		return qtrue;
	}

	// if we didn't locate a portal entity, don't render anything.
	// We don't want to just treat it as a mirror, because without a
	// portal entity the server won't have communicated a proper entity set
	// in the snapshot

	// unfortunately, with local movement prediction it is easily possible
	// to see a surface before the server has communicated the matching
	// portal surface entity, so we don't want to print anything here...

	//ri.Printf( PRINT_ALL, "Portal surface without a portal entity\n" );

	return qfalse;
}

static qboolean IsMirror( const drawSurf_t *drawSurf, int entityNum )
{
	int			i;
	cplane_t	originalPlane, plane;
	trRefEntity_t	*e;
	float		d;

	// create plane axis for the portal we are seeing
	R_PlaneForSurface( drawSurf->surface, &originalPlane );

	// rotate the plane if necessary
	if ( entityNum != REFENTITYNUM_WORLD )
	{
		tr.currentEntityNum = entityNum;
		tr.currentEntity = &tr.refdef.entities[entityNum];

		// get the orientation of the entity
		R_RotateForEntity( tr.currentEntity, &tr.viewParms, &tr.or );

		// rotate the plane, but keep the non-rotated version for matching
		// against the portalSurface entities
		R_LocalNormalToWorld( originalPlane.normal, plane.normal );
		plane.dist = originalPlane.dist + DotProduct( plane.normal, tr.or.origin );

		// translate the original plane
		originalPlane.dist = originalPlane.dist + DotProduct( originalPlane.normal, tr.or.origin );
	} 

	// locate the portal entity closest to this plane.
	// origin will be the origin of the portal, origin2 will be
	// the origin of the camera
	for ( i = 0 ; i < tr.refdef.num_entities ; i++ ) 
	{
		e = &tr.refdef.entities[i];
		if ( e->e.reType != RT_PORTALSURFACE ) {
			continue;
		}

		d = DotProduct( e->e.origin, originalPlane.normal ) - originalPlane.dist;
		if ( d > 64 || d < -64) {
			continue;
		}

		// if the entity is just a mirror, don't use as a camera point
		if ( e->e.oldorigin[0] == e->e.origin[0] && e->e.oldorigin[1] == e->e.origin[1] && e->e.oldorigin[2] == e->e.origin[2] ) 
		{
			return qtrue;
		}

		return qfalse;
	}
	return qfalse;
}

/*
** SurfIsOffscreen
**
** Determines if a surface is completely offscreen.
*/
static qboolean SurfIsOffscreen( const drawSurf_t *drawSurf, vec4_t clipDest[128] )
{
	float shortest = 100000000;
	int entityNum;
	int numTriangles;
	shader_t *shader;
	int		fogNum;
	int dlighted;
	int i;
	unsigned int pointAnd = (unsigned int)~0;

	R_RotateForViewer();

	R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
	RB_BeginSurface( shader, fogNum );
	rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );

    int nVertexes = tess.numVertexes;
	assert( nVertexes < 128 );

	for ( i = 0; i < nVertexes; i++ )
	{
		int j;
		unsigned int pointFlags = 0;
        vec4_t clip;

		//R_TransformModelToClip( tess.xyz[i], tr.or.modelMatrix, tr.viewParms.projectionMatrix, eye, clip );
        {
            float eye[4];
            for ( j = 0 ; j < 4 ; j++ )
            {
                eye[j] = 
                    tess.xyz[i][0] * tr.or.modelMatrix[ j + 0 * 4 ] +
                    tess.xyz[i][1] * tr.or.modelMatrix[ j + 1 * 4 ] +
                    tess.xyz[i][2] * tr.or.modelMatrix[ j + 2 * 4 ] +
                                 1 * tr.or.modelMatrix[ j + 3 * 4 ];
            }

            for ( j = 0 ; j < 4 ; j++ )
            {
                clip[j] = 
                    eye[0] * tr.viewParms.projectionMatrix[ j + 0 * 4 ] +
                    eye[1] * tr.viewParms.projectionMatrix[ j + 1 * 4 ] +
                    eye[2] * tr.viewParms.projectionMatrix[ j + 2 * 4 ] +
                    eye[3] * tr.viewParms.projectionMatrix[ j + 3 * 4 ];
            }
        }
        ////////

		for ( j = 0; j < 3; j++ )
		{
			if ( clip[j] >= clip[3] )
			{
				pointFlags |= (1 << (j*2));
			}
			else if ( clip[j] <= -clip[3] )
			{
				pointFlags |= ( 1 << (j*2+1));
			}
		}
		pointAnd &= pointFlags;
	}

	// trivially reject
	if ( pointAnd )
	{
		return qtrue;
	}

	// determine if this surface is backfaced and also determine the distance
	// to the nearest vertex so we can cull based on portal range.  Culling
	// based on vertex distance isn't 100% correct (we should be checking for
	// range to the surface), but it's good enough for the types of portals
	// we have in the game right now.
	numTriangles = tess.numIndexes / 3;

	for ( i = 0; i < tess.numIndexes; i += 3 )
	{
		vec3_t normal;
		float len;

		VectorSubtract( tess.xyz[tess.indexes[i]], tr.viewParms.or.origin, normal );

		len = normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2];	// lose the sqrt
		if ( len < shortest )
		{
			shortest = len;
		}

		if ( DotProduct( normal, tess.normal[tess.indexes[i]] ) >= 0 )
		{
			numTriangles--;
		}
	}
	if ( !numTriangles )
	{
		return qtrue;
	}

	// mirrors can early out at this point, since we don't do a fade over distance
	// with them (although we could)
	if ( IsMirror( drawSurf, entityNum ) )
	{
		return qfalse;
	}

	if ( shortest > (tess.shader->portalRange*tess.shader->portalRange) )
	{
		return qtrue;
	}

	return qfalse;
}


/*
==========================================================================================

DRAWSURF SORTING

==========================================================================================
*/

static ID_INLINE void R_Radix( int byte, int size, drawSurf_t *source, drawSurf_t *dest )
{
  int count[ 256 ] = { 0 };
  int index[ 256 ] = { 0 };
  int i;

  unsigned char* sortKey = ( (unsigned char *)&source[ 0 ].sort ) + byte;
  unsigned char* end = sortKey + ( size * sizeof( drawSurf_t ) );

  for( ; sortKey < end; sortKey += sizeof( drawSurf_t ) )
    ++count[ *sortKey ];

  for( i = 1; i < 256; ++i )
    index[ i ] = index[ i - 1 ] + count[ i - 1 ];

  sortKey = ( (unsigned char *)&source[ 0 ].sort ) + byte;
  for( i = 0; i < size; ++i, sortKey += sizeof( drawSurf_t ) )
    dest[ index[ *sortKey ]++ ] = source[ i ];
}

/*
===============
R_RadixSort

Radix sort with 4 byte size buckets
===============
*/
static void R_RadixSort( drawSurf_t *source, int size )
{
  static drawSurf_t scratch[ MAX_DRAWSURFS ];
#ifdef Q3_LITTLE_ENDIAN
  R_Radix( 0, size, source, scratch );
  R_Radix( 1, size, scratch, source );
  R_Radix( 2, size, source, scratch );
  R_Radix( 3, size, scratch, source );
#else
  R_Radix( 3, size, source, scratch );
  R_Radix( 2, size, scratch, source );
  R_Radix( 1, size, source, scratch );
  R_Radix( 0, size, scratch, source );
#endif //Q3_LITTLE_ENDIAN
}

/*
========================
R_MirrorViewBySurface: Returns qtrue if another view has been rendered
========================
*/

static qboolean R_MirrorViewBySurface (drawSurf_t *drawSurf, int entityNum)
{
	vec4_t			clipDest[128];
	orientation_t	surface, camera;

	// don't recursively mirror
	if (tr.viewParms.isPortal) {
		ri.Printf( PRINT_DEVELOPER, "WARNING: recursive mirror/portal found\n" );
		return qfalse;
	}

	if ( r_noportals->integer || (r_fastsky->integer == 1) ) {
		return qfalse;
	}

	// trivially reject portal/mirror
	if ( SurfIsOffscreen( drawSurf, clipDest ) ) {
		return qfalse;
	}

	// save old viewParms so we can return to it after the mirror view
	viewParms_t oldParms = tr.viewParms;
	viewParms_t newParms = tr.viewParms;
    
	newParms.isPortal = qtrue;
	if ( !R_GetPortalOrientations( drawSurf, entityNum, &surface, &camera, newParms.pvsOrigin, &newParms.isMirror ) )
    {
		return qfalse;		// bad portal, no portalentity
	}

	R_MirrorPoint(oldParms.or.origin, &surface, &camera, newParms.or.origin );

	VectorSubtract( ORIGIN, camera.axis[0], newParms.portalPlane.normal );
	newParms.portalPlane.dist = DotProduct( camera.origin, newParms.portalPlane.normal );
	
	R_MirrorVector(oldParms.or.axis[0], &surface, &camera, newParms.or.axis[0]);
	R_MirrorVector(oldParms.or.axis[1], &surface, &camera, newParms.or.axis[1]);
	R_MirrorVector(oldParms.or.axis[2], &surface, &camera, newParms.or.axis[2]);

	// OPTIMIZE: restrict the viewport on the mirrored view

	// render the mirror view
	R_RenderView(&newParms);

	tr.viewParms = oldParms;

	return qtrue;
}


static void R_SortDrawSurfs( drawSurf_t *drawSurfs, int numDrawSurfs )
{
	shader_t		*shader;
	int				fogNum;
	int				entityNum;
	int				dlighted;
	int				i;

	// it is possible for some views to not have any surfaces
	if ( numDrawSurfs < 1 ) {
		// we still need to add it for hyperspace cases
		R_AddDrawSurfCmd( drawSurfs, numDrawSurfs );
		return;
	}

	// if we overflowed MAX_DRAWSURFS, the drawsurfs
	// wrapped around in the buffer and we will be missing
	// the first surfaces, not the last ones
	if ( numDrawSurfs > MAX_DRAWSURFS ) {
		numDrawSurfs = MAX_DRAWSURFS;
	}

	// sort the drawsurfs by sort type, then orientation, then shader
	R_RadixSort( drawSurfs, numDrawSurfs );

	// check for any pass through drawing, which
	// may cause another view to be rendered first
	for ( i = 0 ; i < numDrawSurfs ; i++ ) {
		R_DecomposeSort( (drawSurfs+i)->sort, &entityNum, &shader, &fogNum, &dlighted );

		if ( shader->sort > SS_PORTAL ) {
			break;
		}

		// no shader should ever have this sort type
		if ( shader->sort == SS_BAD ) {
			ri.Error (ERR_DROP, "Shader '%s'with sort == SS_BAD", shader->name );
		}

		// if the mirror was completely clipped away, we may need to check another surface
		if ( R_MirrorViewBySurface( (drawSurfs+i), entityNum) ) {
			// this is a debug option to see exactly what is being mirrored
			if ( r_portalOnly->integer ) {
				return;
			}
			break;		// only one mirror view at a time
		}
	}

	R_AddDrawSurfCmd( drawSurfs, numDrawSurfs );
}


/*
=================
R_SpriteFogNum: See if a sprite is inside a fog volume
=================
*/
static int R_SpriteFogNum( trRefEntity_t *ent )
{
	int	i, j;
	fog_t *fog;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return 0;
	}

	for ( i = 1 ; i < tr.world->numfogs ; i++ )
    {
		fog = &tr.world->fogs[i];
		for ( j = 0 ; j < 3 ; j++ ) {
			if ( ent->e.origin[j] - ent->e.radius >= fog->bounds[1][j] ) {
				break;
			}
			if ( ent->e.origin[j] + ent->e.radius <= fog->bounds[0][j] ) {
				break;
			}
		}
		if ( j == 3 ) {
			return i;
		}
	}

	return 0;
}


static void R_AddEntitySurfaces (void)
{
	trRefEntity_t	*ent;
	shader_t		*shader;

	if ( !r_drawentities->integer ) {
		return;
	}

	for ( tr.currentEntityNum = 0; tr.currentEntityNum < tr.refdef.num_entities; tr.currentEntityNum++ )
    {
		ent = tr.currentEntity = &tr.refdef.entities[tr.currentEntityNum];

		ent->needDlights = qfalse;

		// preshift the value we are going to OR into the drawsurf sort
		tr.shiftedEntityNum = tr.currentEntityNum << QSORT_REFENTITYNUM_SHIFT;

		//
		// the weapon model must be handled special --
		// we don't want the hacked weapon position showing in 
		// mirrors, because the true body position will already be drawn
		//
		if ( (ent->e.renderfx & RF_FIRST_PERSON) && tr.viewParms.isPortal) {
			continue;
		}

		// simple generated models, like sprites and beams, are not culled
		switch ( ent->e.reType )
        {
		case RT_PORTALSURFACE:
			break;		// don't draw anything
		case RT_SPRITE:
		case RT_BEAM:
		case RT_LIGHTNING:
		case RT_RAIL_CORE:
		case RT_RAIL_RINGS:
			// self blood sprites, talk balloons, etc should not be drawn in the primary
			// view.  We can't just do this check for all entities, because md3
			// entities may still want to cast shadows from them
			if ( (ent->e.renderfx & RF_THIRD_PERSON) && !tr.viewParms.isPortal) {
				continue;
			}
			shader = R_GetShaderByHandle( ent->e.customShader );
			R_AddDrawSurf( &entitySurface, shader, R_SpriteFogNum( ent ), 0 );
			break;

		case RT_MODEL:
			// we must set up parts of tr.or for model culling
			R_RotateForEntity( ent, &tr.viewParms, &tr.or );

			tr.currentModel = R_GetModelByHandle( ent->e.hModel );
			if (!tr.currentModel) {
				R_AddDrawSurf( &entitySurface, tr.defaultShader, 0, 0 );
			} else {
				switch ( tr.currentModel->type ) {
				case MOD_MESH:
					R_AddMD3Surfaces( ent );
					break;
				case MOD_MDR:
					R_MDRAddAnimSurfaces( ent );
					break;
				case MOD_IQM:
					R_AddIQMSurfaces( ent );
					break;
				case MOD_BRUSH:
					R_AddBrushModelSurfaces( ent );
					break;
				case MOD_BAD:		// null model axis
					if ( (ent->e.renderfx & RF_THIRD_PERSON) && !tr.viewParms.isPortal) {
						break;
					}
					R_AddDrawSurf( &entitySurface, tr.defaultShader, 0, 0 );
					break;
				default:
					ri.Error( ERR_DROP, "R_AddEntitySurfaces: Bad modeltype" );
					break;
				}
			}
			break;
		default:
			ri.Error( ERR_DROP, "R_AddEntitySurfaces: Bad reType" );
		}
	}

}


/*
================
R_DebugPolygon
================
*/
static void R_DebugPolygon( int color, int numPoints, float *points )
{
	int		i;

	GL_State( GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );

	// draw solid shade

	qglColor3f( color&1, (color>>1)&1, (color>>2)&1 );
	qglBegin( GL_POLYGON );
	for ( i = 0 ; i < numPoints ; i++ ) {
		qglVertex3fv( points + i * 3 );
	}
	qglEnd();

	// draw wireframe outline
	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
	qglDepthRange( 0, 0 );
	qglColor3f( 1, 1, 1 );
	qglBegin( GL_POLYGON );
	for ( i = 0 ; i < numPoints ; i++ ) {
		qglVertex3fv( points + i * 3 );
	}
	qglEnd();
	qglDepthRange( 0, 1 );
}

/*
====================
R_DebugGraphics

Visualization aid for movement clipping debugging
====================
*/
static void R_DebugGraphics( void ) 
{
	R_IssuePendingRenderCommands();

	GL_Bind( tr.whiteImage);
	GL_Cull( CT_FRONT_SIDED );
	ri.CM_DrawDebugSurface( R_DebugPolygon );
}

//==================================================================================

void R_SetupFrustum2 (void)
{
	float	xs, xc;
	float	ang;
    unsigned char signbits;

	ang = tr.viewParms.fovX * ( M_PI / 360.0) ;
	xs = sin( ang );
	xc = cos( ang );

    float temp1[3];
    float temp2[3];

    // 0
	VectorScale( tr.viewParms.or.axis[0], xs, temp1 );
	VectorScale( tr.viewParms.or.axis[1], xc, temp2);
    
    VectorAdd(temp1, temp2, tr.viewParms.frustum[0].normal);
	
    tr.viewParms.frustum[0].type = PLANE_NON_AXIAL;
	tr.viewParms.frustum[0].dist = DotProduct (tr.viewParms.or.origin, tr.viewParms.frustum[0].normal);
    signbits = 0;
    if (tr.viewParms.frustum[0].normal[0] < 0)
        signbits |= 1;
    if (tr.viewParms.frustum[0].normal[1] < 0)
        signbits |= 2;
    if (tr.viewParms.frustum[0].normal[2] < 0)
        signbits |= 4;
    tr.viewParms.frustum[0].signbits = signbits;

    // 1
    VectorSubtract(temp1, temp2, tr.viewParms.frustum[1].normal);

    tr.viewParms.frustum[1].type = PLANE_NON_AXIAL;
	tr.viewParms.frustum[1].dist = DotProduct (tr.viewParms.or.origin, tr.viewParms.frustum[1].normal);
    signbits = 0;
    if (tr.viewParms.frustum[1].normal[0] < 0)
        signbits |= 1;
    if (tr.viewParms.frustum[1].normal[1] < 0)
        signbits |= 2;
    if (tr.viewParms.frustum[1].normal[2] < 0)
        signbits |= 4;
    tr.viewParms.frustum[1].signbits = signbits;


	ang = tr.viewParms.fovY *( M_PI / 360.0);
	xs = sin( ang );
	xc = cos( ang );

	VectorScale( tr.viewParms.or.axis[0], xs, temp1);
	VectorScale( tr.viewParms.or.axis[2], xc, temp2);

    // 2
    VectorAdd(temp1, temp2, tr.viewParms.frustum[2].normal);

    tr.viewParms.frustum[2].type = PLANE_NON_AXIAL;
	tr.viewParms.frustum[2].dist = DotProduct (tr.viewParms.or.origin, tr.viewParms.frustum[2].normal);
    signbits = 0;
    if (tr.viewParms.frustum[2].normal[0] < 0)
        signbits |= 1;
    if (tr.viewParms.frustum[2].normal[1] < 0)
        signbits |= 2;
    if (tr.viewParms.frustum[2].normal[2] < 0)
        signbits |= 4;
    tr.viewParms.frustum[2].signbits = signbits;


    // 3
    VectorSubtract(temp1, temp2, tr.viewParms.frustum[3].normal);

    tr.viewParms.frustum[3].type = PLANE_NON_AXIAL;
	tr.viewParms.frustum[3].dist = DotProduct (tr.viewParms.or.origin, tr.viewParms.frustum[3].normal);
    signbits = 0;
    if (tr.viewParms.frustum[3].normal[0] < 0)
        signbits |= 1;
    if (tr.viewParms.frustum[3].normal[1] < 0)
        signbits |= 2;
    if (tr.viewParms.frustum[3].normal[2] < 0)
        signbits |= 4;
    tr.viewParms.frustum[3].signbits = signbits;
}


void R_SetupFrustum (float px, float py)
{

	// Now that we have all the data for the projection matrix we can also setup the view frustum.
	// R_SetupFrustum(dest, xmin, xmax, ymax, zProj, 0);
    // Set up the culling frustum planes for the current view using the results
    // we got from computing the first two rows of the projection matrix.

    // symmetric case can be simplified
    unsigned char signbits;
    float normal_op[3];
    float normal_adj[3];
    float N[3];

    float ofsorigin[3];
    VectorCopy(tr.viewParms.or.origin, ofsorigin);	

    {
        float adjleg = 1 / sqrtf(px * px + 1);
        float oppleg = px * adjleg;

        VectorScale(tr.viewParms.or.axis[0], oppleg, normal_op);
        VectorScale(tr.viewParms.or.axis[1], adjleg, normal_adj);
    }
    ////
    VectorAdd(normal_op, normal_adj, N);
    VectorCopy(N, tr.viewParms.frustum[0].normal);
    tr.viewParms.frustum[0].dist = DotProduct(ofsorigin, N);
    tr.viewParms.frustum[0].type = PLANE_NON_AXIAL;
	// for fast box on planeside test
    signbits = 0;
    if (N[0] < 0)
        signbits |= 1;
    if (N[1] < 0)
        signbits |= 2;
    if (N[2] < 0)
        signbits |= 4;
    tr.viewParms.frustum[0].signbits = signbits;


    ////
    VectorSubtract(normal_op, normal_adj, N);
    VectorCopy(N, tr.viewParms.frustum[1].normal);
    tr.viewParms.frustum[1].dist = DotProduct(ofsorigin, N);
    tr.viewParms.frustum[1].type = PLANE_NON_AXIAL;
	// for fast box on planeside test
    signbits = 0;
    if (N[0] < 0)
        signbits |= 1;
    if (N[1] < 0)
        signbits |= 2;
    if (N[2] < 0)
        signbits |= 4;
    tr.viewParms.frustum[1].signbits = signbits;

    {
        float adjleg = 1 / sqrtf(py * py + 1);
        float oppleg = py * adjleg;

        VectorScale(tr.viewParms.or.axis[0], oppleg, normal_op);
        VectorScale(tr.viewParms.or.axis[2], adjleg, normal_adj);
    }
    
    ////
    VectorAdd(normal_op, normal_adj, N);
    VectorCopy(N, tr.viewParms.frustum[2].normal);
    tr.viewParms.frustum[2].dist = DotProduct(ofsorigin, N);
    tr.viewParms.frustum[2].type = PLANE_NON_AXIAL;
	// for fast box on planeside test
    signbits = 0;
    if (N[0] < 0)
        signbits |= 1;
    if (N[1] < 0)
        signbits |= 2;
    if (N[2] < 0)
        signbits |= 4;
    tr.viewParms.frustum[2].signbits = signbits;

    
    ////
    VectorSubtract(normal_op, normal_adj, N);
    VectorCopy(N, tr.viewParms.frustum[3].normal);
    tr.viewParms.frustum[3].dist = DotProduct(ofsorigin, N);
    tr.viewParms.frustum[3].type = PLANE_NON_AXIAL;
	// for fast box on planeside test
    signbits = 0;
    if (N[0] < 0)
        signbits |= 1;
    if (N[1] < 0)
        signbits |= 2;
    if (N[2] < 0)
        signbits |= 4;
    tr.viewParms.frustum[3].signbits = signbits;
}


static void R_SetupProjection(void)
{

    float py = tan(tr.viewParms.fovY * (M_PI / 360.0f));
    float px = tan(tr.viewParms.fovX * (M_PI / 360.0f));
    
    tr.viewParms.projectionMatrix[0] = 1 / px;
    tr.viewParms.projectionMatrix[4] = 0;
    tr.viewParms.projectionMatrix[8] = 0;
    tr.viewParms.projectionMatrix[12] = 0;

    tr.viewParms.projectionMatrix[1] = 0;
    tr.viewParms.projectionMatrix[5] = 1 / py;
    tr.viewParms.projectionMatrix[9] = 0;	// normally 0
    tr.viewParms.projectionMatrix[13] = 0;

    tr.viewParms.projectionMatrix[3] = 0;
    tr.viewParms.projectionMatrix[7] = 0;
    tr.viewParms.projectionMatrix[11] = -1;
    tr.viewParms.projectionMatrix[15] = 0;


	// Now that we have all the data for the projection matrix 
    // we can also setup the view frustum.
	R_SetupFrustum(px, py);

}


void R_AddDrawSurf( surfaceType_t *surface, shader_t *shader, int fogIndex, int dlightMap )
{
	int	index = tr.refdef.numDrawSurfs & DRAWSURF_MASK;

	// instead of checking for overflow, we just mask the index, so it wraps around
	// the sort data is packed into a single 32 bit value so it can be
	// compared quickly during the qsorting process
	tr.refdef.drawSurfs[index].sort = (shader->sortedIndex << QSORT_SHADERNUM_SHIFT) 
		| tr.shiftedEntityNum | ( fogIndex << QSORT_FOGNUM_SHIFT ) | (int)dlightMap;
	tr.refdef.drawSurfs[index].surface = surface;
	tr.refdef.numDrawSurfs++;
}


void R_DecomposeSort( unsigned sort, int *entityNum, shader_t **shader, int *fogNum, int *dlightMap )
{
	*fogNum = ( sort >> QSORT_FOGNUM_SHIFT ) & 31;
	*shader = tr.sortedShaders[ ( sort >> QSORT_SHADERNUM_SHIFT ) & (MAX_SHADERS-1) ];
	*entityNum = ( sort >> QSORT_REFENTITYNUM_SHIFT ) & REFENTITYNUM_MASK;
	*dlightMap = sort & 3;
}




/*
 * A view may be either the actual camera view, or a mirror / remote location
 */
static void R_RenderView(viewParms_t *parms)
{

	if ( (parms->viewportWidth <= 0) || (parms->viewportHeight <= 0) ) {
		return;
	}

	tr.viewCount++;

	tr.viewParms = *parms;
	tr.viewParms.frameSceneNum = tr.frameSceneNum;
	tr.viewParms.frameCount = tr.frameCount;

	int firstDrawSurf = tr.refdef.numDrawSurfs;

	tr.viewCount++;

	// set viewParms.world
	R_RotateForViewer();

	R_SetupProjection();


    //R_GenerateDrawSurfs();
	if( !( tr.refdef.rdflags & RDF_NOWORLDMODEL ) )
    {
	    R_AddWorldSurfaces();
	}


	//R_AddPolygonSurfaces();
    // Adds all the scene's polys into this view's drawsurf list
    {
        tr.currentEntityNum = REFENTITYNUM_WORLD;
        tr.shiftedEntityNum = tr.currentEntityNum << QSORT_REFENTITYNUM_SHIFT;
        
        int	i;
        srfPoly_t* poly = tr.refdef.polys;

        for ( i = 0; i < tr.refdef.numPolys ; i++, poly++ )
        {
            shader_t* sh = R_GetShaderByHandle( poly->hShader );
            R_AddDrawSurf( ( void * )poly, sh, poly->fogIndex, qfalse );
        }
    }

	// set the projection matrix with the minimum zfar
	// now that we have the world bounded
	// this needs to be done before entities are added, 
    // because they use the projection matrix for lod calculation

	// dynamically compute far clip plane distance
	// if not rendering the world (icons, menus, etc), set a 2k far clip plane

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL )
    {
		tr.viewParms.zFar = 2048.0f;
	}
    else{
        float o[3];

        o[0] = tr.viewParms.or.origin[0];
        o[1] = tr.viewParms.or.origin[1];
        o[2] = tr.viewParms.or.origin[2];

        float farthestCornerDistance = 0;
        int	i;

        // set far clipping planes dynamically
        for ( i = 0; i < 8; i++ )
        {
            float v[3];
     
            v[0] = tr.viewParms.visBounds[(i&1)][0] - o[0];
            v[1] = tr.viewParms.visBounds[((i>>1)&1)][1] - o[1];
            v[2] = tr.viewParms.visBounds[((i>>2)&1)][2] - o[2];

            float distance = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
            if( distance > farthestCornerDistance )
            {
                farthestCornerDistance = distance;
            }
        }
        
        tr.viewParms.zFar = sqrtf(farthestCornerDistance);
    }

	// we know the size of the clipping volume. 
    // Now set the rest of the projection matrix.
    // sets the z-component transformation part in the projection matrix

    float zFar = tr.viewParms.zFar;
	float zNear	= r_znear->value;
	float depth	= zFar - zNear;

	tr.viewParms.projectionMatrix[2] = 0;
	tr.viewParms.projectionMatrix[6] = 0;
	tr.viewParms.projectionMatrix[10] = -( zFar + zNear ) / depth;
	tr.viewParms.projectionMatrix[14] = -2 * zFar * zNear / depth;
 
	R_AddEntitySurfaces();

	R_SortDrawSurfs( tr.refdef.drawSurfs + firstDrawSurf, tr.refdef.numDrawSurfs - firstDrawSurf );

	// draw main system development information (surface outlines, etc)
	if( r_debugSurface->integer )
    {
        R_DebugGraphics();
	}
}




/*
@@@@@@@@@@@@@@@@@@@@@
RE_RenderScene

Draw a 3D view into a part of the window, then return to 2D drawing.

Rendering a scene may require multiple views to be rendered to handle mirrors,
@@@@@@@@@@@@@@@@@@@@@
*/
void RE_RenderScene( const refdef_t *fd )
{

	if ( !tr.registered ) {
		return;
	}

	int startTime = ri.Milliseconds();

	qboolean customscrn = !(fd->rdflags & RDF_NOWORLDMODEL);


#ifndef NDEBUG	
	if (!tr.world && customscrn )
    {
		ri.Error (ERR_DROP, "R_RenderScene: NULL worldmodel");
	}
#endif
	memcpy( tr.refdef.text, fd->text, sizeof( tr.refdef.text ) );

	tr.refdef.x = fd->x;
	tr.refdef.y = fd->y;
	tr.refdef.width = fd->width;
	tr.refdef.height = fd->height;
	tr.refdef.fov_x = fd->fov_x;
	tr.refdef.fov_y = fd->fov_y;

	VectorCopy( fd->vieworg, tr.refdef.vieworg );
	VectorCopy( fd->viewaxis[0], tr.refdef.viewaxis[0] );
	VectorCopy( fd->viewaxis[1], tr.refdef.viewaxis[1] );
	VectorCopy( fd->viewaxis[2], tr.refdef.viewaxis[2] );

	tr.refdef.time = fd->time;
	tr.refdef.rdflags = fd->rdflags;

	// copy the areamask data over and note if it has changed, which
	// will force a reset of the visible leafs even if the view hasn't moved
	tr.refdef.areamaskModified = qfalse;
	if ( customscrn )
    {
		int		areaDiff = 0;
		int		i;

		// compare the area bits
		for (i = 0 ; i < MAX_MAP_AREA_BYTES; i++)
		{
			areaDiff |= tr.refdef.areamask[i] ^ fd->areamask[i];
			tr.refdef.areamask[i] = fd->areamask[i];
		}

		if ( areaDiff ) {
			// a door just opened or something
			tr.refdef.areamaskModified = qtrue;
            ri.Printf(PRINT_ALL, "areamaskModified = qtrue\n");
		}
	}


	// derived info

	tr.refdef.floatTime = tr.refdef.time * 0.001f;

	tr.refdef.numDrawSurfs = r_firstSceneDrawSurf;
	tr.refdef.drawSurfs = backEndData->drawSurfs;

	tr.refdef.num_entities = r_numentities - r_firstSceneEntity;
	tr.refdef.entities = &backEndData->entities[r_firstSceneEntity];

	tr.refdef.num_dlights = r_numdlights - r_firstSceneDlight;
	tr.refdef.dlights = &backEndData->dlights[r_firstSceneDlight];

	tr.refdef.numPolys = r_numpolys - r_firstScenePoly;
	tr.refdef.polys = &backEndData->polys[r_firstScenePoly];

	// turn off dynamic lighting globally by clearing all the dlights 
    // if it needs to be disabled or if vertex lighting is enabled
	//if ( r_dynamiclight->integer == 0 ) {
    //	tr.refdef.num_dlights = 0;
	//}

	// a single frame may have multiple scenes draw inside it --
	// a 3D game view, 3D status bar renderings, 3D menus, etc.
	// They need to be distinguished by the light flare code, because
	// the visibility state for a given surface may be different in
	// each scene / view.
	tr.frameSceneNum++;
	tr.sceneCount++;


	// setup view parms for the initial view
	// The refdef takes 0-at-the-top y coordinates, so
	// convert to GL's 0-at-the-bottom space
	//
    viewParms_t	parms;
	memset(&parms, 0, sizeof( parms ));
	parms.viewportX = tr.refdef.x;
	parms.viewportY = glConfig.vidHeight - tr.refdef.y - tr.refdef.height;
	parms.viewportWidth = tr.refdef.width;
	parms.viewportHeight = tr.refdef.height;
	parms.isPortal = qfalse;

	parms.fovX = tr.refdef.fov_x;
	parms.fovY = tr.refdef.fov_y;

    if ( customscrn )
    {
        // undo vert-
        parms.fovY = parms.fovX * (73.739792 / 90.0);
        // recalculate the fov_x
        parms.fovX = atan(tan(parms.fovY * (M_PI/360.0)) * glConfig.windowAspect) * (360.0/M_PI);
    }
/*	
    // ri.Printf(PRINT_ALL, "B: fovX: %f, fovY: %f, aspect: %f\n", parms.fovX, parms.fovY, tan(parms.fovX *(M_PI/360.0)) / tan(parms.fovY*(M_PI/360.0)));
	// leilei - widescreen
	// recalculate fov according to widescreen parameters
    // figure out our zoom or changed fov magnitiude from cg_fov and cg_zoomfov
    // try not to recalculate fov of ui and hud elements
    // && (parms.fovX > 1.3f * parms.fovY) && (tr.refdef.width * glConfig.vidHeight == glConfig.vidWidth * tr.refdef.height)

    if ( customscrn )
    {
        // undo vert-
        parms.fovY = parms.fovX * (73.739792 / 90.0);
        // recalculate the fov_x
        parms.fovX = atan(tan(parms.fovY * (M_PI/360.0)) * glConfig.windowAspect) * (360.0/M_PI);
    }

	if (customscrn){
		// In Vert- FOV the horizontal FOV is unchanged, so we use it to
		// calculate the vertical FOV that would be used if playing on 4:3 to get the Hor+ vertical FOV
		parms.fovY = (360.0 / M_PI) * atan( tan( (M_PI/360.0) * parms.fovX ) * 0.75f );
		// Then we use the Hor+ vertical FOV to calculate our new expanded horizontal FOV
		parms.fovX = (360.0 / M_PI) * atan( tan( (M_PI/360.0) * parms.fovY ) * tr.refdef.width / tr.refdef.height ) ;
	}

    ri.Printf(PRINT_ALL, "fovX: %f, fovY: %f, aspect: %f\n", parms.fovX, parms.fovY, tan(parms.fovX *(M_PI/360.0)) / tan(parms.fovY*(M_PI/360.0)));
*/


	VectorCopy( fd->vieworg, parms.or.origin );
	VectorCopy( fd->viewaxis[0], parms.or.axis[0] );
	VectorCopy( fd->viewaxis[1], parms.or.axis[1] );
	VectorCopy( fd->viewaxis[2], parms.or.axis[2] );

	VectorCopy( fd->vieworg, parms.pvsOrigin );

	R_RenderView( &parms );

	// the next scene rendered in this frame will tack on after this one
	r_firstSceneDrawSurf = tr.refdef.numDrawSurfs;
	r_firstSceneEntity = r_numentities;
	r_firstSceneDlight = r_numdlights;
	r_firstScenePoly = r_numpolys;

	tr.frontEndMsec += ri.Milliseconds() - startTime;
}



/*
static void R_WorldToLocal(vec3_t world, vec3_t local)
{
	local[0] = DotProduct(world, tr.or.axis[0]);
	local[1] = DotProduct(world, tr.or.axis[1]);
	local[2] = DotProduct(world, tr.or.axis[2]);
}
*/





/*
=================
R_RotateForEntity

Generates an orientation for an entity and viewParms
Does NOT produce any GL calls
Called by both the front end and the back end
=================
*/
void R_RotateForEntity( const trRefEntity_t *ent, const viewParms_t *viewParms, orientationr_t *or )
{
	float	glMatrix[16];
	vec3_t	delta;
	float	axisLength;

	if ( ent->e.reType != RT_MODEL ) {
		*or = viewParms->world;
		return;
	}

	VectorCopy( ent->e.origin, or->origin );

	VectorCopy( ent->e.axis[0], or->axis[0] );
	VectorCopy( ent->e.axis[1], or->axis[1] );
	VectorCopy( ent->e.axis[2], or->axis[2] );

	glMatrix[0] = or->axis[0][0];
	glMatrix[4] = or->axis[1][0];
	glMatrix[8] = or->axis[2][0];
	glMatrix[12] = or->origin[0];

	glMatrix[1] = or->axis[0][1];
	glMatrix[5] = or->axis[1][1];
	glMatrix[9] = or->axis[2][1];
	glMatrix[13] = or->origin[1];

	glMatrix[2] = or->axis[0][2];
	glMatrix[6] = or->axis[1][2];
	glMatrix[10] = or->axis[2][2];
	glMatrix[14] = or->origin[2];

	glMatrix[3] = 0;
	glMatrix[7] = 0;
	glMatrix[11] = 0;
	glMatrix[15] = 1;

    #ifdef PROCESSOR_HAVE_SSE
	MatrixMultiply4x4_SSE(glMatrix, viewParms->world.modelMatrix, or->modelMatrix );
    #else
	MatrixMultiply4x4( glMatrix, viewParms->world.modelMatrix, or->modelMatrix );
    #endif


	// calculate the viewer origin in the model's space
	// needed for fog, specular, and environment mapping
	VectorSubtract( viewParms->or.origin, or->origin, delta );

	// compensate for scale in the axes if necessary
	if( ent->e.nonNormalizedAxes )
    {
		axisLength = VectorLen( ent->e.axis[0] );
		if ( axisLength != 0 ) {
			axisLength = 1.0f / axisLength;
		}
	}
    else
    {
		axisLength = 1.0f;
	}

	or->viewOrigin[0] = DotProduct( delta, or->axis[0] ) * axisLength;
	or->viewOrigin[1] = DotProduct( delta, or->axis[1] ) * axisLength;
	or->viewOrigin[2] = DotProduct( delta, or->axis[2] ) * axisLength;
}

