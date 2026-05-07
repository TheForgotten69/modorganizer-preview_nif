#include "NifWidget.h"
#include "NifExtensions.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions_2_1>
#include <QOpenGLVersionFunctionsFactory>
#include <QWheelEvent>
#include <utility>
using OpenGLFunctions = QOpenGLFunctions_2_1;

NifWidget::NifWidget(std::shared_ptr<nifly::NifFile> nifFile,
                     QString sourceFileName,
                     QHash<QString, QString> resolvedTexturePaths,
                     MOBase::IOrganizer* organizer, const bool debugContext,
                     QWidget* parent,
                     const Qt::WindowFlags f)
  : QOpenGLWidget(parent, f), m_NifFile{std::move(nifFile)},
    m_SourceFileName{std::move(sourceFileName)},
    m_ResolvedTexturePaths{std::move(resolvedTexturePaths)}, m_MOInfo{organizer},
    m_TextureManager{std::make_unique<TextureManager>(m_SourceFileName,
                                                      m_ResolvedTexturePaths)},
    m_ShaderManager{std::make_unique<ShaderManager>(organizer)}
{
  if (debugContext) {
    QSurfaceFormat format;
    format.setOption(QSurfaceFormat::DebugContext);
    setFormat(format);
  }

  qInfo() << "NIF preview widget created; OpenGL debug context"
          << (debugContext ? "requested" : "not requested");
}

NifWidget::~NifWidget()
{
  cleanup();
}

void NifWidget::mousePressEvent(QMouseEvent* event)
{
  m_MousePos = event->globalPosition();
}

void NifWidget::mouseMoveEvent(QMouseEvent* event)
{
  const auto pos   = event->globalPosition();
  const auto delta = pos - m_MousePos;
  m_MousePos       = pos;

  switch (event->buttons()) {
  case Qt::LeftButton: {
    m_Camera->rotate(static_cast<float>(delta.x() * 0.5f),
                     static_cast<float>(delta.y() * 0.5f));
  }
  break;
  case Qt::MiddleButton: {
    const float viewDX = m_Camera->distance() / m_ViewportWidth;
    const float viewDY = m_Camera->distance() / m_ViewportHeight;

    QMatrix4x4 r;
    r.rotate(-m_Camera->yaw(), 0.0f, 1.0f, 0.0f);
    r.rotate(-m_Camera->pitch(), 1.0f, 0.0f, 0.0f);

    const auto pan = r * QVector4D(static_cast<float>(-delta.x() * viewDX),
                                   static_cast<float>(delta.y() * viewDY), 0.0f, 0.0f);

    m_Camera->pan(QVector3D(pan));
  }
  break;

  case Qt::RightButton: {
    if (event->modifiers() == Qt::ShiftModifier) {
      m_Camera->zoomDistance(static_cast<float>(delta.y() * 0.1f));
    }
  }
  break;
  default: ;
  }
}

void NifWidget::wheelEvent(QWheelEvent* event)
{
  m_Camera->zoomFactor(1.0f -
                       (static_cast<float>(event->angleDelta().y()) / 120.0f * 0.38f));
}

void NifWidget::messageLogged(const QOpenGLDebugMessage& message)
{
  const auto msg = tr("OpenGL debug message: %1").arg(message.message());
  qDebug(qUtf8Printable(msg));
}

void NifWidget::initializeGL()
{
  m_GLInitialized = true;
  m_GLClean       = false;

  const auto context = QOpenGLContext::currentContext();
  if (!context) {
    qCritical("No current OpenGL context for NIF preview");
    return;
  }

  const auto format = context->format();
  qInfo() << "NIF preview OpenGL context" << format.majorVersion()
          << format.minorVersion() << "profile" << format.profile();

  connect(context, &QOpenGLContext::aboutToBeDestroyed, this, &NifWidget::cleanup,
          Qt::UniqueConnection);

  const auto f =
      QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(context);
  if (!f) {
    qCritical("Failed to resolve OpenGL 2.1 functions");
    return;
  }

  if (format.testOption(QSurfaceFormat::DebugContext)) {
    m_Logger = new QOpenGLDebugLogger(this);
    if (m_Logger->initialize()) {
      m_Logger->enableMessages();
      qInfo() << "NIF preview OpenGL debug logger initialized";
      connect(m_Logger, &QOpenGLDebugLogger::messageLogged, this,
              &NifWidget::messageLogged);
      m_Logger->startLogging();
    } else {
      qWarning("Failed to initialize NIF preview OpenGL debug logger");
    }
  }

  auto shapes = m_NifFile->GetShapes();
  qInfo() << "NIF preview loading" << shapes.size() << "shape(s)";
  for (auto& shape : shapes) {
    if (shape->flags & TriShape::Hidden) {
      continue;
    }

    qInfo() << "NIF preview shape" << shape->GetNumVertices() << "verts"
            << shape->GetNumTriangles() << "faces";
    m_GLShapes.emplace_back(m_NifFile.get(), shape, m_TextureManager.get());
  }

  m_Camera = SharedCamera;
  if (m_Camera.isNull()) {
    m_Camera     = {new Camera(), &Camera::deleteLater};
    SharedCamera = m_Camera;

    float largestRadius = 0.0f;
    for (const auto& shape : shapes) {

      if (auto bounds = GetBoundingSphere(m_NifFile.get(), shape);
        bounds.radius > largestRadius) {
        largestRadius = bounds.radius;

        m_Camera->setDistance(bounds.radius * 2.4f);
        m_Camera->setLookAt({-bounds.center.x, bounds.center.z, bounds.center.y});
      }
    }
  }

  updateCamera();

  connect(m_Camera.get(), &Camera::cameraMoved, this, [this]() {
    updateCamera();
    update();
  });

  f->glEnable(GL_DEPTH_TEST);
  f->glDepthFunc(GL_LEQUAL);
  f->glClearColor(0.18f, 0.18f, 0.18f, 1.0f);
}

void NifWidget::paintGL()
{
  const auto f = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(
      QOpenGLContext::currentContext());
  if (!f) {
    return;
  }

  f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  std::vector<OpenGLShape*> opaqueShapes;
  std::vector<OpenGLShape*> transparentShapes;

  for (auto& shape : m_GLShapes) {
    if (shape.alpha < 1.0f || shape.alphaBlendEnable || shape.alphaTestEnable) {
      transparentShapes.push_back(&shape);
    } else {
      opaqueShapes.push_back(&shape);
    }
  }

  f->glEnable(GL_POLYGON_OFFSET_FILL);
  f->glPolygonOffset(1.0f, 2.0f);

  for (const auto* shape : opaqueShapes) {
    if (!shape->vertexArray || !shape->vertexArray->isCreated()) {
      continue;
    }

    if (const auto program = m_ShaderManager->getProgram(shape->shaderType);
      program && program->isLinked() && program->bind()) {
      auto binder = QOpenGLVertexArrayObject::Binder(shape->vertexArray);

      auto& modelMatrix    = shape->modelMatrix;
      auto modelViewMatrix = m_ViewMatrix * modelMatrix;
      auto mvpMatrix       = m_ProjectionMatrix * modelViewMatrix;

      program->setUniformValue("worldMatrix", modelMatrix);
      program->setUniformValue("viewMatrix", m_ViewMatrix);
      program->setUniformValue("modelViewMatrix", modelViewMatrix);
      program->setUniformValue("modelViewMatrixInverse", modelViewMatrix.inverted());
      program->setUniformValue("normalMatrix", modelViewMatrix.normalMatrix());
      program->setUniformValue("mvpMatrix", mvpMatrix);
      program->setUniformValue("lightDirection", QVector3D(0, 0, 1));

      shape->setupShaders(program);

      if (shape->indexBuffer && shape->indexBuffer->isCreated()) {
        shape->indexBuffer->bind();
        f->glDrawElements(GL_TRIANGLES, shape->elements, GL_UNSIGNED_SHORT, nullptr);
        shape->indexBuffer->release();
      }

      program->release();
    }
  }

  f->glDisable(GL_POLYGON_OFFSET_FILL);
  f->glDepthMask(GL_FALSE);

  for (const auto* shape : transparentShapes) {
    if (!shape->vertexArray || !shape->vertexArray->isCreated()) {
      continue;
    }

    if (const auto program = m_ShaderManager->getProgram(shape->shaderType);
      program && program->isLinked() && program->bind()) {
      auto binder = QOpenGLVertexArrayObject::Binder(shape->vertexArray);

      auto& modelMatrix    = shape->modelMatrix;
      auto modelViewMatrix = m_ViewMatrix * modelMatrix;
      auto mvpMatrix       = m_ProjectionMatrix * modelViewMatrix;

      program->setUniformValue("worldMatrix", modelMatrix);
      program->setUniformValue("viewMatrix", m_ViewMatrix);
      program->setUniformValue("modelViewMatrix", modelViewMatrix);
      program->setUniformValue("modelViewMatrixInverse", modelViewMatrix.inverted());
      program->setUniformValue("normalMatrix", modelViewMatrix.normalMatrix());
      program->setUniformValue("mvpMatrix", mvpMatrix);
      program->setUniformValue("lightDirection", QVector3D(0, 0, 1));

      shape->setupShaders(program);

      if (shape->indexBuffer && shape->indexBuffer->isCreated()) {
        shape->indexBuffer->bind();
        f->glDrawElements(GL_TRIANGLES, shape->elements, GL_UNSIGNED_SHORT, nullptr);
        shape->indexBuffer->release();
      }

      program->release();
    }
  }

  f->glDepthMask(GL_TRUE);
}

void NifWidget::resizeGL(const int w, const int h)
{
  m_ViewportWidth  = static_cast<float>(w);
  m_ViewportHeight = static_cast<float>(h);

  setProjectionMatrix();
}

void NifWidget::cleanup()
{
  if (!m_GLInitialized || m_GLClean || !context()) {
    return;
  }

  makeCurrent();

  if (m_Logger) {
    m_Logger->stopLogging();
    delete m_Logger;
    m_Logger = nullptr;
  }

  for (auto& shape : m_GLShapes) {
    shape.destroy();
  }
  m_GLShapes.clear();

  m_TextureManager->cleanup();
  doneCurrent();
  m_GLClean = true;
}

void NifWidget::setProjectionMatrix()
{
  if (m_ViewportWidth <= 0.0f || m_ViewportHeight <= 0.0f || m_Camera.isNull()) {
    return;
  }

  QMatrix4x4 m;
  m.perspective(40.0f, m_ViewportWidth / m_ViewportHeight,
                m_Camera->nearPlane(), m_Camera->farPlane());
  m_ProjectionMatrix = m;
}

void NifWidget::updateCamera()
{
  QMatrix4x4 m;
  m.translate(0.0f, 0.0f, -m_Camera->distance());
  m.rotate(m_Camera->pitch(), 1.0f, 0.0f, 0.0f);
  m.rotate(m_Camera->yaw(), 0.0f, 1.0f, 0.0f);
  m.translate(-m_Camera->lookAt());
  m *= QMatrix4x4{
      -1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1,
  };
  m_ViewMatrix = m;

  setProjectionMatrix();
}
