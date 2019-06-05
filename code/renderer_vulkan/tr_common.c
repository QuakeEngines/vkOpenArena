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

#include "tr_common.h"


extern refimport_t ri;


char* R_SkipPath(char *pathname)
{
	char *last = pathname;
    char c;
    do{
        c = *pathname;
    	if (c == '/')
		    last = pathname+1;
        pathname++;
    }while(c);

	return last;
}


void R_StripExtension(const char *in, char *out, int destsize)
{
	const char *dot = strrchr(in, '.');
    const char *slash = strrchr(in, '/');


	if ((dot != NULL) && ( (slash == NULL) || (slash < dot) ) )
    {
        int len = dot-in+1;
        if(len <= destsize)
            destsize = len;
        else
		    ri.Printf( PRINT_WARNING, "stripExtension: dest size not enough!\n");
    }

    if(in != out)
    	strncpy(out, in, destsize-1);
	
    out[destsize-1] = '\0';
}


/*
char* getExtension( const char *name )
{
	char* dot = strrchr(name, '.');
    char* slash = strrchr(name, '/');

	if ((dot != NULL) && ((slash == NULL) || (slash < dot) ))
		return dot + 1;
	else
		return "";
}
*/


void VectorCross( const vec3_t v1, const vec3_t v2, vec3_t cross )
{
	cross[0] = v1[1]*v2[2] - v1[2]*v2[1];
	cross[1] = v1[2]*v2[0] - v1[0]*v2[2];
	cross[2] = v1[0]*v2[1] - v1[1]*v2[0];
}




// fast vector normalize routine that does not check to make sure
// that length != 0, nor does it return length, uses rsqrt approximation
void FastNormalize1f(float v[3])
{
	// writing it this way allows gcc to recognize that rsqrt can be used
    float invLen = 1.0f / sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);

 	v[0] = v[0] * invLen;
	v[1] = v[1] * invLen;
	v[2] = v[2] * invLen;
}

void FastNormalize2f( const float* v, float* out)
{
	// writing it this way allows gcc to recognize that rsqrt can be used
    float invLen = 1.0f / sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);

 	out[0] = v[0] * invLen;
	out[1] = v[1] * invLen;
	out[2] = v[2] * invLen;
}

// use Rodrigue's rotation formula
// dir are not assumed to be unit vector
void PointRotateAroundVector(float* res, const float* vec, const float* p, const float degrees)
{
    float rad = DEG2RAD( degrees );
    float cos_th = cos( rad );
    float sin_th = sin( rad );
    float k[3];

	// writing it this way allows gcc to recognize that rsqrt can be used
    float invLen = 1.0f / sqrtf(vec[0]*vec[0] + vec[1]*vec[1] + vec[2]*vec[2]);
 	k[0] = vec[0] * invLen;
	k[1] = vec[1] * invLen;
	k[2] = vec[2] * invLen;

    float d = (1 - cos_th) * (p[0] * k[0] + p[1] * k[1] + p[2] * k[2]);

	res[0] = sin_th * (k[1]*p[2] - k[2]*p[1]);
	res[1] = sin_th * (k[2]*p[0] - k[0]*p[2]);
	res[2] = sin_th * (k[0]*p[1] - k[1]*p[0]);

    res[0] += cos_th * p[0] + d * k[0]; 
    res[1] += cos_th * p[1] + d * k[1]; 
    res[2] += cos_th * p[2] + d * k[2]; 
}

// vector k are assumed to be unit
void RotateAroundUnitVector(float* res, const float* k, const float* p, const float degrees)
{
    float rad = DEG2RAD( degrees );
    float cos_th = cos( rad );
    float sin_th = sin( rad );
 
    float d = (1 - cos_th) * (p[0] * k[0] + p[1] * k[1] + p[2] * k[2]);

	res[0] = sin_th * (k[1]*p[2] - k[2]*p[1]);
	res[1] = sin_th * (k[2]*p[0] - k[0]*p[2]);
	res[2] = sin_th * (k[0]*p[1] - k[1]*p[0]);

    res[0] += cos_th * p[0] + d * k[0]; 
    res[1] += cos_th * p[1] + d * k[1]; 
    res[2] += cos_th * p[2] + d * k[2]; 
}


// note: vector forward are NOT assumed to be nornalized,
// unit: nornalized of forward,
// dst: unit vector which perpendicular of forward(src) 
void VectorPerp( const vec3_t src, vec3_t dst )
{
    float unit[3];
    
    float sqlen = src[0]*src[0] + src[1]*src[1] + src[2]*src[2];
    if(0 == sqlen)
    {
        ri.Printf( PRINT_WARNING, "MakePerpVectors: zero vertor input!\n");
        return;
    }

  	dst[1] = -src[0];
	dst[2] = src[1];
	dst[0] = src[2];
	// this rotate and negate try to make a vector not colinear with the original
    // actually can not guarantee, for example
    // forward = (1/sqrt(3), 1/sqrt(3), -1/sqrt(3)),
    // then right = (-1/sqrt(3), -1/sqrt(3), 1/sqrt(3))


    float invLen = 1.0f / sqrtf(sqlen);
    unit[0] = src[0] * invLen;
    unit[1] = src[1] * invLen;
    unit[2] = src[2] * invLen;

    
    float d = DotProduct(unit, dst);
	dst[0] -= d*unit[0];
	dst[1] -= d*unit[1];
	dst[2] -= d*unit[2];

    // normalize the result
    invLen = 1.0f / sqrtf(dst[0]*dst[0] + dst[1]*dst[1] + dst[2]*dst[2]);

    dst[0] *= invLen;
    dst[1] *= invLen;
    dst[2] *= invLen;
}

// Given a normalized forward vector, create two other perpendicular vectors
// note: vector forward are NOT assumed to be nornalized,
// after this funtion is called , forward are nornalized.
// right, up: perpendicular of forward 
float MakeTwoPerpVectors(const float forward[3], float right[3], float up[3])
{

    float sqLen = forward[0]*forward[0]+forward[1]*forward[1]+forward[2]*forward[2];
    if(sqLen)
    {
        float nf[3] = {0, 0, 1};
        float invLen = 1.0f / sqrtf(sqLen);
        nf[0] = forward[0] * invLen;
        nf[1] = forward[1] * invLen;
        nf[2] = forward[2] * invLen;

        float adjlen = DotProduct(nf, right);

        // this rotate and negate guarantees a vector
        // not colinear with the original
        right[0] = forward[2] - adjlen * nf[0];
        right[1] = -forward[0] - adjlen * nf[1];
        right[2] = forward[1] - adjlen * nf[2];


        invLen = 1.0f/sqrtf(right[0]*right[0]+right[1]*right[1]+right[2]*right[2]);
        right[0] *= invLen;
        right[1] *= invLen;
        right[2] *= invLen;

        // get the up vector with the right hand rules 
        VectorCross(right, nf, up);

        return (sqLen * invLen);
    }
    return 0;
}


/*
=====================
PlaneFromPoints

Returns false if the triangle is degenrate.
The normal will point out of the clock for clockwise ordered points
=====================
*/
qboolean PlaneFromPoints(vec4_t plane, const vec3_t a, const vec3_t b, const vec3_t c)
{
	vec3_t d1, d2;

	VectorSubtract( b, a, d1 );
	VectorSubtract( c, a, d2 );

	VectorCross( d2, d1, plane );

	plane[3] = a[0]*plane[0] + a[1]*plane[1] + a[2]*plane[2];

	if((plane[0]*plane[0] + plane[1]*plane[1] + plane[2]*plane[2]) == 0)
    {
		return qfalse;
	}

	return qtrue;
}



/*
==================
BoxOnPlaneSide

Returns 1, 2, or 1 + 2
==================
*/
int BoxOnPlaneSide(vec3_t emins, vec3_t emaxs, struct cplane_s *p)
{
	float	dist[2] = {0, 0};
	int		sides = 0, i;

	// fast axial cases
    /*
	if (p->type < 3)
	{
		if (p->dist <= emins[p->type])
			return 1;
        else if (p->dist >= emaxs[p->type])
			return 2;
        
		return 3;
	}
    */
	// general case
	// dist[0] = dist[1] = 0;
	// >= 8: default case is original code (dist[0]=dist[1]=0)
	
    for (i=0 ; i<3; ++i)
    {
        int b = (p->signbits >> i) & 1;
        dist[ b] += p->normal[i]*emaxs[i];
        dist[!b] += p->normal[i]*emins[i];
    }


	// sides = 0;
	if (dist[0] >= p->dist)
		sides = 1;
	if (dist[1] < p->dist)
		sides |= 2;

	return sides;
}


void AddPointToBounds( const vec3_t v, vec3_t mins, vec3_t maxs )
{
	if ( v[0] < mins[0] ) {
		mins[0] = v[0];
	}
	if ( v[0] > maxs[0]) {
		maxs[0] = v[0];
	}

	if ( v[1] < mins[1] ) {
		mins[1] = v[1];
	}
	if ( v[1] > maxs[1]) {
		maxs[1] = v[1];
	}

	if ( v[2] < mins[2] ) {
		mins[2] = v[2];
	}
	if ( v[2] > maxs[2]) {
		maxs[2] = v[2];
	}
}



void ClearBounds(vec3_t mins, vec3_t maxs)
{
	mins[0] = mins[1] = mins[2] = 99999;
	maxs[0] = maxs[1] = maxs[2] = -99999;
}




/*
=================
SkipBracedSection

The next token should be an open brace or set depth to 1 if already parsed it.
Skips until a matching close brace is found.
Internal brace depths are properly skipped.
=================
*/




// tr_extramath.c - extra math needed by the renderer not in qmath.c
// Some matrix helper functions
// FIXME: do these already exist in ioq3 and I don't know about them?




qboolean SpheresIntersect(vec3_t origin1, float radius1, vec3_t origin2, float radius2)
{
	float radiusSum = radius1 + radius2;
	vec3_t diff;
	
	VectorSubtract(origin1, origin2, diff);

	if (DotProduct(diff, diff) <= radiusSum * radiusSum)
	{
		return qtrue;
	}

	return qfalse;
}

void BoundingSphereOfSpheres(vec3_t origin1, float radius1, vec3_t origin2, float radius2, vec3_t origin3, float *radius3)
{
	vec3_t diff;

	VectorScale(origin1, 0.5f, origin3);
	VectorMA(origin3, 0.5f, origin2, origin3);

	VectorSubtract(origin1, origin2, diff);
	*radius3 = VectorLen(diff) * 0.5f + MAX(radius1, radius2);
}

VkBool32 isNonCaseStringEqual(const char * s1, const char * s2)
{
    if( (s1 != NULL) && (s2 != NULL) )
    {
        int	c1;
        int c2;
        do
        {
            c1 = *s1++; 
            c2 = *s2++;

            // consider that most shader is little a - z, 
            if( (c1 >= 'A') && (c1 <= 'Z') )
            {
                c1 += 'a' - 'A';
            }
            
            // to a - z
            if( (c2 >= 'A') && (c2 <= 'Z') )
            {
                c2 +=  'a' - 'A';
            }

            if( c1 != c2 )
            {
                // string are not equal
                return VK_FALSE;
            }
        } while(c1 || c2);

        return VK_TRUE;
    }

    // should not compare null, this is not meaningful ...
    ri.Printf( PRINT_WARNING, "WARNING: compare NULL string. %p == %p ? \n", s1, s2 );
    return VK_FALSE;
}

