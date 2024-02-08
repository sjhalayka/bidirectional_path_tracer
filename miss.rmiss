#version 460
#extension GL_EXT_ray_tracing : require

struct RayPayload
{
	vec3 color;
	float dist;
	vec3 normal;
	float reflector;
	float opacity;
};

layout(location = 0) rayPayloadInEXT RayPayload rayPayload;

void main()
{
	rayPayload.color = vec3(0.0, 0.0, 0.0);
	rayPayload.dist = -1.0; // Sentinel value
	rayPayload.normal = vec3(0.0, 0.0, 0.0);
	rayPayload.reflector = 0.0;
	rayPayload.opacity = 0.0;
}