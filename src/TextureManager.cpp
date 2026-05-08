#include "TextureManager.h"
#include "PreviewNif.h"

#include <gli/gli.hpp>
#include <libbsarch/libbsarch.h>

#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QOpenGLFunctions_2_1>
#include <QOpenGLVersionFunctionsFactory>
#include <QTextStream>
#include <QVector4D>

#include <exception>
#include <memory>

namespace
{
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

QString detachedUtf8Copy(const QString& value)
{
  const auto utf8 = value.toUtf8();
  return QString::fromUtf8(utf8.constData(), utf8.size());
}

std::string textureCacheKey(const QString& value)
{
  const auto key = QDir::cleanPath(QDir::fromNativeSeparators(value))
                     .toCaseFolded()
                     .toUtf8();
  return {key.constData(), static_cast<std::size_t>(key.size())};
}

void addUniquePath(QStringList& paths, const QString& path)
{
  const auto normalizedPath = detachedUtf8Copy(QDir::cleanPath(
      QDir::fromNativeSeparators(path)));
  if (!normalizedPath.isEmpty() && !paths.contains(normalizedPath, Qt::CaseInsensitive)) {
    paths.append(normalizedPath);
  }
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

QString findModsRoot(const QString& dataRoot)
{
  const auto normalizedRoot = QDir::fromNativeSeparators(dataRoot);
  const auto lowerRoot      = normalizedRoot.toCaseFolded();
  const auto marker         = QStringLiteral("/mods/");
  const auto markerIndex    = lowerRoot.lastIndexOf(marker);
  if (markerIndex < 0) {
    return QString();
  }

  return detachedUtf8Copy(
      normalizedRoot.left(markerIndex + QStringLiteral("/mods").size()));
}

QString findModListPath(const QString& modsRoot)
{
  if (modsRoot.isEmpty()) {
    return QString();
  }

  QDir profilesDir(QFileInfo(modsRoot).absoluteDir().absoluteFilePath(
      QStringLiteral("profiles")));
  if (!profilesDir.exists()) {
    return QString();
  }

  QFileInfo newestModList;
  const auto profiles = profilesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const auto& profile : profiles) {
    const QFileInfo modList(QDir(profile.absoluteFilePath()).absoluteFilePath(
        QStringLiteral("modlist.txt")));
    if (!modList.isFile()) {
      continue;
    }
    if (!newestModList.exists() || modList.lastModified() > newestModList.lastModified()) {
      newestModList = modList;
    }
  }

  return newestModList.exists() ? detachedUtf8Copy(newestModList.absoluteFilePath())
                                : QString();
}

QStringList readEnabledMods(const QString& modListPath)
{
  QStringList enabledMods;
  QFile modList(modListPath);
  if (!modList.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return enabledMods;
  }

  QTextStream stream(&modList);
  while (!stream.atEnd()) {
    const auto line = stream.readLine().trimmed();
    if (line.isEmpty() || line.startsWith(QStringLiteral("#"))) {
      continue;
    }
    if (!line.startsWith(QStringLiteral("+"))) {
      continue;
    }

    const auto modName = line.mid(1).trimmed();
    if (!modName.isEmpty()) {
      enabledMods.append(modName);
    }
  }

  return enabledMods;
}

QStringList findDataRoots(const QString& dataRoot)
{
  QStringList dataRoots;

  const auto modsRoot = findModsRoot(dataRoot);
  if (modsRoot.isEmpty()) {
    addUniquePath(dataRoots, dataRoot);
    return dataRoots;
  }

  QDir modsDir(modsRoot);
  if (!modsDir.exists()) {
    addUniquePath(dataRoots, dataRoot);
    return dataRoots;
  }

  const auto modListPath = findModListPath(modsRoot);
  const auto enabledMods = readEnabledMods(modListPath);
  if (!enabledMods.isEmpty()) {
    for (auto it = enabledMods.crbegin(); it != enabledMods.crend(); ++it) {
      addUniquePath(dataRoots, modsDir.absoluteFilePath(*it));
    }
    addUniquePath(dataRoots, dataRoot);
    return dataRoots;
  }

  addUniquePath(dataRoots, dataRoot);
  const auto modDirs = modsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                             QDir::Name | QDir::Reversed);
  for (const auto& modDir : modDirs) {
    addUniquePath(dataRoots, modDir.absoluteFilePath());
  }

  return dataRoots;
}

QStringList findArchives(const QStringList& dataRoots)
{
  QStringList archivePaths;

  for (const auto& dataRoot : dataRoots) {
    QDir rootDir(dataRoot);
    if (!rootDir.exists()) {
      continue;
    }

    const auto archives = rootDir.entryInfoList(
        {QStringLiteral("*.bsa"), QStringLiteral("*.ba2")},
        QDir::Files, QDir::Name | QDir::Reversed);
    for (const auto& archive : archives) {
      addUniquePath(archivePaths, archive.absoluteFilePath());
    }
  }

  return archivePaths;
}

}

TextureManager::TextureManager(QString sourceFileName)
  : m_SourceFileName{detachedUtf8Copy(sourceFileName)},
    m_DataRoot{findDataRoot(m_SourceFileName)},
    m_DataRoots{findDataRoots(m_DataRoot)}
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

  const auto cacheKey = textureCacheKey(stableTexturePath);
  if (const auto cached = m_Textures.find(cacheKey); cached != m_Textures.end()) {
    return cached->second;
  }

  QOpenGLTexture* texture = nullptr;
  try {
    texture = loadTexture(stableTexturePath);
  } catch (const std::exception& e) {
    qWarning() << "Failed to load NIF texture" << stableTexturePath << e.what();
  } catch (...) {
    qWarning() << "Failed to load NIF texture" << stableTexturePath
               << "unknown exception";
  }

  m_Textures[cacheKey] = texture;
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
  if (texturePath.isEmpty()) {
    return nullptr;
  }

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
    if (const auto texture = loadTextureFromArchives(texturePath)) {
      return texture;
    }
  } catch (const std::exception& e) {
    qWarning() << "Failed to load NIF texture from filesystem archives"
               << texturePath << e.what();
  } catch (...) {
    qWarning() << "Failed to load NIF texture from filesystem archives"
               << texturePath << "unknown exception";
  }

  return nullptr;
}

QOpenGLTexture* TextureManager::loadTextureFromArchives(
    const QString& texturePath) const
{
  const auto& archives = archivePaths();
  if (archives.isEmpty()) {
    return nullptr;
  }

  for (const auto& archivePath : archives) {
    if (const auto texture = loadTextureFromBSA(archivePath, texturePath)) {
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

  const auto bsaPathUtf16  = reinterpret_cast<const wchar_t*>(bsaPath.utf16());
  const auto [code, _text] = bsa_load_from_file(bsaHandle.get(), bsaPathUtf16);
  if (code == BSA_RESULT_EXCEPTION) {
    return nullptr;
  }

  const auto texturePathUtf16 =
      reinterpret_cast<const wchar_t*>(texturePath.utf16());
  auto [rBuffer, msg] = bsa_extract_file_data_by_filename(
      bsaHandle.get(), texturePathUtf16);
  if (msg.code == BSA_RESULT_EXCEPTION) {
    return nullptr;
  }

  const UniqueBufferPtr buffer(&rBuffer, BsaBufferDeleter(bsaHandle.get()));

  const auto data = static_cast<char*>(buffer->data);
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

  if (QFileInfo(normalizedPath).isAbsolute()) {
    return QFileInfo(normalizedPath).isFile() ? detachedUtf8Copy(normalizedPath)
                                              : QString();
  }

  if (m_DataRoots.isEmpty()) {
    return QString();
  }

  for (const auto& dataRoot : m_DataRoots) {
    const auto candidate = QDir(dataRoot).absoluteFilePath(normalizedPath);
    if (QFileInfo(candidate).isFile()) {
      return detachedUtf8Copy(candidate);
    }
  }

  return QString();
}

const QStringList& TextureManager::archivePaths() const
{
  if (!m_ArchivePathsScanned) {
    m_ArchivePaths        = findArchives(m_DataRoots);
    m_ArchivePathsScanned = true;
  }

  return m_ArchivePaths;
}
