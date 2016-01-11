#include <QScreen>
#include <QDebug>
#include <QKeyEvent>
#include <unistd.h>
#include <math.h>
#include <QtMath>

#include "shaderwindow.h"
#include "rendermath.h"

ShaderWindow::ShaderWindow()
  : m_program(0), m_frame(0)
{
//  int numJoyPads = SDL_NumJoysticks();
//  if(numJoyPads > 0)
//  {
//    SDL_JoystickEventState(SDL_ENABLE);
//    js = SDL_JoystickOpen(0);
//  }
//  else
//  {
//    js = NULL;
//  }

    m_desiredCamPos = m_camPos = QVector3D(1.09475, 0.0750364, -1.00239);
    m_desiredCamRot = m_camRot = QVector3D(-0.301546, 0.399876, 0);
}

ShaderWindow::~ShaderWindow()
{

}

void ShaderWindow::initialize()
{
  m_program = new QOpenGLShaderProgram(this);
  m_program->addShaderFromSourceFile(QOpenGLShader::Vertex, "shaders/raymarch.vert");
  m_program->addShaderFromSourceFile(QOpenGLShader::Fragment, "shaders/raymarch.frag");

  if(!m_program->link())
  {  
    qDebug() << "Link error in shader program\n";
    exit(1);
  }

  m_vtxPosAttr = m_program->attributeLocation("vtxPos");
  m_vtxUVAttr = m_program->attributeLocation("vtxUV");

//  m_center = m_program->uniformLocation("center");
//  m_zoom = m_program->uniformLocation("zoom");
//  m_c = m_program->uniformLocation("c");

  m_resXUniform = m_program->uniformLocation("resx");
  m_resYUniform = m_program->uniformLocation("resy");
  m_aspectUniform = m_program->uniformLocation("aspect");
  m_timeUniform = m_program->uniformLocation("time");

//  m_rotateMatrixUniform = m_program->uniformLocation("rotmatrix");
//  m_transMatrixUniform = m_program->uniformLocation("posmatrix");

//  m_pitchUniform = m_program->uniformLocation("pitch");
//  m_yawUniform = m_program->uniformLocation("yaw");
  m_posUniform = m_program->uniformLocation("pos");
//  m_rotUniform = m_program->uniformLocation("rot");

//  m_matrix = m_program->uniformLocation("matrix");
  m_normalMatrix = m_program->uniformLocation("normalMatrix");

  //m_camPos = QVector3D(-1, 1, 1);
//  m_camRot = QVector3D(0, 0, 0);
}

void ShaderWindow::update()
{
  m_camPos.setX( FInterpTo( m_camPos.x(), m_desiredCamPos.x(), m_frame, 0.0001) );
  m_camPos.setY( FInterpTo( m_camPos.y(), m_desiredCamPos.y(), m_frame, 0.0001) );
  m_camPos.setZ( FInterpTo( m_camPos.z(), m_desiredCamPos.z(), m_frame, 0.0001) );

  m_camRot.setX( FInterpTo( m_camRot.x(), m_desiredCamRot.x(), m_frame, 0.00025) );
  m_camRot.setY( FInterpTo( m_camRot.y(), m_desiredCamRot.y(), m_frame, 0.00025) );
  m_camRot.setZ( FInterpTo( m_camRot.z(), m_desiredCamRot.z(), m_frame, 0.00025) );

  render();
}

void ShaderWindow::render()
{
  OpenGLWindow::render();

  const qreal retinaScale = devicePixelRatio();
  glViewport(0, 0, width() * retinaScale, height() * retinaScale);

  glClear(GL_COLOR_BUFFER_BIT);

  m_program->bind();

//  qDebug() << m_camPos;
//  qDebug() << m_camRot;

//  QMatrix4x4 translateMatrix;
//  translateMatrix.translate(m_camPos);
//  m_program->setUniformValue(m_transMatrixUniform, translateMatrix);

//  QMatrix4x4 rotateMatrix;
//  //rotateMatrix.translate(m_camPos);
//  rotateMatrix.rotate(m_camRot);
//  m_program->setUniformValue(m_rotateMatrixUniform, rotateMatrix);

//  QMatrix4x4 matrix;
//  //matrix.rotate(m_camRot);
//  matrix.translate(m_camPos);
//  m_program->setUniformValue(m_matrix, matrix);

  QMatrix4x4 normalmatrix, rotX, rotY, rotZ;

  { // X
    float angle = m_camRot.x();
    QVector3D axis = QVector3D(1, 0, 0);
    axis.normalize();
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;

    rotX = QMatrix4x4(oc * axis.x() * axis.x() + c,             oc * axis.x() * axis.y() - axis.z() * s,  oc * axis.z() * axis.x() + axis.y() * s,  0.0,
                      oc * axis.x() * axis.y() + axis.z() * s,  oc * axis.y() * axis.y() + c,             oc * axis.y() * axis.z() - axis.x() * s,  0.0,
                      oc * axis.z() * axis.x() - axis.y() * s,  oc * axis.y() * axis.z() + axis.x() * s,  oc * axis.z() * axis.z() + c,             0.0,
                      0.0,                                0.0,                                0.0,                                1.0);
  }

  { // Y
    float angle = m_camRot.y();
    QVector3D axis = QVector3D(0, 1, 0);
    axis.normalize();
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;

    rotY = QMatrix4x4(oc * axis.x() * axis.x() + c,             oc * axis.x() * axis.y() - axis.z() * s,  oc * axis.z() * axis.x() + axis.y() * s,  0.0,
                      oc * axis.x() * axis.y() + axis.z() * s,  oc * axis.y() * axis.y() + c,             oc * axis.y() * axis.z() - axis.x() * s,  0.0,
                      oc * axis.z() * axis.x() - axis.y() * s,  oc * axis.y() * axis.z() + axis.x() * s,  oc * axis.z() * axis.z() + c,             0.0,
                      0.0,                                0.0,                                0.0,                                1.0);
  }

  { // Z
    float angle = m_camRot.z();
    QVector3D axis = QVector3D(0, 0, 1);
    axis.normalize();
    float s = sin(angle);
    float c = cos(angle);
    float oc = 1.0 - c;

    rotZ = QMatrix4x4(oc * axis.x() * axis.x() + c,             oc * axis.x() * axis.y() - axis.z() * s,  oc * axis.z() * axis.x() + axis.y() * s,  0.0,
                      oc * axis.x() * axis.y() + axis.z() * s,  oc * axis.y() * axis.y() + c,             oc * axis.y() * axis.z() - axis.x() * s,  0.0,
                      oc * axis.z() * axis.x() - axis.y() * s,  oc * axis.y() * axis.z() + axis.x() * s,  oc * axis.z() * axis.z() + c,             0.0,
                      0.0,                                0.0,                                0.0,                                1.0);
  }



//  rotX.rotate( m_camRot.vector().x(), QVector3D(1, 0, 0)  );
//  rotY.rotate( m_camRot.vector().x(), QVector3D(0, 1, 0)  );
//  rotZ.rotate( m_camRot.vector().x(), QVector3D(0, 0, 1)  );

  normalmatrix = rotX * rotY * rotZ;
  m_program->setUniformValue(m_normalMatrix, normalmatrix);

//  QVector2D center(0.0f, 0.0f);
//  m_program->setUniformValueArray(m_center, &center, 2);

//  QVector2D c(0.0f, 0.0f);
//  m_program->setUniformValueArray(m_c, &c, 2);


//  m_program->setUniformValue(m_pitchUniform, m_camRot.x() );
//  m_program->setUniformValue(m_yawUniform, m_camRot.y() );
//  m_program->setUniformValue(m_rotUniform, m_camRot);
  m_program->setUniformValue(m_posUniform, m_camPos);

  QSize res = getResolution();
  float aspect = (res.height() > 0) ? ( (float)res.width() / res.height()) : 1.0f;
  m_program->setUniformValue(m_resXUniform, res.width());
  m_program->setUniformValue(m_resYUniform, res.height());
  m_program->setUniformValue(m_aspectUniform, aspect );

  float time = (float)m_frame / 32.0;
  m_program->setUniformValue(m_timeUniform, time);

  //m_program->setUniformValue(m_zoom, float( ( (sin(m_frame / 32.0) + 1.5 ) * 0.5 ) * 3.0 + 1.0) );


  GLfloat vertices[] = {
    -1,	-1, 0,
    1,	-1,	0,
    1,	1,	0,
    -1,	-1, 0,
    -1,	1,	0,
    1,	1,	0,
  };

  GLfloat uv[] = {
      0,	0,
      1,	0,
      1,	1,
      0,	0,
      0,	1,
      1,	1,
  };

  glVertexAttribPointer(m_vtxPosAttr, 3, GL_FLOAT, GL_FALSE, 0, vertices);
  glVertexAttribPointer(m_vtxUVAttr, 2, GL_FLOAT, GL_FALSE, 0, uv);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);

  glDrawArrays(GL_TRIANGLES, 0, 6);

  glDisableVertexAttribArray(1);
  glDisableVertexAttribArray(0);

  m_program->release();

  ++m_frame;
}

GLuint ShaderWindow::loadShader(GLenum type, const char *source)
{
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, 0);
  glCompileShader(shader);
  return shader;
}

void ShaderWindow::keyPressEvent(QKeyEvent* event)
{
  const float offset = 0.025f;
  const float rotateOffset = 0.10f;

  if( event->key() ==  Qt::Key_A )
  {
      float radius = offset;
      float pitch = m_camRot.x();
      float yaw = m_camRot.y() + ( M_PI/2.0f );

      float pitchRad = pitch;//qDegreesToRadians( pitch );
      float yawRad = yaw;//qDegreesToRadians( yaw );

      //These equations are from the wikipedia page, linked above
      float xMove = radius * -sinf( yawRad ) * cosf( pitchRad );
      float yMove = radius * sinf( pitchRad );
      float zMove = radius * cosf( yawRad ) * cosf( pitchRad );

      m_desiredCamPos.setX( m_desiredCamPos.x() + xMove );
      m_desiredCamPos.setY( m_desiredCamPos.y() + yMove );
      m_desiredCamPos.setZ( m_desiredCamPos.z() + zMove );
  }
  if( event->key() ==  Qt::Key_D )
  {
      float radius = offset;
      float pitch = m_camRot.x();
      float yaw = m_camRot.y() + ( M_PI/2.0f );

      float pitchRad = pitch;//qDegreesToRadians( pitch );
      float yawRad = yaw;//qDegreesToRadians( yaw );

      //These equations are from the wikipedia page, linked above
      float xMove = radius * -sinf( yawRad ) * cosf( pitchRad );
      float yMove = radius * sinf( pitchRad );
      float zMove = radius * cosf( yawRad ) * cosf( pitchRad );

      m_desiredCamPos.setX( m_desiredCamPos.x() - xMove );
      m_desiredCamPos.setY( m_desiredCamPos.y() - yMove );
      m_desiredCamPos.setZ( m_desiredCamPos.z() - zMove );
  }
  if( event->key() ==  Qt::Key_W )
  {
      float radius = offset;
      float pitch = m_camRot.x();
      float yaw = m_camRot.y();

      float pitchRad = pitch;//qDegreesToRadians( pitch );
      float yawRad = yaw;//qDegreesToRadians( yaw );

      //These equations are from the wikipedia page, linked above
      float xMove = radius * -sinf( yawRad ) * cosf( pitchRad );
      float yMove = radius * sinf( pitchRad );
      float zMove = radius * cosf( yawRad ) * cosf( pitchRad );

      m_desiredCamPos.setX( m_desiredCamPos.x() + xMove );
      m_desiredCamPos.setY( m_desiredCamPos.y() + yMove );
      m_desiredCamPos.setZ( m_desiredCamPos.z() + zMove );
  }

  if( event->key() ==  Qt::Key_S )
  {
      float radius = offset;
      float pitch = m_camRot.x();
      float yaw = m_camRot.y();

      float pitchRad = pitch;// qDegreesToRadians( pitch );
      float yawRad = yaw;//qDegreesToRadians( yaw );

      //These equations are from the wikipedia page, linked above
      float xMove = radius * -sinf( yawRad ) * cosf( pitchRad );
      float yMove = radius * sinf( pitchRad );
      float zMove = radius * cosf( yawRad ) * cosf( pitchRad );

      m_desiredCamPos.setX( m_desiredCamPos.x() - xMove );
      m_desiredCamPos.setY( m_desiredCamPos.y() - yMove );
      m_desiredCamPos.setZ( m_desiredCamPos.z() - zMove );
  }

  if( event->key() ==  Qt::Key_Up )
  {
      m_desiredCamRot.setX( m_desiredCamRot.x() + rotateOffset );
  }
  if( event->key() ==  Qt::Key_Down )
  {
      m_desiredCamRot.setX( m_desiredCamRot.x() - rotateOffset );
  }
  if( event->key() ==  Qt::Key_Left )
  {
      m_desiredCamRot.setY( m_desiredCamRot.y() + rotateOffset );
  }
  if( event->key() ==  Qt::Key_Right )
  {
      m_desiredCamRot.setY( m_desiredCamRot.y() - rotateOffset );
  }


  renderNow();
}

void ShaderWindow::keyReleaseEvent(QKeyEvent* event)
{
  if(m_firstRelease)
  {
      processMultipleKeyEvents();
  }
  m_firstRelease = false;
  keysPressed.remove((Qt::Key) event->key());
}

void ShaderWindow::processMultipleKeyEvents(  )
{

}