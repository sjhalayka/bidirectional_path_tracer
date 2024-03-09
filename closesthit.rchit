#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable


struct RayPayload
{
	vec3 color;
	float dist;
	vec3 normal;
	float reflector;
	float opacity;
	float tint;
	vec3 tint_colour;
	float subsurface;
	float density;
};

layout(location = 0) rayPayloadInEXT RayPayload rayPayload;
layout(location = 2) rayPayloadEXT bool shadowed;

layout(binding = 0, set = 1) uniform sampler2D baseColorSampler;
layout(binding = 1, set = 1) uniform sampler2D normalSampler;

hitAttributeEXT vec2 attribs;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 2, set = 0) uniform UBO 
{
	mat4 viewInverse;
	mat4 projInverse;

	mat4 transformation_matrix;

	vec3 camera_pos;
	int vertexSize;
	bool screenshot_mode;

	uint tri_count;
	uint light_tri_count;

} ubo;

layout(binding = 3, set = 0) buffer Vertices { vec4 v[]; } vertices;
layout(binding = 4, set = 0) buffer Indices { uint i[]; } indices;

struct Vertex
{
  vec3 pos;
  vec3 normal;
  vec2 uv;
  vec4 color;
  vec4 _pad0; 
  vec4 _pad1;
  vec4 _pad2;
};

Vertex unpack(uint index)
{
	// Unpack the vertices from the SSBO using the glTF vertex structure
	// The multiplier is the size of the vertex divided by four float components (=16 bytes)
	const int m = ubo.vertexSize / 16;

	vec4 d0 = vertices.v[m * index + 0];
	vec4 d1 = vertices.v[m * index + 1];
	vec4 d2 = vertices.v[m * index + 2];

	Vertex v;
	v.pos = d0.xyz;
	v.normal = vec3(d0.w, d1.x, d1.y);
	v.uv = vec2(d1.z, d1.w);
	v.color = vec4(d2.x, d2.y, d2.z, 1.0);

	return v;
}

void main()
{
	ivec3 index = ivec3(indices.i[3 * gl_PrimitiveID], indices.i[3 * gl_PrimitiveID + 1], indices.i[3 * gl_PrimitiveID + 2]);

	Vertex v0 = unpack(index.x);
	Vertex v1 = unpack(index.y);
	Vertex v2 = unpack(index.z);

	// Interpolate
	const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
	vec3 normal = normalize(v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z);
	const vec3 pos = v0.pos * barycentricCoords.x + v1.pos * barycentricCoords.y + v2.pos * barycentricCoords.z;
	const vec2 uv = v0.uv * barycentricCoords.x + v1.uv * barycentricCoords.y + v2.uv * barycentricCoords.z;

	vec4 n = normalize(ubo.transformation_matrix*vec4(normal, 0.0));

	rayPayload.color = texture(baseColorSampler, uv).rgb;
	rayPayload.opacity = texture(baseColorSampler, uv).a;
	rayPayload.dist = gl_RayTmaxEXT;
	rayPayload.normal = normalize(n.xyz);
	rayPayload.tint = 0;
	rayPayload.tint_colour = vec3(0, 0, 0);




	// Do subsurface scattering coefficient and subsurface density
	rayPayload.subsurface = 0.0;//texture(normalSampler, uv).g;
	rayPayload.density = 0.0;//texture(normalSampler, uv).b;
	
	rayPayload.reflector = texture(normalSampler, uv).a;




	// Make the transparent sphere reflective
	if(rayPayload.opacity == 0.0)
	{
	//	rayPayload.opacity = 0.9;
		rayPayload.reflector = 1.0;

		rayPayload.color = vec3(1.0, 1.0, 1.0);

		rayPayload.tint = 1.0;
		rayPayload.tint_colour = rayPayload.color;

		//rayPayload.subsurface = 3.0;
	}







	if(false)//rayPayload.reflector == 1.0)
	{
	//	rayPayload.color.r = 1.0;
	//	rayPayload.color.g = 0.5;
	//	rayPayload.color.b = 0.0;
	
		rayPayload.opacity = 0.9;
		rayPayload.color = vec3(1,1,1);
		rayPayload.reflector = 0.99;

		//rayPayload.tint = 1.0;
		//rayPayload.tint_colour = vec3(1, 0.5, 0.0);
		//rayPayload.color = rayPayload.tint_colour;
	}
	

	
	// Do exponential lighting:
	//
	// Make sure that light_scale is equal to or less than 127.
	// This is because a 4-byte float's maximum value is 2^127.
	// This value should be zero for non-light triangles, and
	// greater than zero for emissive triangles.
	//
	// The maximum value 2^127 is 1.7e+38, in base-10. 
	// That's SUPER bright! No need for a double here (famous
	// last words).
	//
	float light_scale = 255.0*texture(normalSampler, uv).r;
	light_scale = clamp(light_scale, 0.0, 127.0);
	
//	if(light_scale > 0)
//	light_scale = 9; // for debuggin purposes
	
	rayPayload.color *= pow(2.0, light_scale);
}