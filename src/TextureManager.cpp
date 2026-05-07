#include "TextureManager.h"
#include "PreviewNif.h"

#include <uibase/game_features/dataarchives.h>
#include <uibase/game_features/igamefeatures.h>
#include <uibase/ifiletree.h>
#include <uibase/iplugingame.h>

#include <gli/gli.hpp>
#include <libbsarch/libbsarch.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QOpenGLFunctions_2_1>
#include <QOpenGLVersionFunctionsFactory>
#include <QVector4D>

#include <exception>
#include <memory>
#include <ranges>

struct BsaPtrDeleter
{
  void operator()(void* ptr) const
  {
    bsa_free(ptr);
  }
};

using UniqueBsaPtr = std::unique_ptr<void, BsaPtrDeleter>;

struct BsaBufferDeleter
{
  explicit BsaBufferDeleter(void* bsa) : m_bsa(bsa)
  {
  }

  void operator()(const bsa_result_buffer_t* buffer) const
  {
    bsa_file_data_free(m_bsa, *buffer);
  }

  void* m_bsa;
};

using UniqueBufferPtr = std::unique_ptr<bsa_result_buffer_t, BsaBufferDeleter>;

namespace
{
QString detachedUtf8Copy(const QString& value)
{
  const auto utf8 = value.toUtf8();
  return QString::fromUtf8(utf8.constData(), utf8.size());
}

}

TextureManager::TextureManager(MOBase::IOrganizer* organizer)
  : m_MOInfo{organizer}
{
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

  const auto game = m_MOInfo->managedGame();
  if (!game) {
    qCritical("Failed to interface with managed game plugin");
    return nullptr;
  }

  qInfo() << "Resolving NIF texture path" << texturePath;

  QString realPath;
  try {
    realPath = resolvePath(game, texturePath);
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

  try {
    if (const auto texture = tryLoadTextureFromMods(texturePath)) {
      return texture;
    }
  } catch (const std::exception& e) {
    qWarning() << "Failed to load NIF texture from mod archives"
               << texturePath << e.what();
  } catch (...) {
    qWarning() << "Failed to load NIF texture from mod archives"
               << texturePath << "unknown exception";
  }

  try {
    if (const auto texture = tryLoadTextureFromGame(texturePath)) {
      return texture;
    }
  } catch (const std::exception& e) {
    qWarning() << "Failed to load NIF texture from game archives"
               << texturePath << e.what();
  } catch (...) {
    qWarning() << "Failed to load NIF texture from game archives"
               << texturePath << "unknown exception";
  }

  return nullptr;
}

QOpenGLTexture* TextureManager::tryLoadTextureFromMods(const QString& texturePath) const
{
  const auto fileOrigins = m_MOInfo->getFileOrigins(texturePath);
  qInfo() << "NIF texture mod file origins" << texturePath << fileOrigins.size();
  if (fileOrigins.empty()) {
    return nullptr;
  }

  const auto& modName = fileOrigins.constFirst();
  qInfo() << "Searching first NIF texture origin mod" << modName << "for"
          << texturePath;
  if (const auto mod = m_MOInfo->modList()->getMod(modName)) {
    if (const auto fileTree = mod->fileTree()) {
      for (auto it = fileTree->begin(); it != fileTree->end(); ++it) {
        const auto fileInfo = *it;
        if (!fileInfo) {
          continue;
        }
        if (!fileInfo->name().endsWith(".bsa", Qt::CaseInsensitive)) {
          continue;
        }

        QString bsaPath;
        try {
          bsaPath = resolvePath(m_MOInfo->managedGame(), fileInfo->name());
        } catch (const std::exception& e) {
          qWarning() << "Failed to resolve mod archive for NIF texture"
                     << fileInfo->name() << e.what();
          continue;
        } catch (...) {
          qWarning() << "Failed to resolve mod archive for NIF texture"
                     << fileInfo->name() << "unknown exception";
          continue;
        }
        if (bsaPath.isEmpty()) {
          continue;
        }
        if (const auto texture = loadTextureFromBSA(bsaPath, texturePath)) {
          return texture;
        }
      }
    }
  }
  return nullptr;
}

QOpenGLTexture* TextureManager::tryLoadTextureFromGame(
    const QString& texturePath) const
{
  const auto features = m_MOInfo->gameFeatures();
  if (!features) {
    qInfo() << "No game features available while resolving NIF texture"
            << texturePath;
    return nullptr;
  }

  const auto gameArchives = features->gameFeature<MOBase::DataArchives>();
  if (!gameArchives) {
    qInfo() << "No game archives feature available while resolving NIF texture"
            << texturePath;
    return nullptr;
  }

  auto archives = gameArchives->archives(m_MOInfo->profile());
  qInfo() << "Searching" << archives.size() << "game archive(s) for NIF texture"
          << texturePath;
  for (const auto& archive : std::ranges::reverse_view(archives)) {
    QString bsaPath;
    try {
      bsaPath = resolvePath(m_MOInfo->managedGame(), archive);
    } catch (const std::exception& e) {
      qWarning() << "Failed to resolve game archive for NIF texture" << archive
                 << e.what();
      continue;
    } catch (...) {
      qWarning() << "Failed to resolve game archive for NIF texture" << archive
                 << "unknown exception";
      continue;
    }
    if (bsaPath.isEmpty()) {
      continue;
    }
    if (const auto texture = loadTextureFromBSA(bsaPath, texturePath)) {
      return texture;
    }
  }
  return nullptr;
}

QOpenGLTexture* TextureManager::loadTextureFromBSA(const QString& bsaPath,
                                                   const QString& texturePath)
{
  const UniqueBsaPtr bsaHandle(bsa_create());
  static_assert(sizeof(wchar_t) == 2, "Expected wchar_t to be 2 bytes");

  qInfo() << "Searching BSA for NIF texture" << bsaPath << texturePath;

  const auto bsaPathUtf16  = reinterpret_cast<const wchar_t*>(bsaPath.utf16());
  const auto [code, _text] = bsa_load_from_file(bsaHandle.get(), bsaPathUtf16);
  if (code == BSA_RESULT_EXCEPTION) {
    qWarning() << "Failed to open BSA for NIF texture" << bsaPath;
    return nullptr;
  }

  const auto texturePathUtf16 = reinterpret_cast<const wchar_t*>(texturePath.utf16());
  auto [rBuffer, msg]         = bsa_extract_file_data_by_filename(
      bsaHandle.get(), texturePathUtf16);
  if (msg.code == BSA_RESULT_EXCEPTION) {
    qWarning() << "Failed to extract NIF texture" << texturePath << "from"
               << bsaPath;
    return nullptr;
  }

  const UniqueBufferPtr buffer(&rBuffer, BsaBufferDeleter(bsaHandle.get()));

  const auto data = static_cast<char*>(buffer->data);
  qInfo() << "Extracted NIF texture from BSA" << texturePath << "bytes"
          << buffer->size;
  try {
    return makeTexture(gli::load(data, buffer->size));
  } catch (const std::exception& e) {
    qWarning() << "Failed to load NIF texture" << texturePath << "from" << bsaPath
               << e.what();
    return nullptr;
  } catch (...) {
    qWarning() << "Failed to load NIF texture" << texturePath << "from" << bsaPath
               << "unknown exception";
    return nullptr;
  }
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

QString TextureManager::resolvePath(const MOBase::IPluginGame* game,
                                    QString path) const
{
  if (auto resolved = m_MOInfo->resolvePath(path); !resolved.isEmpty()) {
    return detachedUtf8Copy(resolved);
  }

  const auto dataPath =
      game->dataDirectory().absoluteFilePath(QDir::cleanPath(path));

  return QFileInfo::exists(dataPath) ? dataPath : QString();
}
