in vec3 vtxPos;
in vec2 vtxUV;

varying vec2 texcoord;

void main(void)
{
  gl_Position = vec4(vtxPos, 1.0);
  texcoord = vtxUV;
}