const int iterations = 100;

precision highp float;

varying vec2 texcoord;

uniform sampler2D uSampler;
uniform vec2 center;
uniform float zoom;
uniform vec2 c;

uniform int resx;
uniform int resy;
uniform float aspect;
uniform float time;

uniform mat4 matrix;

const float epsilon = 0.0001;
const float fov = 120.0;

// p = eye + right*u + up*v;
vec3 eye = vec3( matrix * vec4(0, 0.5, -2, 1.0) );
vec3 up	= vec3(0, 1, 0);
vec3 right = vec3(1, 0, 0);
vec3 forward = normalize(cross(right, up));

float sdSphere( vec3 p, float s )
{
  return length(p)-s;
}

float udBox( vec3 p, vec3 b )
{
  return length(max(abs(p)-b,0.0));
}

vec4 shade(vec3 p, vec3 n)
{
  const vec3 lightPos = vec3( .5, .5, -2.0 );

  return max(0.0, dot( normalize(lightPos - p ), n)) * 16.0;
}

vec3 rotate( vec3 pos, float x, float y, float z )
{
  mat3 rotX = mat3( 1.0, 0.0, 0.0, 0.0, cos( x ), -sin( x ), 0.0, sin( x ), cos( x ) );
  mat3 rotY = mat3( cos( y ), 0.0, sin( y ), 0.0, 1.0, 0.0, -sin(y), 0.0, cos(y) );
  mat3 rotZ = mat3( cos( z ), -sin( z ), 0.0, sin( z ), cos( z ), 0.0, 0.0, 0.0, 1.0 );

  return rotX * rotY * rotZ * pos;
}

float hit( vec3 r )
{
  //r = rotate( r, sin(time), cos(time), 0.0 );
  vec3 zn = vec3( r.xyz );
  float rad = 0.0;
  float hit = 0.0;
  float p = 8.0;
  float d = 1.0;
  for( int i = 0; i < 4; i++ )
  {
      rad = length( zn );

      if( rad > 2.0 )
      {
        hit = 0.5 * log(rad) * rad / d;
      }else{

      float th = atan( length( zn.xy ), zn.z );
      float phi = atan( zn.y, zn.x );
      float rado = pow(rad,8.0);
      d = pow(rad, 7.0) * 7.0 * d + 1.0;

      float sint = sin( th * p );
      zn.x = rado * sint * cos( phi * p );
      zn.y = rado * sint * sin( phi * p );
      zn.z = rado * cos( th * p ) ;
      zn += r;
      }

  }

  return hit;

}

vec3 eps = vec3( .1, 0.0, 0.0 );

void main(void)
{
  float fov_ratio = (0.5 * aspect) / (tan(radians(fov * 0.5)));
  float u = aspect * (gl_FragCoord.x * 2.0 / resx - 1.0);
  float v = gl_FragCoord.y * 2.0 / resy - 1.0;
  vec3 rayOrigin = eye;
  vec3 rayDirection = normalize(forward*fov_ratio + right*u + up*v);

  const int maxSteps = 64;

  float t = 0.0;

  float d = 0.0;
  vec3 r = vec3(0,0,0);
  for(int i = 0; i < maxSteps; ++i)
  {
    r = rayOrigin + (rayDirection * t);
    d = hit(r);

    if( d < epsilon )
    {
      break;
    }

    t += d;
  }

  vec3 n = vec3( hit( r + eps ) - hit( r - eps ),
                 hit( r + eps.yxz ) - hit( r - eps.yxz ),
                 hit( r + eps.zyx ) - hit( r - eps.zyx ) );

  gl_FragColor = shade(r,n);
}
