#include "ShaderManager.h"
#include "Shaders.h"

#include <QDebug>
#include <QOpenGLContext>

ShaderManager::ShaderManager(MOBase::IOrganizer* moInfo) : m_MOInfo{moInfo}
{
}

QOpenGLShaderProgram* ShaderManager::getProgram(const ShaderType type)
{
  if (type == None) {
    return nullptr;
  }

  if (m_Programs[type] == nullptr) {
    m_Programs[type] = loadProgram(type);
  }

  return m_Programs[type];
}

QOpenGLShaderProgram* ShaderManager::loadProgram(const ShaderType type)
{
  const auto* source = shaderSourceFor(type);
  if (!source) {
    return nullptr;
  }

  const auto program = new QOpenGLShaderProgram(QOpenGLContext::currentContext());
  if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, source->vertex)) {
    qWarning() << "Failed to compile vertex shader" << source->vertexName
               << program->log();
  }
  if (!program->addShaderFromSourceCode(QOpenGLShader::Fragment, source->fragment)) {
    qWarning() << "Failed to compile fragment shader" << source->fragmentName
               << program->log();
  }

  program->bindAttributeLocation("position", AttribPosition);
  program->bindAttributeLocation("normal", AttribNormal);
  program->bindAttributeLocation("tangent", AttribTangent);
  program->bindAttributeLocation("bitangent", AttribBitangent);
  program->bindAttributeLocation("texCoord", AttribTexCoord);
  program->bindAttributeLocation("color", AttribColor);

  if (!program->link()) {
    qWarning() << "Failed to link shader program" << source->vertexName
               << source->fragmentName
               << program->log();
  }

  return program;
}
