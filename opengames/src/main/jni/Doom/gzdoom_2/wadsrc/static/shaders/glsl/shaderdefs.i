// This file contains common data definitions for both vertex and fragment shader

// these settings are actually pointless but there seem to be some old ATI drivers that fail to compile the shader without setting the precision here.
precision highp int;
precision highp float;

uniform vec4 uCameraPos;
uniform int uTextureMode;
uniform float uClipHeightTop, uClipHeightBottom;

uniform float uAlphaThreshold;


// colors
uniform vec4 uObjectColor;
uniform vec4 uDynLightColor;
uniform vec4 uFogColor;
uniform float uDesaturationFactor;
uniform float uInterpolationFactor;

// Fixed colormap stuff
uniform int uFixedColormap;				// 0, when no fixed colormap, 1 for a light value, 2 for a color blend, 3 for a fog layer
uniform vec4 uFixedColormapStart;
uniform vec4 uFixedColormapRange;

// Glowing walls stuff
uniform vec4 uGlowTopPlane;
uniform vec4 uGlowTopColor;
uniform vec4 uGlowBottomPlane;
uniform vec4 uGlowBottomColor;

// Lighting + Fog
uniform vec4 uLightAttr;
#define uLightLevel uLightAttr.a
#define uFogDensity uLightAttr.b
#define uLightFactor uLightAttr.g
#define uLightDist uLightAttr.r
uniform int uFogEnabled;

// dynamic lights
uniform int uLightIndex;

// matrices
uniform mat4 ProjectionMatrix;
uniform mat4 ViewMatrix;
uniform mat4 ModelMatrix;
uniform mat4 TextureMatrix;

