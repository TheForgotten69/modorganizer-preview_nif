#include "TextureManager.h"
#include "PreviewNif.h"

#include <gli/gli.hpp>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QOpenGLFunctions_2_1>
#include <QOpenGLVersionFunctionsFactory>
#include <QVector4D>

#include <exception>

namespace
{
QString detachedUtf8Copy(const QString& value)
{
  const auto utf8 = value.toUtf8();
  return QString::fromUtf8(utf8.constData(), utf8.size());
}

QString findDataRoot(const QString& sourceFileName)
{
  const auto sourcePath = QDir::fromNativeSeparators(detachedUtf8Copy(sourceFileName));
  const auto lowerPath  = sourcePath.toCaseFolded();

  for (const auto& marker : {QStringLiteral("/meshes/"), QStringLiteral("/textures/")}) {
    if (const auto index = lowerPath.lastIndexOf(marker); index >= 0) {
      return detachedUtf8Copy(sourcePath.left(index));
    }
  }

  return detachedUtf8Copy(QFileInfo(sourcePath).absoluteDir().absolutePath());
}

QString textureKey(const QString& value)
{
  return QDir::cleanPath(QDir::fromNativeSeparators(value)).toCaseFolded();
}

}

TextureManager::TextureManager(QString sourceFileName,
                               QHash<QString, QString> resolvedTexturePaths)
  : m_SourceFileName{detachedUtf8Copy(sourceFileName)},
    m_DataRoot{findDataRoot(m_SourceFileName)},
    m_ResolvedTexturePaths{std::move(resolvedTexturePaths)}
{
  qInfo() << "NIF texture source file" << m_SourceFileName;
  qInfo() << "NIF texture data root" << m_DataRoot;
  qInfo() << "NIF pre-resolved texture path entries"
          << m_ResolvedTexturePaths.size();
}

void TextureManager::cleanup()
{
  for (auto it = m_Textures.cbegin(); it != m_Textures.cend();) {
    const auto* texture = it->second;
    m_Textures.erase(it++);
    delete texture;
  }

  auto cleanupTexture = [&](QOpenGLTexture*& texPtr) {
    if (texPtr) {
      delete texPtr;
      texPtr = nullptr;
    }
  };

  cleanupTexture(m_ErrorTexture);
  cleanupTexture(m_BlackTexture);
  cleanupTexture(m_WhiteTexture);
  cleanupTexture(m_FlatNormalTexture);
}

QOpenGLTexture* TextureManager::getTexture(const std::string& texturePath)
{
  return getTexture(QString::fromUtf8(texturePath.data(),
                                      static_cast<qsizetype>(texturePath.size())));
}

QOpenGLTexture* TextureManager::getTexture(const QString& texturePath)
{
  const auto stableTexturePath = detachedUtf8Copy(texturePath);
  if (stableTexturePath.isEmpty()) {
    return nullptr;
  }

  qInfo() << "NIF texture requested" << stableTexturePath;

  qInfo("NIF texture cache bypassed for diagnostic build");

  QOpenGLTexture* texture = nullptr;
  try {
    texture = loadTexture(stableTexturePath);
  } catch (const std::exception& e) {
    qWarning() << "Failed to load NIF texture" << stableTexturePath << e.what();
  } catch (...) {
    qWarning() << "Failed to load NIF texture" << stableTexturePath
               << "unknown exception";
  }

  qInfo() << "NIF texture result" << stableTexturePath
          << (texture ? "loaded" : "missing");
  return texture;
}

QOpenGLTexture* TextureManager::getErrorTexture()
{
  if (!m_ErrorTexture) {
    m_ErrorTexture = makeSolidColor({1.0f, 0.0f, 1.0f, 1.0f});
  }
  return m_ErrorTexture;
}

QOpenGLTexture* TextureManager::getBlackTexture()
{
  if (!m_BlackTexture) {
    m_BlackTexture = makeSolidColor({0.0f, 0.0f, 0.0f, 1.0f});
  }
  return m_BlackTexture;
}

QOpenGLTexture* TextureManager::getWhiteTexture()
{
  if (!m_WhiteTexture) {
    m_WhiteTexture = makeSolidColor({1.0f, 1.0f, 1.0f, 1.0f});
  }
  return m_WhiteTexture;
}

QOpenGLTexture* TextureManager::getFlatNormalTexture()
{
  if (!m_FlatNormalTexture) {
    m_FlatNormalTexture = makeSolidColor({0.5f, 0.5f, 1.0f, 1.0f});
  }
  return m_FlatNormalTexture;
}

QOpenGLTexture* TextureManager::loadTexture(QString texturePath) const
{
  qInfo() << "NIF texture load entered" << texturePath;
  if (texturePath.isEmpty()) {
    return nullptr;
  }

  qInfo() << "Resolving NIF texture path" << texturePath;

  QString realPath;
  try {
    realPath = resolvePath(texturePath);
  } catch (const std::exception& e) {
    qWarning() << "Failed to resolve NIF texture path" << texturePath
               << e.what();
    return nullptr;
  } catch (...) {
    qWarning() << "Failed to resolve NIF texture path" << texturePath
               << "unknown exception";
    return nullptr;
  }

  const bool fileExists =
      !realPath.isEmpty() && QFileInfo::exists(realPath) && QFileInfo(realPath).
      isFile();

  if (fileExists) {
    try {
      qInfo() << "Loading NIF texture from resolved file" << realPath;
      return makeTexture(gli::load(realPath.toStdString()));
    } catch (const std::exception& e) {
      qWarning() << "Failed to load NIF texture file" << realPath << e.what();
      return nullptr;
    } catch (...) {
      qWarning() << "Failed to load NIF texture file" << realPath
                 << "unknown exception";
      return nullptr;
    }
  }

  qInfo() << "NIF texture not found as loose file" << texturePath;
  return nullptr;
}

QOpenGLTexture* TextureManager::makeTexture(const gli::texture& texture)
{
  if (texture.empty()) {
    return nullptr;
  }

  const gli::gl GL(gli::gl::PROFILE_GL32);
  const auto [internal, external, type, swizzles] =
      GL.translate(texture.format(), texture.swizzles());
  GLenum target = GL.translate(texture.target());

  qInfo() << "Uploading NIF texture to OpenGL target" << target << "format"
          << internal << "levels" << texture.levels() << "layers"
          << texture.layers() << "faces" << texture.faces();

  auto* f = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(
      QOpenGLContext::currentContext());
  if (!f) {
    qWarning("Failed to resolve OpenGL 2.1 functions for NIF texture upload");
    return nullptr;
  }

  auto* glTexture = new QOpenGLTexture(static_cast<QOpenGLTexture::Target>(target));

  try {
    if (!glTexture->create()) {
      qWarning("Failed to create OpenGL texture for NIF preview");
      delete glTexture;
      return nullptr;
    }
    glTexture->bind();
    glTexture->setMipLevels(static_cast<int>(texture.levels()));
    glTexture->setMipBaseLevel(0);
    glTexture->setMipMaxLevel(static_cast<int>(texture.levels()) - 1);
    glTexture->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear,
                                QOpenGLTexture::Linear);
    glTexture->setSwizzleMask(static_cast<QOpenGLTexture::SwizzleValue>(swizzles[0]),
                              static_cast<QOpenGLTexture::SwizzleValue>(swizzles[1]),
                              static_cast<QOpenGLTexture::SwizzleValue>(swizzles[2]),
                              static_cast<QOpenGLTexture::SwizzleValue>(swizzles[3]));
    glTexture->setWrapMode(QOpenGLTexture::Repeat);

    const auto extent = texture.extent();
    qInfo() << "NIF texture extent" << extent.x << extent.y << extent.z;
    glTexture->setSize(extent.x, extent.y, extent.z);
    glTexture->setFormat(static_cast<QOpenGLTexture::TextureFormat>(internal));
    glTexture->allocateStorage(static_cast<QOpenGLTexture::PixelFormat>(external),
                               static_cast<QOpenGLTexture::PixelType>(type));

    for (std::size_t layer = 0; layer < texture.layers(); layer++) {
      for (std::size_t face = 0; face < texture.faces(); face++) {
        for (std::size_t level = 0; level < texture.levels(); level++) {
          const auto levelExtent = texture.extent(level);

          const GLenum targetFace = is_target_cube(texture.target())
                                      ? (GL_TEXTURE_CUBE_MAP_POSITIVE_X + face)
                                      : target;

          const auto dataPtr = texture.data(layer, face, level);

          if (is_compressed(texture.format())) {
            switch (texture.target()) {
            case gli::TARGET_1D:
              f->glCompressedTexSubImage1D(
                  targetFace, static_cast<GLint>(level), 0,
                  levelExtent.x, internal,
                  static_cast<GLsizei>(texture.size(level)), dataPtr);
              break;
            case gli::TARGET_1D_ARRAY:
            case gli::TARGET_2D:
            case gli::TARGET_CUBE:
              f->glCompressedTexSubImage2D(
                  targetFace, static_cast<GLint>(level), 0, 0, levelExtent.x,
                  (texture.target() == gli::TARGET_1D_ARRAY)
                    ? static_cast<GLint>(layer)
                    : levelExtent.y,
                  internal, static_cast<GLsizei>(texture.size(level)), dataPtr);
              break;
            case gli::TARGET_2D_ARRAY:
            case gli::TARGET_3D:
            case gli::TARGET_CUBE_ARRAY:
              f->glCompressedTexSubImage3D(
                  targetFace, static_cast<GLint>(level), 0, 0, 0, levelExtent.x,
                  levelExtent.y,
                  (texture.target() == gli::TARGET_3D)
                    ? levelExtent.z
                    : static_cast<GLint>(layer),
                  internal, static_cast<GLsizei>(texture.size(level)), dataPtr);
              break;
            default:
              break;
            }
          } else {
            switch (texture.target()) {
            case gli::TARGET_1D:
              f->glTexSubImage1D(
                  targetFace, static_cast<GLint>(level), 0, levelExtent.x,
                  external, type, dataPtr);
              break;
            case gli::TARGET_1D_ARRAY:
            case gli::TARGET_2D:
            case gli::TARGET_CUBE:
              f->glTexSubImage2D(
                  targetFace, static_cast<GLint>(level), 0, 0, levelExtent.x,
                  (texture.target() == gli::TARGET_1D_ARRAY)
                    ? static_cast<GLint>(layer)
                    : levelExtent.y,
                  external, type, dataPtr);
              break;
            case gli::TARGET_2D_ARRAY:
            case gli::TARGET_3D:
            case gli::TARGET_CUBE_ARRAY:
              f->glTexSubImage3D(
                  targetFace, static_cast<GLint>(level), 0, 0, 0, levelExtent.x,
                  levelExtent.y,
                  (texture.target() == gli::TARGET_3D)
                    ? levelExtent.z
                    : static_cast<GLint>(layer),
                  external, type, dataPtr);
              break;
            default:
              break;
            }
          }
        }
      }
    }

    glTexture->release();
    return glTexture;
  } catch (const std::exception& e) {
    qWarning() << "Failed to upload NIF texture to OpenGL" << e.what();
  } catch (...) {
    qWarning() << "Failed to upload NIF texture to OpenGL"
               << "unknown exception";
  }

  glTexture->destroy();
  delete glTexture;
  return nullptr;
}

QOpenGLTexture* TextureManager::makeSolidColor(const QVector4D color)
{
  auto* glTexture = new QOpenGLTexture(QOpenGLTexture::Target2D);
  glTexture->create();
  glTexture->bind();

  glTexture->setSize(1, 1);
  glTexture->setFormat(QOpenGLTexture::RGBA32F);
  glTexture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::Float32);

  glTexture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::Float32, &color);

  glTexture->release();
  return glTexture;
}

QString TextureManager::resolvePath(QString path) const
{
  auto normalizedPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
  while (normalizedPath.startsWith(QStringLiteral("/"))) {
    normalizedPath.remove(0, 1);
  }

  if (const auto resolved = m_ResolvedTexturePaths.value(path); !resolved.isEmpty()) {
    qInfo() << "NIF texture pre-resolved candidate" << resolved;
    return detachedUtf8Copy(resolved);
  }

  if (const auto resolved = m_ResolvedTexturePaths.value(normalizedPath);
      !resolved.isEmpty()) {
    qInfo() << "NIF texture pre-resolved normalized candidate" << resolved;
    return detachedUtf8Copy(resolved);
  }

  if (const auto resolved = m_ResolvedTexturePaths.value(textureKey(normalizedPath));
      !resolved.isEmpty()) {
    qInfo() << "NIF texture pre-resolved folded candidate" << resolved;
    return detachedUtf8Copy(resolved);
  }

  if (QFileInfo(normalizedPath).isAbsolute()) {
    qInfo() << "NIF texture absolute candidate" << normalizedPath;
    return QFileInfo(normalizedPath).isFile() ? detachedUtf8Copy(normalizedPath)
                                              : QString();
  }

  if (m_DataRoot.isEmpty()) {
    return QString();
  }

  const auto candidate =
      QDir(m_DataRoot).absoluteFilePath(normalizedPath);
  qInfo() << "NIF texture loose-file candidate" << candidate;
  return QFileInfo(candidate).isFile() ? detachedUtf8Copy(candidate) : QString();
}
